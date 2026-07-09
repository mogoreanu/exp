#include "block_validator/block_validator_lib.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace {

TEST(BlockValidatorTest, GenerateAndVerifyValidBlock) {
  uint64_t buffer[512] = {0};  // 4096 bytes
  uint64_t seq = 12345;
  uint64_t block_idx = 42;
  size_t block_size = 4096;

  GenerateBlockData(
      absl::MakeSpan(reinterpret_cast<uint8_t*>(buffer), block_size), seq,
      block_idx, block_idx, 1);

  uint64_t out_seq = 0;
  uint64_t out_start = 0;
  uint64_t out_num = 0;
  bool is_valid = VerifyBlockData(
      absl::MakeSpan(reinterpret_cast<const uint8_t*>(buffer), block_size),
      block_idx, out_seq, out_start, out_num);

  EXPECT_TRUE(is_valid);
  EXPECT_EQ(out_seq, seq);
}

TEST(BlockValidatorTest, VerifyZeroedBlock) {
  uint64_t buffer[512] = {0};  // 4096 bytes
  uint64_t block_idx = 42;
  size_t block_size = 4096;

  uint64_t out_seq = 0;
  uint64_t out_start = 0;
  uint64_t out_num = 0;
  bool is_valid = VerifyBlockData(
      absl::MakeSpan(reinterpret_cast<const uint8_t*>(buffer), block_size),
      block_idx, out_seq, out_start, out_num);

  EXPECT_TRUE(is_valid);
  EXPECT_EQ(out_seq, 0);
}

TEST(BlockValidatorTest, DetectsCorruptedPayload) {
  uint64_t buffer[512] = {0};  // 4096 bytes
  uint64_t seq = 12345;
  uint64_t block_idx = 42;
  size_t block_size = 4096;

  GenerateBlockData(
      absl::MakeSpan(reinterpret_cast<uint8_t*>(buffer), block_size), seq,
      block_idx, block_idx, 1);

  // Corrupt the payload at some offset
  buffer[5] ^= 0xDEADBEEF;

  uint64_t out_seq = 0;
  uint64_t out_start = 0;
  uint64_t out_num = 0;
  bool is_valid = VerifyBlockData(
      absl::MakeSpan(reinterpret_cast<const uint8_t*>(buffer), block_size),
      block_idx, out_seq, out_start, out_num);

  EXPECT_FALSE(is_valid);
}

TEST(BlockValidatorTest, DetectsWrongBlockIdx) {
  uint64_t buffer[512] = {0};
  uint64_t seq = 12345;
  uint64_t block_idx = 42;
  size_t block_size = 4096;

  GenerateBlockData(
      absl::MakeSpan(reinterpret_cast<uint8_t*>(buffer), block_size), seq,
      block_idx, block_idx, 1);

  // Attempt to read it as a different block_idx
  uint64_t wrong_block_idx = 43;
  uint64_t out_seq = 0;
  uint64_t out_start = 0;
  uint64_t out_num = 0;
  bool is_valid = VerifyBlockData(
      absl::MakeSpan(reinterpret_cast<const uint8_t*>(buffer), block_size),
      wrong_block_idx, out_seq, out_start, out_num);

  EXPECT_FALSE(is_valid);
}

TEST(BlockShardTest, ValidateSequenceTracking) {
  BlockShard shard(200);
  uint64_t block_idx = 100;

  // Write 1 starts
  uint64_t seq1 = 1;
  uint64_t start_time1 = 10;
  shard.StartWrite(block_idx, seq1, start_time1, absl::Now());
  EXPECT_TRUE(shard.IsValidSeq(block_idx, seq1));

  // Write 1 completes
  uint64_t end_time1 = 20;
  shard.EndWrite(block_idx, seq1, end_time1,
                 /*oldest_read=*/UINT64_MAX, absl::Now());
  EXPECT_TRUE(shard.IsValidSeq(block_idx, seq1));

  // Write 2 overlaps
  uint64_t seq2 = 2;
  uint64_t start_time2 = 30;
  shard.StartWrite(block_idx, seq2, start_time2, absl::Now());
  EXPECT_TRUE(
      shard.IsValidSeq(block_idx, seq1));  // Still valid (was completed)
  EXPECT_TRUE(shard.IsValidSeq(block_idx, seq2));  // Now valid (in-flight)

  // Corrupt sequence
  EXPECT_FALSE(shard.IsValidSeq(block_idx, 999));
}

