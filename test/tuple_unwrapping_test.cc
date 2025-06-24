#include <stddef.h>

#include <tuple>
#include <utility>

#include "absl/log/log.h"
#include "gtest/gtest.h"

/*
blaze test --test_output=streamed experimental/mogo/test:tuple_unwrapping_test
 */

template<typename T>
const T& Unwrap(const T& v) {
  LOG(INFO) << "Unwrapping: " << v;
  return v;
}

template<typename... Args>
void Print(const Args& ...);

template<typename T, typename... Tail>
void Print(const T& v, const Tail&... args) {
  LOG(INFO) << v;
  Print(args...);
}

template<>
void Print() {}

template<typename T>
struct Wrapper {
  Wrapper(const T& v) : v(v) {
    LOG(INFO) << "Wrapping: " << v;
  }
  const T& v;
};

template <typename Tuple, size_t... Indices>
void MyPrintImpl(const Tuple& t, std::index_sequence<Indices...>) {
  Print(std::get<Indices>(t).v...);
}

template<typename... Args>
void MyPrint(const Args& ...args) {
  auto wrappers = std::make_tuple(Wrapper<Args>(args)...);
  auto index = std::make_index_sequence<sizeof...(Args)>();
  LOG(INFO) << &wrappers;
  MyPrintImpl(wrappers, index);
}


TEST(StringTest, Test1) {
  Print('a', "b", 1, 2);
  MyPrint(3, 4);

  auto tuple = std::make_tuple(5, 6);
  LOG(INFO) << std::get<0>(tuple);
}
