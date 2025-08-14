#ifndef PERF_TIGHTLOOP_LIB_H_
#define PERF_TIGHTLOOP_LIB_H_

#include <sched.h>
#include <stdint.h>

#include <utility>

#include "perf/bits.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/base/internal/cycleclock_config.h"

ABSL_DECLARE_FLAG(absl::Duration, run_duration);
ABSL_DECLARE_FLAG(absl::Duration, sleep_duration);
ABSL_DECLARE_FLAG(bool, exclude_sleep);

ABSL_DECLARE_FLAG(bool, print_csv);

// Returns [cycles_min, cycles_shift] from the histogram flags.
std::pair<uint64_t, uint64_t> SetupEnvironment();

// Prints the specified histogram to the console in human-readable format.
void PrintHistogram(mogo::Histogram64& h);

inline double Frequency() {
  return absl::base_internal::CycleClock::Frequency();
}

inline int64_t SecondsToCycles(double seconds) {
  return static_cast<int64_t>(Frequency() * seconds);
}

inline int64_t DurationToCycles(absl::Duration duration) {
  return SecondsToCycles(absl::FDivDuration(duration, absl::Seconds(1)));
}

inline absl::Duration CyclesToDuration(int64_t cycles) {
  // TODO(mogo): Check me.
  return absl::Seconds(cycles / Frequency());
}

// Will run the tight loop invoking the callback on every iteration.
// The callback argument is the current cycle clock.
template <typename T>
absl::Status RunLoop(T&& callback) {
  auto [cycles_min, cycles_shift] = SetupEnvironment();

  mogo::Histogram64 h(cycles_min, cycles_shift);
  using CycleClock = absl::base_internal::CycleClock;


  int64_t clocks_prev = CycleClock::Now();
  int64_t clocks_end =
      clocks_prev + SecondsToCycles(absl::ToDoubleSeconds(
                        absl::GetFlag(FLAGS_run_duration)));

  absl::Duration sleep_duration = absl::GetFlag(FLAGS_sleep_duration);
  if (sleep_duration >= absl::ZeroDuration()) {
    if (absl::GetFlag(FLAGS_exclude_sleep)) {
      // Loop that sleeps for the given duration and then measures the callback
      // invocation duration after sleep. Useful to allow the pipelines that
      // the callback uses to clear.
      do {
        absl::SleepFor(sleep_duration);
        clocks_prev = CycleClock::Now();
        callback(clocks_prev);
        int64_t clocks_now = CycleClock::Now();
        h.Add(clocks_now - clocks_prev);
      } while (clocks_prev < clocks_end);
    } else {
      // Loop that sleeps for the given duration and then measures
      // sleep+callback duration. Useful to measure the sleep jitter.
      do {
        absl::SleepFor(sleep_duration);
        callback(clocks_prev);
        int64_t clocks_now = CycleClock::Now();
        h.Add(clocks_now - clocks_prev);
        clocks_prev = clocks_now;
      } while (clocks_prev < clocks_end);
    }
  } else {
    // Hot loop that measures the callback duration.
    do {
      callback(clocks_prev);
      int64_t clocks_now = CycleClock::Now();
      h.Add(clocks_now - clocks_prev);
      clocks_prev = clocks_now;
    } while (clocks_prev < clocks_end);
  }

  PrintHistogram(h);
  return absl::OkStatus();
}

#endif  // MOGO_PERF_TIGHTLOOP_LIB_H_
