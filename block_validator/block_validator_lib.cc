#include "block_validator/block_validator_lib.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

bool BlockShard::StartWrite(uint64_t local_idx, uint64_t seq,
                            uint64_t start_time, absl::Time wall_start_time) {
  absl::MutexLock lock(mutex_);

  uint64_t state = states_[local_idx];
  bool is_overlapping = false;

  if ((state & (1ULL << 63)) == 0) {
    uint32_t overflow_idx = 0;
    if (free_overflow_indices_.empty()) {
      overflow_idx = overflow_states_.size();
      overflow_states_.push_back(ActiveState());
    } else {
      overflow_idx = free_overflow_indices_.back();
      free_overflow_indices_.pop_back();
    }
    auto& active = overflow_states_[overflow_idx];
    active.history.push_back({state, 0, start_time - 1, UINT64_MAX});
    active.history.push_back({seq, start_time, 0, UINT64_MAX});
    states_[local_idx] = (1ULL << 63) | overflow_idx;
  } else {
    is_overlapping = true;
    uint32_t overflow_idx = state & ~(1ULL << 63);
    auto& active = overflow_states_[overflow_idx];
    active.history.push_back({seq, start_time, 0, UINT64_MAX});
  }

  auto& log = action_logs_[local_idx];
  log.entries[log.idx % 5] = {seq, wall_start_time, absl::InfiniteFuture()};
  log.idx++;

  return is_overlapping;
}

void BlockShard::EndWrite(uint64_t local_idx, uint64_t seq, uint64_t end_time,
                          uint64_t oldest_read, absl::Time wall_end_time) {
  absl::MutexLock lock(mutex_);
  uint64_t state = states_[local_idx];
  if ((state & (1ULL << 63)) == 0) return;

  uint32_t overflow_idx = state & ~(1ULL << 63);
  auto& active = overflow_states_[overflow_idx];

  for (auto& w : active.history) {
    if (w.seq == seq) w.end_time = end_time;
  }

  for (auto& w : active.history) {
    if (w.end_time > 0 && w.overwritten_time == UINT64_MAX) {
      uint64_t min_overwritten_time = UINT64_MAX;
      for (const auto& other : active.history) {
        if (other.end_time > 0) {
          bool overwrites = false;
          if ((w.seq & kTrimBit) == 0) {
            if ((other.seq & kTrimBit) == 0) {
              if (w.end_time < other.start_time) overwrites = true;
            }
          } else {
            if (w.end_time < other.start_time) overwrites = true;
          }
          if (overwrites) {
            if (other.end_time < min_overwritten_time) {
              min_overwritten_time = other.end_time;
            }
          }
        }
      }
      if (min_overwritten_time != UINT64_MAX) {
        w.overwritten_time = min_overwritten_time;
      }
    }
  }

  size_t keep = 0;
  for (size_t i = 0; i < active.history.size(); ++i) {
    if (active.history[i].overwritten_time < oldest_read) {
      // Pruned
    } else {
      active.history[keep++] = active.history[i];
    }
  }
  active.history.resize(keep);

  if (active.history.size() == 1 && active.history[0].end_time > 0) {
    states_[local_idx] = active.history[0].seq;
    active.history.clear();
    free_overflow_indices_.push_back(overflow_idx);
  }

  auto& log = action_logs_[local_idx];
  for (int i = 0; i < 5; ++i) {
    if (log.entries[i].seq == seq) {
      log.entries[i].write_end = wall_end_time;
      break;
    }
  }
}

void BlockShard::PrintActionLog(uint64_t local_idx,
                                absl::Time read_wall_start_time) {
  absl::MutexLock lock(mutex_);
  LOG(ERROR) << "Read started at: " << read_wall_start_time;
  LOG(ERROR) << "Last 5 write actions for this block:";
  auto& log = action_logs_[local_idx];
  for (int k = 0; k < 5; ++k) {
    int entry_idx = (log.idx + k) % 5;
    auto& entry = log.entries[entry_idx];
    if (entry.write_start != absl::InfinitePast()) {
      LOG(ERROR) << "  Action " << k << ": seq=" << entry.seq
                 << ", write_start=" << entry.write_start
                 << ", write_end=" << entry.write_end;
    }
  }
}

