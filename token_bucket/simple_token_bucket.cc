#include "simple_token_bucket.h"

namespace mogo {

absl::Duration SimpleTokenBucket::TryGetTokens(absl::Time now,
                                               absl::Duration d) {
  if (now >= zero_time_) {
    // If the bucket has already returned back to zero, extract tokens and
    // allow the request through.
    zero_time_ = now + d;
    return absl::ZeroDuration();
  } else {
    return zero_time_ - now;
  }
}

}  // namespace mogo