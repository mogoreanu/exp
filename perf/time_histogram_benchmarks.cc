#include <string>

#include "absl/log/log.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"
#include "perf/time_histogram.h"

/*
sudo cpufreq-set -g performance

bazel test -c opt --dynamic_mode=off --test_output=streamed \
  --cache_test_results=no perf:time_histogram_benchmarks \
  --test_arg=--benchmark_filter=all \
  --test_arg=--benchmark_repetitions=1 \
  --test_arg=--benchmark_enable_random_interleaving=false

sudo cpufreq-set -g powersave

------------------------------------------------------------------
Benchmark                        Time             CPU   Iterations
------------------------------------------------------------------
BM_TimeHistogram_Simple       23.2 ns         23.2 ns     30216846
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

}  // namespace
}  // namespace cloud_util_stat