TEST(BlockShardTest, ValidateTrimSequenceTracking) {
  BlockShard shard(200);
  uint64_t block_idx = 100;

  // Write 1 (Real)
  uint64_t seq1 = 1;
  uint64_t start_time1 = 10;
  shard.StartWrite(block_idx, seq1, start_time1, absl::Now());
  shard.EndWrite(block_idx, seq1, 20, UINT64_MAX, absl::Now());

  // Write 2 (Trim)
  uint64_t seq2 = 2 | kTrimBit;
  uint64_t start_time2 = 30;
  shard.StartWrite(block_idx, seq2, start_time2, absl::Now());
  shard.EndWrite(block_idx, seq2, 40, UINT64_MAX, absl::Now());

  // Read starts at 50. Since TRIM is active, it should be able to read TRIM.
  EXPECT_TRUE(shard.HasTrim(block_idx, 50));
  // It should also be able to read seq1 since TRIM doesn't overwrite real data.
  EXPECT_TRUE(shard.IsValidSeq(block_idx, seq1));

  // Write 3 (Real) overwrites everything
  uint64_t seq3 = 3;
  uint64_t start_time3 = 60;
  shard.StartWrite(block_idx, seq3, start_time3, absl::Now());
  shard.EndWrite(block_idx, seq3, 70, UINT64_MAX, absl::Now());

  // Read starts at 80. seq1 and TRIM should both be completely overwritten.
  EXPECT_FALSE(shard.HasTrim(block_idx, 80));
  // seq3 is valid.
  EXPECT_TRUE(shard.IsValidSeq(block_idx, seq3));

  // Pruning check: oldest_read = 90. Both seq1 and seq2 should be pruned.
  shard.EndWrite(block_idx, seq3, 70, 90, absl::Now());
  EXPECT_FALSE(shard.IsValidSeq(block_idx, seq1));
}

class MockBlockDevice : public BlockDevice {
 public:
  MockBlockDevice(size_t size, bool is_block_device)
      : data_(size, 0), is_block_device_(is_block_device) {}

  bool IsBlockDevice() const override { return is_block_device_; }

  absl::Status Pread(void* buf, size_t count, off_t offset) override {
    absl::MutexLock lock(&mu_);
    if (offset + count > data_.size())
      return absl::OutOfRangeError("pread out of bounds");

    // Simulate torn read logic
    // Just copy data directly for now
    memcpy(buf, data_.data() + offset, count);

    // Perform simulated corruptions if requested
    if (corrupt_next_read_) {
      corrupt_next_read_ = false;
      static_cast<uint8_t*>(buf)[0] ^= 0xFF;  // Corrupt first byte
    }

    return absl::OkStatus();
  }

  absl::Status Pwrite(const void* buf, size_t count, off_t offset) override {
    absl::MutexLock lock(&mu_);
    if (offset + count > data_.size())
      return absl::OutOfRangeError("pwrite out of bounds");

    // If we're dropping writes, return OkStatus() but don't write.
    if (drop_next_write_) {
      drop_next_write_ = false;
      return absl::OkStatus();
    }

    memcpy(data_.data() + offset, buf, count);

    // If we want a torn write, we corrupt half the write right after doing it
    if (torn_next_write_) {
      torn_next_write_ = false;
      size_t half = count / 2;
      for (size_t i = half; i < count; ++i) {
        data_[offset + i] ^= 0x55;
      }
    }

    return absl::OkStatus();
  }

  absl::Status Discard(off_t offset, size_t count) override {
    absl::MutexLock lock(&mu_);
    if (offset + count > data_.size())
      return absl::OutOfRangeError("discard out of bounds");

    if (ignore_some_discards_ && (discard_count_++ % 2 == 0)) {
      // Physically ignore the discard, leaving old data intact
      return absl::OkStatus();
    }

    if (discard_writes_zeroes_) {
      memset(data_.data() + offset, 0, count);
    } else {
      memset(data_.data() + offset, 0xAA, count);
    }
    return absl::OkStatus();
  }

  void CorruptData(off_t offset, size_t count) {
    absl::MutexLock lock(&mu_);
    for (size_t i = 0; i < count; ++i) {
      if (offset + i < data_.size()) {
        data_[offset + i] ^= 0xFF;
      }
    }
  }

  void FillWithValidFormatGarbage(uint64_t block_size) {
    absl::MutexLock lock(&mu_);
    for (size_t offset = 0; offset < data_.size(); offset += block_size) {
      uint64_t* ptr = reinterpret_cast<uint64_t*>(data_.data() + offset);
      uint64_t block_idx = offset / block_size;
      ptr[0] = 1337 + block_idx;
      ptr[1] = block_idx;
      ptr[2] = 1;
    }
  }

  void SetDropNextWrite(bool b) {
    absl::MutexLock lock(&mu_);
    drop_next_write_ = b;
  }

