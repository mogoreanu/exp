#ifndef MOGO_EXP_STAT_STAT_UTILS_H_
#define MOGO_EXP_STAT_STAT_UTILS_H_

#include <cstdint>

namespace mogo {

template <bool IsPowerOfTwo>
struct FastModulo {
  static int64_t Get(int64_t x, int64_t m);
};

template <>
inline int64_t FastModulo<true>::Get(int64_t x, int64_t m) {
  return x & (m - 1);
}

template <>
inline int64_t FastModulo<false>::Get(int64_t x, int64_t m) {
  return (x % m + m) % m;
}

// Requires `x` to be positive. Returns whether `x` is a power of 2.
constexpr bool IsPowerOfTwo(int64_t x) { return !(x & (x - 1)); }

// A helper function that returns modulo in [0, M - 1] range.
// For zero and positive numbers the result is equal to the C++ modulo operator.
// For negative numbers this function returns a modulo that is equivalent to
// adding (k * M) until the number becomes positive and then getting the modulo,
// in particular PositiveModulo<M>(-1) returns (M - 1).
template <int64_t M>
int64_t PositiveModulo(int64_t x) {
  return FastModulo<IsPowerOfTwo(M)>::Get(x, M);
}

}  // namespace mogo

#endif  // MOGO_EXP_STAT_STAT_UTILS_H_
