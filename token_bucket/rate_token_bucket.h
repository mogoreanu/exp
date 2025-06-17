#ifndef MOGO_EXP_TOKEN_BUCKET_RATE_TOKEN_BUCKET_H
#define MOGO_EXP_TOKEN_BUCKET_RATE_TOKEN_BUCKET_H

#include "absl/time/time.h"
#include "token_bucket/simple_token_bucket.h"

namespace mogo {

class RateTokenBucket {
 public:
  RateTokenBucket(absl::Time now, double refill_rate)
      : tb_(now), duration_per_token_(absl::Seconds(1) / refill_rate) {}

  // Attempts to extract the specified tokens from the token bucket.
  // Returns absl::Zero duration if the extraction was successful.
  // Returns a delay that the caller should wait for until tokens are going to
  // be available.
  absl::Duration TryGetTokens(absl::Time now, double token_count) {
    return tb_.TryGetTokens(now, token_count * duration_per_token_);
  }

 private:
  SimpleTokenBucket tb_;
  absl::Duration duration_per_token_;
};

}  // namespace mogo

#endif  // MOGO_EXP_TOKEN_BUCKET_RATE_TOKEN_BUCKET_H