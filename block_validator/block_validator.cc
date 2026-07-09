#include <fcntl.h>
#include <linux/fs.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "block_validator/block_validator_lib.h"
#include "strings/human_readable.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

ABSL_FLAG(std::string, target_file, "", "Target block device or file");
ABSL_FLAG(std::string, region_offset, "0",
          "Offset in the target device (accepts 1G, 100M, etc)");
ABSL_FLAG(std::string, region_size, "0",
          "Size of the region to test (0 = auto size, accepts 1G, 100M, etc)");
ABSL_FLAG(uint64_t, block_size, 4096, "Atomic write unit size");
ABSL_FLAG(int, num_threads, 4, "Number of IO threads");
ABSL_FLAG(int, run_time_sec, 0, "Run time in seconds (0 = infinite)");
ABSL_FLAG(bool, skip_init, false,
          "Skip sequential initialization (device must be zeroed)");
ABSL_FLAG(bool, validate_on_exit, false,
          "If true, validates the entire disk at the end of the run");
ABSL_FLAG(bool, validate_trim_zeroes, true,
          "If true, reading a trimmed block can validly return zeroes. Some "
          "NVMe devices guarantee this.");
ABSL_FLAG(bool, read_before_write, true, "Whether to read before writing");

std::atomic<int> g_sigint_count{0};

void SigIntHandler(int) {
  g_sigint_count.fetch_add(1, std::memory_order_relaxed);
}

class PosixBlockDevice : public BlockDevice {
 public:
  explicit PosixBlockDevice(int fd) : fd_(fd) {}
  ~PosixBlockDevice() override { close(fd_); }

  bool IsBlockDevice() const override { return true; }

  absl::Status Pread(void* buf, size_t count, off_t offset) override {
    ssize_t ret = pread(fd_, buf, count, offset);
    if (ret != count) return absl::InternalError("pread failed");
    return absl::OkStatus();
  }

  absl::Status Pwrite(const void* buf, size_t count, off_t offset) override {
    ssize_t ret = pwrite(fd_, buf, count, offset);
    if (ret != count) return absl::InternalError("pwrite failed");
    return absl::OkStatus();
  }

  absl::Status Discard(off_t offset, size_t count) override {
    uint64_t range[2] = {static_cast<uint64_t>(offset),
                         static_cast<uint64_t>(count)};
    if (ioctl(fd_, BLKDISCARD, &range) < 0) {
      return absl::InternalError("BLKDISCARD failed");
    }
    return absl::OkStatus();
  }

 private:
  int fd_;
};

class PosixFileDevice : public BlockDevice {
 public:
  explicit PosixFileDevice(int fd) : fd_(fd) {}
  ~PosixFileDevice() override { close(fd_); }

  bool IsBlockDevice() const override { return false; }

  absl::Status Pread(void* buf, size_t count, off_t offset) override {
    absl::MutexLock lock(file_mutex_);
    ssize_t ret = pread(fd_, buf, count, offset);
    if (ret != count) return absl::InternalError("pread failed");
    return absl::OkStatus();
  }

  absl::Status Pwrite(const void* buf, size_t count, off_t offset) override {
    absl::MutexLock lock(file_mutex_);
    ssize_t ret = pwrite(fd_, buf, count, offset);
    if (ret != count) return absl::InternalError("pwrite failed");
    return absl::OkStatus();
  }

  absl::Status Discard(off_t offset, size_t count) override {
    absl::MutexLock lock(file_mutex_);
    if (fallocate(fd_, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset,
                  count) < 0) {
      return absl::InternalError("fallocate failed");
    }
    return absl::OkStatus();
  }

 private:
  int fd_;
  absl::Mutex file_mutex_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  struct sigaction sig_int_handler;
  sig_int_handler.sa_handler = SigIntHandler;
  sigemptyset(&sig_int_handler.sa_mask);
  sig_int_handler.sa_flags = 0;
  sigaction(SIGINT, &sig_int_handler, nullptr);

  ValidatorOptions options;
  options.target_file = absl::GetFlag(FLAGS_target_file);

  std::string region_offset_str = absl::GetFlag(FLAGS_region_offset);
  int64_t region_offset_parsed = 0;
  if (!strings::HumanReadableNumBytes::ToInt64(region_offset_str,
                                               &region_offset_parsed)) {
    LOG(ERROR) << "Invalid region_offset: " << region_offset_str;
    return 1;
  }
  options.region_offset = region_offset_parsed;

