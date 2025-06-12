#include "stat_utils.h"

#include <stddef.h>

#include "gtest/gtest.h"

namespace mogo {
namespace {

TEST(PositiveModulo, Test7) {
  for (auto x : {3, 7, 9, 25, -3, -7, -9, -25}) {
    ASSERT_EQ(PositiveModulo<7>(x), (x + 49) % 7) << "x=" << x;
  }
}

TEST(PositiveModulo, Test8) {
  for (auto x : {3, 8, 9, 25, -3, -8, -9, -25}) {
    ASSERT_EQ(PositiveModulo<8>(x), (x + 64) % 8) << "x=" << x;
  }
}

}  // namespace
}  // namespace mogo
