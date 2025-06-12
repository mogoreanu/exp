#include "absl/time/time.h"
#include "absl/strings/str_format.h"

namespace mogo {

// Refills at one second per second.
// Tokens extracted in units of absl::Duration.
// Has no burst.
class SimpleTokenBucket {
 public:
  explicit SimpleTokenBucket(absl::Time now) : zero_time_(now) {}

  // Attempts to extract the specified tokens from the token bucket.
  // Returns absl::Zero duration if the extraction was successful.
  // Returns a delay that the caller should wait for until tokens are going to
  // be available.
  absl::Duration TryGetTokens(absl::Time now, absl::Duration d);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, SimpleTokenBucket stb) {
    absl::Format(&sink, "{SimpleTokenBucket zero_time: %v} ", stb.zero_time_);
  }

 private:
  // The time when the token bucket returns back to zero and starts allowing
  // requests through.
  absl::Time zero_time_;
};

}  // namespace mogo