/*
bazel test token_bucket:burst_token_bucket_test
*/

#include "burst_token_bucket.h"

#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "gtest/gtest.h"

namespace mogo {
namespace {

TEST(RateTokenBucketTest, Test50s) {
  absl::Time start_time = absl::UnixEpoch();
  absl::Time now = start_time;
  absl::Duration token = absl::Milliseconds(1);
  absl::Duration burst = token * 3;
  mogo::BurstTokenBucket btb(now, burst);

  LOG(INFO) << "Just constructed: " << btb;
  // Drain the burst, all should go through.
  ASSERT_EQ(absl::ZeroDuration(), btb.TryGetTokens(now, token));
  LOG(INFO) << "After extracted 1 : " << btb;
  ASSERT_EQ(absl::ZeroDuration(), btb.TryGetTokens(now, token));
  LOG(INFO) << "After extracted 2 : " << btb;
  ASSERT_EQ(absl::ZeroDuration(), btb.TryGetTokens(now, token));
  LOG(INFO) << "After extracted 3 : " << btb;
  // One more request is going to go through bringing the bucket into
  // negative territory and blocking subsequent requests.
  const auto negative_bucket_value = token;
  ASSERT_EQ(absl::ZeroDuration(), btb.TryGetTokens(now, negative_bucket_value));
  LOG(INFO) << "After extracted 4 : " << btb;

  // The next attempt should delay because the bucket needs to recover
  // back to zero.
  ASSERT_EQ(negative_bucket_value, btb.TryGetTokens(now, token));

  auto small_delay = absl::Microseconds(400);
  now += small_delay;  // Move time by 400us.
  // Now the delay is whatever is left
  auto leftover_delay_to_zero = negative_bucket_value - small_delay;
  ASSERT_EQ(leftover_delay_to_zero, btb.TryGetTokens(now, token));

  now += leftover_delay_to_zero;
  // Now the bucket should allow a request through and go negative again.
  ASSERT_EQ(absl::ZeroDuration(), btb.TryGetTokens(now, negative_bucket_value));
  ASSERT_EQ(negative_bucket_value, btb.TryGetTokens(now, token));

  //   absl::Time end_time = now + absl::Seconds(50);
  //   absl::Time next_log_time = now + absl::Seconds(1);
  //   int request_count = 0;
  //   int prev_log_request_count = request_count;
  //   while (now < end_time) {
  //     absl::Duration d = t.TryGetTokens(now, 1);
  //     if (d == absl::ZeroDuration()) {
  //       request_count++;
  //     } else {
  //       // Sleep until we can schedule the next request.
  //       now += d;
  //     }
  //     if (now > next_log_time) {
  //       double log_delta_seconds =
  //           absl::ToDoubleSeconds(now - next_log_time + absl::Seconds(1));
  //       int log_delta_requests = request_count - prev_log_request_count;
  //       LOG(INFO) << "Request rate: " << log_delta_requests /
  //       log_delta_seconds
  //                 << " r/s";
  //       next_log_time += absl::Seconds(1);
  //       prev_log_request_count = request_count;
  //     }
  //   }
  //   double total_rate = request_count / absl::ToDoubleSeconds(now -
  //   start_time); LOG(INFO) << "Total rate: " << total_rate << " r/s";
  //   ASSERT_NEAR(total_rate, rate, /*abs_error=*/1);
}

}  // namespace
}  // namespace mogo
