#include "check.hpp"
#include "numbers.hpp"
#include <iostream>

int main() {
  std::cout << "=== Fraction 比较测试 ===" << '\n';

  const Fraction a(1, 2);
  const Fraction b(2, 4);
  const Fraction c(-1, 2);

  CHECK_TRUE(a == b);
  CHECK_TRUE(!(a == c));
  CHECK_TRUE(a != c);
  CHECK_TRUE(a < Fraction(3, 4));
  CHECK_TRUE(Fraction(0, 1) < a);
  CHECK_TRUE(c < a);
  CHECK_TRUE(c < Fraction(0, 1));
  CHECK_TRUE(a <= b);
  CHECK_TRUE(c >= Fraction(-1, 2));

  // 负数区间：-1/2 > -2/3，-3/4 < -1/4
  CHECK_TRUE(Fraction(-1, 2) > Fraction(-2, 3));
  CHECK_TRUE(Fraction(-3, 4) < Fraction(-1, 4));
  CHECK_TRUE(Fraction(-1, 3) == Fraction(2, -6));

  // 零取反不应产生负零
  const Fraction zero(0, 1);
  const Fraction negZero = -zero;
  CHECK_EQ(negZero, zero);
  CHECK_TRUE(!negZero.isNegative());

  // 交叉相乘中间结果溢出 ull 时，比较仍应正确（仅在支持 128 位整型时生效）
#if defined(__SIZEOF_INT128__)
  CHECK_TRUE(Fraction(9223372036854775807LL, 1) > Fraction(1, 9223372036854775807LL));
#endif

  // 除以 0：Result 路径返回错误码，不再抛异常
  CHECK_ERR(Fraction(1, 2) / Fraction(0, 1), MathsError::DivisionByZero);
  CHECK_OK(Fraction(1, 2) / Fraction(1, 4));

  TEST_SUMMARY();
}
