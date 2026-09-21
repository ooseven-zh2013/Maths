#include "check.hpp"
#include "expression_parser.hpp"
#include "rational_function.hpp"
#include "scope.hpp"
#include <iostream>
#include <limits>
#include <string>

namespace {

// 解析表达式；失败时记为断言失败并返回零，避免 unwrap 抛异常中断整个测试
RationalFunction parsed(const char *text, int line) {
  const Result<RationalFunction> result = parseExpression(text);
  if (result.isErr()) {
    maths_test::report(false, text, __FILE__, line, "解析失败");
    return RationalFunction(Fraction(0, 1));
  }
  return result.unwrap();
}

// 借宏带上调用点行号
#define PARSE(text) parsed(text, __LINE__)

// 取出绑定的常数值（测试里绝大多数绑定都是常数）
Fraction boundValue(const Scope &scope, const Variable &variable) {
  const Result<RationalFunction> value = scope.lookup(variable);
  if (value.isErr()) {
    return Fraction(0, 1);
  }
  const Result<Fraction> evaluated = value.unwrap().evaluate(Scope());
  return evaluated.isOk() ? evaluated.unwrap() : Fraction(0, 1);
}

} // namespace

int main() {
  std::cout << "=== Scope 测试 ===" << '\n';

  const Variable x("x");
  const Variable y("y");
  const Variable s("s");
  const Variable v("v");
  const Variable t("t");

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
    CHECK_OK(scope.assign(x, Fraction(1, 2)));
    CHECK_TRUE(!scope.empty());
    CHECK_TRUE(scope.contains(x));
    CHECK_EQ(scope.size(), 1ULL);
    CHECK_OK(scope.lookup(x));
    CHECK_EQ(boundValue(scope, x), Fraction(1, 2));
    CHECK_EQ(scope.str(), std::string(R"({"x": "1/2"})"));
  }

  // 3. 绑定整数：按等值分数存储
  {
    Scope scope;
    CHECK_OK(scope.assign(y, Integer(5LL)));
    CHECK_EQ(boundValue(scope, y), Fraction(5, 1));
    CHECK_EQ(boundValue(scope, y).getDenominator(), 1LL);
    CHECK_TRUE(!boundValue(scope, y).isNegative());
    CHECK_EQ(scope.str(), std::string(R"({"y": "5"})"));
  }

  // 4. 重复赋值是覆盖语义，不报错
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Fraction(1, 3)));
    CHECK_OK(scope.assign(x, Fraction(2, 3)));
    CHECK_EQ(scope.size(), 1ULL);
    CHECK_EQ(boundValue(scope, x), Fraction(2, 3));

    CHECK_OK(scope.assign(x, Integer(-7LL)));
    CHECK_EQ(boundValue(scope, x), Fraction(-7, 1));
    CHECK_EQ(scope.size(), 1ULL);
  }

  // 5. Integer 转换无截断（走内部表示而非 getVal()）
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(std::numeric_limits<long long>::max())));
    CHECK_EQ(boundValue(scope, x).getNumerator(), std::numeric_limits<long long>::max());
    CHECK_EQ(boundValue(scope, x).getDenominator(), 1LL);
    CHECK_TRUE(!boundValue(scope, x).isNegative());

    // 边界值：只断言符号与分母，getNumerator() 在 |value| == 2^63 时会溢出
    CHECK_OK(scope.assign(y, Integer(std::numeric_limits<long long>::min())));
    CHECK_TRUE(boundValue(scope, y).isNegative());
    CHECK_EQ(boundValue(scope, y).getDenominator(), 1LL);

    // 直接验证转换函数本身
    const Fraction converted = Fraction::fromInteger(Integer(-42LL));
    CHECK_EQ(converted.getNumerator(), -42LL);
    CHECK_EQ(converted.getDenominator(), 1LL);
    CHECK_TRUE(converted.isNegative());
  }

  // 6. 解除绑定与清空
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Fraction(1, 2)));
    CHECK_TRUE(scope.erase(x));
    CHECK_TRUE(!scope.contains(x));
    CHECK_TRUE(!scope.erase(x)); // 已不存在，返回 false 而不是报错
    CHECK_ERR(scope.lookup(x), MathsError::UndefinedVariable);

    CHECK_OK(scope.assign(x, Fraction(1, 2)));
    CHECK_OK(scope.assign(y, Fraction(1, 3)));
    CHECK_EQ(scope.size(), 2ULL);
    scope.clear();
    CHECK_TRUE(scope.empty());
    CHECK_EQ(scope.size(), 0ULL);
  }

  // 7. 多变量按变量名排序输出
  {
    Scope scope;
    CHECK_OK(scope.assign(y, Integer(-3LL)));
    CHECK_OK(scope.assign(x, Fraction(1, 2)));
    CHECK_EQ(scope.size(), 2ULL);
    CHECK_EQ(scope.str(), std::string(R"({"x": "1/2", "y": "-3"})"));
  }

  // 8. 带下标的变量名
  {
    Scope scope;
    const Variable indexed("a_{i,j}");
    CHECK_OK(scope.assign(indexed, Fraction(3, 4)));
    CHECK_TRUE(scope.contains(indexed));
    CHECK_TRUE(!scope.contains(Variable("a_{i,k}")));
    CHECK_EQ(boundValue(scope, indexed), Fraction(3, 4));
    CHECK_EQ(scope.str(), std::string(R"({"a_{i,j}": "3/4"})"));
  }

  // ==================== 绑定到表达式 ====================

  // 9. 允许把变量绑定到含其它变量的表达式
  {
    Scope scope;
    CHECK_OK(scope.assign(s, PARSE("v*t")));
    CHECK_TRUE(scope.contains(s));
    CHECK_EQ(scope.str(), std::string(R"({"s": "t v"})"));

    // 未绑定的 v、t 仍然可以在后续被替换
    CHECK_OK(scope.assign(v, Integer(3LL)));
    CHECK_OK(scope.assign(t, Integer(4LL)));

    const RationalFunction result = PARSE("2s");
    CHECK_EQ(result.substitute(scope).unwrap().latex(), std::string("24"));
  }

  // 10. 链式绑定：s = v*t、v = a*b 应当一路展开
  {
    const Variable a("a");
    const Variable b("b");
    Scope scope;
    CHECK_OK(scope.assign(s, PARSE("v*t")));
    CHECK_OK(scope.assign(v, PARSE("a*b")));
    CHECK_OK(scope.assign(a, Integer(2LL)));
    CHECK_OK(scope.assign(b, Integer(3LL)));
    CHECK_OK(scope.assign(t, Integer(5LL)));

    // s = (2*3)*5 = 30
    CHECK_EQ(PARSE("s").substitute(scope).unwrap().latex(), std::string("30"));
  }

  // 11. 自引用必须被拒绝 —— 那是方程不是赋值
  {
    Scope scope;
    CHECK_ERR(scope.assign(x, PARSE("2x")), MathsError::NotAnAssignment);
    CHECK_ERR(scope.assign(x, PARSE("x + 1")), MathsError::NotAnAssignment);
    CHECK_ERR(scope.assign(x, PARSE("1/x")), MathsError::NotAnAssignment);
    CHECK_TRUE(scope.empty()); // 被拒绝后不应留下任何绑定

    // 引用别的变量没问题
    CHECK_OK(scope.assign(x, PARSE("2y")));
  }

  // ==================== 代入替换 ====================

  const Monomial x1(Fraction(1, 1), {{x, 1ULL}});
  const Monomial y1(Fraction(1, 1), {{y, 1ULL}});
  const Monomial one1(Fraction(1, 1));

  // 12. 单项式：全部变量都能替换 → 变成常数
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(2LL)));
    CHECK_OK(scope.assign(y, Fraction(1, 3)));

    // 3 x y 代入 x=2、y=1/3 → 3 * 2 * 1/3 = 2
    const Monomial product(Fraction(3, 1), {{x, 1ULL}, {y, 1ULL}});
    const RationalFunction result = product.substitute(scope);
    CHECK_EQ(result.latex(), std::string("2"));
    CHECK_EQ(result, RationalFunction(Fraction(2, 1)));
  }

  // 13. 单项式：未绑定的变量原样保留
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(2LL)));

    const Monomial product(Fraction(3, 1), {{x, 1ULL}, {y, 1ULL}});
    const RationalFunction result = product.substitute(scope);
    CHECK_EQ(result.latex(), std::string("6y"));
    CHECK_EQ(result.getNumerator().degree(), 1ULL);
  }

  // 14. 绑定的值是分式时，结果也应当是分式
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Fraction(1, 2)));
    // x^2 代入 x=1/2 → 1/4
    CHECK_EQ(Monomial(Fraction(1, 1), {{x, 2ULL}}).substitute(scope).latex(), std::string("\\frac{1}{4}"));

    // 3 x^3 代入 x=2 → 24
    Scope scope2;
    CHECK_OK(scope2.assign(x, Integer(2LL)));
    CHECK_EQ(Monomial(Fraction(3, 1), {{x, 3ULL}}).substitute(scope2).latex(), std::string("24"));
  }

  // 15. 代入后可能不再是多项式
  {
    Scope scope;
    CHECK_OK(scope.assign(x, PARSE("1/y")));

    // 2x 代入 x = 1/y → 2/y
    const RationalFunction result = Monomial(Fraction(2, 1), {{x, 1ULL}}).substitute(scope);
    CHECK_EQ(result.latex(), std::string("\\frac{2}{y}"));
    CHECK_ERR(result.toPolynomial(), MathsError::NotAPolynomial);
  }

  // 16. 单项式：零单项式与空 Scope
  {
    const Scope empty;
    CHECK_TRUE(Monomial().substitute(empty).isZero());

    const Monomial value(Fraction(3, 2), {{x, 1ULL}});
    // 代入结果统一由分式承载，所以是 \frac{3x}{2} 而不是单项式写法 \frac{3}{2}x
    CHECK_EQ(value.substitute(empty).latex(), std::string("\\frac{3x}{2}"));
  }

  // 17. 多项式：全部替换后塌成常数
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(2LL)));

    // (x + 1)(x - 1) = x^2 - 1，代入 x=2 → 3
    const Polynomial plusOne = Polynomial(x1) + Polynomial(one1);
    const Polynomial minusOne = Polynomial(x1) - Polynomial(one1);
    const Polynomial product = (plusOne * minusOne).unwrap();

    const RationalFunction result = product.substitute(scope);
    CHECK_EQ(result.latex(), std::string("3"));
  }

  // 18. 多项式：部分替换，剩余项按同类项合并
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Fraction(1, 2)));

    // (x + y)^2 = x^2 + 2 x y + y^2，代入 x=1/2 → y^2 + y + 1/4
    const Polynomial binom = Polynomial(x1) + Polynomial(y1);
    const Polynomial squared = (binom * binom).unwrap();

    const RationalFunction result = squared.substitute(scope);
    // 通分后统一成分式（1/4 被并入分子）
    CHECK_EQ(result.latex(), std::string("\\frac{4y^{2} + 4y + 1}{4}"));
  }

  // 19. 多项式：多项分别替换后相消
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(5LL)));
    CHECK_OK(scope.assign(y, Integer(-1LL)));

    // x + y 代入 x=5、y=-1 → 4
    const Polynomial sum = Polynomial(x1) + Polynomial(y1);
    CHECK_EQ(sum.substitute(scope).latex(), std::string("4"));

    const Polynomial difference = Polynomial(x1) + Polynomial(y1) - Polynomial(x1) - Polynomial(y1);
    CHECK_TRUE(difference.substitute(scope).isZero());
  }

  // 20. 多项式：空 Scope 时原样返回
  {
    const Scope empty;
    const Polynomial original = Polynomial(x1) + Polynomial(y1);
    CHECK_EQ(original.substitute(empty).latex(), original.latex());
  }

  // ==================== 完全求值 ====================

  // 21. 单项式求值
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(2LL)));
    CHECK_OK(scope.assign(y, Fraction(1, 3)));

    const Monomial product(Fraction(3, 1), {{x, 1ULL}, {y, 1ULL}});
    CHECK_OK(product.evaluate(scope));
    CHECK_EQ(product.evaluate(scope).unwrap(), Fraction(2, 1));

    CHECK_EQ(Monomial(Fraction(1, 2), {{x, 2ULL}}).evaluate(scope).unwrap(), Fraction(2, 1)); // 1/2 * 4

    // 变量未绑定
    CHECK_ERR(Monomial(Fraction(1, 1), {{Variable("z"), 1ULL}}).evaluate(scope), MathsError::UndefinedVariable);
  }

  // 22. 多项式求值
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(2LL)));

    // (x + 1)(x - 1) = x^2 - 1，代入 x=2 → 3
    const Polynomial plusOne = Polynomial(x1) + Polynomial(one1);
    const Polynomial minusOne = Polynomial(x1) - Polynomial(one1);
    const Polynomial product = (plusOne * minusOne).unwrap();
    CHECK_EQ(product.evaluate(scope).unwrap(), Fraction(3, 1));

    // 含未绑定变量 y
    CHECK_ERR((Polynomial(x1) + Polynomial(y1)).evaluate(scope), MathsError::UndefinedVariable);

    // 化简后只剩一项，但那一项本身仍是未绑定变量
    CHECK_ERR(Polynomial(y1).evaluate(scope), MathsError::UndefinedVariable);
  }

  // 23. 零单项式与零多项式恒为 0，不需要任何绑定
  {
    const Scope empty;
    CHECK_EQ(Monomial().evaluate(empty).unwrap(), Fraction(0, 1));
    CHECK_EQ(Polynomial().evaluate(empty).unwrap(), Fraction(0, 1));
  }

  // 24. 负数与分数取值
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(-3LL)));
    // 1/2 x^2，x=-3 → 9/2
    CHECK_EQ(Monomial(Fraction(1, 2), {{x, 2ULL}}).evaluate(scope).unwrap(), Fraction(9, 2));

    Scope half;
    CHECK_OK(half.assign(x, Fraction(1, 2)));
    // x + x = 2x，x=1/2 → 1
    CHECK_EQ((Polynomial(x1) + Polynomial(x1)).evaluate(half).unwrap(), Fraction(1, 1));

    // 绑定值是分式时求值同样成立
    Scope symbolic;
    CHECK_OK(symbolic.assign(y, Fraction(3, 4)));
    CHECK_OK(symbolic.assign(x, PARSE("1/y")));
    // x = 1/y、y = 3/4 → 1/(3/4) = 4/3
    CHECK_EQ(PARSE("x").substitute(symbolic).unwrap().latex(), std::string("\\frac{4}{3}"));
    CHECK_EQ(Monomial(Fraction(1, 1), {{x, 1ULL}}).evaluate(symbolic).unwrap(), Fraction(4, 3));
  }

  TEST_SUMMARY();
}
