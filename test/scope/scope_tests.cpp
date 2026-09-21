#include "check.hpp"
#include "scope.hpp"
#include <iostream>
#include <limits>
#include <string>

int main() {
  std::cout << "=== Scope 测试 ===" << '\n';

  const Variable x("x");
  const Variable y("y");

  // 1. 空表
  {
    const Scope scope;
    CHECK_TRUE(scope.empty());
    CHECK_EQ(scope.size(), 0ULL);
    CHECK_TRUE(!scope.contains(x));
    CHECK_EQ(scope.bindings().size(), 0ULL);
    CHECK_EQ(scope.str(), std::string("{}"));
    CHECK_ERR(scope.lookup(x), MathsError::UndefinedVariable);
  }

  // 2. 绑定分数
  {
    Scope scope;
    scope.assign(x, Fraction(1, 2));
    CHECK_TRUE(!scope.empty());
    CHECK_TRUE(scope.contains(x));
    CHECK_EQ(scope.size(), 1ULL);
    CHECK_OK(scope.lookup(x));
    CHECK_EQ(scope.lookup(x).unwrap(), Fraction(1, 2));
    CHECK_EQ(scope.str(), std::string("{x = 1/2}"));
  }

  // 3. 绑定整数：按等值分数存储
  {
    Scope scope;
    scope.assign(y, Integer(5LL));
    CHECK_EQ(scope.lookup(y).unwrap(), Fraction(5, 1));
    CHECK_EQ(scope.lookup(y).unwrap().getDenominator(), 1LL);
    CHECK_TRUE(!scope.lookup(y).unwrap().isNegative());
    CHECK_EQ(scope.str(), std::string("{y = 5}"));
  }

  // 4. 重复赋值是覆盖语义，不报错
  {
    Scope scope;
    scope.assign(x, Fraction(1, 3));
    scope.assign(x, Fraction(2, 3));
    CHECK_EQ(scope.size(), 1ULL);
    CHECK_EQ(scope.lookup(x).unwrap(), Fraction(2, 3));

    scope.assign(x, Integer(-7LL));
    CHECK_EQ(scope.lookup(x).unwrap(), Fraction(-7, 1));
    CHECK_EQ(scope.size(), 1ULL);
  }

  // 5. Integer 转换无截断（走内部表示而非 getVal()）
  {
    Scope scope;
    scope.assign(x, Integer(std::numeric_limits<long long>::max()));
    CHECK_EQ(scope.lookup(x).unwrap().getNumerator(), std::numeric_limits<long long>::max());
    CHECK_EQ(scope.lookup(x).unwrap().getDenominator(), 1LL);
    CHECK_TRUE(!scope.lookup(x).unwrap().isNegative());

    // 边界值：只断言符号与分母，getNumerator() 在 |value| == 2^63 时会溢出
    scope.assign(y, Integer(std::numeric_limits<long long>::min()));
    CHECK_TRUE(scope.lookup(y).unwrap().isNegative());
    CHECK_EQ(scope.lookup(y).unwrap().getDenominator(), 1LL);

    // 直接验证转换函数本身
    const Fraction converted = Fraction::fromInteger(Integer(-42LL));
    CHECK_EQ(converted.getNumerator(), -42LL);
    CHECK_EQ(converted.getDenominator(), 1LL);
    CHECK_TRUE(converted.isNegative());
  }

  // 6. 解除绑定与清空
  {
    Scope scope;
    scope.assign(x, Fraction(1, 2));
    CHECK_TRUE(scope.erase(x));
    CHECK_TRUE(!scope.contains(x));
    CHECK_TRUE(!scope.erase(x)); // 已不存在，返回 false 而不是报错
    CHECK_ERR(scope.lookup(x), MathsError::UndefinedVariable);

    scope.assign(x, Fraction(1, 2));
    scope.assign(y, Fraction(1, 3));
    CHECK_EQ(scope.size(), 2ULL);
    scope.clear();
    CHECK_TRUE(scope.empty());
    CHECK_EQ(scope.size(), 0ULL);
  }

  // 7. 多变量按变量名排序输出
  {
    Scope scope;
    scope.assign(y, Integer(-3LL));
    scope.assign(x, Fraction(1, 2));
    CHECK_EQ(scope.size(), 2ULL);
    CHECK_EQ(scope.str(), std::string("{x = 1/2, y = -3}"));
  }

  // 8. 带下标的变量名
  {
    Scope scope;
    const Variable indexed("a_{i,j}");
    scope.assign(indexed, Fraction(3, 4));
    CHECK_TRUE(scope.contains(indexed));
    CHECK_TRUE(!scope.contains(Variable("a_{i,k}")));
    CHECK_EQ(scope.lookup(indexed).unwrap(), Fraction(3, 4));
    CHECK_EQ(scope.str(), std::string("{a_{i,j} = 3/4}"));
  }

  // ==================== 代入替换 ====================

  const Monomial x1(Fraction(1, 1), {{x, 1ULL}});
  const Monomial y1(Fraction(1, 1), {{y, 1ULL}});
  const Monomial one1(Fraction(1, 1));

  // 9. 单项式：全部变量都能替换 → 变成常数
  {
    Scope scope;
    scope.assign(x, Integer(2LL));
    scope.assign(y, Fraction(1, 3));

    // 3 x y 代入 x=2、y=1/3 → 3 * 2 * 1/3 = 2
    const Monomial product(Fraction(3, 1), {{x, 1ULL}, {y, 1ULL}});
    const Monomial result = product.substitute(scope);
    CHECK_TRUE(result.isConstant());
    CHECK_EQ(result, Monomial(Fraction(2, 1)));
    CHECK_EQ(result.str(), std::string("2"));
  }

  // 10. 单项式：未绑定的变量原样保留
  {
    Scope scope;
    scope.assign(x, Integer(2LL));

    const Monomial product(Fraction(3, 1), {{x, 1ULL}, {y, 1ULL}});
    const Monomial result = product.substitute(scope);
    CHECK_EQ(result.str(), std::string("6 y"));
    CHECK_EQ(result.getFactors().size(), 1ULL);
    CHECK_EQ(result.degree(), 1ULL);
  }

  // 11. 单项式：幂次并进系数
  {
    Scope scope;
    scope.assign(x, Fraction(1, 2));
    // x^2 代入 x=1/2 → 1/4
    CHECK_EQ(Monomial(Fraction(1, 1), {{x, 2ULL}}).substitute(scope), Monomial(Fraction(1, 4)));

    Scope scope2;
    scope2.assign(x, Integer(2LL));
    // 3 x^3 代入 x=2 → 24
    CHECK_EQ(Monomial(Fraction(3, 1), {{x, 3ULL}}).substitute(scope2), Monomial(Fraction(24, 1)));
  }

  // 12. 单项式：零单项式与空 Scope
  {
    const Scope empty;
    CHECK_TRUE(Monomial().substitute(empty).isZero());

    const Monomial value(Fraction(3, 2), {{x, 1ULL}});
    CHECK_EQ(value.substitute(empty), value); // 没有任何绑定时原样返回
  }

  // 13. 单项式：代入后系数自动约分
  {
    Scope scope;
    scope.assign(x, Fraction(2, 3));
    // 3/4 x 代入 x=2/3 → 1/2
    CHECK_EQ(Monomial(Fraction(3, 4), {{x, 1ULL}}).substitute(scope), Monomial(Fraction(1, 2)));
  }

  // 14. 多项式：全部替换后塌成常数
  {
    Scope scope;
    scope.assign(x, Integer(2LL));

    // (x + 1)(x - 1) = x^2 - 1，代入 x=2 → 3
    const Polynomial plusOne = Polynomial(x1) + Polynomial(one1);
    const Polynomial minusOne = Polynomial(x1) - Polynomial(one1);
    const Polynomial product = (plusOne * minusOne).unwrap();

    const Polynomial result = product.substitute(scope);
    CHECK_TRUE(result.isMonomial());
    CHECK_EQ(result.str(), std::string("3"));
  }

  // 15. 多项式：部分替换，剩余项按同类项合并
  {
    Scope scope;
    scope.assign(x, Fraction(1, 2));

    // (x + y)^2 = x^2 + 2 x y + y^2，代入 x=1/2 → y^2 + y + 1/4
    const Polynomial binom = Polynomial(x1) + Polynomial(y1);
    const Polynomial squared = (binom * binom).unwrap();

    const Polynomial result = squared.substitute(scope);
    CHECK_EQ(result.getTerms().size(), 3ULL);
    CHECK_EQ(result.str(), std::string("y^2 + y + 1/4"));
  }

  // 16. 多项式：多项分别替换后相消
  {
    Scope scope;
    scope.assign(x, Integer(5LL));
    scope.assign(y, Integer(-1LL));

    // x + y 代入 x=5、y=-1 → 4
    const Polynomial sum = Polynomial(x1) + Polynomial(y1);
    CHECK_EQ(sum.substitute(scope).str(), std::string("4"));

    const Polynomial difference = Polynomial(x1) + Polynomial(y1) - Polynomial(x1) - Polynomial(y1);
    CHECK_TRUE(difference.substitute(scope).isZero());
  }

  // 17. 多项式：空 Scope 时原样返回
  {
    const Scope empty;
    const Polynomial original = Polynomial(x1) + Polynomial(y1);
    CHECK_EQ(original.substitute(empty), original);
  }

  TEST_SUMMARY();
}
