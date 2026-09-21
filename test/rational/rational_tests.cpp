#include "check.hpp"
#include "rational_function.hpp"
#include "scope.hpp"
#include <iostream>
#include <string>

int main() {
  std::cout << "=== RationalFunction 测试 ===" << '\n';

  const Variable x("x");
  const Variable y("y");

  const Monomial x1(Fraction(1, 1), {{x, 1ULL}});
  const Monomial y1(Fraction(1, 1), {{y, 1ULL}});
  const Monomial twoX1(Fraction(2, 1), {{x, 1ULL}});
  const Monomial one1(Fraction(1, 1));

  // 1. 隐式提升：分母为 1
  {
    const RationalFunction fromMonomial(x1);
    CHECK_EQ(fromMonomial.getNumerator().str(), std::string("x"));
    CHECK_EQ(fromMonomial.getDenominator().str(), std::string("1"));
    CHECK_EQ(fromMonomial.str(), std::string("x")); // 分母为 1 时按多项式输出

    const RationalFunction fromFraction(Fraction(3, 4));
    CHECK_EQ(fromFraction.str(), std::string("3/4"));

    const RationalFunction zero;
    CHECK_TRUE(zero.isZero());
    CHECK_EQ(zero.str(), std::string("0"));
  }

  // 2. 分母为零多项式时应失败
  {
    CHECK_ERR(RationalFunction::make(Polynomial(x1), Polynomial()), MathsError::ZeroDenominator);
    CHECK_OK(RationalFunction::make(Polynomial(x1), Polynomial(one1)));
  }

  // 3. L1：数值内容约分（不产生任何约束）
  {
    const Polynomial numerator(Monomial(Fraction(6, 1), {{x, 1ULL}}));
    const Polynomial denominator(Monomial(Fraction(12, 1), {{y, 1ULL}}));
    const RationalFunction reduced = RationalFunction::make(numerator, denominator).unwrap();

    CHECK_EQ(reduced.getNumerator().str(), std::string("x"));
    CHECK_EQ(reduced.getDenominator().str(), std::string("2 y"));
    CHECK_TRUE(reduced.discardedConstraints().empty()); // 约常数不丢定义域
  }

  // 4. L1：分数系数的 gcd（gcd(a/b, c/d) = gcd(a,c) / lcm(b,d)）
  {
    const Polynomial numerator =
        Polynomial(Monomial(Fraction(1, 2), {{x, 1ULL}})) + Polynomial(Monomial(Fraction(1, 2)));
    const Polynomial denominator =
        Polynomial(Monomial(Fraction(1, 4), {{x, 1ULL}})) + Polynomial(Monomial(Fraction(1, 4)));
    const RationalFunction reduced = RationalFunction::make(numerator, denominator).unwrap();

    // 公共因子 1/4，约掉后 1/2 ÷ 1/4 = 2
    CHECK_EQ(reduced.getNumerator().str(), std::string("2 x + 2"));
    CHECK_EQ(reduced.getDenominator().str(), std::string("x + 1"));
  }

  // 5. L2：单项式公因子约分，并记录被丢掉的约束
  {
    const Polynomial numerator(Monomial(Fraction(6, 1), {{x, 2ULL}, {y, 1ULL}}));   // 6 x^2 y
    const Polynomial denominator(Monomial(Fraction(4, 1), {{x, 1ULL}, {y, 2ULL}})); // 4 x y^2
    const RationalFunction reduced = RationalFunction::make(numerator, denominator).unwrap();

    CHECK_EQ(reduced.getNumerator().str(), std::string("3 x"));
    CHECK_EQ(reduced.getDenominator().str(), std::string("2 y"));
    CHECK_EQ(reduced.str(), std::string("(3 x) / (2 y)"));

    // 约掉了 x 与 y，二者都被记为「非零」前提
    CHECK_EQ(reduced.discardedConstraints().size(), 2ULL);
    CHECK_TRUE(reduced.discardedConstraints().count(x) == 1);
    CHECK_TRUE(reduced.discardedConstraints().count(y) == 1);
  }

  // 6. L2：只约公共部分，指数取 min
  {
    const Polynomial numerator(Monomial(Fraction(1, 1), {{x, 3ULL}}));   // x^3
    const Polynomial denominator(Monomial(Fraction(1, 1), {{x, 1ULL}})); // x
    const RationalFunction reduced = RationalFunction::make(numerator, denominator).unwrap();

    CHECK_EQ(reduced.getNumerator().str(), std::string("x^2"));
    CHECK_EQ(reduced.getDenominator().str(), std::string("1"));
    CHECK_EQ(reduced.discardedConstraints().size(), 1ULL);
  }

  // 7. L3：分母符号归一
  {
    const Polynomial numerator(one1);
    const Polynomial denominator = Polynomial(-x1) - Polynomial(one1); // -x - 1
    const RationalFunction normalized = RationalFunction::make(numerator, denominator).unwrap();

    CHECK_TRUE(!normalized.getDenominator().getTerms().begin()->second.isNegative());
    CHECK_EQ(normalized.getDenominator().str(), std::string("x + 1"));
    CHECK_EQ(normalized.str(), std::string("(-1) / (x + 1)"));
  }

  // 8. 四则运算
  {
    const RationalFunction half(Fraction(1, 2));
    const RationalFunction third(Fraction(1, 3));

    CHECK_EQ((half + third).str(), std::string("5/6"));
    CHECK_EQ((half - third).str(), std::string("1/6"));
    CHECK_EQ((half * third).str(), std::string("1/6"));
    CHECK_EQ((half / third).unwrap().str(), std::string("3/2"));
    CHECK_EQ((-half).str(), std::string("-1/2"));

    // 分式之间的加减乘除
    const RationalFunction inverseX = RationalFunction::make(Polynomial(one1), Polynomial(x1)).unwrap(); // 1/x
    CHECK_EQ((inverseX * RationalFunction(x1)).str(), std::string("1"));
    CHECK_EQ((inverseX + RationalFunction(one1)).str(), std::string("(x + 1) / (x)"));
  }

  // 9. 除法：除数的分子为零多项式时失败
  {
    const RationalFunction half(Fraction(1, 2));
    CHECK_ERR(half / RationalFunction(), MathsError::ZeroDenominator);
    CHECK_OK(half / RationalFunction(one1));
  }

  // 10. 相等判断靠交叉相乘，不需要约成正规形式
  {
    const RationalFunction simple =
        RationalFunction::make(Polynomial(x1) + Polynomial(one1), Polynomial(x1) - Polynomial(one1)).unwrap();

    // (x+1)^2 / ((x-1)(x+1))，公共因子 x+1 不是单项式，L1/L2 约不掉
    const Polynomial factor = Polynomial(x1) + Polynomial(one1);
    const RationalFunction complex =
        RationalFunction::make((factor * factor).unwrap(), ((Polynomial(x1) - Polynomial(one1)) * factor).unwrap())
            .unwrap();

    CHECK_TRUE(complex.getNumerator().str() != simple.getNumerator().str()); // 确实没化简成一样
    CHECK_EQ(simple, complex);                                               // 但判等成立
    CHECK_TRUE(simple != RationalFunction(x1));
  }

  // 11. 约束跨运算传播，且 simplify 幂等
  {
    const RationalFunction reduced =
        RationalFunction::make(Polynomial(x1), Polynomial(Monomial(Fraction(1, 1), {{x, 2ULL}}))).unwrap(); // x/x^2
    CHECK_EQ(reduced.getNumerator().str(), std::string("1"));
    CHECK_EQ(reduced.getDenominator().str(), std::string("x"));
    CHECK_EQ(reduced.discardedConstraints().size(), 1ULL);

    RationalFunction sum = reduced + RationalFunction(one1);
    CHECK_EQ(sum.getNumerator().str(), std::string("x + 1"));
    CHECK_TRUE(sum.discardedConstraints().count(x) == 1); // 约束被带过来

    sum.simplify(); // 再次化简不应重复记录约束
    sum.simplify();
    CHECK_EQ(sum.discardedConstraints().size(), 1ULL);
  }

  // ==================== 化为多项式（长除法） ====================

  // 12. 分母整除分子时成功
  {
    // (x^2 - 1) / (x - 1) = x + 1
    const Polynomial factor = Polynomial(x1) - Polynomial(one1);
    const Polynomial numerator = ((Polynomial(x1) + Polynomial(one1)) * factor).unwrap();
    const RationalFunction r = RationalFunction::make(numerator, factor).unwrap();

    CHECK_OK(r.toPolynomial());
    CHECK_EQ(r.toPolynomial().unwrap().str(), std::string("x + 1"));

    // 常数分式
    const RationalFunction constant =
        RationalFunction::make(Polynomial(Monomial(Fraction(6, 1))), Polynomial(Monomial(Fraction(3, 1)))).unwrap();
    CHECK_EQ(constant.toPolynomial().unwrap().str(), std::string("2"));

    // 分母为 1 的分式本身就是多项式
    CHECK_EQ(RationalFunction(x1).toPolynomial().unwrap().str(), std::string("x"));
  }

  // 13. 分母不整除分子时失败
  {
    const RationalFunction inverseX = RationalFunction::make(Polynomial(one1), Polynomial(x1)).unwrap();
    CHECK_ERR(inverseX.toPolynomial(), MathsError::NotAPolynomial);

    const RationalFunction proper =
        RationalFunction::make(Polynomial(x1) + Polynomial(one1), Polynomial(x1) - Polynomial(one1)).unwrap();
    CHECK_ERR(proper.toPolynomial(), MathsError::NotAPolynomial);

    // 零分式可化为零多项式
    CHECK_EQ(RationalFunction().toPolynomial().unwrap().str(), std::string("0"));
  }

  // 14. 代入与求值
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(2LL)));

    const RationalFunction r =
        RationalFunction::make(Polynomial(x1) + Polynomial(one1), Polynomial(x1) - Polynomial(one1)).unwrap();
    CHECK_OK(r.substitute(scope));
    CHECK_EQ(r.substitute(scope).unwrap().str(), std::string("3")); // (2+1)/(2-1)
    CHECK_EQ(r.evaluate(scope).unwrap(), Fraction(3, 1));

    // 代入后分母退化为零多项式
    Scope pole;
    CHECK_OK(pole.assign(x, Integer(1LL)));
    CHECK_ERR(r.substitute(pole), MathsError::ZeroDenominator);
    CHECK_ERR(r.evaluate(pole), MathsError::ZeroDenominator);

    // 变量未绑定
    const Scope empty;
    CHECK_ERR(r.evaluate(empty), MathsError::UndefinedVariable);
  }

  // 15. 代入后仍保留已记录的约束
  {
    Scope scope;
    CHECK_OK(scope.assign(x, Integer(3LL)));

    const RationalFunction reduced =
        RationalFunction::make(Polynomial(x1), Polynomial(Monomial(Fraction(1, 1), {{x, 2ULL}}))).unwrap();
    CHECK_EQ(reduced.discardedConstraints().size(), 1ULL);

    const Result<RationalFunction> substituted = reduced.substitute(scope);
    CHECK_OK(substituted);
    CHECK_EQ(substituted.unwrap().discardedConstraints().size(), 1ULL);
  }

  // 16. 字典序单项式序满足乘法相容性 —— 这是多项式长除法必然终止的前提
  {
    const VarPowers xv{{x, 1ULL}};
    const VarPowers yv{{y, 1ULL}};
    const VarPowers x2{{x, 2ULL}};
    const VarPowers xy{{x, 1ULL}, {y, 1ULL}};
    const VarPowers y2{{y, 2ULL}};

    CHECK_TRUE(maths_detail::compareLex(xv, yv) == std::strong_ordering::greater);
    // 两边同乘 x，顺序保持：x > y ⟹ x^2 > x*y
    CHECK_TRUE(maths_detail::compareLex(x2, xy) == std::strong_ordering::greater);
    // 两边同乘 y，顺序保持：x > y ⟹ x*y > y^2
    CHECK_TRUE(maths_detail::compareLex(xy, y2) == std::strong_ordering::greater);

    // 同变量比指数；缺失的变量按指数 0 处理
    CHECK_TRUE(maths_detail::compareLex(x2, xv) == std::strong_ordering::greater);
    CHECK_TRUE(maths_detail::compareLex(xv, x2) == std::strong_ordering::less);
    CHECK_TRUE(maths_detail::compareLex(xv, xv) == std::strong_ordering::equal);
    // 常数项最小
    CHECK_TRUE(maths_detail::compareLex(xv, VarPowers{}) == std::strong_ordering::greater);
  }

  // 17. LaTeX 输出：分式一律呈现为统一的 \frac{}{} 形式
  {
    const RationalFunction divided = RationalFunction::make(Polynomial(x1), Polynomial(y1)).unwrap(); // x/y
    CHECK_EQ(divided.latex(), std::string("\\frac{x}{y}"));

    // 通分由 operator+ 完成，latex() 只负责不把它拆散：x/y + 1 → (x + y)/y
    const RationalFunction sum = divided + RationalFunction(one1);
    CHECK_EQ(sum.getNumerator().latex(), std::string("x + y"));
    CHECK_EQ(sum.getDenominator().latex(), std::string("y"));
    CHECK_EQ(sum.latex(), std::string("\\frac{x + y}{y}"));

    // 分母恰为 1 时退化成多项式，不写成 \frac{x}{1}
    CHECK_EQ(RationalFunction(x1).latex(), std::string("x"));

    // 常数分式：latex 写 \frac{5}{6}，str 仍是 5/6
    const RationalFunction half(Fraction(1, 2));
    const RationalFunction third(Fraction(1, 3));
    CHECK_EQ((half + third).str(), std::string("5/6"));
    CHECK_EQ((half + third).latex(), std::string("\\frac{5}{6}"));

    // \frac{}{} 自带分组，分子分母内部不需要额外括号
    const RationalFunction proper =
        RationalFunction::make(Polynomial(x1) + Polynomial(one1), Polynomial(x1) - Polynomial(one1)).unwrap();
    CHECK_EQ(proper.latex(), std::string("\\frac{x + 1}{x - 1}"));
  }

  TEST_SUMMARY();
}
