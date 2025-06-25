#ifndef MOGO_EXP_TOKEN_BUCKET_SIMPLE_TOKEN_BUCKET_H
#define MOGO_EXP_TOKEN_BUCKET_SIMPLE_TOKEN_BUCKET_H

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace mogo {

class MultiTokenBucket {
 public:
  explicit MultiTokenBucket(absl::Time now)
      : rates_{{/*rate_multiplier=*/0.0, /*end_time=*/now},
                {/*rate_multiplier=1*/ 1.0,
                 /*end_time=*/absl::InfiniteFuture()}} {}

  absl::Duration TryGetTokens(absl::Time now, absl::Duration d);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, MultiTokenBucket stb) {
    absl::Format(&sink, "{MultiTokenBucket head: %v, tail: %v} ", stb.head_, stb.tail_);
  }

 private:
  struct RateAndEndTime {
    double rate_multiplier;
    absl::Time end_time;
  };
  // A circular buffer would be better with the last element always being {1.0,
  // InfiniteFuture()}
  static constexpr int kRateBucketCount = 16;
  // `head_` points to the next rate scope to be used.
  // The next rate scope end time is the delay required always points to the
  // rate == 0 cell;
  int head_ = 0;
  // Points to the next element after the InfiniteFuture one. Shouldn't have to
  // be used in practice.
  int tail_ = 2;
  RateAndEndTime rates_[kRateBucketCount];
};

}  // namespace mogo

#endif  // MOGO_EXP_TOKEN_BUCKET_SIMPLE_TOKEN_BUCKET_H