/*
bazel test stat:approx_counter_test --test_output=streamed
*/

#include "approx_counter.h"

#include <stddef.h>

#include <cstdint>
#include <deque>
#include <vector>

#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
// #include "stats/base/timeseries.h"

namespace mogo {

constexpr double kAbsoluteError = 1;
constexpr double kPercentageError = .05;
const int kKilobyte = 1024;
const int kMegabyte = kKilobyte * 1024;

class ThroughputCounterTest : public ::testing::TestWithParam<absl::Duration> {
 protected:
  ThroughputCounterTest() : current_time_(), counter_(current_time_) {}

  void SimulateThroughputWith4KBlocks(int64_t bps, absl::Duration interval) {
    constexpr int64_t kRequestSize = 4 * kKilobyte;
    const size_t kNumRequests = bps / kRequestSize;
    const absl::Duration kRequestDelay = interval / kNumRequests;
    for (size_t i = 0; i < kNumRequests; ++i) {
      counter_.RecordRequest(kRequestSize, current_time_);
      current_time_ += kRequestDelay;
    }
    const int64_t remainder = bps - kRequestSize * kNumRequests;
    counter_.RecordRequest(remainder, current_time_ - kRequestDelay);
    current_time_ += interval - (kNumRequests * kRequestDelay);
  }

  void SimulateThroughput(double bytes, absl::Duration interval) {
    constexpr absl::Duration kRequestDelay = absl::Microseconds(10);
    const size_t kNumRequests = interval / kRequestDelay;
    const int64_t request_size = bytes / kNumRequests;
    const double remainder = bytes / kNumRequests - request_size;
    double accumulated_remainder = 0;
    for (size_t i = 0; i < kNumRequests; ++i) {
      counter_.RecordRequest(request_size + accumulated_remainder,
                             current_time_);
      current_time_ += kRequestDelay;
      if (accumulated_remainder >= 1) {
        accumulated_remainder -= 1;
      }
      accumulated_remainder += remainder;
    }
    counter_.RecordRequest(accumulated_remainder,
                           current_time_ - kRequestDelay);
    absl::Duration kUnaccountedRemainder =
        interval - kNumRequests * kRequestDelay;
    current_time_ += kUnaccountedRemainder;
  }

