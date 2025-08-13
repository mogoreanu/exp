#include "perf/time_histogram.h"

#include <iostream>
#include <ostream>
#include <string>

#include "absl/log/log.h"
#include "gtest/gtest.h"

/*
blaze test --test_output=streamed experimental/mogo/perf:time_histogram_test
 */

namespace cloud_util_stat {
namespace {

TEST(StringTest, Test1) {
  // min bucket: < 1000
  // 0: 1000 - 1512
  // 1: 1512 - 2024
  // 2: 2024 - 3048
  TimeHistogram th(1000, 500);

  // Empty histogram shouldn't crash.
  LOG(INFO) << th.ToHumanString();

  // Samples smaller than min should go to last bucket.
  th.AddSample(300);
  ASSERT_EQ(1, th.buckets(64)) << th.ToHumanString();

  // Weird sample, but hey, should work.
  th.AddSample(0);
  ASSERT_EQ(2, th.buckets(64)) << th.ToHumanString();

  // Negative duration.
  th.AddSample(-100);
  ASSERT_EQ(3, th.buckets(64)) << th.ToHumanString();

  // Samples larger than min, but smaller than min + step1 go in first bucket.
  th.AddSample(1300);
  ASSERT_EQ(1, th.buckets(0)) << th.ToHumanString();

  // Second bucket.
  th.AddSample(1800);
  ASSERT_EQ(1, th.buckets(1)) << th.ToHumanString();

  // Second bucket.
  th.AddSample(2500);
  ASSERT_EQ(1, th.buckets(2)) << th.ToHumanString();

  LOG(INFO) << "Success!" << std::endl << th.ToHumanString();
}

}  // namespace
}  // namespace cloud_util_stat
