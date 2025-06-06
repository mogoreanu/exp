/*
# Basic test
bazel run :my_hello

# Tinker with ABSL logging
# Flags that control the logging behavior are here:
# bazel-exp/external/abseil-cpp+/absl/log/flags.cc
bazel run :my_hello -- --stderrthreshold=0

# Specifying and working with flags
bazel run :my_hello -- --mytest=true
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

ABSL_FLAG(bool, mytest, false, "");

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  std::vector<std::string> v = {"foo", "bar", "baz"};
  std::string s = absl::StrJoin(v, "-");

  std::cout << "Joined string: " << s << "\n";
  std::cout << "`mytest` flag value: " << absl::GetFlag(FLAGS_mytest) << "\n";

  LOG(INFO) << "This is an INFO log.";
  LOG(WARNING) << "This is a WARNING log.";
  LOG(ERROR) << "This is an ERROR log!";

  return 0;
}