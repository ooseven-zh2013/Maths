#include "numbers.hpp"
#include <cassert>
#include <iostream>

int main() {
  Fraction a(1LL, 2LL);
  Fraction b(2LL, 4LL);
  Fraction c(-1LL, 2LL);

  assert(a == b);
  assert(!(a == c));
  assert(a < Fraction(3LL, 4LL));
  assert(Fraction(0LL, 1LL) < a);

  // 除以0检查
  try {
    Fraction x(1LL, 2LL);
    Fraction z(0LL, 1LL);
    volatile auto r = x / z;
    (void)r;
    assert(false && "Fraction division by zero did not throw");
  } catch (const std::domain_error &) {
    // expected
  }

  std::cout << "compare_more fraction: PASS\n";
  return 0;
}
