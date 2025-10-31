#ifndef MOGO_EXP_STAT_THROUGHPUT_COUNTER_H_
#define MOGO_EXP_STAT_THROUGHPUT_COUNTER_H_

#include <array>
#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
// #include "absl/time/clock.h"
// #include "absl/time/time.h"
// #include "stat_utils.h"

namespace mogo {

template <int64_t kSpanLengthNs, int kMonitorSpanCount>
class ThroughputCounter {
 public:
  ThroughputCounter() {}

  explicit ThroughputCounter(int64_t now_ns);

  void Record(int64_t len, int64_t end_ns) { Record(len, end_ns, end_ns); }

  // Smear the provided length over the time period from `start` to `now`.
  void Record(int64_t len, int64_t start_ns, int64_t end_ns) {
    int64_t start_span_abs = start_ns / kSpanLengthNs;
    int64_t end_span_abs = end_ns / kSpanLengthNs;
    if (start_span_abs == end_span_abs) {
      // All IO fits within the same span, easy.
      span_bytes_[end_span_abs % kMonitorArraySize].fetch_add(
          len, std::memory_order_relaxed);
      return;
    }
    if (start_ns < end_ns - kMonitorDurationNs) {
      // Adjust start to fit within monitor duration.
      start_ns = end_ns - kMonitorDurationNs;
      start_span_abs = start_ns / kSpanLengthNs;
    }

    int64_t duration_ns = end_ns - start_ns;
    int64_t duration_spans = duration_ns / kSpanLengthNs;
    DCHECK_LE(duration_spans, kMonitorSpanCount);
    // The fraction of the `len` that should be recorded in each full span.
    int64_t len_per_span = len / duration_spans;

    int64_t full_span_count = end_span_abs - start_span_abs - 1;
    // full_span_count * len_per_span will go into full spans, everything else
    // is going to go into the first and last spans.

    int64_t start_span_duration_ns =
        RoundUp(start_ns, kSpanLengthNs) - start_ns;
    int64_t len_start_span =
        start_span_duration_ns * len_per_span / kSpanLengthNs;

    // Make sure any rounding is recorded in the end span.
    int64_t len_end_span =
        len - (full_span_count * len_per_span) - len_start_span;

    SpanAt(end_ns).fetch_add(len_end_span, std::memory_order_relaxed);
    SpanAt(start_ns).fetch_add(len_start_span, std::memory_order_relaxed);
    for (int i = 0; i < full_span_count; ++i) {
      int64_t span_end_ns = end_ns - (i + 1) * kSpanLengthNs;
      SpanAt(span_end_ns).fetch_add(len_per_span, std::memory_order_relaxed);
    }
  }

  // Get the throughput between the provided `start` time and `now`.
  // TODO(mogo): Try duration_ns divisible by kSpanLengthNs.
  int64_t GetThroughput(int64_t end_ns) const {
    int64_t duration_ns = 1000000000LL;
    DCHECK_GE(kMonitorDurationNs, duration_ns);
    int64_t total = 0;
    for (int i = 0; i < duration_ns / kSpanLengthNs; ++i) {
      int64_t span_end_ns = end_ns - i * kSpanLengthNs;
      total += LoadSpanAt(span_end_ns);
    }
    int64_t first_span_start_ns = end_ns - duration_ns;
    int64_t first_span_end_ns = RoundUp(end_ns, kSpanLengthNs) - duration_ns;
    if (first_span_start_ns == first_span_end_ns) {
      return total;
    }
    total += LoadSpanAt(first_span_start_ns) *
             (first_span_end_ns - first_span_start_ns) / kSpanLengthNs;
    return total;
  };

  std::vector<int64_t> CleanupSpans(int64_t last_cleanup_ns, int64_t now_ns) {
    int64_t last_unused_span_ns = now_ns - kMonitorDurationNs;
    int64_t last_cleanup_unused_ns = last_cleanup_ns - kMonitorDurationNs;
    int64_t cleanup_spans = (now_ns - last_cleanup_ns) / kSpanLengthNs;
    if (cleanup_spans > kMaxCleanupSpans) {
      cleanup_spans = kMaxCleanupSpans;
    }
    std::vector<int64_t> result;
    int64_t cleanup_start_ns = now_ns - cleanup_spans * kSpanLengthNs;
    while (cleanup_start_ns < now_ns) {
      result.push_back(SpanAt(cleanup_start_ns).exchange(0));
      cleanup_start_ns += kSpanLengthNs;
    }
    return result;
  }

  std::string ToDebugString(int64_t now_ns) const {
    std::ostringstream oss;
    for (int i = kMonitorSpanCount + 1; i >= 0; --i) {
      int64_t span_end_ns = now_ns - i * kSpanLengthNs;
      oss << LoadSpanAt(span_end_ns) << " \t";
    }
    return oss.str();
  };

 private:
  int64_t SpanIndex(int64_t time_ns) const {
    return (time_ns / kSpanLengthNs) % static_cast<int64_t>(kMonitorArraySize);
  }
  int64_t LoadSpanAt(int64_t time_ns) const {
    return span_bytes_[SpanIndex(time_ns)].load(std::memory_order_relaxed);
  }
  std::atomic<int64_t>& SpanAt(int64_t time_ns) {
    return span_bytes_[SpanIndex(time_ns)];
  }

  static constexpr inline bool IsPowerOf2(int64_t value) {
    if (ABSL_PREDICT_FALSE(value <= 0)) {
      return false;
    }
    return ((value - 1) & value) == 0;
  }

  static constexpr inline int64_t RoundUp(int64_t value, int64_t round_by) {
    CHECK_LT(0, round_by);
    CHECK_LE(0, value);

    if (IsPowerOf2(round_by)) {
      // Bit trick to avoid division in case 'round_by' is power of 2.
      return (value + round_by - 1) & ~(round_by - 1);
    } else {
      return round_by * ((value + round_by - 1) / round_by);
    }
  }

  static constexpr int kMonitorArraySize = 2 * kMonitorSpanCount;
  static constexpr int kMaxCleanupSpans =
      kMonitorArraySize - kMonitorSpanCount - 1;
  static constexpr int64_t kMonitorDurationNs =
      kSpanLengthNs * kMonitorSpanCount;
  std::array<std::atomic<int64_t>, kMonitorArraySize> span_bytes_ = {};
};

}  // namespace mogo

#endif  // MOGO_EXP_STAT_THROUGHPUT_COUNTER_H_
