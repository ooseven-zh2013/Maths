#include "check.hpp"
#include "numbers.hpp"
#include <iostream>

int main() {
  std::cout << "=== Integer 比较测试 ===" << '\n';

  const Integer a(5LL);
  const Integer b(-3LL);
  const Integer c(5ULL);

  // 与 Integer / 原生整型比较
  CHECK_TRUE(a > b);
  CHECK_TRUE(b < a);
  CHECK_TRUE(a >= 5LL);
  CHECK_TRUE(a == c);
  CHECK_TRUE(!(a != 5LL));
  CHECK_TRUE(a == 5LL);
  CHECK_TRUE(!(a < 5LL));
  CHECK_TRUE(b <= -3LL);
  CHECK_TRUE(b == -3LL);
  CHECK_TRUE(b < 0LL);

  // 同负号区间的排序关系（回归：同负时必须反转绝对值比较方向）
  const Integer n5(-5LL);
  const Integer n3(-3LL);
  const Integer n1(-1LL);
  const Integer n100(-100LL);
  CHECK_TRUE(n5 < n3);
  CHECK_TRUE(n3 > n5);
  CHECK_TRUE(n100 < n5);
  CHECK_TRUE(n1 > n100);
  CHECK_TRUE(n1 > n3);
  CHECK_TRUE(n3 <= n3);
  CHECK_TRUE(n3 >= n3);
  CHECK_TRUE(n5 != n3);
  CHECK_TRUE(!(n5 > n3));
  CHECK_TRUE(!(n3 < n5));

  // 与无符号整型比较：负数恒小于任何无符号数
  CHECK_TRUE(n5 < 0ULL);
  CHECK_TRUE(n5 < 1ULL);
  CHECK_TRUE(!(n5 > 0ULL));

  TEST_SUMMARY();
}
