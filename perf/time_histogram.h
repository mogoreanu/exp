#ifndef PERF_TIME_HISTOGRAM_H_
#define PERF_TIME_HISTOGRAM_H_

#include <atomic>
#include <cstdint>
#include <string>

#include "base/cycleclock.h"
#include "base/timer.h"
#include "base/types.h"
#include "third_party/absl/numeric/bits.h"
#include "third_party/absl/time/clock.h"
#include "third_party/absl/time/time.h"
#include "util/bits/bits.h"

namespace cloud_util_stat {

namespace internal {
// Find last set. Bits are numbered from 1 - least significant to
// 64 - most significant.
// Returns zero if the input was zero.
inline int Fls64(uint64_t x) { return 64 - absl::countl_zero(x); }
}  // namespace internal

class TimeHistogram;

class TimeHistogramSpan {
 public:
  void End();
  void End(int64_t total_samples);

 private:
  explicit TimeHistogramSpan(TimeHistogram* hist)
      : hist_(hist), start_cycles_(CycleClock::Now()) {}

  TimeHistogram* hist_;
  int64_t start_cycles_;

  friend TimeHistogram;
  friend class TimeHistogramScopeSpan;
};

class TimeHistogramScopeSpan {
 public:
  ~TimeHistogramScopeSpan() { span_.End(); }

 private:
  explicit TimeHistogramScopeSpan(TimeHistogram* hist) : span_(hist) {}

  TimeHistogramSpan span_;
  friend TimeHistogram;
};

class TimeHistogram {
 public:
  TimeHistogram(absl::Duration min, absl::Duration step1)
      : cycles_min_(CycleTimerBase::DurationToCycles(min)),
        cycles_shift_(::cloud_util_stat::internal::Fls64(
            CycleTimerBase::DurationToCycles(step1))) {}

  TimeHistogram(int64_t cycles_min, int64_t cycles_step1)
      : cycles_min_(cycles_min),
        cycles_shift_(::cloud_util_stat::internal::Fls64(cycles_step1)) {}

  // Returns an object that is tracking the time for a code span.
  // The TimeHistogramSpan::End method has to be called to record the result
  // in the histogram.
  // See TimeHistogram::NewScopeSpan to get an object that records the sample
  // automatically.
  // for (const auto& request : batch) {
  //   .. Unmeasured preparation code.
  //   auto block_span = my_histogram.NewExplicitSpan();
  //   .. Measured code span.
  //   block_span.End()
  //   .. Unmeasured cleanup code.
  // }
  TimeHistogramSpan NewExplicitSpan() { return TimeHistogramSpan(this); }

  // Returns an object that will record the wall time into the histogram upon
  // destruction.
  // for(const auto& request : batch) {
  //   auto request_span = my_histogram.NewScopeSpan();
  //   ... more stuff
  // }
  //
  // When request_span is destroyed it will add the wall time of the loop scope
  // into my_histogram.
  TimeHistogramScopeSpan NewScopeSpan() { return TimeHistogramScopeSpan(this); }

  std::string ToHumanString(bool cycles = false) const;

  void AddSample(int64_t cycles) {
    buckets_[GetBucketNumber(cycles)].fetch_add(1, std::memory_order_relaxed);
  }

  void AddSamples(int64_t total_samples, int64_t total_cycles) {
    buckets_[GetBucketNumber(total_cycles / total_samples)].fetch_add(
        total_samples, std::memory_order_relaxed);
  }

  int64_t buckets(int index) const {
    return buckets_[index].load(std::memory_order_relaxed);
  }

 private:
  inline int GetBucketNumber(int64_t elapsed_cycles) const {
    // All values smaller than cycles_min will be put in bucket #64
    // After that the bucket number is computed in the following way:
    // if (cycles_min <= elapsed < cycles_min + 2 ^ cycles_shift) return 0
    // if (elapsed < cycles_min + 2 ^ (cycles_shift + 1)) return 1
    // if (elapsed < cycles_min + 2 ^ (cycles_shift + 2)) return 2
    // ...
    return ::cloud_util_stat::internal::Fls64((elapsed_cycles - cycles_min_) >>
                                              cycles_shift_);
  }

  int64_t GetElapsedRangeLow(int bucket) const {
    if (bucket == 0) {
      return cycles_min_;
    } else {
      return cycles_min_ + (1LL << (cycles_shift_ + bucket - 1));
    }
  }

  int64_t GetElapsedRangeHigh(int bucket) const {
    return cycles_min_ + (1LL << (cycles_shift_ + bucket));
  }

  // buckets[64] will accumulate values smaller than cycles_min
  std::atomic<int64_t> buckets_[65] = {};

  // All samples smaller than cycles_min_ will be accumulated into a single
  // bucket. See ::GetBucketNumber for more details.
  const int64_t cycles_min_;

  // The resolution around cycles_min. See ::GetBucketNumber for more details.
  const int64_t cycles_shift_;
};

}  // namespace cloud_util_stat

#endif  // PERF_TIME_HISTOGRAM_H_
