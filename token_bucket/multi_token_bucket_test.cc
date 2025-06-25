/*
bazel test token_bucket:multi_token_bucket_test --test_output=streamed
*/

#include "absl/container/inlined_vector.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "gtest/gtest.h"

namespace mogo {
namespace {

struct TestStruct {
  TestStruct(double x_arg, absl::Time y_arg) : x(x_arg), y(y_arg) {}
  double x;
  absl::Time y;
};

TEST(MultiTokenBucketTest, Test50s) {
  TestStruct s(0.5, absl::Now());
  LOG(INFO) << s.x;

  absl::InlinedVector<TestStruct, 4> v2 = {s, s};
  LOG(INFO) << v2.size();

  absl::InlinedVector<TestStruct, 4> v1 = { {0, absl::InfiniteFuture()} };
  LOG(INFO) << v1.size();

  absl::InlinedVector<TestStruct, 4> v3 = { TestStruct(0, absl::InfiniteFuture()) };
  LOG(INFO) << v3.size();
}

}  // namespace
}  // namespace mogo
