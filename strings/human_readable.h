#ifndef STRINGS_HUMAN_READABLE_H_
#define STRINGS_HUMAN_READABLE_H_

#include <cstdint>
#include <string>
#include "absl/strings/string_view.h"

namespace strings {

class HumanReadableNumBytes {
 public:
  static bool ToInt64(absl::string_view str, int64_t* num_bytes);
  static std::string ToString(int64_t num_bytes);
};

}  // namespace strings

#endif  // STRINGS_HUMAN_READABLE_H_
