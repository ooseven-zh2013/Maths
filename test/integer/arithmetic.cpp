#include "check.hpp"

#include <iostream>
#include <sstream>
#include <string>

#include <maths/numeric/numbers.hpp>

int main() {
  std::cout << "=== Integer 功能测试 ===" << '\n';

  // 1. 构造与取值
  {
    const Integer a(5LL);
    const Integer b(-3LL);
    const Integer c(0ULL);

    CHECK_EQ(a.getVal(), 5LL);
    CHECK_EQ(a.getAbs(), 5ULL);
    CHECK_TRUE(!a.isNegative());
    CHECK_EQ(a.getSign(), 1LL);

    CHECK_EQ(b.getVal(), -3LL);
    CHECK_EQ(b.getAbs(), 3ULL);
    CHECK_TRUE(b.isNegative());
    CHECK_EQ(b.getSign(), -1LL);

    CHECK_EQ(c.getVal(), 0LL);
    CHECK_TRUE(!c.isNegative());
  }

  // 2. 四则运算
  {
    const Integer a(5LL);
    const Integer b(-3LL);

    CHECK_EQ(a + b, Integer(2LL));
    CHECK_EQ(a - b, Integer(8LL));
    CHECK_EQ(a * b, Integer(-15LL));
    CHECK_EQ((a / b).unwrap(), Integer(-1LL)); // 向零截断
    CHECK_EQ((a % b).unwrap(), Integer(2LL));  // 余数符号随被除数

    CHECK_EQ((Integer(-10LL) % Integer(3LL)).unwrap(), Integer(-1LL));
    CHECK_EQ((Integer(-5LL) / Integer(-3LL)).unwrap(), Integer(1LL));
    CHECK_EQ((Integer(-7LL) / Integer(2LL)).unwrap(), Integer(-3LL));
    CHECK_EQ(Integer(0LL) - Integer(5LL), Integer(-5LL));
  }

  // 3. 复合赋值
  {
    Integer d(10LL);
    d += Integer(5LL);
    CHECK_EQ(d, Integer(15LL));
    d -= Integer(3LL);
    CHECK_EQ(d, Integer(12LL));
    d *= Integer(2LL);
    CHECK_EQ(d, Integer(24LL));
    d /= Integer(4LL);
    CHECK_EQ(d, Integer(6LL));
    d %= Integer(4LL);
    CHECK_EQ(d, Integer(2LL));
  }

  // 4. 自增自减
  {
    Integer e(7LL);
    CHECK_EQ(++e, Integer(8LL));
    CHECK_EQ(e++, Integer(8LL));
    CHECK_EQ(e, Integer(9LL));
    CHECK_EQ(--e, Integer(8LL));
    CHECK_EQ(e--, Integer(8LL));
    CHECK_EQ(e, Integer(7LL));
  }

  // 5. 幂运算（正指数）
  {
    CHECK_EQ(Integer(2LL).pow(Integer(10LL)).unwrap(), Fraction(1024, 1));
    CHECK_EQ(Integer(-3LL).pow(Integer(3LL)).unwrap(), Fraction(-27, 1));
    CHECK_EQ(Integer(-2LL).pow(Integer(4LL)).unwrap(), Fraction(16, 1));
    CHECK_EQ((Integer(2LL) ^ Integer(10LL)).unwrap(), Fraction(1024, 1));
  }

  // 6. 幂运算（负指数）
  {
    CHECK_EQ(Integer(2LL).pow(Integer(-3LL)).unwrap(), Fraction(1, 8));
    CHECK_EQ(Integer(-2LL).pow(Integer(-3LL)).unwrap(), Fraction(-1, 8));
    CHECK_EQ(Integer(-3LL).pow(Integer(-2LL)).unwrap(), Fraction(1, 9));
  }

  // 7. 幂运算（零指数与零底数）
  {
    CHECK_EQ(Integer(123LL).pow(Integer(0LL)).unwrap(), Fraction(1, 1));
    CHECK_EQ(Integer(0LL).pow(Integer(5LL)).unwrap(), Fraction(0, 1));
    CHECK_ERR(Integer(0LL).pow(Integer(-2LL)), MathsError::ZeroToNegativePower);
  }

  // 8. 复合幂赋值：非整数结果应抛异常
  {
    Integer f(2LL);
    f ^= Integer(10LL);
    CHECK_EQ(f, Integer(1024LL));

    Integer g(2LL);
    CHECK_THROWS(g ^= Integer(-1LL), MathsException);
  }

  // 9. 除零与取模零
  {
    CHECK_ERR(Integer(10LL) / Integer(0LL), MathsError::DivisionByZero);
    CHECK_ERR(Integer(10LL) % Integer(0LL), MathsError::DivisionByZero);
  }

  // 10. 比较（含同负号情形）
  {
    CHECK_TRUE(Integer(5LL) > Integer(-3LL));
    CHECK_TRUE(Integer(-5LL) < Integer(-3LL)); // 同负：绝对值大的更小
    CHECK_TRUE(Integer(-3LL) > Integer(-5LL));
    CHECK_TRUE(Integer(-5LL) <= Integer(-5LL));
    CHECK_TRUE(Integer(5LL) == Integer(5ULL));
    CHECK_TRUE(Integer(-1LL) < Integer(0LL));
    CHECK_TRUE(Integer(-1LL) < Integer(1LL));
  }

  // 11. 流输入输出
  {
    std::ostringstream os;
    os << Integer(-42LL);
    CHECK_EQ(os.str(), std::string("-42"));

    std::stringstream ss;
    ss << "-7";
    Integer input;
    ss >> input;
    CHECK_EQ(input, Integer(-7LL));
  }

  TEST_SUMMARY();
}
