#include <bitset>
#include <cstdint>
#include <ostream>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "gtest/gtest.h"
#include "perf/bits.h"
#include "perf/time_histogram.h"

/*
bazel test --test_output=streamed perf:tightloop_test
*/

namespace mogo {
namespace {

TEST(StringTest, Test1) {
  absl::InsecureBitGen gen;
  int64_t x = 0;
  LOG(INFO) << std::bitset<64>(x) << " clz= " << Fls64(x)
            << " bsr= " << Fls64Bsr(x);
  EXPECT_EQ(Fls64(0), Fls64Bsr(0)) << "x=" << x << std::endl
                                   << std::bitset<64>(x);
  x = 1;
  for (int i = 0; i < 63; ++i) {
    LOG(INFO) << std::bitset<64>(x) << " clz= " << Fls64(x)
              << " bsr= " << Fls64Bsr(x);
    EXPECT_EQ(Fls64(x), Fls64Bsr(x)) << "x=" << x << std::endl
                                     << std::bitset<64>(x);
    EXPECT_EQ(mogo::internal::Fls64(x), i + 1);
    EXPECT_EQ(mogo::Fls64(x), i + 1);
    x <<= 1;
  }
  x = 3;
  for (int i = 0; i < 63; ++i) {
    LOG(INFO) << std::bitset<64>(x) << " clz= " << Fls64(x)
              << " bsr= " << Fls64Bsr(x);
    EXPECT_EQ(Fls64(x), Fls64Bsr(x)) << "x=" << x << std::endl
                                     << std::bitset<64>(x);
    x <<= 1;
  }

  for (int i = 0; i < 100; ++i) {
    x = absl::uniform_int_distribution<int64_t>(
        0, std::numeric_limits<int64_t>::max())(gen);
    LOG(INFO) << std::bitset<64>(x) << " clz= " << Fls64(x)
              << " bsr= " << Fls64Bsr(x);
    EXPECT_EQ(Fls64(x), Fls64Bsr(x)) << "x=" << x << std::endl
                                     << std::bitset<64>(x);
  }
}

TEST(Bits, SignedUnsignedShiftTest) {
  int x = -1;
  int y = x >> 1;
  LOG(INFO) << std::bitset<sizeof(int) * 8>(x);
  LOG(INFO) << std::bitset<sizeof(int) * 8>(y);
  uint x2 = 0;
  x2 -= 1;
  LOG(INFO) << std::bitset<sizeof(uint) * 8>(x2);
  LOG(INFO) << std::bitset<sizeof(uint) * 8>(x2 >> 1);
}

TEST(Test, Clz) {
  static_assert(sizeof(unsigned int) == sizeof(uint32_t));
  static_assert(sizeof(unsigned long) == sizeof(uint64_t));  // NOLINT
  LOG(INFO) << "__builtin_clz(-10)=" << __builtin_clz(-10);
  LOG(INFO) << "__builtin_clz(-1)=" << __builtin_clz(-1);
  LOG(INFO) << "__builtin_clz(0)=" << __builtin_clz(0);  // Undefined
  LOG(INFO) << "__builtin_clz(1)=" << __builtin_clz(1);
  LOG(INFO) << "__builtin_clz(5)=" << __builtin_clz(5);

  LOG(INFO) << "__builtin_clzl(-10)=" << __builtin_clzl(-10);
  LOG(INFO) << "__builtin_clzl(-1)=" << __builtin_clzl(-1);
  LOG(INFO) << "__builtin_clzl(0)=" << __builtin_clzl(0);  // Undefined
  LOG(INFO) << "__builtin_clzl(1)=" << __builtin_clzl(1);
  LOG(INFO) << "__builtin_clzl(5)=" << __builtin_clzl(5);
}

TEST(TestHistogram32, ZeroRange) {
  Histogram32 h(0, 0);
  ASSERT_EQ(h.range_max_pos(0), 0ULL);

  ASSERT_EQ(h.range_min_pos(1), 0ULL);
  ASSERT_EQ(h.range_max_pos(1), 1ULL);

  ASSERT_EQ(h.range_min_pos(2), 1ULL);
  ASSERT_EQ(h.range_max_pos(2), 2ULL);

  ASSERT_EQ(h.range_min_pos(3), 2ULL);
  ASSERT_EQ(h.range_max_pos(3), 4ULL);

  ASSERT_EQ(h.range_min_pos(4), 4ULL);
  ASSERT_EQ(h.range_max_pos(4), 8ULL);

  ASSERT_EQ(h.range_min_pos(5), 8ULL);
  ASSERT_EQ(h.range_max_pos(5), 16ULL);
}

#define VALIDATE_POS(h, v, pos)          \
  {                                      \
    (h).Reset();                         \
    (h).Add(v);                          \
    EXPECT_EQ((h).value_at_pos(pos), 1ULL); \
  }

TEST(TestHistogram32, ZeroValue) {
  Histogram32 h(0, 0);

  VALIDATE_POS(h, -10, 0);
  VALIDATE_POS(h, -1, 0);
  VALIDATE_POS(h, 0, 1);
  VALIDATE_POS(h, 1, 2);
  VALIDATE_POS(h, 2, 3);
  VALIDATE_POS(h, 3, 3);
  VALIDATE_POS(h, 4, 4);
  VALIDATE_POS(h, 7, 4);
  VALIDATE_POS(h, 8, 5);
}

TEST(TestHistogram32, MinRange) {
  Histogram32 hmin(7, 0);

  ASSERT_EQ(hmin.range_max_pos(0), 7ULL);

  ASSERT_EQ(hmin.range_min_pos(1), 7ULL);
  ASSERT_EQ(hmin.range_max_pos(1), 8ULL);

  ASSERT_EQ(hmin.range_min_pos(2), 8ULL);
  ASSERT_EQ(hmin.range_max_pos(2), 9ULL);

  ASSERT_EQ(hmin.range_min_pos(3), 9ULL);
  ASSERT_EQ(hmin.range_max_pos(3), 11ULL);

  ASSERT_EQ(hmin.range_min_pos(4), 11ULL);
  ASSERT_EQ(hmin.range_max_pos(4), 15ULL);

  ASSERT_EQ(hmin.range_min_pos(5), 15ULL);
  ASSERT_EQ(hmin.range_max_pos(5), 23ULL);
}

TEST(TestHistogram32, MinValue) {
  Histogram32 hmin(7, 0);

  VALIDATE_POS(hmin, -10, 0);
  VALIDATE_POS(hmin, 6, 0);
  VALIDATE_POS(hmin, 7, 1);
  VALIDATE_POS(hmin, 8, 2);
  VALIDATE_POS(hmin, 9, 3);
  VALIDATE_POS(hmin, 10, 3);
  VALIDATE_POS(hmin, 11, 4);
  VALIDATE_POS(hmin, 14, 4);
  VALIDATE_POS(hmin, 15, 5);
}

TEST(TestHistogram32, ShiftRange) {
  Histogram32 h(/*min=*/1000, /*shift=*/3);

  ASSERT_EQ(h.range_max_pos(0), 1000ULL);

  ASSERT_EQ(h.range_min_pos(1), 1000ULL);
  ASSERT_EQ(h.range_max_pos(1), 1008ULL);

  ASSERT_EQ(h.range_min_pos(2), 1008ULL);
  ASSERT_EQ(h.range_max_pos(2), 1016ULL);

  ASSERT_EQ(h.range_min_pos(3), 1016ULL);
  ASSERT_EQ(h.range_max_pos(3), 1032ULL);

  ASSERT_EQ(h.range_min_pos(4), 1032ULL);
  ASSERT_EQ(h.range_max_pos(4), 1064ULL);

  ASSERT_EQ(h.range_min_pos(5), 1064ULL);
  ASSERT_EQ(h.range_max_pos(5), 1128ULL);
}

TEST(TestHistogram32, ShiftValue) {
  Histogram32 h(/*min=*/1000, /*shift=*/3);
  VALIDATE_POS(h, 500, 0);
  VALIDATE_POS(h, 999, 0);
  VALIDATE_POS(h, 1000, 1);
  VALIDATE_POS(h, 1007, 1);
  VALIDATE_POS(h, 1008, 2);
  VALIDATE_POS(h, 1015, 2);
  VALIDATE_POS(h, 1016, 3);
  VALIDATE_POS(h, 1031, 3);
  VALIDATE_POS(h, 1032, 4);
  VALIDATE_POS(h, 1063, 4);
  VALIDATE_POS(h, 1064, 5);
  VALIDATE_POS(h, 1127, 5);
  VALIDATE_POS(h, 1128, 6);
}

}  // namespace
}  // namespace mogo
