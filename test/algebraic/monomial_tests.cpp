#include "algebraic_expression.hpp"
#include "check.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>

int main() {
  std::cout << "=== Monomial 测试 ===" << '\n';

  const Variable x("x");
  const Variable y("y");
  const Variable z("z");
  const Fraction one(1, 1);

  // 1. 构造与规范化
  {
    const Monomial zero;
    CHECK_TRUE(zero.isZero());
    CHECK_TRUE(zero.isConstant());
    CHECK_EQ(zero.getCoefficient(), Fraction(0, 1));
    CHECK_EQ(zero.degree(), 0ULL);
    CHECK_EQ(zero.str(), std::string("0"));
    CHECK_TRUE(zero.getFactors().empty());

    const Monomial constant(Fraction(3, 4));
    CHECK_TRUE(!constant.isZero());
    CHECK_TRUE(constant.isConstant());
    CHECK_EQ(constant.str(), std::string("3/4"));

    // 变量书写顺序任意，规范化后应一致；同底数幂要合并
    const Monomial scrambled(Fraction(2, 1), {{y, 1ULL}, {x, 2ULL}, {y, 1ULL}});
    const Monomial canonical(Fraction(2, 1), {{x, 2ULL}, {y, 2ULL}});
    CHECK_TRUE(scrambled == canonical);
    CHECK_EQ(scrambled.str(), std::string("2 x^2 y^2"));
    CHECK_EQ(scrambled.degree(), 4ULL);

    // 零次幂因子应被移除
    const Monomial dropped(Fraction(5, 1), {{x, 0ULL}, {y, 1ULL}});
    CHECK_EQ(dropped.str(), std::string("5 y"));
    CHECK_EQ(dropped.degree(), 1ULL);

    // 系数为零时统一为零单项式，不携带变量
    const Monomial vanished(Fraction(0, 1), {{x, 3ULL}});
    CHECK_TRUE(vanished.isZero());
    CHECK_TRUE(vanished.getFactors().empty());
    CHECK_EQ(vanished.str(), std::string("0"));
  }

  // 2. 相等与顺序
  {
    const Monomial xy(one, {{x, 1ULL}, {y, 1ULL}});
    const Monomial yx(one, {{y, 1ULL}, {x, 1ULL}});
    CHECK_TRUE(xy == yx);

    const Monomial xOnly(one, {{x, 1ULL}});
    CHECK_TRUE(xOnly != xy);
    CHECK_TRUE(xOnly < xy); // 前缀更短者更小
    CHECK_TRUE(xy > xOnly);

    // 变量部分相同则比较系数
    const Monomial twoX(Fraction(2, 1), {{x, 1ULL}});
    const Monomial threeX(Fraction(3, 1), {{x, 1ULL}});
    CHECK_TRUE(twoX < threeX);
    CHECK_TRUE(twoX != threeX);
  }

  // 3. 乘法
  {
    const Monomial a(Fraction(2, 3), {{x, 2ULL}, {y, 1ULL}});
    const Monomial b(Fraction(3, 4), {{y, 1ULL}, {z, 1ULL}});
    const Monomial product = (a * b).unwrap();
    CHECK_EQ(product.getCoefficient(), Fraction(1, 2));
    CHECK_EQ(product.str(), std::string("1/2 x^2 y^2 z"));
    CHECK_EQ(product.degree(), 5ULL);
    CHECK_OK(a * b);

    CHECK_TRUE((a * Monomial()).unwrap().isZero());
    CHECK_TRUE((Monomial() * b).unwrap().isZero());
    CHECK_EQ((Monomial(Fraction(2, 3)) * Monomial(Fraction(3, 4))).unwrap().getCoefficient(), Fraction(1, 2));
  }

  // 4. 复合乘法赋值
  {
    Monomial a(Fraction(2, 1), {{x, 1ULL}});
    a *= Monomial(Fraction(3, 1), {{y, 1ULL}});
    CHECK_EQ(a.str(), std::string("6 x y"));
  }

  // 5. 一元负号
  {
    const Monomial a(Fraction(2, 3), {{x, 1ULL}});
    CHECK_EQ((-a).getCoefficient(), Fraction(-2, 3));
    CHECK_EQ((-a).str(), std::string("-2/3 x"));
    CHECK_EQ(-(-a), a);

    // 零单项式取负仍是零，不应产生 "-0"
    CHECK_TRUE((-Monomial()).isZero());
    CHECK_EQ((-Monomial()).str(), std::string("0"));
  }

  // 6. str 输出
  {
    CHECK_EQ(Monomial(one, {{x, 1ULL}}).str(), std::string("x"));
    CHECK_EQ(Monomial(Fraction(-1, 1), {{x, 1ULL}}).str(), std::string("-x"));
    CHECK_EQ(Monomial(one, {{x, 5ULL}}).str(), std::string("x^5"));
    CHECK_EQ(Monomial(Fraction(-1, 1), {{x, 2ULL}, {y, 1ULL}}).str(), std::string("-x^2 y"));
    CHECK_EQ(Monomial(Fraction(7, 1)).str(), std::string("7"));
    CHECK_EQ(Monomial(Fraction(-7, 1)).str(), std::string("-7"));
  }

  // 7. 流输出
  {
    std::ostringstream os;
    os << Monomial(Fraction(3, 2), {{x, 2ULL}});
    CHECK_EQ(os.str(), std::string("3/2 x^2"));
  }

  // 8. 指数只能是常数：x^x 这类写法不合法
  {
    // 变量名中的 '^' 不是合法字符，解析阶段就直接拒绝
    CHECK_THROWS(Variable("x^2"), MathsException);
    CHECK_THROWS(Variable("x^x"), MathsException);
    CHECK_THROWS(Variable("a^{b}"), MathsException);
    CHECK_THROWS(Variable("x^"), MathsException);

    // 下标与指数是两回事：x_2 表示「x 的第 2 个」，不等于 x 的 2 次方
    CHECK_EQ(Variable("x_2").str(), std::string("x_2"));
    CHECK_EQ(Variable("x_y").str(), std::string("x_y"));

    // 指数位置在类型上就是 unsigned long long，塞不进 Variable
    static_assert(std::is_same_v<VarPowers::value_type, std::pair<Variable, unsigned long long>>);
    static_assert(!std::is_constructible_v<VarPowers::value_type, Variable, Variable>);
    static_assert(!std::is_convertible_v<Variable, unsigned long long>);
  }

  // 9. 指数合并不得静默溢出
  {
    constexpr auto huge = std::numeric_limits<unsigned long long>::max();

    // 乘法路径返回错误码
    CHECK_ERR(Monomial(one, {{x, huge}}) * Monomial(one, {{x, 1ULL}}), MathsError::ExponentOverflow);

    // 构造路径无法返回 Result，仍抛 MathsException
    CHECK_THROWS(Monomial(one, {{x, huge}, {x, 1ULL}}), MathsException);

    // 未越界时应正常合并
    const Monomial maxed(one, {{x, huge - 1}, {x, 1ULL}});
    CHECK_EQ(maxed.getFactors()[0].second, huge);
  }

  // 10. LaTeX 输出
  {
    CHECK_EQ(Monomial(one, {{x, 1ULL}}).latex(), std::string("x"));
    CHECK_EQ(Monomial(Fraction(-1, 1), {{x, 1ULL}}).latex(), std::string("-x"));
    CHECK_EQ(Monomial(one, {{x, 5ULL}}).latex(), std::string("x^{5}"));
    CHECK_EQ(Monomial(Fraction(3, 1), {{x, 1ULL}}).latex(), std::string("3x"));

    // 分数系数写成 \frac{}{}，变量间直接相连
    CHECK_EQ(Monomial(Fraction(1, 2), {{x, 2ULL}, {y, 1ULL}}).latex(), std::string("\\frac{1}{2}x^{2}y"));
    CHECK_EQ(Monomial(Fraction(-1, 2), {{x, 1ULL}}).latex(), std::string("-\\frac{1}{2}x"));
    CHECK_EQ(Monomial(Fraction(3, 4)).latex(), std::string("\\frac{3}{4}"));
    CHECK_EQ(Monomial(Fraction(-7, 1)).latex(), std::string("-7"));
    CHECK_EQ(Monomial().latex(), std::string("0"));
  }

  TEST_SUMMARY();
}
