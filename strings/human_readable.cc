#include "strings/human_readable.h"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"

namespace strings {

bool HumanReadableNumBytes::ToInt64(absl::string_view str, int64_t* num_bytes) {
  if (str.empty()) return false;

  // Find where the suffix starts
  size_t suffix_start = 0;
  while (suffix_start < str.size() && (std::isdigit(str[suffix_start]) || str[suffix_start] == '.')) {
    suffix_start++;
  }

  absl::string_view num_str = str.substr(0, suffix_start);
  absl::string_view suffix = str.substr(suffix_start);

  double val;
  if (!absl::SimpleAtod(num_str, &val)) {
    return false;
  }

  std::string clean_suffix = std::string(suffix);
  clean_suffix = absl::AsciiStrToUpper(clean_suffix);
  // Remove trailing 'B' if present, e.g., "KB" -> "K"
  if (clean_suffix.size() > 1 && clean_suffix.back() == 'B') {
    clean_suffix.pop_back();
  }
  // Remove 'I' if present, e.g., "KIB" -> "K" (after removing B)
  if (clean_suffix.size() > 1 && clean_suffix.back() == 'I') {
    clean_suffix.pop_back();
  }

  int64_t multiplier = 1;
  if (clean_suffix == "K") {
    multiplier = 1024LL;
  } else if (clean_suffix == "M") {
    multiplier = 1024LL * 1024;
  } else if (clean_suffix == "G") {
    multiplier = 1024LL * 1024 * 1024;
  } else if (clean_suffix == "T") {
    multiplier = 1024LL * 1024 * 1024 * 1024;
  } else if (clean_suffix == "P") {
    multiplier = 1024LL * 1024 * 1024 * 1024 * 1024;
  } else if (!clean_suffix.empty()) {
    return false; // Unknown suffix
  }

  *num_bytes = static_cast<int64_t>(val * multiplier);
  return true;
}

std::string HumanReadableNumBytes::ToString(int64_t num_bytes) {
  if (num_bytes < 0) {
    return "-" + ToString(-num_bytes);
  }
  if (num_bytes < 1024) {
    return std::to_string(num_bytes) + " B";
  }

  static const char* suffixes[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  int s = 0;
  double count = num_bytes;
  while (count >= 1024 && s < 5) {
    s++;
    count /= 1024;
  }

  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << count << " " << suffixes[s];
  return out.str();
}

}  // namespace strings
