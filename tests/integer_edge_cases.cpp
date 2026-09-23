#include "check.hpp"

#include <iostream>
#include <limits>

import maths;

using namespace maths;

int main() {
  std::cout << "=== Integer 边界测试 ===" << '\n';

  // 1. 除零与取模零
  {
    CHECK_ERR(Integer(10LL) / Integer(0LL), MathsError::DivisionByZero);
    CHECK_ERR(Integer(10LL) % Integer(0LL), MathsError::DivisionByZero);
    CHECK_ERR(Integer(0LL) / Integer(0LL), MathsError::DivisionByZero);
  }

  // 2. 取模：余数符号随被除数
  {
    CHECK_EQ((Integer(10LL) % Integer(3LL)).unwrap(), Integer(1LL));
    CHECK_EQ((Integer(-10LL) % Integer(3LL)).unwrap(), Integer(-1LL));
    CHECK_EQ((Integer(10LL) % Integer(-3LL)).unwrap(), Integer(1LL));
    CHECK_EQ((Integer(-10LL) % Integer(-3LL)).unwrap(), Integer(-1LL));
    CHECK_EQ((Integer(9LL) % Integer(3LL)).unwrap(), Integer(0LL));
  }

  // 3. 极值：取绝对值不应触发有符号溢出（UB）
  {
    const Integer minVal(std::numeric_limits<long long>::min());
    CHECK_TRUE(minVal.isNegative());
    CHECK_EQ(minVal.getAbs(), 9223372036854775808ULL);

    const Integer maxVal(std::numeric_limits<long long>::max());
    CHECK_TRUE(!maxVal.isNegative());
    CHECK_EQ(maxVal.getAbs(), 9223372036854775807ULL);
  }

  // 4. 零的符号规范化与取反
  {
    const Integer zero(0LL);
    CHECK_TRUE(!zero.isNegative());
    CHECK_EQ(zero.getSign(), 1LL);

    const Integer negZero = -zero;
    CHECK_EQ(negZero, zero);
    CHECK_EQ(negZero.getVal(), 0LL);
    CHECK_TRUE(!negZero.isNegative()); // 零取反后不应变成负数
  }

  TEST_SUMMARY();
}
