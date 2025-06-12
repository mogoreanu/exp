#ifndef MOGO_EXP_STAT_APPROX_COUNTER_H_
#define MOGO_EXP_STAT_APPROX_COUNTER_H_

#include <array>
#include <cstdint>
#include <sstream>
#include <string>

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "stat_utils.h"

namespace mogo {

/* A class that continuously counts bytes transferred during the last interval.
 *
 * Not thread-safe. This class is thread-compatible.
 *
 * We split the interval into kSpanLength sub-intervals and keep a
 * circular buffer where we accumulate bytes transferred during the span.
 *
 * When we want to compute bytes transferred during the last interval we sum up
 * the values for all spans. Only the last span that has
 * partially expired needs special treatment. We expire bytes from the last
 * span proportional to the percent of the span that has expired.
 *
 * The counter has correct information for the entire interval, but the last
 * span.
 * */
class ApproxCounter {
 public:
  ApproxCounter(absl::Time now, const absl::Duration interval)
      : kInterval(interval) {
    cur_span_ = (now - kStartTime) / kSpanLength;
    cur_span_end_ = kStartTime + (cur_span_ + 1) * kSpanLength;
  }

  explicit ApproxCounter(absl::Time now)
      : ApproxCounter(now, absl::Seconds(1)) {}

  void RecordRequest(int64_t len, absl::Time now) {
    if (now < cur_span_end_) {
      // We're just appending to the current span. Boring.
      cur_span_bytes_ += len;
      return;
    }

    const int64_t new_span = (now - kStartTime) / kSpanLength;

    DCHECK(new_span > cur_span_);

    if (new_span - cur_span_ > kSpansPerInterval + 1) {
      // Enough time has passed to obliterate our history, reset.
      cur_span_ = new_span;
      cur_span_end_ = kStartTime + (cur_span_ + 1) * kSpanLength;
      cur_span_bytes_ = len;

      span_bytes_.fill(0);
      return;
    }

    // Drop the current value into the storage overwriting the oldest value.
    SpanAt(cur_span_) = cur_span_bytes_;
    ++cur_span_;
    cur_span_end_ += kSpanLength;

    // If there were spans with no data between the time we last recorded a
    // value and now, fill them with zeroes.
    while (cur_span_ < new_span) {
      SpanAt(cur_span_) = 0;
      ++cur_span_;
      cur_span_end_ += kSpanLength;
    }

    // The current span has the request we're recording.
    cur_span_bytes_ = len;
  }

  double GetBytesPerSecond(absl::Time now) const {
    return GetBytesPerInterval(now) / absl::ToDoubleSeconds(kInterval);
  }

  double GetBytesPerInterval(absl::Time now) const {
    // We want to optimize for the case where GetBytesPerInterval is invoked
    // often, so instead of aggregating just the spans that overlap the
    // interesting interval we assume all of them are part of the interesting
    // interval and then exclude the ones that are not.

    int64_t all_bytes = cur_span_bytes_;
    for (auto bytes : span_bytes_) {
      all_bytes += bytes;
    }

    if (ABSL_PREDICT_TRUE(now < cur_span_end_)) {
      // If RecordRequest and GetBytesPerInterval are invoked often then this
      // branch of the code is going to be executed.
      absl::Time cur_span_start = cur_span_end_ - kSpanLength;
      const absl::Duration dint = now - cur_span_start;
      if (ABSL_PREDICT_FALSE(dint < absl::ZeroDuration())) {
        // Time moved back, return the oldest data we have.
        return all_bytes - cur_span_bytes_;
      }

      // A portion of the last span has expired, remove the bytes associated
      // with that portion.
      const double r = absl::FDivDuration(dint, kSpanLength);
      DCHECK_LE(r, 1);
      DCHECK_GE(r, 0);

      return all_bytes - r * SpanAt(cur_span_);
    }

    // Here we know that now >= cur_span_end_ that means that the oldest span
    // in span_bytes_ has expired, remove it.
    all_bytes -= SpanAt(cur_span_);

    int64_t oldest_span = cur_span_ - kSpansPerInterval + 1;
    absl::Time oldest_span_start = cur_span_end_ - kInterval;

    // Remove all other spans that are not part of the interval we're
    // interested in.
    absl::Time one_interval_and_span_ago = now - kInterval - kSpanLength;
    while (oldest_span_start < one_interval_and_span_ago &&
           oldest_span != cur_span_) {
      all_bytes -= SpanAt(oldest_span);
      oldest_span_start += kSpanLength;
      ++oldest_span;
    }

    absl::Time one_interval_ago = now - kInterval;
    if (oldest_span == cur_span_) {
      // We've removed all spans stored in span_bytes_.
      if (cur_span_end_ <= one_interval_ago) {
        // If we also have to remove the current span we have nothing left.
        return 0;
      } else {
        // Only the current span enters the interesting interval partially.
        const double r =
            absl::FDivDuration(cur_span_end_ - one_interval_ago, kSpanLength);
        DCHECK_LE(r, 1);
        DCHECK_GE(r, 0);
        return cur_span_bytes_ * r;
      }
    }

    const double r =
        absl::FDivDuration(one_interval_ago - oldest_span_start, kSpanLength);
    DCHECK_LE(r, 1);
    DCHECK_GE(r, 0);
    return all_bytes - SpanAt(oldest_span) * r;
  }

  std::string ToDebugString() const {
    std::stringstream ss;

    // The span at cur_span_ contains the oldest bucket. Here, even though
    // we move past the current span we actually start from the oldest bucket
    // and traverse the span_bytes_ array.
    for (int64_t i = cur_span_; i < cur_span_ + kSpansPerInterval; ++i) {
      ss << SpanAt(i) << " ";
    }
    ss << cur_span_bytes_ << " ";

    return ss.str();
  }

 private:
  // Keep this a power of 2, it makes remainder calculations easier.
  static constexpr int kSpansPerInterval = 1 << 4;
  static constexpr absl::Time kStartTime = absl::UnixEpoch();

  const absl::Duration kInterval;
  const absl::Duration kSpanLength = kInterval / kSpansPerInterval;

  // Bytes transferred during a span of time.
  // The data in span_bytes_ array is a circular buffer with
  // SpanAt(cur_span_) pointing at the oldest span.
  // The spans are closed on the left side:
  // [....)[....)[....)
  std::array<int64_t, kSpansPerInterval> span_bytes_ = {};

  // The number of the current time span.
  int64_t cur_span_;
  // The end time for the current span, should be updated every time cur_span_
  // is updated.
  absl::Time cur_span_end_;

  // The amount of data recorded in the current time span.
  int64_t cur_span_bytes_ = 0;

  int64_t& SpanAt(int64_t index) { return SpanAt(&span_bytes_, index); }

  int64_t SpanAt(int64_t index) const { return SpanAt(span_bytes_, index); }

  static int64_t& SpanAt(std::array<int64_t, kSpansPerInterval>* array,
                         int64_t index) {
    return array->at(PositiveModulo<kSpansPerInterval>(index));
  }

  static int64_t SpanAt(const std::array<int64_t, kSpansPerInterval>& array,
                        int64_t index) {
    return array[PositiveModulo<kSpansPerInterval>(index)];
  }
};

}  // namespace mogo

#endif  // MOGO_EXP_STAT_APPROX_COUNTER_H_
