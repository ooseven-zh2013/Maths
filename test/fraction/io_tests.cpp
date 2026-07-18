#include "numbers.hpp"
#include <cassert>
#include <iostream>
#include <sstream>

int main() {
  std::stringstream ss;
  ss << "3/4";
  Fraction f;
  ss >> f;
  assert(f == Fraction(3LL, 4LL));

  ss.clear();
  ss.str("5");
  ss >> f;
  assert(f == Fraction(5LL, 1LL));

  std::cout << "fraction io_tests: PASS\n";
  return 0;
}
