#include "simple_token_bucket.h"

namespace mogo {

  absl::Duration SimpleTokenBucket::TryGetTokens(absl::Time now, absl::Duration d) {
    if (now >= zero_time_) {
      zero_time_ = now + d;
      return absl::ZeroDuration();
    } else {
      return zero_time_ - now;
    }
  }


}  // namespace mogo