bool BlockShard::IsValidSeq(uint64_t local_idx, uint64_t seq) {
  absl::MutexLock lock(mutex_);
  uint64_t state = states_[local_idx];
  if ((state & (1ULL << 63)) == 0) {
    return state == seq;
  } else {
    uint32_t overflow_idx = state & ~(1ULL << 63);
    for (const auto& w : overflow_states_[overflow_idx].history) {
      if (w.seq == seq) return true;
    }
    return false;
  }
}

bool BlockShard::GetSeqTimes(uint64_t local_idx, uint64_t seq,
                             uint64_t& start_time, uint64_t& end_time,
                             uint64_t& overwritten_time) {
  overwritten_time = UINT64_MAX;
  absl::MutexLock lock(mutex_);
  uint64_t state = states_[local_idx];
  if ((state & (1ULL << 63)) == 0) {
    if (state == seq) {
      start_time = 0;
      end_time = UINT64_MAX;
      overwritten_time = UINT64_MAX;
      return true;
    }
    return false;
  } else {
    uint32_t overflow_idx = state & ~(1ULL << 63);
    for (const auto& w : overflow_states_[overflow_idx].history) {
      if (w.seq == seq) {
        start_time = w.start_time;
        end_time = w.end_time == 0 ? UINT64_MAX : w.end_time;
        overwritten_time = w.overwritten_time;
        return true;
      }
    }
    return false;
  }
}

void GenerateBlockData(absl::Span<uint8_t> buffer, uint64_t seq,
                       uint64_t block_idx, uint64_t write_start_block,
                       uint64_t write_num_blocks) {
  uint64_t* ptr = reinterpret_cast<uint64_t*>(buffer.data());
  size_t num_words = buffer.size() / 8;
  ptr[0] = seq;
  ptr[1] = block_idx;
  ptr[2] = write_start_block;
  ptr[3] = write_num_blocks;
  for (size_t i = 4; i < num_words; ++i) {
    ptr[i] = seq ^ block_idx ^ write_start_block ^ write_num_blocks ^ i;
  }
}

bool VerifyBlockData(absl::Span<const uint8_t> buffer, uint64_t block_idx,
                     uint64_t& out_seq, uint64_t& out_write_start_block,
                     uint64_t& out_write_num_blocks) {
  const uint64_t* ptr = reinterpret_cast<const uint64_t*>(buffer.data());
  size_t num_words = buffer.size() / 8;

  // The first 4 words contain the metadata payload:
  // Word 0: The sequence number. 0 means uninitialized/trimmed block.
  // Word 1: The block index (LBA) representing where this block belongs.
  // Word 2: The starting block index of the write operation that generated this
  // block. Word 3: The total number of blocks in the write operation that
  // generated this block.
  uint64_t seq = ptr[0];
  out_seq = seq;
  out_write_start_block = ptr[2];
  out_write_num_blocks = ptr[3];

  if (seq == 0) {
    for (size_t i = 1; i < num_words; ++i) {
      if (ptr[i] != 0) return false;
    }
    return true;
  }

  if (ptr[1] != block_idx) return false;

  for (size_t i = 4; i < num_words; ++i) {
    if (ptr[i] !=
        (seq ^ block_idx ^ out_write_start_block ^ out_write_num_blocks ^ i))
      return false;
  }
  return true;
}

namespace {

struct InFlightWrite {
  uint64_t seq;
  uint64_t start_block;
  uint64_t num_blocks;
};

class ValidatorContext {
 public:
  ValidatorContext(const ValidatorOptions& options, BlockDevice* device);

  absl::Status InitDevice();

  absl::Status Run();

 private:
  uint64_t GetOldestRead();

  uint64_t GenerateOverlappingIo(
      absl::BitGen& bitgen, const std::vector<InFlightWrite>& in_flight_writes,
      const uint64_t num_blocks);

  void IssueWrite(int thread_id, absl::BitGen& bitgen,
                  const uint64_t num_blocks, bool is_trim = false);

  void DoReadAndValidate(int thread_id, uint64_t start_block,
                         const uint64_t num_blocks);

  void IssueRead(int& thread_id, absl::BitGen& bitgen,
                 const uint64_t num_blocks);

