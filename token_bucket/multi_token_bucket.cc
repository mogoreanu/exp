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
    // at zero.
    CHECK(rates_[head_].rate_multiplier < kMinRate ||
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

  auto inc_idx = [this](auto& idx) { idx = (idx + 1) % kRateBucketCount; };
  auto dec_idx = [this](auto& idx) {
    idx = (idx + kRateBucketCount - 1) % kRateBucketCount;
  };

  {
    // Burn tokens from the past
    inc_idx(head_);
    while (rates_[head_].end_time <= now) {
      inc_idx(head_);
    }
    dec_idx(head_);
    rates_[head_].end_time = now;
    rates_[head_].rate_multiplier = 0;
  }
  absl::Time span_start_time = now;

  // Withdraw tokens from the future.
  // This will generate at most one additional split.
  auto i = head_;
  inc_idx(i);
  while (d > absl::ZeroDuration()) {
    auto span_rate_delta = kRateAdjustmentDelta;
    if (rates_[i].rate_multiplier < kRateAdjustmentDelta) {
      span_rate_delta = rates_[i].rate_multiplier;
    }
    absl::Duration tokens_full_span =
        span_rate_delta * (rates_[i].end_time - span_start_time);
    if (tokens_full_span <= d) {
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
    rates_[i].end_time = now + time_to_generate_d;
    inc_idx(i);
    auto tmp2 = rates_[i];
    rates_[i] = old_rate;
    while (rates_[i].end_time < absl::InfiniteFuture()) {
      inc_idx(i);
      auto tmp3 = rates_[i];
      rates_[i] = tmp2;
      tmp2 = tmp3;
    } ;
    return absl::ZeroDuration();
  }

  return absl::ZeroDuration();
}

/*
absl::Duration tokens_accumulated = absl::ZeroDuration();
absl::Time span_start_time = rates_[head_].end_time;
// Accumulate all the tokens that we have already generated because the time
// has moved.
while (rates_[++head_].end_time < now) {
  CHECK_LT(head_, tail_);
  tokens_accumulated += rates_[head_].rate_multiplier *
                        (rates_[head_].end_time - span_start_time);
  span_start_time = rates_[head_].end_time;

  // If we've accumulated more tokens that are necessary to process the
  // request - done.
  if (tokens_accumulated >= d) {
    --head_;
    rates_[head_].end_time = now;
    rates_[head_].rate_multiplier = 0;
    return absl::ZeroDuration();
  }
}

// rates_[head_].end_time >= now, the current span is partially overlapping
// now. We always have such a span because the last span is infinite future.
tokens_accumulated += rates_[head_].rate_multiplier * (now - span_start_time);
if (tokens_accumulated >= d) {
  --head_;
  rates_[head_].end_time = now;
  rates_[head_].rate_multiplier = 0;
  return absl::ZeroDuration();
}

CHECK_LT(tokens_accumulated, d);
d -= tokens_accumulated;
*/

}  // namespace mogo