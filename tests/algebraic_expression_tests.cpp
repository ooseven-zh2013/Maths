#include "check.hpp"

#include <iostream>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

RealAlgebraicNumber number(const std::string &latex) { return RealAlgebraicNumber::parse(latex).unwrap(); }

// √2·x + 1
AlgebraicPolynomial linearWithAlgebraicCoefficient() {
  AlgebraicPolynomial result;
  result.addTerm({{Variable("x"), 1ULL}}, number("\\sqrt{2}"));
  result.addTerm({}, RealAlgebraicNumber(Fraction(1, 1)));
  return result;
}

} // namespace

int main() {
  std::cout << "=== 代数系数代数式 / 代数 Scope 测试 ===" << std::endl;
  std::cout << std::unitbuf; // 崩溃时也能看到已输出的断言结果

  // ---------- 构造与输出 ----------
  {
    const AlgebraicPolynomial polynomial = linearWithAlgebraicCoefficient();
    CHECK_TRUE(!polynomial.isZero());
    CHECK_EQ(polynomial.degree(), AlgebraicPolynomial::ull(1));
    CHECK_TRUE(polynomial.containsVariable(Variable("x")));
    CHECK_TRUE(!polynomial.str().empty());
    CHECK_TRUE(!polynomial.latex().empty());
    // 系数就是 √2，不是它的浮点近似
    CHECK_TRUE(polynomial.getTerms().size() == std::size_t(2));
  }

  // ---------- 乘法：(√2·x + 1)(√2·x − 1) = 2x² − 1 ----------
  {
    AlgebraicPolynomial left = linearWithAlgebraicCoefficient();
    AlgebraicPolynomial right;
    right.addTerm({{Variable("x"), 1ULL}}, number("\\sqrt{2}"));
    right.addTerm({}, RealAlgebraicNumber(Fraction(-1, 1)));

    const AlgebraicPolynomial product = (left * right).unwrap();

    // 期望 2x² − 1：系数都是有理数，落到代数数里也应当能被精确识别
    const auto squared = product.getTerms().find({{Variable("x"), 2ULL}});
    CHECK_TRUE(squared != product.getTerms().end());
    CHECK_TRUE(squared->second == Fraction(2, 1));
    const auto constant = product.getTerms().find({});
    CHECK_TRUE(constant != product.getTerms().end());
    CHECK_TRUE(constant->second == Fraction(-1, 1));
    CHECK_TRUE(product.getTerms().size() == std::size_t(2));
  }

  // ---------- 代数 Scope：代入并完全求值 ----------
  {
    AlgebraicScope scope;
    CHECK_OK(scope.assign(Variable("x"), number("\\sqrt{2}")));

    AlgebraicPolynomial polynomial; // x^2 + x
    polynomial.addTerm({{Variable("x"), 2ULL}}, RealAlgebraicNumber(Fraction(1, 1)));
    polynomial.addTerm({{Variable("x"), 1ULL}}, RealAlgebraicNumber(Fraction(1, 1)));

    const RealAlgebraicNumber value = polynomial.evaluate(scope).unwrap();
    // (√2)² + √2 = 2 + √2
    CHECK_TRUE(value == number("2 + \\sqrt{2}"));

    // 部分代入：未绑定的变量原样保留
    AlgebraicScope partial;
    CHECK_OK(partial.assign(Variable("y"), number("\\sqrt{3}")));
    const AlgebraicRationalFunction substituted = polynomial.substitute(partial);
    CHECK_TRUE(substituted.containsVariable(Variable("x")));
    CHECK_TRUE(!substituted.containsVariable(Variable("y")));
  }

  // ---------- 有理数代入：代数式的值与有理数完全一致 ----------
  {
    AlgebraicScope scope;
    CHECK_OK(scope.assign(Variable("x"), Fraction(3, 1)));

    AlgebraicPolynomial polynomial;
    polynomial.addTerm({{Variable("x"), 2ULL}}, RealAlgebraicNumber(Fraction(1, 1)));
    polynomial.addTerm({}, RealAlgebraicNumber(Fraction(1, 1))); // x^2 + 1

    CHECK_TRUE(polynomial.evaluate(scope).unwrap() == Fraction(10, 1));
  }

  // ---------- 极点：1/(x − √2) 代入 x = √2 ----------
  {
    AlgebraicPolynomial denominator;
    denominator.addTerm({{Variable("x"), 1ULL}}, RealAlgebraicNumber(Fraction(1, 1)));
    denominator.addTerm({}, -number("\\sqrt{2}")); // x − √2

    const AlgebraicRationalFunction fraction =
        AlgebraicRationalFunction::make(AlgebraicPolynomial(AlgebraicMonomial(RealAlgebraicNumber(Fraction(1, 1)))),
                                        denominator)
            .unwrap();

    AlgebraicScope scope;
    CHECK_OK(scope.assign(Variable("x"), number("\\sqrt{2}")));
    // 分母归零是原式的极点，属于数学结论而非程序错误
    CHECK_ERR(fraction.evaluate(scope), MathsError::ZeroDenominator);
  }

  // ---------- 提升：有理系数 → 代数系数 ----------
  {
    Polynomial rational;
    rational.addTerm({{Variable("x"), 2ULL}}, Fraction(1, 1));
    rational.addTerm({}, Fraction(-2, 1)); // x^2 − 2

    const AlgebraicPolynomial lifted = toAlgebraic(rational);
    CHECK_EQ(lifted.getTerms().size(), rational.getTerms().size()); // 结构一一对应
    CHECK_EQ(lifted.degree(), AlgebraicPolynomial::ull(2));

    // 代数 Scope：x = √2 时 x^2 − 2 归零
    AlgebraicScope algebraicScope;
    CHECK_OK(algebraicScope.assign(Variable("x"), number("\\sqrt{2}")));
    CHECK_TRUE(lifted.evaluate(algebraicScope).unwrap().isZero());

    // 有理 Scope：x = 2 时 x^2 − 2 = 2
    Scope rationalScope;
    CHECK_OK(rationalScope.assign(Variable("x"), Fraction(2, 1)));
    CHECK_TRUE(rational.evaluate(rationalScope).unwrap() == Fraction(2, 1));

    // 代数 Scope 装有理值，结果必须与有理版本完全一致
    AlgebraicScope integerScope;
    CHECK_OK(integerScope.assign(Variable("x"), Fraction(2, 1)));
    CHECK_TRUE(lifted.evaluate(integerScope).unwrap() == Fraction(2, 1));
  }

  // ---------- 提升要保留定义域约束 ----------
  {
    // (x^2 + x) / (x^2 − x) 化简会约掉 x，从而丢掉「x ≠ 0」
    RationalFunction rational;
    {
      Polynomial numerator;
      numerator.addTerm({{Variable("x"), 2ULL}}, Fraction(1, 1));
      numerator.addTerm({{Variable("x"), 1ULL}}, Fraction(1, 1));
      Polynomial denominator;
      denominator.addTerm({{Variable("x"), 2ULL}}, Fraction(1, 1));
      denominator.addTerm({{Variable("x"), 1ULL}}, Fraction(-1, 1));
      rational = RationalFunction::make(numerator, denominator).unwrap();
    }
    CHECK_TRUE(rational.discardedConstraints().count(Variable("x")) == 1);

    const AlgebraicRationalFunction lifted = toAlgebraic(rational);
    CHECK_TRUE(lifted.discardedConstraints().count(Variable("x")) == 1);
  }

  // ---------- L1 只对有理系数启用 ----------
  {
    // (6x + 6) / (4x + 4)：有理版本会先约数值内容，得 (3x + 3)/(2x + 2)
    Polynomial numerator;
    numerator.addTerm({{Variable("x"), 1ULL}}, Fraction(6, 1));
    numerator.addTerm({}, Fraction(6, 1));
    Polynomial denominator;
    denominator.addTerm({{Variable("x"), 1ULL}}, Fraction(4, 1));
    denominator.addTerm({}, Fraction(4, 1));
    const RationalFunction rational = RationalFunction::make(numerator, denominator).unwrap();
    CHECK_EQ(rational.str(), std::string("(3 x + 3) / (2 x + 2)"));

    // 代数系数的版本跳过 L1（ℚ(α) 里内容恒为 1），同一个式子保持原样
    AlgebraicPolynomial algebraicNumerator;
    algebraicNumerator.addTerm({{Variable("x"), 1ULL}}, RealAlgebraicNumber(Fraction(6, 1)));
    algebraicNumerator.addTerm({}, RealAlgebraicNumber(Fraction(6, 1)));
    AlgebraicPolynomial algebraicDenominator;
    algebraicDenominator.addTerm({{Variable("x"), 1ULL}}, RealAlgebraicNumber(Fraction(4, 1)));
    algebraicDenominator.addTerm({}, RealAlgebraicNumber(Fraction(4, 1)));
    const AlgebraicRationalFunction algebraic =
        AlgebraicRationalFunction::make(algebraicNumerator, algebraicDenominator).unwrap();
    CHECK_EQ(algebraic.str(), std::string("(6 x + 6) / (4 x + 4)"));
  }

  // ---------- 与有理数互相比较 / 判等 ----------
  {
    AlgebraicPolynomial first;
    first.addTerm({}, number("\\sqrt{2}"));
    AlgebraicPolynomial second;
    // √8 / 2 = 2√2/2 = √2，和直接写的 √2 是同一个数
    second.addTerm({}, (number("\\sqrt{8}") / RealAlgebraicNumber(Fraction(2, 1))).unwrap());
    CHECK_TRUE(first == second);
  }

  // ---------- 高次根式取值：代入不能失败 ----------
  //
  // 回归用：曾经 √[4]{2}、√[5]{2}、√2+√3 代入会失败（ZeroDenominator / NumericOverflow）。
  // 根因是「与有理数相乘相加」也走了环上的线性代数；现已改走廉价特例。
  {
    struct Case {
      const char *value;
      const char *expectedLatex;
    };
    const Case cases[] = {
        {"\\sqrt[4]{2}", "\\sqrt[4]{2}"},
        {"\\sqrt[5]{2}", "\\sqrt[5]{2}"},
        {"\\sqrt{2}+\\sqrt{3}", "\\operatorname{RootOf}(x^{4} - 10x^{2} + 1, [\\frac{3}{4}, \\frac{21}{4}])"},
        {"\\frac{1+\\sqrt{5}}{2}", "\\operatorname{RootOf}(x^{2} - x - 1, [\\frac{11}{8}, \\frac{17}{8}])"},
    };

    AlgebraicPolynomial single; // 式子 x
    single.addTerm({{Variable("x"), 1ULL}}, RealAlgebraicNumber(Fraction(1, 1)));
    AlgebraicPolynomial withZ; // 式子 x*z
    withZ.addTerm({{Variable("x"), 1ULL}, {Variable("z"), 1ULL}}, RealAlgebraicNumber(Fraction(1, 1)));

    for (const Case &item : cases) {
      const RealAlgebraicNumber value = number(item.value);
      AlgebraicScope scope;
      CHECK_OK(scope.assign(Variable("x"), value));

      // x 单独代入：应当就是这个值本身
      const Result<RealAlgebraicNumber> resolved = single.evaluate(scope);
      CHECK_OK(resolved);
      CHECK_TRUE(resolved.unwrap() == value);

      // x·z 代入：保留变量 z，系数换成该值，且渲染成可读形式
      const Result<AlgebraicRationalFunction> substituted = AlgebraicRationalFunction(withZ).substitute(scope);
      CHECK_OK(substituted);
      CHECK_EQ(substituted.unwrap().latex(), std::string(item.expectedLatex) + "z");
    }
  }

  // ---------- 与有理数运算的廉价特例 ----------
  {
    const RealAlgebraicNumber root = number("\\sqrt[4]{2}"); // ⁴√2

    // α + c 与 α·c 既不能失败，也要精确
    CHECK_TRUE((root + RealAlgebraicNumber(Fraction(1, 1))) - RealAlgebraicNumber(Fraction(1, 1)) == root);
    CHECK_TRUE(root * RealAlgebraicNumber(Fraction(2, 1)) == root + root);
    CHECK_TRUE((root * RealAlgebraicNumber(Fraction(2, 1))) / RealAlgebraicNumber(Fraction(2, 1)) == root);
    CHECK_TRUE(root * RealAlgebraicNumber(Fraction(0, 1)) == Fraction(0, 1));

    // 乘以负数：区间方向要翻转，符号也跟着变
    const RealAlgebraicNumber negated = root * RealAlgebraicNumber(Fraction(-1, 1));
    CHECK_TRUE(negated < Fraction(0, 1));
    CHECK_TRUE(negated == -root);

    // (2·⁴√2)^4 = 16 · 2 = 32
    const Result<RealAlgebraicNumber> powered = (root * RealAlgebraicNumber(Fraction(2, 1))).pow(4);
    CHECK_OK(powered);
    CHECK_TRUE(powered.unwrap() == Fraction(32, 1));
  }

  TEST_SUMMARY();
}
