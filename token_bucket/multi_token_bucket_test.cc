/*
bazel test token_bucket:multi_token_bucket_test --test_output=streamed

bazel build -c dbg token_bucket:multi_token_bucket_test
*/

#include "token_bucket/multi_token_bucket.h"

#include "absl/container/inlined_vector.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "gtest/gtest.h"

namespace mogo {

struct RateAndEndTime {
  double rate_multiplier;
  absl::Time end_time;
};

class MultiTokenBucketIntrospector {
 public:
  explicit MultiTokenBucketIntrospector(MultiTokenBucket& b) : b_(b) {}
  std::vector<RateAndEndTime> GetRates() {
    std::vector<RateAndEndTime> v;
    auto i = b_.head_;
    do {
      v.push_back({b_.rates_[i].rate_multiplier, b_.rates_[i].end_time});
      ++i;
      i %= b_.kRateBucketCount;
    } while (v.back().end_time < absl::InfiniteFuture());

    return v;
  }

 private:
  MultiTokenBucket& b_;
};

namespace {

TEST(MultiTokenBucketTest, Test50s) {
  double err = 0.0000001;
  absl::Time now = absl::UnixEpoch() + absl::Hours(1);
  MultiTokenBucket b(now);
  MultiTokenBucketIntrospector introspector(b);
  {
    auto r = introspector.GetRates();
    ASSERT_NEAR(r[0].rate_multiplier, 0, err);
    ASSERT_EQ(r[0].end_time, now);

    ASSERT_NEAR(r[1].rate_multiplier, 1, err);
    ASSERT_EQ(r[1].end_time, absl::InfiniteFuture());
  }

  // Will consume 10ms over the next 100 ms.
  ASSERT_EQ(absl::ZeroDuration(), b.TryGetTokens(now, absl::Milliseconds(10)));
  LOG(INFO) << b;
  {
    auto r = introspector.GetRates();
    ASSERT_NEAR(r[0].rate_multiplier, 0, err);
    ASSERT_EQ(r[0].end_time, now);

    ASSERT_NEAR(r[1].rate_multiplier, 0.9, err);
    ASSERT_EQ(r[1].end_time, now + absl::Milliseconds(100));

    ASSERT_NEAR(r[2].rate_multiplier, 1, err);
    ASSERT_EQ(r[2].end_time, absl::InfiniteFuture());
  }

  // Will consume 10ms over the next 100 ms again.
  ASSERT_EQ(absl::ZeroDuration(), b.TryGetTokens(now, absl::Milliseconds(10)));
  LOG(INFO) << b;
  {
    auto r = introspector.GetRates();
    ASSERT_NEAR(r[0].rate_multiplier, 0,err);
    ASSERT_EQ(r[0].end_time, now);

    ASSERT_NEAR(r[1].rate_multiplier, 0.8, err);
    ASSERT_EQ(r[1].end_time, now + absl::Milliseconds(100));

    ASSERT_NEAR(r[2].rate_multiplier, 1, err);
    ASSERT_EQ(r[2].end_time, absl::InfiniteFuture());
  }

  now += absl::Milliseconds(50);

  // Will consume 5ms over the leftover 50ms and 5ms more after that.
  ASSERT_EQ(absl::ZeroDuration(), b.TryGetTokens(now, absl::Milliseconds(10)));
  LOG(INFO) << b;
  {
    auto r = introspector.GetRates();
    ASSERT_EQ(r[0].rate_multiplier, 0);
    ASSERT_EQ(r[0].end_time, now);

    ASSERT_NEAR(r[1].rate_multiplier, 0.7, err);
    ASSERT_EQ(r[1].end_time, now + absl::Milliseconds(50));

    ASSERT_NEAR(r[2].rate_multiplier, 0.9, err);
    ASSERT_EQ(r[2].end_time, now + absl::Milliseconds(100));

    ASSERT_NEAR(r[3].rate_multiplier, 1, err);
    ASSERT_EQ(r[3].end_time, absl::InfiniteFuture());
  }

  // Test split interval in the middle.
  // Will consume 1ms over the leftover 10ms.
  ASSERT_EQ(absl::ZeroDuration(), b.TryGetTokens(now, absl::Milliseconds(1)));
  LOG(INFO) << b;
  {
    auto r = introspector.GetRates();
    ASSERT_NEAR(r[0].rate_multiplier, 0, err);
    ASSERT_EQ(r[0].end_time, now);

    ASSERT_NEAR(r[1].rate_multiplier, 0.6, err);
    ASSERT_EQ(r[1].end_time, now + absl::Milliseconds(10));

    ASSERT_NEAR(r[2].rate_multiplier, 0.7, err);
    ASSERT_EQ(r[2].end_time, now + absl::Milliseconds(50));

    ASSERT_NEAR(r[3].rate_multiplier, 0.9, err);
    ASSERT_EQ(r[3].end_time, now + absl::Milliseconds(100));

    ASSERT_NEAR(r[4].rate_multiplier, 1, err);
    ASSERT_EQ(r[4].end_time, absl::InfiniteFuture());
  }

  // 6 more
  for (int q = 0; q < 6; ++q) {
    ASSERT_EQ(absl::ZeroDuration(), b.TryGetTokens(now, absl::Milliseconds(1)));
    LOG(INFO) << b;
  }

  {
    auto r = introspector.GetRates();
    ASSERT_NEAR(r[0].rate_multiplier, 0.0, err);
    ASSERT_EQ(r[0].end_time, now + absl::Milliseconds(10));
  }
}

}  // namespace
}  // namespace mogo