  void WorkerThread(int thread_id);

 private:
  BlockDevice* device_;
  ValidatorOptions options_;
  uint64_t num_blocks_;
  uint64_t shard_shift_ = 0;
  uint64_t shard_mask_ = 0;
  std::vector<std::unique_ptr<BlockShard>> shards_;

  std::atomic<uint64_t> logical_clock_{2};
  std::atomic<uint64_t> seq_allocator_{2};
  std::atomic<bool> validation_failed_{false};
  std::atomic<bool> stop_io_{false};

  std::atomic<uint64_t> read_io_count_{0};
  std::atomic<uint64_t> read_block_count_{0};
  std::atomic<uint64_t> write_io_count_{0};
  std::atomic<uint64_t> write_block_count_{0};
  std::atomic<uint64_t> trim_io_count_{0};
  std::atomic<uint64_t> trim_block_count_{0};
  std::atomic<uint64_t> overlapping_write_count_{0};

  bool is_block_device_ = false;
  std::atomic<bool> printed_trim_warning_{false};

  std::vector<std::atomic<uint64_t>> worker_read_starts_;

  absl::Mutex issue_mutex_;
  std::vector<InFlightWrite> in_flight_writes_ ABSL_GUARDED_BY(issue_mutex_);
};

ValidatorContext::ValidatorContext(const ValidatorOptions& options,
                                   BlockDevice* device)
    : device_(device),
      options_(options),
      num_blocks_(options.region_size / options.block_size),
      worker_read_starts_(options.num_threads) {
  if (options_.num_threads <= 0) {
    LOG(FATAL) << "num_threads must be > 0";
  }
  if (options_.block_size % 8 != 0) {
    LOG(FATAL) << "block_size must be a multiple of 8";
  }
  size_t num_shards = 1;
  while (num_shards * 2 <= std::min<size_t>(num_blocks_, 65536)) {
    num_shards *= 2;
  }
  shards_.resize(num_shards);
  size_t shift = 0;
  while ((1ULL << shift) < num_shards) shift++;
  shard_shift_ = shift;
  shard_mask_ = num_shards - 1;

  for (size_t i = 0; i < shards_.size(); ++i) {
    size_t local_blocks = (num_blocks_ + num_shards - 1 - i) / num_shards;
    shards_[i] = std::make_unique<BlockShard>(local_blocks);
  }
  for (int i = 0; i < options_.num_threads; ++i) {
    worker_read_starts_[i].store(UINT64_MAX, std::memory_order_relaxed);
  }

  is_block_device_ = device_->IsBlockDevice();
}

absl::Status ValidatorContext::InitDevice() {
  if (options_.skip_init) {
    LOG(INFO) << "Skipping device initialization";
    return absl::OkStatus();
  }
  LOG(INFO) << "Initializing device...";

  size_t chunk_size = std::min<size_t>(1024 * 1024, options_.region_size);
  void* ptr = nullptr;
  int ret = posix_memalign(&ptr, 4096, chunk_size);
  if (ret != 0) {
    return absl::ErrnoToStatus(ret, "posix_memalign failed");
  }
  memset(ptr, 0, chunk_size);

  for (off_t offset = 0; offset < static_cast<off_t>(options_.region_size);
       offset += chunk_size) {
    if (options_.sigint_count_ptr &&
        options_.sigint_count_ptr->load(std::memory_order_relaxed) > 0) {
      LOG(INFO) << "SIGINT received, aborting initialization...";
      free(ptr);
      return absl::CancelledError("Initialization cancelled by user");
    }
    size_t to_write = std::min(
        chunk_size, static_cast<size_t>(options_.region_size - offset));
    absl::Status status =
        device_->Pwrite(ptr, to_write, options_.region_offset + offset);
    if (!status.ok()) {
      free(ptr);
      LOG(FATAL) << "Init device failed: " << status;
    }
  }
  free(ptr);
  LOG(INFO) << "Initialization complete.";
  return absl::OkStatus();
}

absl::Status ValidatorContext::Run() {
  absl::Time test_start_time = absl::Now();
  absl::Status init_status = InitDevice();
  if (!init_status.ok()) {
    return init_status;
  }

  LOG(INFO) << "Starting " << options_.num_threads << " IO threads...";
  std::vector<std::thread> threads;
  for (int i = 0; i < options_.num_threads; ++i) {
    threads.emplace_back(&ValidatorContext::WorkerThread, this, i);
  }

  absl::Time end_time = absl::InfiniteFuture();
  if (options_.run_time_sec > 0) {
    end_time = absl::Now() + absl::Seconds(options_.run_time_sec);
  }

  while (absl::Now() < end_time) {
    if (options_.sigint_count_ptr &&
        options_.sigint_count_ptr->load(std::memory_order_relaxed) > 0) {
      LOG(INFO) << "SIGINT received, stopping IO...";
      break;
    }
    absl::SleepFor(absl::Seconds(1));
  }

  LOG(INFO) << "Stopping...";
  stop_io_.store(true, std::memory_order_relaxed);

  for (auto& t : threads) {
    t.join();
  }

  int sigint_count =
      options_.sigint_count_ptr
          ? options_.sigint_count_ptr->load(std::memory_order_relaxed)
          : 0;
  bool should_validate = options_.validate_on_exit || sigint_count == 1;
  if (should_validate) {
    LOG(INFO) << "Validating entire disk contents...";
    const size_t batch_blocks = 1024 * 1024 / options_.block_size;
    for (uint64_t i = 0; i < num_blocks_; i += batch_blocks) {
      if (options_.sigint_count_ptr &&
          options_.sigint_count_ptr->load(std::memory_order_relaxed) > 1) {
        LOG(WARNING) << "Second SIGINT received, aborting validation...";
        break;
      }
      uint64_t blocks_to_read =
          std::min(static_cast<uint64_t>(batch_blocks), num_blocks_ - i);
      DoReadAndValidate(-1, i, blocks_to_read);
    }
    LOG(INFO) << "Final validation complete.";
  }

  absl::Duration test_duration = absl::Now() - test_start_time;
  LOG(INFO) << "Metrics:";
  LOG(INFO) << "  Read IO count: "
            << read_io_count_.load(std::memory_order_relaxed)
            << ", read blocks count: "
            << read_block_count_.load(std::memory_order_relaxed);
  LOG(INFO) << "  Write IO count: "
            << write_io_count_.load(std::memory_order_relaxed)
            << ", write blocks count: "
            << write_block_count_.load(std::memory_order_relaxed);
  if (is_block_device_) {
    LOG(INFO) << "  Trim IO count: "
              << trim_io_count_.load(std::memory_order_relaxed)
              << ", trim blocks count: "
              << trim_block_count_.load(std::memory_order_relaxed);
  }
  LOG(INFO) << "  Overlapping write count: "
            << overlapping_write_count_.load(std::memory_order_relaxed);
  LOG(INFO) << "  Test duration: " << absl::ToInt64Seconds(test_duration)
            << "s";

  if (validation_failed_.load(std::memory_order_relaxed)) {
    LOG(ERROR) << "Test completed with validation failures.";
    return absl::InternalError("Validation failed");
  }
  LOG(INFO) << "Test completed successfully.";
  return absl::OkStatus();
}

uint64_t ValidatorContext::GetOldestRead() {
  uint64_t oldest = UINT64_MAX;
  for (size_t i = 0; i < worker_read_starts_.size(); ++i) {
    uint64_t t = worker_read_starts_[i].load(std::memory_order_acquire);
    if (t < oldest) oldest = t;
  }
  return oldest == UINT64_MAX ? logical_clock_.load() : oldest;
}

uint64_t ValidatorContext::GenerateOverlappingIo(
    absl::BitGen& bitgen, const std::vector<InFlightWrite>& in_flight_writes,
    const uint64_t num_blocks) {
  uint64_t start_block = 0;
  size_t idx = absl::Uniform<size_t>(bitgen, 0u, in_flight_writes.size());
  uint64_t target_start = in_flight_writes[idx].start_block;
  uint64_t target_len = in_flight_writes[idx].num_blocks;

  int64_t min_start = std::max(static_cast<int64_t>(0),
                               static_cast<int64_t>(target_start) -
                                   static_cast<int64_t>(num_blocks) + 1);
  int64_t max_start =
      std::min(static_cast<int64_t>(target_start + target_len - 1),
               (int64_t)num_blocks_ - (int64_t)num_blocks);
  if (min_start <= max_start) {
    start_block = absl::Uniform(bitgen, min_start, max_start + 1);
  } else {
    start_block = absl::Uniform(bitgen, 0ULL, num_blocks_ - num_blocks + 1);
  }
  return start_block;
}

void ValidatorContext::IssueWrite(int thread_id, absl::BitGen& bitgen,
                                  const uint64_t num_blocks, bool is_trim) {
  uint64_t start_block = 0;
  uint64_t seq = 0;
  uint64_t start_time = 0;

  {
    bool overlap = absl::Bernoulli(bitgen, 0.05);
    absl::MutexLock lock(issue_mutex_);
    if (overlap && !in_flight_writes_.empty()) {
      start_block =
          GenerateOverlappingIo(bitgen, in_flight_writes_, num_blocks);
    } else {
      start_block = absl::Uniform(bitgen, 0ULL, num_blocks_ - num_blocks + 1);
    }

    seq = seq_allocator_.fetch_add(1);
    if (is_trim) {
      seq |= kTrimBit;
    }
    start_time = logical_clock_.fetch_add(1);
    in_flight_writes_.push_back({seq, start_block, num_blocks});
  }

  if (is_trim) {
    trim_io_count_.fetch_add(1, std::memory_order_relaxed);
    trim_block_count_.fetch_add(num_blocks, std::memory_order_relaxed);
  } else {
    write_io_count_.fetch_add(1, std::memory_order_relaxed);
    write_block_count_.fetch_add(num_blocks, std::memory_order_relaxed);
  }

  if (options_.read_before_write) {
    DoReadAndValidate(thread_id, start_block, num_blocks);
  }

  absl::Time wall_start_time = absl::Now();
  bool has_overlap = false;
  for (uint64_t i = 0; i < num_blocks; ++i) {
    uint64_t shard_idx = (start_block + i) & shard_mask_;
    uint64_t local_idx = (start_block + i) >> shard_shift_;
    if (shards_[shard_idx]->StartWrite(local_idx, seq, start_time,
                                       wall_start_time)) {
      has_overlap = true;
    }
  }
  if (has_overlap) {
    overlapping_write_count_.fetch_add(1, std::memory_order_relaxed);
  }

  size_t size = num_blocks * options_.block_size;
  off_t offset = options_.region_offset + start_block * options_.block_size;

  if (is_trim) {
    absl::Status status = device_->Discard(offset, size);
    if (!status.ok()) {
      LOG(FATAL) << "Discard failed for offset " << offset << " size " << size
                 << ", status: " << status;
    }
  } else {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, size) != 0) {
      LOG(FATAL) << "posix_memalign failed";
    }
    for (uint64_t i = 0; i < num_blocks; ++i) {
      GenerateBlockData(
          absl::MakeSpan(static_cast<uint8_t*>(ptr) + i * options_.block_size,
                         options_.block_size),
          seq, start_block + i, start_block, num_blocks);
    }

