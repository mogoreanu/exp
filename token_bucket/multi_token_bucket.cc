#include "token_bucket/multi_token_bucket.h"

#include "absl/log/check.h"

namespace mogo {

absl::Duration MultiTokenBucket::TryGetTokens(absl::Time now,
                                              absl::Duration d) {
  const double kMinRate = 0.0001;
  // Here rates_[head_] is equivalent to the zero_time in the SimpleTokenBucket.
  // It's the time when the token bucket returns back to zero and allows
  // requests through.
  if (now < rates_[head_].end_time) {
    // The head bucket should always point to the span where the refill rate is
    // near zero.
    CHECK(rates_[head_].rate_multiplier < kMinRate &&
          rates_[head_].rate_multiplier > -kMinRate);
    return rates_[head_].end_time - now;
  }
  // Now has moved past the zero time, so we allow the request to go through
  // and need to adjust the rates accordingly.

  // Each new request in-flight is going to decrease the refill rate by 10% of
  // the norminal.
  // We're going to allow up to 10 requests in-flight.
  // The `d` tokens will be extracted over (10 * d) duration.
  const double kRateAdjustmentDelta = 0.1;
  // We need to have enough buckets to cover all possible rates.
  DCHECK_GE(kRateAdjustmentDelta * kRateBucketCount, 1);

  {
    auto orig_head = head_;
    // Burn tokens from the past
    IncIdx(head_);
    while (rates_[head_].end_time <= now) {
      IncIdx(head_);
      DCHECK(head_ != orig_head);
    }
    DecIdx(head_);
    rates_[head_].end_time = now;
    rates_[head_].rate_multiplier = 0;
  }

  // Withdraw tokens from the future.
  // This will generate at most one additional split.
  auto i = head_;
  while (d > absl::ZeroDuration()) {
    absl::Time span_start_time = rates_[i].end_time;
    IncIdx(i);
    DCHECK(i != head_);
    auto span_rate_delta = kRateAdjustmentDelta;
    if (rates_[i].rate_multiplier < kRateAdjustmentDelta) {
      span_rate_delta = rates_[i].rate_multiplier;
    }
    absl::Duration tokens_full_span =
        span_rate_delta * (rates_[i].end_time - span_start_time);
    if (tokens_full_span <= d) {
      // We need more tokens than what we get from the current span.
      d -= tokens_full_span;
      rates_[i].rate_multiplier -= span_rate_delta;
      if (rates_[i].rate_multiplier < kMinRate) {
        // We've consumed all tokens from this span.
        head_ = i;
      }
      continue;
    }
    // tokens_full_span > d
    // The entire span would generate more tokens than necessary
    // Need to split the span in two:
    // * One that has a multiplier: (rate_multiplier - span_rate_delta)
    // * Another that has the same `rate_multiplier` as before
    absl::Duration time_to_generate_d = d / span_rate_delta;
    auto old_rate = rates_[i];
    rates_[i].rate_multiplier -= span_rate_delta;
    rates_[i].end_time = span_start_time + time_to_generate_d;
    IncIdx(i);
    DCHECK(i != head_);
    auto tmp2 = rates_[i];
    rates_[i] = old_rate;
    while (rates_[i].end_time < absl::InfiniteFuture()) {
      IncIdx(i);
      DCHECK(i != head_);
      auto tmp3 = rates_[i];
      rates_[i] = tmp2;
      tmp2 = tmp3;
    };
    return absl::ZeroDuration();
  }

  return absl::ZeroDuration();
}

}  // namespace mogo