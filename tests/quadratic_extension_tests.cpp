#include "check.hpp"

#include <iostream>
#include <string>

import maths;

using namespace maths;

namespace {

RationalFunction expression(const char *latex) { return parseExpression(latex).unwrap(); }

QuadraticExtension radicalX() { return QuadraticExtension::make(expression("x")).unwrap(); }

} // namespace

int main() {
  std::cout << "=== 二次扩张 QuadraticExtension（√(有理函数)）测试 ===" << std::endl;
  std::cout << std::unitbuf;

  // ---------- 构造与拒收 ----------
  {
    // √x 是合法元素：a = 0、b = 1、f = x
    const QuadraticExtension rootX = radicalX();
    CHECK_TRUE(rootX.rationalPart().isZero());
    CHECK_TRUE(rootX.radicalPart() == expression("1"));
    CHECK_TRUE(rootX.radicand() == expression("x"));
    CHECK_TRUE(rootX.variable() == Variable("x"));

    // 完全平方式被开方数必须拒收：√(x²) = |x|，不是单值代数函数
    CHECK_ERR(QuadraticExtension::make(expression("x^2")), MathsError::RadicandIsSquare);
    CHECK_ERR(QuadraticExtension::make(expression("(x+1)^2")), MathsError::RadicandIsSquare);
    CHECK_ERR(QuadraticExtension::make(expression("4x^2")), MathsError::RadicandIsSquare);
    CHECK_ERR(QuadraticExtension::make(expression("(x^2-1)^2")), MathsError::RadicandIsSquare);

    // 平方自由的被开方数是合法的
    CHECK_OK(QuadraticExtension::make(expression("x^2-1")));
    CHECK_OK(QuadraticExtension::make(expression("x+1")));
    CHECK_OK(QuadraticExtension::make(expression("1/x")));

    // 常数被开方数归实代数数；多变量暂不支持
    CHECK_ERR(QuadraticExtension::make(expression("2")), MathsError::InvalidExpression);
    CHECK_ERR(QuadraticExtension::make(expression("x*y")), MathsError::InvalidExpression);
    CHECK_ERR(QuadraticExtension::make(expression("0")), MathsError::InvalidExpression);
  }

  // ---------- 四则运算：乘方把根号消掉 ----------
  {
    const QuadraticExtension rootX = radicalX();

    // √x · √x = x —— 这是这个类型存在的意义
    const QuadraticExtension squared = (rootX * rootX).unwrap();
    CHECK_TRUE(squared.radicalPart().isZero());
    CHECK_TRUE(squared.toRationalFunction().unwrap() == expression("x"));

    // (1+√x)(1−√x) = 1 − x
    const QuadraticExtension onePlus =
        QuadraticExtension::make(expression("1"), expression("1"), expression("x")).unwrap();
    const QuadraticExtension oneMinus =
        QuadraticExtension::make(expression("1"), expression("-1"), expression("x")).unwrap();
    const QuadraticExtension product = (onePlus * oneMinus).unwrap();
    CHECK_TRUE(product.radicalPart().isZero());
    CHECK_TRUE(product.toRationalFunction().unwrap() == expression("1-x"));

    // 相加：同类项合并
    const QuadraticExtension sum = (onePlus + oneMinus).unwrap();
    CHECK_TRUE(sum.radicalPart().isZero());
    CHECK_TRUE(sum.toRationalFunction().unwrap() == expression("2"));

    // √x + √x = 2√x（a = 0、b = 2）
    CHECK_TRUE((rootX + rootX).unwrap() ==
               QuadraticExtension::make(expression("0"), expression("2"), expression("x")).unwrap());
    CHECK_TRUE((rootX - rootX).unwrap().radicalPart().isZero());

    // 1/√x 有理化成 √x/x，且乘回 √x 得 1
    const QuadraticExtension inverse =
        (QuadraticExtension::make(expression("1"), expression("0"), expression("x")).unwrap() / rootX).unwrap();
    CHECK_TRUE(inverse.radicalPart() == expression("1/x")); // b = 1/x ⟹ √x/x
    const QuadraticExtension back = (inverse * rootX).unwrap();
    CHECK_TRUE(back.radicalPart().isZero());
    CHECK_TRUE(back.toRationalFunction().unwrap() == expression("1"));

    // 不同被开方数属于不同的域，运算必须报错而不是瞎算
    const QuadraticExtension rootXPlusOne = QuadraticExtension::make(expression("x+1")).unwrap();
    CHECK_ERR(rootX + rootXPlusOne, MathsError::InvalidExpression);
    CHECK_ERR(rootX * rootXPlusOne, MathsError::InvalidExpression);

    // 零元与降一阶
    const QuadraticExtension zero =
        QuadraticExtension::make(expression("0"), expression("0"), expression("x")).unwrap();
    CHECK_TRUE(zero.radicalPart().isZero());
    CHECK_TRUE(zero.toRationalFunction().unwrap().isZero());
    CHECK_ERR(rootX.toRationalFunction(), MathsError::NotARational); // 含根号部分，降不下去
  }

  // ---------- 求值：代入有理数落到实代数数 ----------
  {
    const QuadraticExtension rootX = radicalX();

    Scope perfect;
    CHECK_OK(perfect.assign(Variable("x"), Fraction(4, 1)));
    CHECK_TRUE(rootX.evaluate(perfect).unwrap() == Fraction(2, 1)); // √4 = 2

    Scope negative;
    CHECK_OK(negative.assign(Variable("x"), Fraction(-1, 1)));
    CHECK_ERR(rootX.evaluate(negative), MathsError::NegativeEvenRoot); // 该点处无实值

    Scope two;
    CHECK_OK(two.assign(Variable("x"), Fraction(2, 1)));
    const QuadraticExtension onePlusRootX =
        QuadraticExtension::make(expression("1"), expression("1"), expression("x")).unwrap();
    CHECK_TRUE(onePlusRootX.evaluate(two).unwrap() == RealAlgebraicNumber::parse("1+\\sqrt{2}").unwrap());

    // √(x²−1) 在 x = 3 处是 √8
    Scope three;
    CHECK_OK(three.assign(Variable("x"), Fraction(3, 1)));
    const QuadraticExtension rootXSquaredMinusOne = QuadraticExtension::make(expression("x^2-1")).unwrap();
    CHECK_TRUE(rootXSquaredMinusOne.evaluate(three).unwrap() == RealAlgebraicNumber::parse("\\sqrt{8}").unwrap());

    // 未绑定的变量：求值应当失败而不是猜
    Scope other;
    CHECK_OK(other.assign(Variable("y"), Fraction(1, 1)));
    CHECK_TRUE(rootX.evaluate(other).isErr());
  }

  // ---------- 渲染 ----------
  {
    CHECK_EQ(radicalX().latex(), std::string("\\sqrt{x}"));
    CHECK_EQ(radicalX().str(), std::string("sqrt(x)"));

    const QuadraticExtension onePlus =
        QuadraticExtension::make(expression("1"), expression("1"), expression("x")).unwrap();
    CHECK_EQ(onePlus.latex(), std::string("1 + \\sqrt{x}"));
    CHECK_EQ(onePlus.str(), std::string("1 + sqrt(x)"));

    const QuadraticExtension twoRootX =
        QuadraticExtension::make(expression("0"), expression("2"), expression("x")).unwrap();
    CHECK_EQ(twoRootX.latex(), std::string("2\\sqrt{x}"));

    const QuadraticExtension negativeRootX =
        QuadraticExtension::make(expression("0"), expression("-1"), expression("x")).unwrap();
    CHECK_EQ(negativeRootX.latex(), std::string("-\\sqrt{x}"));

    const QuadraticExtension shifted =
        QuadraticExtension::make(expression("x"), expression("1"), expression("x+1")).unwrap();
    CHECK_EQ(shifted.latex(), std::string("x + \\sqrt{x + 1}"));
  }

  TEST_SUMMARY();
}
