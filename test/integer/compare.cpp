#include "numbers.hpp"
#include <iostream>
#define endl '\n'
signed main() {
  Integer a(123ll);
  std::cout << (a != 123ll) << endl;
  std::cout << (a >= 123ll) << endl;
  std::cout << (a < 123ll) << endl;
}