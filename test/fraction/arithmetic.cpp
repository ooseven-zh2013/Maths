#include "check.hpp"
#include "numbers.hpp"
#include <iostream>
#include <sstream>
#include <string>

int main() {
  std::cout << "=== Fraction 功能测试 ===" << '\n';

  // 1. 构造与约分
  {
    const Fraction a(3, 4); // 原生 int 字面量也应可用（构造无歧义）
    CHECK_EQ(a.getNumerator(), 3LL);
    CHECK_EQ(a.getDenominator(), 4LL);
    CHECK_TRUE(!a.isNegative());

    const Fraction reduced(2, 4); // 应约分为 1/2
    CHECK_EQ(reduced.getNumerator(), 1LL);
    CHECK_EQ(reduced.getDenominator(), 2LL);

    const Fraction doubleNegative(-2, -4); // 双负号相消为正
    CHECK_EQ(doubleNegative.getNumerator(), 1LL);
    CHECK_EQ(doubleNegative.getDenominator(), 2LL);
    CHECK_TRUE(!doubleNegative.isNegative());

    const Fraction integral(6, 3); // 应约分为 2/1
    CHECK_EQ(integral.getNumerator(), 2LL);
    CHECK_EQ(integral.getDenominator(), 1LL);

    const Fraction zero(0, 5); // 零应规范化为 0/1 且非负
    CHECK_EQ(zero.getNumerator(), 0LL);
    CHECK_EQ(zero.getDenominator(), 1LL);
    CHECK_TRUE(!zero.isNegative());

    const Fraction unsignedPair(3ULL, 4ULL);
    CHECK_EQ(unsignedPair.getNumerator(), 3LL);
    CHECK_TRUE(!unsignedPair.isNegative());
  }

  // 2. 四则运算
  {
    const Fraction a(3, 4);
    const Fraction b(-2, 5);

    CHECK_EQ(a + b, Fraction(7, 20));
    CHECK_EQ(a - b, Fraction(23, 20));
    CHECK_EQ(a * b, Fraction(-3, 10));
    CHECK_EQ((a / b).unwrap(), Fraction(-15, 8));
  }

  // 3. 复合赋值
  {
    Fraction d(1, 2);
    d += Fraction(1, 3);
    CHECK_EQ(d, Fraction(5, 6));
    d -= Fraction(1, 6);
    CHECK_EQ(d, Fraction(2, 3));
    d *= Fraction(3, 4);
    CHECK_EQ(d, Fraction(1, 2));
    d /= Fraction(1, 4);
    CHECK_EQ(d, Fraction(2, 1));
  }

  // 4. 比较运算
  {
    const Fraction half(1, 2);
    const Fraction twoThirds(2, 3);
    CHECK_TRUE(half < twoThirds);
    CHECK_TRUE(twoThirds > half);
    CHECK_TRUE(half == Fraction(2, 4));
    CHECK_TRUE(half <= Fraction(1, 2));
    CHECK_TRUE(half >= Fraction(1, 2));
    CHECK_TRUE(half != Fraction(1, 3));
    CHECK_TRUE(!(half == 0LL));
    CHECK_TRUE(!(half == 1LL));

    // 负数比较
    const Fraction negHalf(-1, 2);
    const Fraction negTwoThirds(-2, 3);
    CHECK_TRUE(negHalf > negTwoThirds); // -1/2 > -2/3
    CHECK_TRUE(negHalf < half);
    CHECK_TRUE(negHalf < Fraction(0, 1));
    CHECK_TRUE(negHalf == Fraction(-2, 4));
  }

  // 5. 取反与符号
  {
    const Fraction g(-3, 4);
    CHECK_EQ(g + Fraction(1, 2), Fraction(-1, 4));
    CHECK_EQ(-g, Fraction(3, 4));
    CHECK_EQ(g * Fraction(1, 2), Fraction(-3, 8));
    CHECK_TRUE(g.isNegative());
  }

  // 6. 幂运算（正指数）
  {
    CHECK_EQ(Fraction(2, 3).pow(Integer(3LL)).unwrap(), Fraction(8, 27));
    CHECK_EQ(Fraction(-1, 2).pow(Integer(4LL)).unwrap(), Fraction(1, 16));
    CHECK_EQ(Fraction(-2, 3).pow(Integer(3LL)).unwrap(), Fraction(-8, 27));
    CHECK_EQ((Fraction(2, 3) ^ Integer(3LL)).unwrap(), Fraction(8, 27));
  }

  // 7. 幂运算（负指数）
  {
    CHECK_EQ(Fraction(2, 3).pow(Integer(-2LL)).unwrap(), Fraction(9, 4));
    CHECK_EQ(Fraction(-1, 2).pow(Integer(-3LL)).unwrap(), Fraction(-8, 1));
  }

  // 8. 幂运算（零指数与零底数）
  {
    CHECK_EQ(Fraction(123, 456).pow(Integer(0LL)).unwrap(), Fraction(1, 1));
    CHECK_EQ(Fraction(0, 1).pow(Integer(5LL)).unwrap(), Fraction(0, 1));
    CHECK_ERR(Fraction(0, 1).pow(Integer(-2LL)), MathsError::ZeroToNegativePower);
  }

  // 9. 除零：Result 路径返回错误码
  {
    CHECK_ERR(Fraction(1, 2) / Fraction(0, 1), MathsError::DivisionByZero);
    CHECK_OK(Fraction(1, 2) / Fraction(1, 2));

    // 构造路径无法返回 Result，仍抛 MathsException
    CHECK_THROWS(Fraction(1LL, 0LL), MathsException);
  }

  // 10. 转换为 double
  {
    const double third = static_cast<double>(Fraction(1, 3));
    CHECK_TRUE(third > 0.333 && third < 0.334);
    CHECK_EQ(static_cast<double>(Fraction(-3, 4)), -0.75);
    CHECK_EQ(static_cast<double>(Fraction(1, 2)), 0.5);
  }

  // 11. 流输入输出
  {
    std::stringstream ss;
    ss << "3/4";
    Fraction fromSlash;
    ss >> fromSlash;
    CHECK_EQ(fromSlash, Fraction(3, 4));

    std::stringstream ss2;
    ss2 << "5";
    Fraction fromInt;
    ss2 >> fromInt;
    CHECK_EQ(fromInt, Fraction(5, 1));

    std::ostringstream os;
    os << Fraction(-3, 4);
    CHECK_EQ(os.str(), std::string("-3/4"));

    std::ostringstream os2;
    os2 << Fraction(6, 3);
    CHECK_EQ(os2.str(), std::string("2")); // 分母为 1 时按整数输出
  }

  // 12. LaTeX 风格字符串构造
  {
    CHECK_EQ(Fraction(std::string_view("\\frac{1}{2}")), Fraction(1, 2));
    CHECK_EQ(Fraction(std::string_view("\\frac{3}{6}")), Fraction(1, 2));
    CHECK_EQ(Fraction(std::string_view("\\frac{-3}{6}")), Fraction(-1, 2));
    CHECK_EQ(Fraction(std::string_view("\\frac{\\frac{1}{2}}{3}")), Fraction(1, 6));
    CHECK_EQ(Fraction(std::string_view("\\frac{1}{2}/3")), Fraction(1, 6));
    CHECK_EQ(Fraction(std::string_view("6/3/2")), Fraction(1, 1)); // 左结合：(6/3)/2

    // 构造函数无法返回 Result，解析失败时抛 MathsException
    CHECK_THROWS(Fraction(std::string_view("\\frac{1}{")), MathsException);

    // parse 是 Result 路径：解析外部输入时用它才能显式处理失败
    CHECK_EQ(Fraction::parse("3/4").unwrap(), Fraction(3, 4));
    CHECK_EQ(Fraction::parse("\\frac{1}{2}").unwrap(), Fraction(1, 2));
    CHECK_ERR(Fraction::parse("\\frac{1}{"), MathsError::InvalidExpression);
    CHECK_ERR(Fraction::parse("abc"), MathsError::InvalidExpression);
    CHECK_ERR(Fraction::parse(""), MathsError::InvalidExpression);
    CHECK_ERR(Fraction::parse("1/0"), MathsError::DivisionByZero);
  }

  TEST_SUMMARY();
}
