/*
# Run the demo with the simple token bucket:
bazel run token_bucket:token_bucket_demo -- --stderrthreshold=0

# Run the demo with the burst token bucket:
bazel run token_bucket:token_bucket_demo -- --stderrthreshold=0 --tb_type=burst
*/

#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "stat/approx_counter.h"
#include "token_bucket/burst_token_bucket.h"
#include "token_bucket/rate_token_bucket.h"
#include "token_bucket/simple_token_bucket.h"

ABSL_FLAG(
    std::string, tb_type, "simple",
    "Type of rate-limiter to use, valid values are `simple`, `rate`, `burst`");

namespace mogo {

struct TokenBucketInstanceInterface {
  virtual ~TokenBucketInstanceInterface() = default;
  virtual absl::Duration TryGetTokens(absl::Time now) = 0;
};

struct FunctionTokenBucketInstance : public TokenBucketInstanceInterface {
  FunctionTokenBucketInstance(std::function<absl::Duration(absl::Time now)> f)
      : f_(f) {}

  absl::Duration TryGetTokens(absl::Time now) override { return f_(now); };

  std::function<absl::Duration(absl::Time now)> f_;
};

absl::Status RunDemo(absl::Time now, TokenBucketInstanceInterface& tb) {
  // Run at max speed for 5s twice.
  for (int i = 0; i < 2; ++i) {
    const absl::Time start_time = now;

    ApproxCounter req_cnt(now);

    absl::Time end_time = now + absl::Seconds(5);
    absl::Time next_log_time = now + absl::Seconds(1);
    int request_count = 0;
    int prev_log_request_count = request_count;
    while (now < end_time) {
      absl::Duration d = tb.TryGetTokens(now);
      if (d == absl::ZeroDuration()) {
        request_count++;
        req_cnt.RecordRequest(1, now);
      } else {
        // Sleep until we can schedule the next request.
        now += d;
      }
      if (now >= next_log_time) {
        double log_delta_seconds =
            absl::ToDoubleSeconds(now - next_log_time + absl::Seconds(1));
        int log_delta_requests = request_count - prev_log_request_count;
        LOG(INFO) << "Request rate: " << log_delta_requests / log_delta_seconds
                  << " r/s, approx_rate: " << req_cnt.GetBytesPerSecond(now)
                  << " r/s, request_count: " << log_delta_requests
                  << ", seconds: " << log_delta_seconds;
        next_log_time += absl::Seconds(1);
        prev_log_request_count = request_count;
      }
    }
    double total_seconds = absl::ToDoubleSeconds(now - start_time);
    LOG(INFO) << "Total rate: " << request_count / total_seconds
              << " r/s, request_count: " << request_count
              << ", total_seconds: " << total_seconds;

    LOG(INFO) << "Sleeping for 1h";
    now += absl::Hours(1);
  }

  {
    int total_requests = 20;
    LOG(INFO) << "Delays for the first " << total_requests << " after a break:";
    for (int i = 0; i < total_requests; ++i) {
      absl::Duration d = tb.TryGetTokens(now);
      LOG(INFO) << "d" << i << ": " << d;
      if (d > absl::ZeroDuration()) {
        now += d;
        CHECK_EQ(absl::ZeroDuration(), tb.TryGetTokens(now));
      }
    }
  }

  return absl::OkStatus();
}

absl::Status RunSimpleDemo() {
  absl::Time now = absl::Now();
  mogo::SimpleTokenBucket t(now);
  absl::Duration request_cost = absl::Microseconds(100);  // 10k requests/sec
  FunctionTokenBucketInstance tb([&](absl::Time now) -> absl::Duration {
    return t.TryGetTokens(now, request_cost);
  });
  return RunDemo(now, tb);
}

absl::Status RunRateDemo() {
  double refill_rate = 10000;
  LOG(INFO) << "Running RateTokenBucket with refill_rate: " << refill_rate;
  absl::Time now = absl::Now();
  mogo::RateTokenBucket t(now, /*refill_rate=*/refill_rate);
  FunctionTokenBucketInstance tb([&](absl::Time now) -> absl::Duration {
    return t.TryGetTokens(now, /*token_count=*/1);
  });
  return RunDemo(now, tb);
}

absl::Status RunBurstDemo() {
  double rate = 10000;
  absl::Duration op_cost = absl::Seconds(1) / rate;
  absl::Duration burst_duration = absl::Milliseconds(1);
  LOG(INFO) << "Running BurstTokenBucket with rate: " << rate
            << ", burst_duration: " << burst_duration;
  absl::Time now = absl::Now();
  mogo::BurstTokenBucket t(now, /*burst_tokens=*/burst_duration);
  FunctionTokenBucketInstance tb([&](absl::Time now) -> absl::Duration {
    return t.TryGetTokens(now, op_cost);
  });
  return RunDemo(now, tb);
}

}  // namespace mogo

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  std::string tb_type = absl::GetFlag(FLAGS_tb_type);
  absl::Status status;
  if (tb_type == "simple") {
    status = mogo::RunSimpleDemo();
  } else if (tb_type == "rate") {
    status = mogo::RunRateDemo();
  } else if (tb_type == "burst") {
    status = mogo::RunBurstDemo();
  } else {
    status = absl::InvalidArgumentError(
        absl::StrCat("Uknown token bucket type: '", tb_type, "'"));
  }

  if (status.ok()) {
    return EXIT_SUCCESS;
  } else {
    LOG(ERROR) << status;
    return EXIT_FAILURE;
  }
}