#include "numbers.hpp"
#include <cassert>
#include <iostream>

int main() {
  // 除以零应抛出异常
  try {
    Integer a(10LL);
    Integer z(0LL);
    volatile auto r = a / z; // should throw
    (void)r;
    assert(false && "Division by zero did not throw");
  } catch (const std::domain_error &) {
    // expected
  }

  // 取模：符号和被除数一致
  Integer m1(10LL);
  Integer m2(3LL);
  Integer r1 = m1 % m2; // 10 % 3 == 1
  assert(static_cast<long long>(r1) == 1LL);

  Integer neg(-10LL);
  Integer r2 = neg % m2; // -10 % 3 == -1 (implementation keeps sign of dividend)
  assert(static_cast<long long>(r2) == -1LL);

  std::cout << "edge_cases.cpp: PASS\n";
  return 0;
}
