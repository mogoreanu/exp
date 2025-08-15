#ifndef PERF_CYCLE_CLOCK_UTILS_H_
#define PERF_CYCLE_CLOCK_UTILS_H_

#include "absl/time/time.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/base/internal/cycleclock_config.h"

namespace mogo {

using CycleClock = absl::base_internal::CycleClock;

inline double Frequency() {
  return CycleClock::Frequency();
}

inline int64_t SecondsToCycles(double seconds) {
  return static_cast<int64_t>(Frequency() * seconds);
}

inline int64_t DurationToCycles(absl::Duration duration) {
  return SecondsToCycles(absl::FDivDuration(duration, absl::Seconds(1)));
}

inline double CyclesToSeconds(int64_t cycles) {
  return static_cast<double>(cycles) / Frequency();
}

inline absl::Duration CyclesToDuration(int64_t cycles) {
  return absl::Seconds(CyclesToSeconds(cycles));
}


inline int64_t CyclesToUsec(int64_t cycles) {
  return static_cast<int64_t>(round(cycles * (1e6 / Frequency())));
}

}  // namespace mogo

#endif  // PERF_CYCLE_CLOCK_UTILS_H_