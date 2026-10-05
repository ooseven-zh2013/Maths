#include "check.hpp"

#include <iostream>
#include <set>
#include <string>

import maths;

using namespace maths;

namespace {

// parseExpression 给的是有理函数；这里要的是**多项式**，所以取分子
Polynomial poly(const char *latex) { return parseExpression(latex).unwrap().getNumerator(); }

MultiRationalFunction frac(const char *numerator, const char *denominator) {
  return MultiRationalFunction::make(poly(numerator), poly(denominator)).unwrap();
}

MultiRationalFunction value(const char *latex) { return MultiRationalFunction(poly(latex)); }

} // namespace

int main() {
  std::cout << "=== 多元有理函数 MultiRationalFunction 测试 ===" << '\n';
  std::cout << std::unitbuf;

  // ---------- 构造与观察 ----------
  {
    const MultiRationalFunction half = frac("1", "2");
    CHECK_TRUE(half.numerator() == poly("1"));
    CHECK_TRUE(half.denominator() == poly("2"));
    CHECK_TRUE(!half.isZero() && !half.isOne());
    CHECK_TRUE(half.isPolynomial()); // 分母是常数 → 能降回多项式
    CHECK_TRUE(!frac("x", "y").isPolynomial());
    CHECK_TRUE(frac("x", "y").variables() == std::set<Variable>({Variable("x"), Variable("y")}));
    CHECK_TRUE(value("x + y").isPolynomial());
    CHECK_TRUE(MultiRationalFunction(Fraction(0, 1)).isZero());
  }

  // ---------- 约掉公共单项式 ----------
  {
    // (x²y)/(xy²) → x/y
    const MultiRationalFunction reduced = frac("x^2*y", "x*y^2");
    CHECK_TRUE(reduced.numerator() == poly("x"));
    CHECK_TRUE(reduced.denominator() == poly("y"));
    // 约不掉就保持原样（不做完整 GCD）
    const MultiRationalFunction kept = frac("x^2+y", "x");
    CHECK_TRUE(kept.numerator() == poly("x^2+y"));
  }

  // ---------- 符号规范化 ----------
  {
    // 分母首项为负 → 分子分母同时取负
    const MultiRationalFunction flipped = frac("-x", "1-y");
    CHECK_TRUE(!flipped.denominator().isZero());
    CHECK_TRUE(flipped == frac("x", "y-1"));
  }

  // ---------- 四则 ----------
  {
    // 1/2 + 1/3 = 5/6
    const MultiRationalFunction sum = MultiRationalFunction(Fraction(1, 2)) + MultiRationalFunction(Fraction(1, 3));
    CHECK_TRUE(sum == MultiRationalFunction(Fraction(5, 6)));
    // 1/2 - 1/3 = 1/6
    const MultiRationalFunction difference =
        MultiRationalFunction(Fraction(1, 2)) - MultiRationalFunction(Fraction(1, 3));
    CHECK_TRUE(difference == MultiRationalFunction(Fraction(1, 6)));
    // 乘除
    CHECK_TRUE((MultiRationalFunction(Fraction(2, 3)) * MultiRationalFunction(Fraction(3, 4))).unwrap() ==
               MultiRationalFunction(Fraction(1, 2)));
    CHECK_TRUE(MultiRationalFunction(Fraction(1, 2)) / MultiRationalFunction(Fraction(3, 4)) ==
               MultiRationalFunction(Fraction(2, 3)));
    // 除以零
    CHECK_ERR(MultiRationalFunction(Fraction(1, 2)) / MultiRationalFunction(Fraction(0, 1)),
              MathsError::ZeroDenominator);
    // 分母为零的构造
    CHECK_ERR(MultiRationalFunction::make(poly("1"), poly("0")), MathsError::ZeroDenominator);
    // 取负
    CHECK_TRUE(-MultiRationalFunction(Fraction(1, 2)) == MultiRationalFunction(Fraction(-1, 2)));
  }

  // ---------- 判等靠交叉相乘（不需要规范形）----------
  {
    // (x+y)/(x-y) 与 (x²-y²)/(x-y)² 是同一个分式吗？不是 —— 这里只验交叉相乘这条路走得通
    const MultiRationalFunction a = frac("x", "y");
    const MultiRationalFunction b = frac("x*z", "y*z"); // z 约掉后与 a 相等
    CHECK_TRUE(a == b);
    CHECK_TRUE(!(a == frac("y", "x")));
    CHECK_TRUE(MultiRationalFunction(Fraction(0, 1)) == MultiRationalFunction(Fraction(0, 1)));
    CHECK_TRUE(!(MultiRationalFunction(Fraction(0, 1)) == MultiRationalFunction(Fraction(1, 1))));
  }

  // ---------- 求值（有理赋值）----------
  {
    Scope scope;
    scope.assign(Variable("x"), Fraction(3, 1)).unwrap();
    scope.assign(Variable("y"), Fraction(4, 1)).unwrap();
    // (x+y)/(y-x) 在 (3,4) = 7
    CHECK_TRUE(frac("x+y", "y-x").evaluate(scope).unwrap() == Fraction(7, 1));
    // x/y = 3/4
    CHECK_TRUE(frac("x", "y").evaluate(scope).unwrap() == Fraction(3, 4));
    // 分母为零 → ZeroDenominator（那是「函数没定义」，不是算错）
    Scope zero;
    zero.assign(Variable("x"), Fraction(0, 1)).unwrap();
    zero.assign(Variable("y"), Fraction(0, 1)).unwrap();
    CHECK_ERR(frac("x", "y").evaluate(zero), MathsError::ZeroDenominator);
  }

  // ---------- 渲染 ----------
  {
    CHECK_EQ(frac("x+y", "2").latex(), value("x+y").latex()); // 分母是常数就不写分式
    CHECK_TRUE(frac("x", "y").latex().find("frac") != std::string::npos);
    CHECK_TRUE(frac("x", "y").str().find('/') != std::string::npos);
  }

  TEST_SUMMARY();
}
