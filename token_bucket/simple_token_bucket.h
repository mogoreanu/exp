#include "absl/time/time.h"
#include "absl/time/clock.h"

namespace mogo {

// Refills at one second per second.
// Tokens extracted in units of absl::Duration.
class SimpleTokenBucket {
 public:
  explicit SimpleTokenBucket(absl::Time now) : zero_time_(now) {}

  // Attempts to extract the specified tokens from the token bucket.
  // Returns absl::Zero duration if the extraction was successful.
  // Returns a delay that the caller should wait for until tokens are going to
  // be available.
  absl::Duration TryGetTokens(absl::Time now, absl::Duration d);

 private:
  absl::Time zero_time_;
};

}  // namespace mogo