  std::string region_size_str = absl::GetFlag(FLAGS_region_size);
  int64_t region_size_parsed = 0;
  if (!strings::HumanReadableNumBytes::ToInt64(region_size_str,
                                               &region_size_parsed)) {
    LOG(ERROR) << "Invalid region_size: " << region_size_str;
    return 1;
  }
  options.region_size = region_size_parsed;

  options.block_size = absl::GetFlag(FLAGS_block_size);
  options.num_threads = absl::GetFlag(FLAGS_num_threads);
  options.run_time_sec = absl::GetFlag(FLAGS_run_time_sec);
  options.skip_init = absl::GetFlag(FLAGS_skip_init);
  options.validate_on_exit = absl::GetFlag(FLAGS_validate_on_exit);
  options.validate_trim_zeroes = absl::GetFlag(FLAGS_validate_trim_zeroes);
  options.read_before_write = absl::GetFlag(FLAGS_read_before_write);
  options.sigint_count_ptr = &g_sigint_count;

  if (options.target_file.empty()) {
    LOG(ERROR) << "Must specify --target_file";
    return 1;
  }

  bool create_file = false;
  struct stat st;
  if (stat(options.target_file.c_str(), &st) != 0) {
    if (errno == ENOENT) {
      create_file = true;
    } else {
      LOG(ERROR) << "Failed to stat target file: " << options.target_file
                 << ", error: " << errno;
      return 1;
    }
  }

  int open_flags = O_RDWR | O_DIRECT | O_SYNC;
  if (create_file) {
    open_flags |= O_CREAT;
  }

  int fd = open(options.target_file.c_str(), open_flags, 0644);
  if (fd < 0) {
    LOG(ERROR) << "Failed to open target file: " << options.target_file
               << ", error: " << errno;
    return 1;
  }

  int flags = fcntl(fd, F_GETFL);
  if (flags == -1) {
    LOG(ERROR) << "Failed to get file flags: " << errno;
    return 1;
  }
  if ((flags & O_DIRECT) == 0) {
    LOG(ERROR) << "Failed to verify O_DIRECT flag on target file. "
                  "The tool must bypass filesystem caching to work properly. "
                  "Ensure the filesystem supports O_DIRECT.";
    return 1;
  }

  bool is_block_device = false;
  if (fstat(fd, &st) == 0 && S_ISBLK(st.st_mode)) {
    is_block_device = true;
  }

  uint64_t region_size = options.region_size;
  if (is_block_device) {
    uint64_t device_size = 0;
    if (ioctl(fd, BLKGETSIZE64, &device_size) < 0) {
      LOG(ERROR) << "Failed to query block device size, error: " << errno;
      return 1;
    }
    if (options.region_offset >= device_size) {
      LOG(ERROR) << "Error: region_offset ("
                 << strings::HumanReadableNumBytes::ToString(
                        options.region_offset)
                 << ") is beyond the block device size ("
                 << strings::HumanReadableNumBytes::ToString(device_size)
                 << ").";
      return 1;
    }
    if (region_size == 0) {
      region_size = device_size - options.region_offset;
    } else if (options.region_offset + region_size > device_size) {
      LOG(ERROR) << "Error: Requested region (offset: "
                 << strings::HumanReadableNumBytes::ToString(
                        options.region_offset)
                 << ", size: "
                 << strings::HumanReadableNumBytes::ToString(region_size)
                 << ") exceeds the block device size ("
                 << strings::HumanReadableNumBytes::ToString(device_size)
                 << ").";
      return 1;
    }
  } else {
    if (create_file || st.st_size == 0) {
      if (region_size == 0) {
        region_size = 1024ULL * 1024 * 1024;  // 1GiB default
      }
      if (ftruncate(fd, region_size) < 0) {
        LOG(ERROR) << "Failed to truncate file, error: " << errno;
        return 1;
      }
    } else {
      if (region_size == 0) {
        region_size = st.st_size;
      }
    }
  }
  options.region_size = region_size;

  LOG(INFO) << "Testing region size: "
            << strings::HumanReadableNumBytes::ToString(options.region_size)
            << " at offset "
            << strings::HumanReadableNumBytes::ToString(options.region_offset)
            << " on " << (is_block_device ? "block device" : "file") << " "
            << options.target_file;

  std::unique_ptr<BlockDevice> device;
  if (is_block_device) {
    device = std::make_unique<PosixBlockDevice>(fd);
  } else {
    device = std::make_unique<PosixFileDevice>(fd);
  }

  absl::Status status = RunValidator(options, device.get());
  if (!status.ok()) {
    LOG(ERROR) << "Validator failed: " << status;
    return 1;
  }
  return 0;
}
