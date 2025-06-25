#include <cstdint>

#include "perf/bits.h"
#include "benchmark/benchmark.h"


// clang-format off
/*
bazel build -c opt perf:bits_bin && \
gdb bazel-bin/perf/bits_bin -iex "set auto-load off" \
  -iex "set pagination off" \
  -ex="disass main2" -ex="disass main3" -ex="disass main4" \
  -ex="disass main_absl_countl_zero" -ex="disass main_get_bucket" -ex="exit"
*/
// clang-format on

int main2(uint64_t val) {
  return mogo::Fls64(val);
  /*
   0x00000000003d8e40 <+0>:     bsr    %rdi,%rcx
   0x00000000003d8e44 <+4>:     xor    $0xffffffc0,%ecx
   0x00000000003d8e47 <+7>:     add    $0x41,%ecx
   0x00000000003d8e4a <+10>:    xor    %eax,%eax
   0x00000000003d8e4c <+12>:    test   %rdi,%rdi
   0x00000000003d8e4f <+15>:    cmovne %ecx,%eax
   0x00000000003d8e52 <+18>:    ret
   */
}

constexpr inline int MyAbslInternalCountLeadingZeroes64(uint64_t x) {
  // Use __builtin_clzll, which uses the following instructions:
  //  x86: bsr, lzcnt
  //  ARM64: clz
  //  PPC: cntlzd
  static_assert(sizeof(unsigned long long) == sizeof(x),  // NOLINT(runtime/int)
                "__builtin_clzll does not take 64-bit arg");

  // Handle 0 as a special case because __builtin_clzll(0) is undefined.
  // MOGO: This if statement is messing up the optimizations.
  if (x == 0) return 64;

  return __builtin_clzll(x);
}

inline int MyAbslFls64(uint64_t x) {
  return 64 - MyAbslInternalCountLeadingZeroes64(x);
}

int main4(uint64_t val) {
  return MyAbslFls64(val);
  /*
   0x00000000003d8ee0 <+0>:     test   %rdi,%rdi
   0x00000000003d8ee3 <+3>:     je     0x3d8ef5 <_Z5main4m+21>
   0x00000000003d8ee5 <+5>:     bsr    %rdi,%rcx
   0x00000000003d8ee9 <+9>:     xor    $0x3f,%rcx
   0x00000000003d8eed <+13>:    mov    $0x40,%eax
   0x00000000003d8ef2 <+18>:    sub    %ecx,%eax
   0x00000000003d8ef4 <+20>:    ret
   0x00000000003d8ef5 <+21>:    mov    $0x40,%ecx
   0x00000000003d8efa <+26>:    mov    $0x40,%eax
   0x00000000003d8eff <+31>:    sub    %ecx,%eax
   0x00000000003d8f01 <+33>:    ret
   */
}

int main_absl_countl_zero(uint64_t val) {
  return absl::countl_zero(val);
  /*
   0x00000000003d8f20 <+0>:     test   %rdi,%rdi
   0x00000000003d8f23 <+3>:     je     0x3d8f2e <_Z21main_absl_countl_zerom+14>
   0x00000000003d8f25 <+5>:     bsr    %rdi,%rax
   0x00000000003d8f29 <+9>:     xor    $0x3f,%rax
   0x00000000003d8f2d <+13>:    ret
   0x00000000003d8f2e <+14>:    mov    $0x40,%eax
   0x00000000003d8f33 <+19>:    ret
   */
}
int main_get_bucket(uint64_t val) {
  mogo::Histogram<uint64_t, uint64_t> h(/*min=*/0xABC, /*shift=*/4);
  return h.GetBucket(val);
  /*
bazel build -c opt perf:bits_bin && \
gdb bazel-bin/perf/bits_bin -iex "set auto-load off" \
  -iex "set pagination off" -ex="disass main_get_bucket" -ex="exit"

BM_Histogram32GetBucket0            0.741
BM_Histogram32GetBucket1            0.740
BM_Histogram32GetBucket012          2.85

   0x00000000003d8f80 <+0>:     add    $0xfffffffffffff544,%rdi
   0x00000000003d8f87 <+7>:     sar    $0x4,%rdi
   0x00000000003d8f8b <+11>:    je     0x3d8f96 <_Z15main_get_bucketm+22>
   0x00000000003d8f8d <+13>:    bsr    %rdi,%rax
   0x00000000003d8f91 <+17>:    xor    $0x3f,%rax
   0x00000000003d8f95 <+21>:    ret
   0x00000000003d8f96 <+22>:    mov    $0x40,%eax
   0x00000000003d8f9b <+27>:    ret

   // mogo FLS, slower, but without a branch.
BM_Histogram32GetBucket0            0.965
BM_Histogram32GetBucket1            0.965
BM_Histogram32GetBucket012          3.09

   0x00000000003d8f80 <+0>:     add    $0xfffffffffffff544,%rdi
   0x00000000003d8f87 <+7>:     mov    %rdi,%rax
   0x00000000003d8f8a <+10>:    sar    $0x4,%rax
   0x00000000003d8f8e <+14>:    bsr    %rax,%rcx
   0x00000000003d8f92 <+18>:    xor    $0xffffffc0,%ecx
   0x00000000003d8f95 <+21>:    add    $0x41,%ecx
   0x00000000003d8f98 <+24>:    xor    %eax,%eax
   0x00000000003d8f9a <+26>:    cmp    $0x10,%rdi
   0x00000000003d8f9e <+30>:    cmovae %ecx,%eax
   0x00000000003d8fa1 <+33>:    ret

   // absl::countl_zero
BM_Histogram32GetBucket0            0.740          0.740  940097220
BM_Histogram32GetBucket1            0.741          0.742  946819784
BM_Histogram32GetBucket012          2.85           2.86   245284615
   0x00000000003d8f80 <+0>:     add    $0xfffffffffffff544,%rdi
   0x00000000003d8f87 <+7>:     sar    $0x4,%rdi
   0x00000000003d8f8b <+11>:    je     0x3d8f96 <_Z15main_get_bucketm+22>
   0x00000000003d8f8d <+13>:    bsr    %rdi,%rax
   0x00000000003d8f91 <+17>:    xor    $0x3f,%rax
   0x00000000003d8f95 <+21>:    ret
   0x00000000003d8f96 <+22>:    mov    $0x40,%eax
   0x00000000003d8f9b <+27>:    ret
   */
}

namespace testing {
template <class T>
void DoNotOptimize(const T& var) {
  asm volatile("" : "+m"(const_cast<T&>(var)));
}
}  // namespace testing

int main(int argc, char** argv) {
  testing::DoNotOptimize(&main2);
  testing::DoNotOptimize(&main4);
  testing::DoNotOptimize(&main_absl_countl_zero);
  testing::DoNotOptimize(&main_get_bucket);
}