    absl::Status status = device_->Pwrite(ptr, size, offset);
    if (!status.ok()) {
      LOG(FATAL) << "Pwrite failed: " << status;
    }
    free(ptr);
  }

  uint64_t end_time = logical_clock_.fetch_add(1);
  uint64_t oldest_read = GetOldestRead();
  absl::Time wall_end_time = absl::Now();
  for (uint64_t i = 0; i < num_blocks; ++i) {
    uint64_t shard_idx = (start_block + i) & shard_mask_;
    uint64_t local_idx = (start_block + i) >> shard_shift_;
    shards_[shard_idx]->EndWrite(local_idx, seq, end_time, oldest_read,
                                 wall_end_time);
  }

  {
    absl::MutexLock lock(issue_mutex_);
    for (auto it = in_flight_writes_.begin(); it != in_flight_writes_.end();
         ++it) {
      if (it->seq == seq) {
        in_flight_writes_.erase(it);
        break;
      }
    }
  }
}

void ValidatorContext::DoReadAndValidate(int thread_id, uint64_t start_block,
                                         const uint64_t num_blocks) {
  absl::Time read_wall_start_time = absl::Now();
  uint64_t start_time = logical_clock_.fetch_add(1);

  if (thread_id >= 0) {
    worker_read_starts_[thread_id].store(start_time, std::memory_order_release);
  }

  size_t size = num_blocks * options_.block_size;
  off_t offset = options_.region_offset + start_block * options_.block_size;

  void* ptr = nullptr;
  if (posix_memalign(&ptr, 4096, size) != 0) {
    LOG(FATAL) << "posix_memalign failed";
  }

  absl::Status status = device_->Pread(ptr, size, offset);
  if (!status.ok()) {
    LOG(FATAL) << "Pread failed: " << status;
  }

  for (uint64_t i = 0; i < num_blocks; ++i) {
    uint64_t block_idx = start_block + i;
    uint64_t read_seq = 0;
    uint64_t write_start_block = 0;
    uint64_t write_num_blocks = 0;
    bool valid_format = VerifyBlockData(
        absl::MakeSpan(static_cast<uint8_t*>(ptr) + i * options_.block_size,
                       options_.block_size),
        block_idx, read_seq, write_start_block, write_num_blocks);

    uint64_t shard_idx = block_idx & shard_mask_;
    uint64_t local_idx = block_idx >> shard_shift_;

    // Variables for sequence '0' (the initial state before any writes).
    // The start logical time of the write/trim (always 0 for sequence 0).
    uint64_t st_0 = 0;
    // The end logical time of the write/trim (always 0 for sequence 0).
    uint64_t et_0 = 0;
    // The overwrite time, i.e., the logical time when the *first* write
    // to this block completed. If ot_0 > start_time, it means sequence 0 was
    // not fully overwritten when our read started.
    uint64_t ot_0 = 0;

    bool uninit_allowed =
        options_.skip_init &&
        shards_[shard_idx]->GetSeqTimes(local_idx, 0, st_0, et_0, ot_0) &&
        (ot_0 > start_time);
    bool trim_active = shards_[shard_idx]->HasTrim(local_idx, start_time);
    bool trim_allowed = !options_.validate_trim_zeroes && trim_active;

    if (!valid_format) {
      if (uninit_allowed || trim_allowed) {
        continue;
      } else {
        if (options_.validate_trim_zeroes && trim_active) {
          if (!printed_trim_warning_.exchange(true,
                                              std::memory_order_relaxed)) {
            LOG(ERROR) << "The tool expected zeroes or the old value because "
                       << "validate_trim_zeroes is set and if the device "
                          "being tested "
                       << "does not guarantee that the user should override "
                          "the flag.";
          }
        }
        const uint64_t* ptr_err = reinterpret_cast<const uint64_t*>(
            static_cast<const uint8_t*>(ptr) + i * options_.block_size);
        LOG(ERROR) << "Corruption detected: invalid block format at block_idx "
                   << block_idx << " data=" << ptr_err[0] << "," << ptr_err[1]
                   << "," << ptr_err[2] << "," << ptr_err[3];
        shards_[shard_idx]->PrintActionLog(local_idx, read_wall_start_time);
        validation_failed_.store(true, std::memory_order_relaxed);
        continue;
      }
    }

    // Fetch the logical timestamps for the sequence we actually read from the
    // block.
    // st (start_time): When the write that generated `read_seq` began.
    uint64_t st;
    // et (end_time): When the write that generated `read_seq`
    // completed.
    uint64_t et;
    // ot (overwrite_time) : When a subsequent overlapping write to this block
    // completed.
    // If ot != UINT64_MAX, `read_seq` was explicitly overwritten.
    uint64_t ot;
    bool valid_seq =
        shards_[shard_idx]->GetSeqTimes(local_idx, read_seq, st, et, ot);
    bool originally_valid = valid_seq;
    bool causality_failed = false;

    if (valid_seq) {
      if (ot <= start_time) {
        // Causality violation: read old data that was definitely overwritten
        // before read started
        valid_seq = false;
        causality_failed = true;
      }
    }

    if (!valid_seq && read_seq == 0) {
      // If a TRIM is active, reading zeroes is always acceptable, regardless
      // of validate_trim_zeroes which only dictates whether we REJECT
      // garbage.
      if (trim_active) {
        valid_seq = true;
      }
    }

    if (!valid_seq) {
      if (uninit_allowed || trim_allowed) {
        valid_seq = true;
      }
    }

    if (!valid_seq) {
      if (options_.validate_trim_zeroes && trim_active) {
        if (!printed_trim_warning_.exchange(true, std::memory_order_relaxed)) {
          LOG(ERROR)
              << "The tool expected zeroes or the old value because "
              << "validate_trim_zeroes is set and if the device being tested "
              << "does not guarantee that the user should override the flag.";
        }
      }
      LOG(ERROR) << "Corruption detected: invalid sequence number " << read_seq
                 << " at block_idx " << block_idx
                 << " originally_valid=" << originally_valid
                 << " causality_failed=" << causality_failed << " ot=" << ot
                 << " start_time=" << start_time;
      if (thread_id >= 0) {
        LOG(ERROR) << "Read started at: " << read_wall_start_time;
      }
      shards_[shard_idx]->PrintActionLog(local_idx, read_wall_start_time);
      validation_failed_.store(true, std::memory_order_relaxed);
    }
  }
  if (thread_id >= 0) {
    worker_read_starts_[thread_id].store(UINT64_MAX, std::memory_order_release);
  }
  free(ptr);
}

