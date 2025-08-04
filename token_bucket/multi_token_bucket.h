#ifndef MOGO_EXP_TOKEN_BUCKET_MULTI_TOKEN_BUCKET_H
#define MOGO_EXP_TOKEN_BUCKET_MULTI_TOKEN_BUCKET_H

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"
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
  friend void AbslStringify(Sink& sink, MultiTokenBucket mtb) {
    absl::Time zero_time = mtb.rates_[mtb.head_].end_time;
    sink.Append(absl::StrCat("{MultitokenBucket head=", mtb.head_, " "));

    auto i = mtb.head_;
    while (true) {
      ++i;
      i %= mtb.kRateBucketCount;
      if (i == mtb.head_) {
        sink.Append(" overflow! ");
        break;
      }
      sink.Append(absl::StrCat("{", mtb.rates_[i].rate_multiplier, ",",
                               mtb.rates_[i].end_time - zero_time, "}"));
      if (mtb.rates_[i].end_time == absl::InfiniteFuture()) {
        break;
      }
    };

    sink.Append("}");
  }

 private:
  // Describes the refill rate over and interval of time.
  struct RateAndEndTime {
    double rate_multiplier;
    // The interval is open, as in, applies only for now < end_time.
    absl::Time end_time;
  };
  // A circular buffer would be better with the last element always being {1.0,
  // InfiniteFuture()}
  static constexpr int kRateBucketCount = 16;
  // `head_` points to the next rate scope to be used.
  // The next rate scope end time is the delay required always points to the
  // rate == 0 cell;
  int head_ = 0;

  // A circular buffer with the refill rates and the time span associated with
  // them. `rates_[head_]` always points to a span with a refill rate of zero
  // and the end time of that span is the time when requests are going to start
  // to be unblocked. The last span ends at InfiniteFuture and has a refill rate
  // of 1.
  RateAndEndTime rates_[kRateBucketCount];

  friend class MultiTokenBucketIntrospector;
};

}  // namespace mogo

#endif  // MOGO_EXP_TOKEN_BUCKET_MULTI_TOKEN_BUCKET_H