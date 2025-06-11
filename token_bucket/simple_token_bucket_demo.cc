/*
bazel run token_bucket:simple_token_bucket_demo -- --stderrthreshold=0
*/

#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/flags.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "simple_token_bucket.h"

ABSL_FLAG(bool, mytest, false, "");

namespace mogo {

absl::Status RunDemo() {
  absl::Time start_time = absl::Now();
  absl::Time now = start_time;
  mogo::SimpleThrottler t(now);
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
      double log_delta_seconds = absl::ToDoubleSeconds(now - next_log_time + absl::Seconds(1));
      int log_delta_requests = request_count - prev_log_request_count;
      LOG(INFO) << "Request rate: " << log_delta_requests / log_delta_seconds << " r/s";
      next_log_time += absl::Seconds(1);
      prev_log_request_count = request_count;
    }
  }
  LOG(INFO) << "Total rate: " << request_count / absl::ToDoubleSeconds(now - start_time) << " r/s";
  return absl::OkStatus();
}

}  // namespace mogo

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  absl::Status s = mogo::RunDemo();
  if (s.ok()) {
    return EXIT_SUCCESS;
  } else {
    return EXIT_FAILURE;
  }
}