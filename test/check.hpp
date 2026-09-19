#pragma once
#ifndef MATHS_TEST_CHECK_HPP
#define MATHS_TEST_CHECK_HPP

// 轻量断言工具：不依赖第三方框架，断言不随 NDEBUG 失效（assert 在 Release 下会被移除）。
// 每个测试文件以 TEST_SUMMARY() 结尾，失败时以非零码退出，ctest 才能真正感知失败。

#include <iostream>
#include <sstream>
#include <string>

namespace maths_test {

inline int g_checks = 0;
inline int g_failures = 0;

template <typename T> std::string show(const T &value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

inline std::string show(bool value) { return value ? "true" : "false"; }

inline void report(bool ok, const char *expr, const char *file, int line, const std::string &detail) {
  ++g_checks;
  if (ok) {
    return;
  }
  ++g_failures;
  std::cout << "FAIL " << file << ":" << line << "  " << expr;
  if (!detail.empty()) {
    std::cout << "  [" << detail << "]";
  }
  std::cout << '\n';
}

} // namespace maths_test

// 断言 actual == expected
#define CHECK_EQ(actual, expected)                                                                                     \
  do {                                                                                                                 \
    const auto _actual = (actual);                                                                                     \
    const auto _expected = (expected);                                                                                 \
    maths_test::report(_actual == _expected, #actual " == " #expected, __FILE__, __LINE__,                             \
                       "actual=" + maths_test::show(_actual) + ", expected=" + maths_test::show(_expected));           \
  } while (false)

// 断言条件成立
#define CHECK_TRUE(condition)                                                                                          \
  do {                                                                                                                 \
    maths_test::report(static_cast<bool>(condition), #condition, __FILE__, __LINE__, "");                              \
  } while (false)

// 断言表达式抛出指定异常
#define CHECK_THROWS(expr, exception_type)                                                                             \
  do {                                                                                                                 \
    bool _thrown = false;                                                                                              \
    try {                                                                                                              \
      (void)(expr);                                                                                                    \
    } catch (const exception_type &) {                                                                                 \
      _thrown = true;                                                                                                  \
    } catch (...) {                                                                                                    \
      _thrown = false;                                                                                                 \
    }                                                                                                                  \
    maths_test::report(_thrown, #expr " throws " #exception_type, __FILE__, __LINE__, "");                             \
  } while (false)

// 输出汇总并以失败数作为退出码
#define TEST_SUMMARY()                                                                                                 \
  do {                                                                                                                 \
    std::cout << (maths_test::g_failures == 0 ? "PASS" : "FAIL") << ": "                                               \
              << (maths_test::g_checks - maths_test::g_failures) << "/" << maths_test::g_checks << " checks passed\n"; \
    return maths_test::g_failures == 0 ? 0 : 1;                                                                        \
  } while (false)

#endif // MATHS_TEST_CHECK_HPP
