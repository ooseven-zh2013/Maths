#include "check.hpp"

#include <iostream>
#include <sstream>
#include <string>

#include <maths/numeric/numbers.hpp>

int main() {
  std::cout << "=== Fraction 流 I/O 测试 ===" << '\n';

  // 1. 读入分数形式
  {
    std::stringstream ss;
    ss << "3/4";
    Fraction f;
    ss >> f;
    CHECK_EQ(f, Fraction(3, 4));
  }

  // 2. 读入整数形式
  {
    std::stringstream ss;
    ss << "5";
    Fraction f;
    ss >> f;
    CHECK_EQ(f, Fraction(5, 1));
  }

  // 3. 读入负数
  {
    std::stringstream ss;
    ss << "-7/2";
    Fraction f;
    ss >> f;
    CHECK_EQ(f, Fraction(-7, 2));
  }

  // 4. 输入失败时不应改动原对象
  {
    std::stringstream ss;
    ss << "abc";
    Fraction f(1, 2);
    ss >> f;
    CHECK_EQ(f, Fraction(1, 2));
  }

  // 5. 输出格式
  {
    std::ostringstream os;
    os << Fraction(-3, 4);
    CHECK_EQ(os.str(), std::string("-3/4"));

    std::ostringstream os2;
    os2 << Fraction(6, 3);
    CHECK_EQ(os2.str(), std::string("2")); // 分母为 1 时按整数输出

    std::ostringstream os3;
    os3 << Fraction(0, 1);
    CHECK_EQ(os3.str(), std::string("0"));

    std::ostringstream os4;
    os4 << -Fraction(0, 1);
    CHECK_EQ(os4.str(), std::string("0")); // 不应输出 "-0"
  }

  // 6. 往返一致：输出后重新读入应相等
  {
    const Fraction original(-7, 12);
    std::stringstream ss;
    ss << original;
    Fraction roundTrip;
    ss >> roundTrip;
    CHECK_EQ(roundTrip, original);
  }

  TEST_SUMMARY();
}
