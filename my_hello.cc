#include <iostream>
#include <string>
#include <vector>

#include "absl/strings/str_join.h"
#include "absl/log/log.h"
#include "absl/log/initialize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

ABSL_FLAG(bool, mytest, false, "");

int main(int argc, char** argv)
{
    absl::ParseCommandLine(argc, argv);
    absl::InitializeLog();
    std::vector<std::string> v = {"foo", "bar", "baz"};
    std::string s = absl::StrJoin(v, "-");

    std::cout << "Joined string: " << s << "\n";

    LOG(INFO) << "This is a log??";

    return 0;
}