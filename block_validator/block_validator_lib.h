#ifndef BLOCK_VALIDATOR_BLOCK_VALIDATOR_LIB_H_
#define BLOCK_VALIDATOR_BLOCK_VALIDATOR_LIB_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

// Represents an action log entry for a specific write.
struct ActionLogEntry {
  uint64_t seq = 0;
  absl::Time write_start = absl::InfinitePast();
  absl::Time write_end = absl::InfiniteFuture();
};

// Keeps the last 5 write actions for a block.
struct BlockActionLog {
  std::array<ActionLogEntry, 5> entries;
  uint8_t idx = 0;
};

// Tracks the lifecycle of a specific write sequence (seq) issued to a block.
// This is used to resolve which sequences are valid to read during and after
// overlapping writes to the same block.
struct WriteRecord {
  // The globally unique sequence number associated with this write.
  uint64_t seq;
  uint64_t start_time;
  uint64_t end_time;          // 0 if still in-flight
  uint64_t overwritten_time;  // When this data was definitively overwritten
};

// Represents a block's full state when multiple writes are
// currently in-flight or recently completed.
struct ActiveState {
  // A historical record of writes to this block, used to determine valid reads.
  std::vector<WriteRecord> history;
};

// Manages the validation state for a subset (shard) of blocks on the device.
// To maximize concurrency, the total device blocks are partitioned into shards,
// with each shard protecting its blocks using its own mutex.
//
// Fast path: If a block has no overlapping writes, its expected sequence number
// is stored directly in the `states` vector.
// Slow path: If multiple writes overlap, the block's entry in the `states`
// vector is flagged and points to an `ActiveState` managed by this shard.
constexpr uint64_t kTrimBit = 1ULL << 62;

class BlockShard {
 public:
  explicit BlockShard(size_t local_blocks) : states_(local_blocks, 0) {}

  bool HasTrim(uint64_t local_idx, uint64_t read_start_time) {
    absl::MutexLock lock(mutex_);
    uint64_t state = states_[local_idx];
    if ((state & (1ULL << 63)) == 0) {
      return (state & kTrimBit) != 0;
    } else {
      uint32_t overflow_idx = state & ~(1ULL << 63);
      for (const auto& w : overflow_states_[overflow_idx].history) {
        if ((w.seq & kTrimBit) && w.overwritten_time > read_start_time) {
          return true;
        }
      }
      return false;
    }
  }
  // Registers the start of a write operation to a specific block.
  // This transitions the block into an overlapping (ActiveState) tracking mode
  // if another write is already in-flight or recently completed.
  // Returns true if this write overlaps with another in-flight write to the
  // same block.
  bool StartWrite(uint64_t local_idx, uint64_t seq, uint64_t start_time,
                  absl::Time wall_start_time);

  // Registers the completion of a write operation.
  // Overwritten or obsolete WriteRecords are pruned from history based on the
  // `oldest_read` timestamp to prevent unbounded memory growth. If all overlaps
  // have resolved, the block returns to the fast-path state tracking.
  void EndWrite(uint64_t local_idx, uint64_t seq, uint64_t end_time,
                uint64_t oldest_read, absl::Time wall_end_time);

  // Prints the last 5 write actions for the given block.
  void PrintActionLog(uint64_t local_idx, absl::Time read_wall_start_time);

  // Validates whether the given sequence number (`seq`) read from the block
  // is mathematically legal given the current history of overlapping writes.
  bool IsValidSeq(uint64_t local_idx, uint64_t seq);

  // Retrieves the start_time, end_time, and overwritten_time for a given valid
  // sequence. Returns true if the sequence is valid, false otherwise.
  // `start_time`: The start logical time of the write/trim
  // `end_time`: The end logical time of the write/trim
  // `overwritten_time`: The overwrite time, i.e., the logical time when the
  // *first* write to this block completed. If `overwritten_time` >
  // `start_time`, it means sequence 0 was not fully overwritten when our read
  // started.
  bool GetSeqTimes(uint64_t local_idx, uint64_t seq, uint64_t& start_time,
                   uint64_t& end_time, uint64_t& overwritten_time);

 private:
  absl::Mutex mutex_;

  // Fast Path directly tracks block sequence number.
  // Slow Path (overlapping reads/writes): the highest bit of a state is set
  // (1ULL << 63). In that case, the lower 63 bits store the overflow_idx.
  std::vector<uint64_t> states_ ABSL_GUARDED_BY(mutex_);

  // Storage for complex `ActiveState` tracking blocks undergoing concurrent IO.
  // Indexed by the `overflow_idx` encoded directly into a block's `states_`
  // uint64_t bucket.
  std::vector<ActiveState> overflow_states_ ABSL_GUARDED_BY(mutex_);

  // Tracks reclaimed `overflow_idx` items so they can be reused without
  // unbounded allocation.
  std::vector<uint32_t> free_overflow_indices_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<uint64_t, BlockActionLog> action_logs_
      ABSL_GUARDED_BY(mutex_);
};

// Fills the provided buffer with deterministic verification payload data.
// Uses a fast XOR-based arithmetic pattern that incorporates the sequence
// number and block index to guard against torn pages, stale reads, and
// misdirected writes.
void GenerateBlockData(absl::Span<uint8_t> buffer, uint64_t seq,
                       uint64_t block_idx, uint64_t write_start_block,
                       uint64_t write_num_blocks);

// Verifies that the payload in the given buffer matches the exact XOR pattern
// expected for this block.
// Returns true if the block's data format is internally consistent. The
// sequence number extracted from the block is populated into `out_seq`.
bool VerifyBlockData(absl::Span<const uint8_t> buffer, uint64_t block_idx,
                     uint64_t& out_seq, uint64_t& out_write_start_block,
                     uint64_t& out_write_num_blocks);

// Configuration options for executing the block validator.
struct ValidatorOptions {
  // The absolute path to the block device or file to test.
  std::string target_file;
  // The offset in bytes where validation IO should start.
  uint64_t region_offset = 0;
  // The total size in bytes of the region to subject to IO.
  uint64_t region_size = 1024ULL * 1024 * 1024;
  // The size in bytes of a single logical block (e.g., 4096).
  uint64_t block_size = 4096;
  // The number of concurrent threads to issue random reads and writes.
  int num_threads = 4;
  // How long the validator should run. If 0, it runs indefinitely until
  // manually stopped.
  int run_time_sec = 0;
  // If true, the validator will skip zeroing/initializing the device before
  // testing. Used only when testing on a completely fresh, known zeroed device.
  bool skip_init = false;
  // If true, validates the entire disk at the end of the run.
  bool validate_on_exit = false;
  // If true, validates all overlapping block data paths before doing the write
  bool read_before_write = false;
  // If true, validate zeroes for trimmed blocks
  bool validate_trim_zeroes = false;
  // Used to count Ctrl-C signals
  std::atomic<int>* sigint_count_ptr = nullptr;
};

// Interface for block device operations to allow dependency injection.
class BlockDevice {
 public:
  virtual ~BlockDevice() = default;
  virtual bool IsBlockDevice() const = 0;
  virtual absl::Status Pread(void* buf, size_t count, off_t offset) = 0;
  virtual absl::Status Pwrite(const void* buf, size_t count, off_t offset) = 0;
  virtual absl::Status Discard(off_t offset, size_t count) = 0;
};

// Runs the block validator with the given options against the provided device.
absl::Status RunValidator(const ValidatorOptions& options, BlockDevice* device);

#endif  // BLOCK_VALIDATOR_BLOCK_VALIDATOR_LIB_H_
