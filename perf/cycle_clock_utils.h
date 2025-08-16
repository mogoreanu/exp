#ifndef PERF_CYCLE_CLOCK_UTILS_H_
#define PERF_CYCLE_CLOCK_UTILS_H_

#include "absl/base/internal/cycleclock.h"
#include "absl/base/internal/cycleclock_config.h"
#include "absl/time/time.h"

namespace mogo {

using CycleClock = absl::base_internal::CycleClock;

// A helper class for cycles and duration conversion.
class CycleClockUtils {
 public:
  CycleClockUtils() { Init(); }

  inline int64_t SecondsToCycles(double seconds) {
    return static_cast<int64_t>(cycles_per_second_ * seconds);
  }

  inline int64_t DurationToCycles(absl::Duration duration) {
    return SecondsToCycles(absl::FDivDuration(duration, absl::Seconds(1)));
  }

  inline double CyclesToSeconds(int64_t cycles) {
    return static_cast<double>(cycles) * seconds_per_cycle_;
  }

  inline absl::Duration CyclesToDuration(int64_t cycles) {
    return absl::Seconds(CyclesToSeconds(cycles));
  }

  inline int64_t CyclesToUsec(int64_t cycles) {
    return static_cast<int64_t>(round(cycles * usec_per_cycle_));
  }

 protected:
  double cycles_per_second_;
  double seconds_per_cycle_;
  double cycles_per_ms_;
  double ms_per_cycle_;
  double cycles_per_usec_;
  double usec_per_cycle_;

  void Init();

 private:
  void ProtectedInit();
};

inline int64_t SecondsToCycles(double seconds) {
  return static_cast<int64_t>(CycleClock::Frequency() * seconds);
}

inline int64_t DurationToCycles(absl::Duration duration) {
  return SecondsToCycles(absl::FDivDuration(duration, absl::Seconds(1)));
}

inline double CyclesToSeconds(int64_t cycles) {
  return static_cast<double>(cycles) / CycleClock::Frequency();
}

inline absl::Duration CyclesToDuration(int64_t cycles) {
  return absl::Seconds(CyclesToSeconds(cycles));
}

inline int64_t CyclesToUsec(int64_t cycles) {
  return static_cast<int64_t>(round(cycles * (1e6 / CycleClock::Frequency())));
}

}  // namespace mogo

#endif  // PERF_CYCLE_CLOCK_UTILS_H_