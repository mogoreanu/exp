// This tool spins a tight background loop measuring the time it takes to
// complete a loop in CPU cycles and micoseconds. The output is a histogram of
// elapsed time repartition
// Similar to http://google3/third_party/py/perfkitbenchmarker/data/rt_bench.cc
// Doc: go/vcpu-jitter
// Doc: go/gce-compute-jitter-roadmap
//
// http://btorpey.github.io/blog/2014/02/18/clock-sources-in-linux/
// cat /proc/cpuinfo | grep -i tsc
// Flag         Meaning
// tsc          The system has a TSC clock.
// rdtscp       The RDTSCP instruction is available.
// constant_tsc The TSC is synchronized across all sockets/cores.
// nonstop_tsc  The TSC is not affected by power management code.
//
// https://docs.microsoft.com/en-us/windows/win32/dxtecharts/game-timing-and-multicore-processors?redirectedfrom=MSDN

// clang-format off
/*
# Easiest way to run
bazel run -c opt perf:tightloop -- --run_duration=10s \
   --cycles_min=10 --cycles_shift=0

sudo apt-get install cpufrequtils
sudo cpufreq-set -r -g performance

bazel build -c opt --dynamic_mode=off perf:tightloop && \
taskset -c 0 \
bazel-bin/perf/tightloop --cycles_min=10 --cycles_shift=0 \
    --run_duration=10s

# mogo::FLS
Count      Cyc                      Microseconds             % tot  % this
----------------------------------------------------------------------------
33583801   10 cyc                   6.75ns                   2      2
332388966  10 cyc - 11 cyc          6.75ns - 7.25ns          27     24
415668813  11 cyc - 12 cyc          7.25ns - 8ns             58     31
543357981  12 cyc - 14 cyc          8ns - 9.25ns             99     40
5879542    14 cyc - 18 cyc          9.25ns - 12ns            99     0
72941      18 cyc - 26 cyc          12ns - 17.25ns           99     0
3142       26 cyc - 42 cyc          17.25ns - 28ns           99     0
856        42 cyc - 74 cyc          28ns - 49.5ns            99     0
16699      74 cyc - 138 cyc         49.5ns - 92.25ns         99     0
2522       138 cyc - 266 cyc        92.25ns - 177.75ns       99     0
6937       266 cyc - 522 cyc        177.75ns - 348.75ns      99     0
102        522 cyc - 1034 cyc       348.75ns - 691ns         99     0
367        1034 cyc - 2058 cyc      691ns - 1.37525us        99     0
13         2058 cyc - 4106 cyc      1.37525us - 2.74375us    99     0
905        4106 cyc - 8202 cyc      2.74375us - 5.48075us    99     0
1245       8202 cyc - 16394 cyc     5.48075us - 10.955us     99     0
1245       16394 cyc - 32778 cyc    10.955us - 21.90325us    99     0
83         32778 cyc - 65546 cyc    21.90325us - 43.79975us  99     0
14         65546 cyc - 131082 cyc   43.79975us - 87.5925us   99     0
2          131082 cyc - 262154 cyc  87.5925us - 175.1785us   100    0
----------------------------------------------------------------------------

bazel run -c opt perf:tightloop -- --run_duration=10s \
  --duration_min=4ms --duration_shift=3us --sleep_duration=4000us

sudo cpufreq-set -g powersave
*/
// clang-format on

#include <sched.h>

#include <cstdlib>

#include "perf/tightloop_lib.h"
#include "absl/log/log.h"
#include "absl/log/initialize.h"
#include "absl/flags/parse.h"

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  absl::Status status = RunLoop(/*callback=*/[](int64_t _) {});
  if (!status.ok()) {
    LOG(ERROR) << status;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
/*
# absl::countl_zero
Interesting that more samples < 12 cycles, but there's some noise around
18-26 cycles. Guess is that this is due to the extra branch.

Count      Cyc                      Microseconds             % tot  % this
----------------------------------------------------------------------------
69154132   10 cyc                   6.75ns                   6      6
392369778  10 cyc - 11 cyc          6.75ns - 7.25ns          42     36
265505764  11 cyc - 12 cyc          7.25ns - 8ns             66     24
22137073   12 cyc - 14 cyc          8ns - 9.25ns             68     2
316743     14 cyc - 18 cyc          9.25ns - 12ns            68     0
338636028  18 cyc - 26 cyc          12ns - 17.25ns           99     31
263599     26 cyc - 42 cyc          17.25ns - 28ns           99     0
3589       42 cyc - 74 cyc          28ns - 49.5ns            99     0
17144      74 cyc - 138 cyc         49.5ns - 92.25ns         99     0
5010       138 cyc - 266 cyc        92.25ns - 177.75ns       99     0
5980       266 cyc - 522 cyc        177.75ns - 348.75ns      99     0
68         522 cyc - 1034 cyc       348.75ns - 691ns         99     0
356        1034 cyc - 2058 cyc      691ns - 1.37525us        99     0
12         2058 cyc - 4106 cyc      1.37525us - 2.74375us    99     0
972        4106 cyc - 8202 cyc      2.74375us - 5.48075us    99     0
1166       8202 cyc - 16394 cyc     5.48075us - 10.955us     99     0
1201       16394 cyc - 32778 cyc    10.955us - 21.90325us    99     0
139        32778 cyc - 65546 cyc    21.90325us - 43.8us      99     0
16         65546 cyc - 131082 cyc   43.8us - 87.5935us       99     0
1          131082 cyc - 262154 cyc  87.5935us - 175.18025us  100    0
----------------------------------------------------------------------------
*/