  absl::Time current_time_;
  ApproxCounter counter_;
};

TEST_P(ThroughputCounterTest, MeasuresThroughput) {
  absl::Duration interval = GetParam();
  new (&counter_) ApproxCounter(current_time_, interval);
  const std::vector<double> throughputs_to_test = {
      10 * kMegabyte,  40 * kMegabyte,  120 * kMegabyte,  240 * kMegabyte,
      400 * kMegabyte, 800 * kMegabyte, 1200 * kMegabyte, 2000 * kMegabyte};
  for (const double expected_throughput : throughputs_to_test) {
    SimulateThroughput(expected_throughput, interval);
    EXPECT_NEAR(counter_.GetBytesPerInterval(current_time_),
                expected_throughput, kAbsoluteError);
  }
}

TEST_P(ThroughputCounterTest, MeasuresThroughputWith4KBlocks) {
  LOG(INFO) << "BPS at start = " << counter_.GetBytesPerInterval(current_time_);
  absl::Duration interval = GetParam();
  new (&counter_) ApproxCounter(current_time_, interval);
  const std::vector<int64_t> throughputs_to_test = {
      10 * kMegabyte,  40 * kMegabyte,  120 * kMegabyte,  240 * kMegabyte,
      400 * kMegabyte, 800 * kMegabyte, 1200 * kMegabyte, 2000 * kMegabyte};
  for (const int64_t expected_throughput : throughputs_to_test) {
    SimulateThroughputWith4KBlocks(expected_throughput, interval);
    EXPECT_DOUBLE_EQ(counter_.GetBytesPerInterval(current_time_),
                     expected_throughput);
  }
}

TEST_F(ThroughputCounterTest, SkipSpansTest) {
  absl::Time simulated_now = absl::Now();
  ApproxCounter counter(simulated_now);

  absl::Time end_time = simulated_now + absl::Seconds(2);
  int c = 0;
  while (true) {
    if (simulated_now > end_time) {
      break;
    }
    counter.RecordRequest(1, simulated_now);
    ++c;

    VLOG_EVERY_N_SEC(1, 0.1) << counter.ToDebugString();

    simulated_now += absl::Milliseconds(1);
  }

  simulated_now += absl::Seconds(2);
  counter.RecordRequest(1, simulated_now);

  // The skip by 2 seconds should have wiped out the history.
  ASSERT_EQ(1, counter.GetBytesPerSecond(simulated_now));
}

TEST_F(ThroughputCounterTest, BigSkipTest) {
  absl::Time simulated_now = absl::Now();
  ApproxCounter counter(simulated_now);

  counter.RecordRequest(1, simulated_now);
  ASSERT_EQ(1, counter.GetBytesPerSecond(simulated_now));

  simulated_now += absl::Seconds(2);
  ASSERT_EQ(0, counter.GetBytesPerSecond(simulated_now));

  counter.RecordRequest(1, simulated_now);
  simulated_now += absl::Seconds(0.5);
  counter.RecordRequest(1, simulated_now);
  ASSERT_EQ(2, counter.GetBytesPerSecond(simulated_now));

  simulated_now += absl::Seconds(1.1);
  ASSERT_EQ(0, counter.GetBytesPerSecond(simulated_now));
}

TEST_F(ThroughputCounterTest, SkipBackTest) {
  absl::Time simulated_now = absl::Now();
  ApproxCounter counter(simulated_now);

  counter.RecordRequest(1, simulated_now - absl::Milliseconds(10));
  ASSERT_EQ(1, counter.GetBytesPerSecond(simulated_now));

  counter.RecordRequest(1, simulated_now - absl::Milliseconds(500));
  ASSERT_EQ(2, counter.GetBytesPerSecond(simulated_now));
}

TEST_F(ThroughputCounterTest, BadTimeTest) {
  absl::Time simulated_now = absl::UnixEpoch() - absl::Hours(1);
  ApproxCounter counter(simulated_now);

  ASSERT_EQ(0, counter.GetBytesPerSecond(simulated_now));

  counter.RecordRequest(1, simulated_now);
  ASSERT_EQ(1, counter.GetBytesPerSecond(simulated_now));

  simulated_now += absl::Milliseconds(500);
  counter.RecordRequest(1, simulated_now);
  ASSERT_EQ(2, counter.GetBytesPerSecond(simulated_now));
}

TEST_F(ThroughputCounterTest, LastSpanTest) {
  absl::Time simulated_now = absl::Now();
  ApproxCounter counter(simulated_now);

  // Generate requests for 2 virtual seconds.
  for (int i = 0; i < 200; ++i) {
    simulated_now += absl::Milliseconds(10);
    counter.RecordRequest(1, simulated_now);
  }

  // Skip all of the second.
  simulated_now += absl::Milliseconds(1500);

  ASSERT_GE(0, counter.GetBytesPerSecond(simulated_now));
}

/* This test can be used to display the accuracy of the ApproxThroughputCouter.
approx=41 timeseries=23 actual=41
approx=41 timeseries=47 actual=41
approx=41 timeseries=35 actual=41
approx=41 timeseries=46 actual=41
approx=41 timeseries=39 actual=41

bazel test stat:approx_counter_test --test_output=streamed \
  --test_filter=ThroughputCounterTest.DisplayThroughput
 */
TEST_F(ThroughputCounterTest, DisplayThroughput) {
  absl::Time simulated_now = absl::Now();
  ApproxCounter approx_throughput_counter(simulated_now);

  // TimeSeries<int64_t> time_series_counter;
  std::deque<absl::Time> io_times;

  absl::Time start_time = simulated_now;
  absl::Time end_time = simulated_now + absl::Seconds(30);

  absl::Time last_log_time = start_time;

  int c = 0;
  while (simulated_now < end_time) {
    // Move time forward with some jitter.
    if (c % 2 == 0) {
      simulated_now += absl::Milliseconds(1);
    } else {
      simulated_now += absl::Milliseconds(9);
    }
    if (c % 40 == 0) {
      simulated_now += absl::Milliseconds(500);
    }

    // Clean up IOs that are older than 1 second.
    absl::Time one_sec_ago = simulated_now - absl::Seconds(1);
    while (!io_times.empty() && io_times.front() < one_sec_ago) {
      io_times.pop_front();
    }

    approx_throughput_counter.RecordRequest(1, simulated_now);
    // time_series_counter._Add(1, simulated_now);
    io_times.push_back(simulated_now);
    ++c;

    if (last_log_time < one_sec_ago) {
      LOG(INFO) << "approx="
                << approx_throughput_counter.GetBytesPerSecond(simulated_now)
                // << " timeseries="
                // << time_series_counter.Get(one_sec_ago, simulated_now)
                << " actual=" << io_times.size();
      last_log_time = simulated_now;
    }
  }
}

TEST_P(ThroughputCounterTest, LongIntervalTest) {
  const absl::Duration interval = GetParam();
  ApproxCounter counter_(current_time_, interval);

  const double rate = 100.0;
  const absl::Duration delay = absl::Seconds(1) / rate;

  absl::Time simulated_now = absl::Now();
  absl::Time end_time = simulated_now + interval;
  while (simulated_now < end_time) {
    simulated_now += delay;
    counter_.RecordRequest(1, simulated_now);
  }
  ASSERT_NEAR(rate, counter_.GetBytesPerSecond(simulated_now),
              kPercentageError * rate);
  simulated_now += interval / 2.0;
  ASSERT_NEAR(rate / 2, counter_.GetBytesPerSecond(simulated_now),
              kPercentageError * rate);
}

INSTANTIATE_TEST_SUITE_P(CountersByInterval, ThroughputCounterTest,
                         ::testing::Values(absl::Seconds(1), absl::Seconds(2),
                                           absl::Seconds(4), absl::Seconds(8),
                                           absl::Seconds(16), absl::Seconds(32),
                                           absl::Seconds(64),
                                           absl::Seconds(128)));
}  // namespace mogo
