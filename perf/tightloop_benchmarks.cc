#include <bitset>
#include <cstdint>
#include <ostream>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"
#include "perf/bits.h"
#include "perf/time_histogram.h"

/*
sudo cpufreq-set -g performance

bazel build -c opt  --dynamic_mode=off perf:tightloop_benchmarks \
&& taskset -c 0 bazel-bin/perf/tightloop_benchmarks \
  --benchmark_filter=all \
  --benchmark_repetitions=1 \
  --benchmark_enable_random_interleaving=false

sudo cpufreq-set -g powersave

# FLS
Benchmark                    Time(ns)        CPU(ns)     Iterations
-------------------------------------------------------------------
BM_Histogram32GetBucket0            0.826          0.825  848676721
BM_Histogram32GetBucket1            0.827          0.826  848328581
BM_Histogram32GetBucket012          2.97           2.96   236592155

# absl::countl_zero
Benchmark                    Time(ns)        CPU(ns)     Iterations
-------------------------------------------------------------------
BM_Histogram32GetBucket0            0.742          0.741  943017355
BM_Histogram32GetBucket1            0.742          0.741  945192155
BM_Histogram32GetBucket012          2.87           2.86   244212645

# InsecureBitGen
---------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations
---------------------------------------------------------------------
BM_Histogram32GetBucket0         1.37 ns         1.37 ns    474934929
BM_Histogram32GetBucket1        0.692 ns        0.692 ns    756549732
BM_Histogram32GetBucket012       2.70 ns         2.70 ns    254424239
*/

namespace mogo {

void BM_Histogram32GetBucket0(benchmark::State& state) {
  Histogram<uint32_t, uint32_t> h(/*min=*/100, /*shift=*/3);
  uint32_t x = 36;
  uint64_t sum = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(x);
    sum += h.GetBucket(x);
  }
  VLOG(2) << sum;
}
BENCHMARK(BM_Histogram32GetBucket0);

void BM_Histogram32GetBucket1(benchmark::State& state) {
  Histogram<uint32_t, uint32_t> h(/*min=*/100, /*shift=*/3);
  uint32_t x = 150;
  uint64_t sum = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(x);
    sum += h.GetBucket(x);
  }
  VLOG(2) << sum;
}
BENCHMARK(BM_Histogram32GetBucket1);

void BM_Histogram32GetBucket012(benchmark::State& state) {
  Histogram<uint32_t, uint32_t> h(/*min=*/10, /*shift=*/2);
  uint32_t x = 0;
  uint64_t sum = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(x);
    sum += h.GetBucket(x);
    x += 1;
    x %= 30;
  }
  VLOG(2) << sum;
}
BENCHMARK(BM_Histogram32GetBucket012);

}  // namespace mogo