void ValidatorContext::IssueRead(int& thread_id, absl::BitGen& bitgen,
                                 const uint64_t num_blocks) {
  uint64_t start_block = 0;
  {
    bool overlap = absl::Bernoulli(bitgen, 0.05);
    absl::MutexLock lock(issue_mutex_);
    if (overlap && !in_flight_writes_.empty()) {
      start_block =
          GenerateOverlappingIo(bitgen, in_flight_writes_, num_blocks);
    } else {
      start_block = absl::Uniform(bitgen, 0ULL, num_blocks_ - num_blocks + 1);
    }
  }
  read_io_count_.fetch_add(1, std::memory_order_relaxed);
  read_block_count_.fetch_add(num_blocks, std::memory_order_relaxed);
  DoReadAndValidate(thread_id, start_block, num_blocks);
}

void ValidatorContext::WorkerThread(int thread_id) {
  absl::BitGen bitgen;
  const uint64_t b16 = std::max(1ULL, 16384ULL / options_.block_size);
  const uint64_t b64 = std::max(1ULL, 65536ULL / options_.block_size);
  const uint64_t b128 = std::max(1ULL, 131072ULL / options_.block_size);

  while (!stop_io_.load(std::memory_order_relaxed)) {
    uint64_t num_blocks = 0;
    double r = absl::Uniform(bitgen, 0.0, 1.0);

    if (r < 0.5) {
      num_blocks = absl::Uniform(bitgen, 1ULL, b16 + 1);
    } else if (r < 0.9) {
      num_blocks = absl::Uniform(bitgen, std::min(b16 + 1, b64), b64 + 1);
    } else {
      num_blocks = absl::Uniform(bitgen, std::min(b64 + 1, b128), b128 + 1);
    }
    if (num_blocks > num_blocks_) {
      num_blocks = num_blocks_;
    }

    if (is_block_device_) {
      if (r < 0.5) {
        IssueRead(thread_id, bitgen, num_blocks);
      } else if (r < 0.95) {
        IssueWrite(thread_id, bitgen, num_blocks, /*is_trim=*/false);
      } else {
        IssueWrite(thread_id, bitgen, num_blocks, /*is_trim=*/true);
      }
    } else {
      if (absl::Bernoulli(bitgen, 0.5)) {
        IssueWrite(thread_id, bitgen, num_blocks, /*is_trim=*/false);
      } else {
        IssueRead(thread_id, bitgen, num_blocks);
      }
    }
  }
}

}  // namespace

absl::Status RunValidator(const ValidatorOptions& options,
                          BlockDevice* device) {
  uint64_t block_size = options.block_size;
  int num_threads = options.num_threads;

  if (block_size < 128) {
    return absl::InvalidArgumentError("block_size must be >= 128");
  }
  if (num_threads <= 0) {
    return absl::InvalidArgumentError("num_threads must be > 0");
  }

  ValidatorContext ctx(options, device);
  return ctx.Run();
}
