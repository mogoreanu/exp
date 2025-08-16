#include "perf/cycle_clock_utils.h"

#include "absl/base/call_once.h"
#include "absl/log/check.h"

namespace mogo {

// static absl::once_flag g_cycle_timer_once;

void CycleClockUtils::Init() {
    //   absl::base_internal::LowLevelCallOnce(&g_cycle_timer_once,
  //                                         &CycleClockUtils::ProtectedInit);
  ProtectedInit();
}

void CycleClockUtils::ProtectedInit() {
  double cps = CycleClock::Frequency();
  CHECK_GT(cps, 0.0);
  cycles_per_second_ = cps;
  seconds_per_cycle_ = 1.L / cps;
  cycles_per_ms_ = cps / 1e3;
  ms_per_cycle_ = 1e3 / cps;
  cycles_per_usec_ = cps / 1e6;
  usec_per_cycle_ = 1e6 / cps;
}

}  // namespace mogo
