#include "burst_token_bucket.h"

namespace mogo {

absl::Duration BurstTokenBucket::TryGetTokens(absl::Time now,
                                              absl::Duration tokens) {
  // If the bucket would have refilled to the max_burst_tokens - use that.
  absl::Time past_with_burst = now - max_burst_tokens_;
  absl::Duration delay = tb_.TryGetTokens(past_with_burst, tokens);
  if (delay == absl::ZeroDuration()) {
    return absl::ZeroDuration();
  }
  if (delay <= max_burst_tokens_) {
    delay = tb_.TryGetTokens(past_with_burst + delay, tokens);
    DCHECK_EQ(delay, absl::ZeroDuration());
    return absl::ZeroDuration();
  }
  return delay - max_burst_tokens_;
}

}  // namespace mogo