  void SetTornNextWrite(bool b) {
    absl::MutexLock lock(&mu_);
    torn_next_write_ = b;
  }

  void SetCorruptNextRead(bool b) {
    absl::MutexLock lock(&mu_);
    corrupt_next_read_ = b;
  }

  void SetDiscardWritesZeroes(bool b) {
    absl::MutexLock lock(&mu_);
    discard_writes_zeroes_ = b;
  }

  void SetIgnoreSomeDiscards(bool b) {
    absl::MutexLock lock(&mu_);
    ignore_some_discards_ = b;
  }

 private:
  absl::Mutex mu_;
  std::vector<uint8_t> data_;
  bool is_block_device_;

  bool drop_next_write_ = false;
  bool torn_next_write_ = false;
  bool corrupt_next_read_ = false;
  bool discard_writes_zeroes_ = true;
  bool ignore_some_discards_ = false;
  uint64_t discard_count_ = 0;
};

TEST(BlockValidatorE2ETest, RunSuccessfulValidation) {
  MockBlockDevice device(10 * 1024 * 1024, false);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 2;
  options.run_time_sec = 2;
  options.skip_init = false;

  absl::Status status = RunValidator(options, &device);
  EXPECT_TRUE(status.ok());
}

TEST(BlockValidatorE2ETest, DetectsCorruption) {
  MockBlockDevice device(10 * 1024 * 1024, false);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 2;
  options.run_time_sec = 3;
  options.skip_init = false;

  std::thread corrupter([&]() {
    absl::SleepFor(absl::Seconds(1));
    device.SetCorruptNextRead(true);
  });

  absl::Status status = RunValidator(options, &device);
  corrupter.join();

  EXPECT_FALSE(status.ok());
}

TEST(BlockValidatorE2ETest, RunSuccessfulSkipInitValidation) {
  MockBlockDevice device(10 * 1024 * 1024, false);

  // Fill with garbage
  device.CorruptData(0, 10 * 1024 * 1024);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 2;
  options.run_time_sec = 2;
  options.skip_init = true;
  options.validate_on_exit = true;

  absl::Status status = RunValidator(options, &device);
  EXPECT_TRUE(status.ok());
}

TEST(BlockValidatorE2ETest, RunSuccessfulSkipInitValidationWithMultiThreads) {
  MockBlockDevice device(10 * 1024 * 1024, false);

  // Fill with garbage
  device.CorruptData(0, 10 * 1024 * 1024);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 10;
  options.run_time_sec = 2;
  options.skip_init = true;
  options.validate_on_exit = true;

  absl::Status status = RunValidator(options, &device);
  EXPECT_TRUE(status.ok());
}

TEST(BlockValidatorE2ETest, DetectsDroppedWriteWithReadBeforeWrite) {
  MockBlockDevice device(10 * 1024 * 1024, false);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 1;
  options.run_time_sec = 3;
  options.skip_init = false;
  options.read_before_write = true;  // IMPORTANT
  options.validate_on_exit = true;

  std::thread corrupter([&]() {
    absl::SleepFor(absl::Seconds(1));
    device.SetDropNextWrite(true);
  });

  absl::Status status = RunValidator(options, &device);
  corrupter.join();

  EXPECT_FALSE(status.ok());
}

TEST(BlockValidatorE2ETest, DetectsTornWriteWithReadBeforeWrite) {
  MockBlockDevice device(10 * 1024 * 1024, false);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 1;
  options.run_time_sec = 3;
  options.skip_init = false;
  options.read_before_write = true;
  options.validate_on_exit = true;

  std::thread corrupter([&]() {
    absl::SleepFor(absl::Seconds(1));
    device.SetTornNextWrite(true);
  });

  absl::Status status = RunValidator(options, &device);
  corrupter.join();

  EXPECT_FALSE(status.ok());
}

TEST(BlockValidatorE2ETest, DetectsTrimGarbageIfValidateTrimZeroesIsTrue) {
  MockBlockDevice device(10 * 1024 * 1024, true);
  device.SetDiscardWritesZeroes(false);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 2;
  options.run_time_sec = 2;
  options.skip_init = false;
  options.validate_trim_zeroes = true;

  absl::Status status = RunValidator(options, &device);
  EXPECT_FALSE(status.ok());
}

TEST(BlockValidatorE2ETest, IgnoresTrimGarbageIfValidateTrimZeroesIsFalse) {
  MockBlockDevice device(10 * 1024 * 1024, true);
  device.SetDiscardWritesZeroes(false);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 2;
  options.run_time_sec = 2;
  options.skip_init = false;
  options.validate_trim_zeroes = false;

  absl::Status status = RunValidator(options, &device);
  EXPECT_TRUE(status.ok());
}

