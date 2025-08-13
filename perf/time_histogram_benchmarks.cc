#include <string>

#include "base/logging.h"
#include "cloud/util/stopwatch/stopwatch.h"
#include "experimental/mogo/perf/time_histogram.h"
#include "testing/base/public/benchmark.h"

/*
sudo cpufreq-set -g performance

blaze test -c opt --dynamic_mode=off --test_output=streamed \
  --cache_test_results=no experimental/mogo/perf:time_histogram_benchmarks \
  --test_arg=--benchmark_filter=all \
  --test_arg=--benchmark_repetitions=1 \
  --test_arg=--benchmark_enable_random_interleaving=false \
  --test_arg=--heap_check=

sudo cpufreq-set -g powersave

Benchmark                 Time(ns)        CPU(ns)     Iterations
----------------------------------------------------------------
BM_TimeHistogram_Simple         22.8           22.8     30651634
BM_Stopwatch_Simple           2064           2064         338994
*/

namespace cloud_util_stat {
namespace {

void BM_TimeHistogram_Simple(benchmark::State& state) {
  TimeHistogram h(1000, 512);
  for (auto s : state) {
    auto scope_span = h.NewScopeSpan();
  }
  VLOG(2) << h.ToHumanString();
}
BENCHMARK(BM_TimeHistogram_Simple);

void BM_Stopwatch_Simple(benchmark::State& state) {
  for (auto s : state) {
    STOPWATCH(loop);
  }
}
BENCHMARK(BM_Stopwatch_Simple);

}  // namespace
}  // namespace cloud_util_stat
