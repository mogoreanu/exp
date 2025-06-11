/*
bazel test token_bucket:simple_token_bucket_test
*/

#include "simple_token_bucket.h"

#include "absl/log/log.h"
#include "gtest/gtest.h"

namespace mogo {
namespace {

TEST(SimpleTokenBucketTest, Test50s) {
  absl::Time start_time = absl::Now();
  absl::Time now = start_time;
  mogo::SimpleTokenBucket t(now);
  absl::Duration request_cost = absl::Microseconds(100);  // 10k requests/sec
  absl::Time end_time = now + absl::Seconds(50);
  absl::Time next_log_time = now + absl::Seconds(1);
  int request_count = 0;
  int prev_log_request_count = request_count;
  while (now < end_time) {
    absl::Duration d = t.TryGetTokens(now, request_cost);
    if (d == absl::ZeroDuration()) {
      request_count++;
    } else {
      // Sleep until we can schedule the next request.
      now += d;
    }
    if (now > next_log_time) {
      double log_delta_seconds =
          absl::ToDoubleSeconds(now - next_log_time + absl::Seconds(1));
      int log_delta_requests = request_count - prev_log_request_count;
      LOG(INFO) << "Request rate: " << log_delta_requests / log_delta_seconds
                << " r/s";
      next_log_time += absl::Seconds(1);
      prev_log_request_count = request_count;
    }
  }
  double total_rate = request_count / absl::ToDoubleSeconds(now - start_time);
  LOG(INFO) << "Total rate: " << total_rate << " r/s";
  ASSERT_NEAR(total_rate, absl::FDivDuration(absl::Seconds(1), request_cost),
              /*abs_error=*/1);
}

}  // namespace
}  // namespace mogo
