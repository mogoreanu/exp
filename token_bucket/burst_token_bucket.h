#ifndef MOGO_EXP_TOKEN_BUCKET_BURST_TOKEN_BUCKET_H
#define MOGO_EXP_TOKEN_BUCKET_BURST_TOKEN_BUCKET_H

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "simple_token_bucket.h"

namespace mogo {

class BurstTokenBucket {
 public:
  BurstTokenBucket(absl::Time now, absl::Duration burst_tokens)
      : tb_(now - burst_tokens), max_burst_tokens_(burst_tokens) {}

  // Attempts to extract the specified tokens from the token bucket.
  // Returns absl::Zero duration if the extraction was successful.
  // Returns a delay that the caller should wait for until tokens are going to
  // be available.
  absl::Duration TryGetTokens(absl::Time now, absl::Duration tokens);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, BurstTokenBucket btb) {
    absl::Format(&sink, "{BurstTokenBucket %v, max_burst: %v }", btb.tb_,
                 btb.max_burst_tokens_);
  }

 private:
  SimpleTokenBucket tb_;
  const absl::Duration max_burst_tokens_;
};

}  // namespace mogo

#endif  // MOGO_EXP_TOKEN_BUCKET_BURST_TOKEN_BUCKET_H
