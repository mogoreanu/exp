#ifndef PERF_BITS_H_
#define PERF_BITS_H_

#include <stdint.h>

#include <limits>

#include "absl/numeric/bits.h"

namespace mogo {

// flsl - find last set bit in a quad word
//
// This is defined in a similar way as the libc and compiler builtin
// ffs, but returns the position of the most significant set bit.
//
// flsl(value) returns 0 if value is 0 or the position of the last
// set bit if value is nonzero. The last (most significant) bit is
// at position 64.

// More info:
// https://www.chessprogramming.org/BitScan
//
// https://stackoverflow.com/questions/9353973/implementation-of-builtin-clz
inline int Fls64(uint64_t x) {
  if (x == 0) {
    return 0;
  }
  if constexpr (sizeof(int64_t) == sizeof(long)) {  // NOLINT
    return 64 - __builtin_clzl(x);
  } else {
    return 64 - __builtin_clzll(x);
  }
}

inline int Fls64Bsr(int64_t x) {
  // According to AMD64 spec the value that bsrq returns if x==0 is undefined,
  // but in practice both Intel and AMD CPUs leave the value unchanged.
  int r = -1;
  asm("bsrq %1,%q0" : "+r"(r) : "rm"(x));
  return r + 1;
}

template <typename TSample>
struct ToSigned;

template <>
struct ToSigned<uint64_t> {
  using Type = int64_t;
};

template <>
struct ToSigned<int64_t> {
  using Type = int64_t;
};

template <>
struct ToSigned<uint32_t> {
  using Type = int32_t;
};

template <>
struct ToSigned<int32_t> {
  using Type = int32_t;
};

template <typename TSample>
struct ToUnsigned;

template <>
struct ToUnsigned<uint64_t> {
  using Type = uint64_t;
};

template <>
struct ToUnsigned<int64_t> {
  using Type = uint64_t;
};

template <>
struct ToUnsigned<uint32_t> {
  using Type = uint32_t;
};

template <>
struct ToUnsigned<int32_t> {
  using Type = uint32_t;
};

#define HISTOGRAM_FLS

template <typename TSample = uint64_t, typename TBucket = uint64_t>
class Histogram {
 public:
  Histogram(TSample min, int shift) : min_(min), shift_(shift) { Reset(); }

  void Add(TSample v) { ++values_[GetBucket(v)]; }
  void Reset() {
    for (int i = 0; i < bucket_count(); ++i) {
      values_[i] = 0;
    }
  }

  static constexpr int bucket_count() {
    // uint32_t samples need 33 buckets.
    // uint64_t samples need 65 buckets.
    return sizeof(TSample) * 8 + 1;
  }

  // Returns the number of samples at the specified position.
  // Unlike bucket indices positions go in order. For a sample `v`:
  //                         v < min                   => pos = 0
  // v >= min             && v < min + 2 ^ shift       => pos = 1
  // v >= min + 2 ^ shift && v < min + 2 ^ (shift + 1) => pos = 2
  // ...
  TBucket value_at_pos(int pos) const {
#ifndef HISTOGRAM_FLS
    // Whe absl::countl_zero  is used in GetBucket we have the following
    // position to bucket mapping:
    //                         v < min                   => pos = 0, b = 0
    // v >= min             && v < min + 2 ^ shift       => pos = 1, b = 64
    // v >= min + 2 ^ shift && v < min + 2 ^ (shift + 1) => pos = 2, b = 63
    // ...
    if (pos == 0) {
      return values_[0];
    } else {
      return values_[bucket_count() - pos];
    }
#else
    // Whe FLS is used in GetBucket we have the following position to bucket
    // mapping:
    //                         v < min                   => pos = 0, b = 64
    // v >= min             && v < min + 2 ^ shift       => pos = 1, b = 0
    // v >= min + 2 ^ shift && v < min + 2 ^ (shift + 1) => pos = 2, b = 1
    // ...
    if (pos == 0) {
      return values_[bucket_count() - 1];
    } else {
      return values_[pos - 1];
    }

#endif
  }

  // Returns the smallest value that will go into the specified position.
  TSample range_min_pos(int pos) const {
    if (pos == 0) {
      return std::numeric_limits<TSample>::min();
    } else {
      return range_max_pos(pos - 1);
    }
  }

  // Returns the non-inclusive upper boundary for the bucket at the specified
  // position.
  TSample range_max_pos(int pos) const {
    if (pos == 0) {
      return min_;
    } else {
      return min_ + (TSample(1) << (shift_ + pos - 1));
    }
  }

  int max_pos() const { return bucket_count() - 1; }

  TBucket total() {
    TBucket result = 0;
    for (int i = 0; i < bucket_count(); ++i) {
      result += values_[i];
    }
    return result;
  }

  // Returns the bucket index for the specified sample. Depending on the
  // mechanism used, the mapping differs, see value_at_pos instead.
  int GetBucket(TSample v) const {
    typename ToSigned<TSample>::Type v_signed = v;
    // v_signed can go negative.
    v_signed -= min_;
    // Signed shift will preserve uppermost bit. So if `v` is smaller than
    // `min_` then clz will return 0 as the uppermost bit is set.
    v_signed >>= shift_;

#ifndef HISTOGRAM_FLS
    //                               v < min                   => returns 0
    // v >= min                   && v < min + 2 ^ shift       => returns 64
    // v >= min + 2 ^ shift       && v < min + 2 ^ (shift + 1) => returns 63
    // v >= min + 2 ^ (shift + 1) && v < min + 2 ^ (shift + 2) => returns 62
    // ....
    // If v_signed is zero `v` >= `min_`, but `v` - `min_ < 2 ^ shift
    return absl::countl_zero(typename ToUnsigned<TSample>::Type(v_signed));
#else
    // When absl::countl_zero is replaced with mogo::Fls64, the code doesn't
    // have branches results are worse, see bits_bin main_get_bucket.
    //                         v < min                   => returns 64
    // v >= min             && v < min + 2 ^ shift       => returns 0
    // v >= min + 2 ^ shift && v < min + 2 ^ (shift + 1) => returns 1
    // ...
    return mogo::Fls64(typename ToUnsigned<TSample>::Type(v_signed));
#endif
  }

 private:
  TSample min_;
  int shift_;

  TBucket values_[bucket_count()];
};

using Histogram32 = Histogram</*TSample=*/uint32_t, /*TBucket=*/uint64_t>;
using Histogram64 = Histogram</*TSample=*/uint64_t, /*TBucket=*/uint64_t>;

}  // namespace mogo

#endif  // PERF_BITS_H_