TEST(BlockValidatorE2ETest,
     IgnoresTrimAndRetainsOldDataWhenDiscardWritesZeroesIsTrue) {
  MockBlockDevice device(10 * 1024 * 1024, true);
  device.SetDiscardWritesZeroes(true);
  device.SetIgnoreSomeDiscards(true);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 2;
  options.run_time_sec = 2;
  options.skip_init = false;
  // If the device writes zeroes, we can ask the validator to validate them.
  options.validate_trim_zeroes = true;

  absl::Status status = RunValidator(options, &device);
  EXPECT_TRUE(status.ok());
}

TEST(BlockValidatorE2ETest,
     IgnoresTrimAndRetainsOldDataWhenDiscardWritesZeroesIsFalse) {
  MockBlockDevice device(10 * 1024 * 1024, true);
  device.SetDiscardWritesZeroes(false);
  device.SetIgnoreSomeDiscards(true);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 2;
  options.run_time_sec = 2;
  options.skip_init = false;
  // If the device does not guarantee zeroes, we MUST NOT validate zeroes.
  options.validate_trim_zeroes = false;

  absl::Status status = RunValidator(options, &device);
  EXPECT_TRUE(status.ok());
}

TEST(BlockValidatorE2ETest, RunSkipInitWithGarbageBlocks) {
  ValidatorOptions options;
  options.num_threads = 4;
  options.run_time_sec = 2;
  options.skip_init = true;
  options.validate_on_exit = true;
  MockBlockDevice device(options.region_size, false);
  device.FillWithValidFormatGarbage(options.block_size);
  absl::Status status = RunValidator(options, &device);
  EXPECT_TRUE(status.ok());
}

TEST(BlockValidatorE2ETest, RunSkipInitWithMultipleThreads) {
  MockBlockDevice device(10 * 1024 * 1024, false);
  device.CorruptData(0, 10 * 1024 * 1024);

  ValidatorOptions options;
  options.region_offset = 0;
  options.region_size = 10 * 1024 * 1024;
  options.block_size = 4096;
  options.num_threads = 4;
  options.run_time_sec = 5;
  options.skip_init = true;
  options.validate_on_exit = true;

  absl::Status status = RunValidator(options, &device);
  EXPECT_TRUE(status.ok());
}

class TestPosixFileDevice : public BlockDevice {
  int fd_;
  absl::Mutex file_mutex_;

 public:
  TestPosixFileDevice(int fd) : fd_(fd) {}
  ~TestPosixFileDevice() override { close(fd_); }

  bool IsBlockDevice() const override { return false; }

  absl::Status Pread(void* buf, size_t count, off_t offset) override {
    absl::MutexLock lock(&file_mutex_);
    ssize_t ret = pread(fd_, buf, count, offset);
    if (ret != count) return absl::InternalError("pread failed");
    return absl::OkStatus();
  }

  absl::Status Pwrite(const void* buf, size_t count, off_t offset) override {
    absl::MutexLock lock(&file_mutex_);
    ssize_t ret = pwrite(fd_, buf, count, offset);
    if (ret != count) return absl::InternalError("pwrite failed");
    return absl::OkStatus();
  }

  absl::Status Discard(off_t offset, size_t count) override {
    absl::MutexLock lock(&file_mutex_);
    if (fallocate(fd_, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset,
                  count) < 0) {
      return absl::InternalError("fallocate failed");
    }
    return absl::OkStatus();
  }
};

TEST(BlockValidatorE2ETest, RunsOnLocalFile) {
  std::string tmp_file = absl::StrCat(testing::TempDir(), "/test_file_",
                                      absl::ToUnixMicros(absl::Now()));
  int fd = open(tmp_file.c_str(), O_RDWR | O_CREAT | O_DIRECT | O_SYNC, 0644);
  if (fd < 0) {
    // If tmpfs does not support O_DIRECT (often true in testing environments),
    // fall back to default flags.
    fd = open(tmp_file.c_str(), O_RDWR | O_CREAT | O_SYNC, 0644);
  }
  ASSERT_GE(fd, 0);

  // Initialize file to 16 MB
  uint64_t file_size = 16 * 1024 * 1024;
  ASSERT_EQ(0, ftruncate(fd, file_size));

  TestPosixFileDevice file_device(fd);

  ValidatorOptions options;
  options.target_file = tmp_file;
  options.region_offset = 0;
  options.region_size = file_size;
  options.block_size = 4096;
  options.num_threads = 4;
  options.run_time_sec = 2;
  options.skip_init = false;
  options.validate_on_exit = true;

  absl::Status status = RunValidator(options, &file_device);
  EXPECT_TRUE(status.ok()) << status;
}

}  // namespace
