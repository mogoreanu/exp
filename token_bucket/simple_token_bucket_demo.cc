/*
bazel run token_bucket:simple_token_bucket_demo -- --stderrthreshold=0
*/

#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/flags.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "simple_token_bucket.h"
#include "absl/status/status.h"

ABSL_FLAG(bool, mytest, false, "");

namespace mogo {

absl::Status RunDemo() {
 mogo::SimpleThrottler t;
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