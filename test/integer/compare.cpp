#include "numbers.hpp"
#include <cassert>
#include <iostream>

int main() {
  Integer a(5LL);
  Integer b(-3LL);
  Integer c(5ULL);

  // 基本比较
  assert(a > b);
  assert(b < a);
  assert(a >= 5LL);
  assert(a == c);
  assert(!(a != 5LL));

  // 与原生整型比较
  assert(a == 5LL);
  assert(!(a < 5LL));
  assert((b <= -3LL));

  std::cout << "compare.cpp: PASS\n";
  return 0;
}
