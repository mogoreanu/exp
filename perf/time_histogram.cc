#include "perf/time_histogram.h"

#include <cstdint>
#include <sstream>
#include <string>

#include "absl/base/internal/cycleclock.h"
#include "perf/cycle_clock_utils.h"

namespace mogo {

void TimeHistogramSpan::End() {
  hist_->AddSample(CycleClock::Now() - start_cycles_);
}

void TimeHistogramSpan::End(int64_t total_samples) {
  hist_->AddSamples(total_samples, CycleClock::Now() - start_cycles_);
}

TimeHistogramSpan::TimeHistogramSpan(TimeHistogram* hist)
    : hist_(hist), start_cycles_(CycleClock::Now()) {}

TimeHistogram::TimeHistogram(absl::Duration min, absl::Duration step1)
    : cycles_min_(DurationToCycles(min)),
      cycles_shift_(internal::Fls64(DurationToCycles(step1))) {}

std::string TimeHistogram::ToHumanString(bool cycles) const {
  std::ostringstream s;

  // Find the first non-zero bucket.
  decltype(ABSL_ARRAYSIZE(buckets_)) first_nonzero_bucket = 0;
  while (first_nonzero_bucket < ABSL_ARRAYSIZE(buckets_) &&
         buckets_[first_nonzero_bucket] == 0) {
    ++first_nonzero_bucket;
  }

  // Find the last non-zero bucket.
  auto last_nonzero_bucket = ABSL_ARRAYSIZE(buckets_) - 2;
  while (last_nonzero_bucket >= first_nonzero_bucket &&
         buckets_[last_nonzero_bucket] == 0) {
    --last_nonzero_bucket;
  }

  // Values smaller than min have their own special bucket.
  int less_min_bucket = ABSL_ARRAYSIZE(buckets_) - 1;

  // Compute the sum of all non-zero buckets.
  int64_t total_sample_count = buckets_[less_min_bucket];
  int64_t running_sample_count = buckets_[less_min_bucket];
  for (auto i = first_nonzero_bucket; i <= last_nonzero_bucket; ++i) {
    total_sample_count += buckets_[i];
  }

  // Print the special bucket with values smaller than min.
  if (buckets_[less_min_bucket] != 0) {
    s << buckets_[less_min_bucket] * 100 / total_sample_count << "% \t"
      << buckets_[less_min_bucket] * 100 / total_sample_count << "% \t"
      << buckets_[less_min_bucket] << "\t< ";
    if (cycles) {
      s << CyclesToUsec(cycles_min_) << " cyc\n";
    } else {
      s << CyclesToUsec(cycles_min_) << " us\n";
    }
  }

  // Print all buckets between first non-zero bucket and last non-zero bucket.
  // We don't want to skip buckets even if they have values with zeroes.
  for (auto i = first_nonzero_bucket; i <= last_nonzero_bucket; ++i) {
    running_sample_count += buckets_[i];
    s << running_sample_count * 100 / total_sample_count << "% \t"
      << buckets_[i] * 100 / total_sample_count << "% \t" << buckets_[i]
      << "\t";
    if (cycles) {
      s << GetElapsedRangeLow(i) << " cyc - " << GetElapsedRangeHigh(i)
        << " cyc\n";
    } else {
      s << CyclesToUsec(GetElapsedRangeLow(i)) << " us - "
        << CyclesToUsec(GetElapsedRangeHigh(i)) << " us\n";
    }
  }
  return s.str();
}

}  // namespace mogo
