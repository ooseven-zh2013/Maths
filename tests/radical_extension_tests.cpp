#include "check.hpp"

#include <iostream>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

RationalFunction expression(const char *latex) { return parseExpression(latex).unwrap(); }

RadicalExtension radicalX() { return RadicalExtension::make(expression("x")).unwrap(); }

// √(x²+1) + √(x²+2)
RadicalExtension twoRadicals() {
  return RadicalExtension::sumOfRadicals({expression("x^2+1"), expression("x^2+2")}).unwrap();
}

} // namespace

int main() {
  std::cout << "=== 根式扩张 RadicalExtension（多重根号）测试 ===" << std::endl;
  std::cout << std::unitbuf; // 崩溃时也能看到已输出的断言结果

  // ---------- 构造与拒收 ----------
  {
    const RadicalExtension rootX = radicalX();
    CHECK_TRUE(rootX.radicands().size() == std::size_t(1));
    CHECK_TRUE(rootX.termCount() == std::size_t(2)); // {1, √x}
    CHECK_TRUE(rootX.coefficient(0).isZero());
    CHECK_TRUE(rootX.coefficient(1) == expression("1"));

    // 完全平方的被开方数必须拒收：√(x²) = |x|，不是单值代数函数
    CHECK_ERR(RadicalExtension::make(expression("x^2")), MathsError::RadicandIsSquare);
    CHECK_ERR(RadicalExtension::make(expression("(x+1)^2")), MathsError::RadicandIsSquare);
    CHECK_ERR(RadicalExtension::make(expression("4x^2")), MathsError::RadicandIsSquare);
    CHECK_ERR(RadicalExtension::make(expression("(x^2-1)^2")), MathsError::RadicandIsSquare);

    // 平方自由的被开方数合法
    CHECK_OK(RadicalExtension::make(expression("x^2-1")));
    CHECK_OK(RadicalExtension::make(expression("x+1")));
    CHECK_OK(RadicalExtension::make(expression("1/x")));

    // 常数被开方数 / 多变量 / 零
    CHECK_ERR(RadicalExtension::make(expression("2")), MathsError::InvalidExpression);
    CHECK_ERR(RadicalExtension::make(expression("x*y")), MathsError::InvalidExpression);
    CHECK_ERR(RadicalExtension::make(expression("0")), MathsError::InvalidExpression);

    // 两个根号：基是 {1, √f₁, √f₂, √f₁√f₂}
    const RadicalExtension both = twoRadicals();
    CHECK_TRUE(both.radicands().size() == std::size_t(2));
    CHECK_TRUE(both.termCount() == std::size_t(4));
    CHECK_TRUE(both.coefficient(1) == expression("1"));
    CHECK_TRUE(both.coefficient(2) == expression("1"));
    CHECK_TRUE(both.coefficient(0).isZero());
    CHECK_TRUE(both.coefficient(3).isZero());

    // 两个根号必须独立：√x 与 √(4x) 是同一个根号的不同写法，折叠需要最简根式化，本库不做
    CHECK_ERR(RadicalExtension::sumOfRadicals({expression("x"), expression("4x")}),
              MathsError::RadicandsNotIndependent);
    CHECK_ERR(RadicalExtension::sumOfRadicals({expression("x"), expression("9x")}),
              MathsError::RadicandsNotIndependent);

    // 被开方数必须在同一个变量里；√x + √y 是两个变量，不支持
    CHECK_ERR(RadicalExtension::sumOfRadicals({expression("x"), expression("y")}), MathsError::InvalidExpression);

    // 生成元个数有上限（维度 2^k）
    CHECK_ERR(RadicalExtension::sumOfRadicals(
                  {expression("x"), expression("x+1"), expression("x+2"), expression("x+3"), expression("x+4")}),
              MathsError::InvalidExpression);
  }

  // ---------- 单根号：乘法把根号消掉 ----------
  {
    const RadicalExtension rootX = radicalX();

    const RadicalExtension squared = (rootX * rootX).unwrap();
    CHECK_TRUE(squared.isRadicalFree());
    CHECK_TRUE(squared.toRationalFunction().unwrap() == expression("x")); // √x·√x = x

    const RadicalExtension onePlus =
        RadicalExtension::make({expression("x")}, {expression("1"), expression("1")}).unwrap();
    const RadicalExtension oneMinus =
        RadicalExtension::make({expression("x")}, {expression("1"), expression("-1")}).unwrap();
    const RadicalExtension product = (onePlus * oneMinus).unwrap();
    CHECK_TRUE(product.toRationalFunction().unwrap() == expression("1-x")); // (1+√x)(1−√x) = 1−x

    const RadicalExtension sum = (onePlus + oneMinus).unwrap();
    CHECK_TRUE(sum.toRationalFunction().unwrap() == expression("2"));

    // 1/√x = √x/x，乘回 √x 得 1
    const RadicalExtension inverse =
        (RadicalExtension::make({expression("x")}, {expression("1"), expression("0")}).unwrap() / rootX).unwrap();
    CHECK_TRUE(inverse.coefficient(1) == expression("1/x"));
    const RadicalExtension back = (inverse * rootX).unwrap();
    CHECK_TRUE(back.isRadicalFree());
    CHECK_TRUE(back.toRationalFunction().unwrap() == expression("1"));

    // 不同被开方数属于不同的域，四则运算必须报错而不是瞎算
    const RadicalExtension other = RadicalExtension::make(expression("x+1")).unwrap();
    CHECK_ERR(rootX + other, MathsError::InvalidExpression);
    CHECK_ERR(rootX * other, MathsError::InvalidExpression);

    CHECK_ERR(rootX.toRationalFunction(), MathsError::NotARational); // 含根号部分，降不下去
  }

  // ---------- 两个根号同时出现：这正是多重扩张的用处 ----------
  {
    const RadicalExtension both = twoRadicals();

    // 同类项合并：(√(x²+1)+√(x²+2)) + (√(x²+1)−√(x²+2)) = 2√(x²+1)
    std::vector<RationalFunction> coefficients = both.coefficients();
    coefficients[2] = expression("-1"); // 第二项改 −√(x²+2)
    const RadicalExtension mixed = RadicalExtension::make(both.radicands(), coefficients).unwrap();
    const RadicalExtension summed = (both + mixed).unwrap();
    CHECK_TRUE(summed.coefficient(1) == expression("2"));
    CHECK_TRUE(summed.coefficient(2).isZero());

    // 共轭相乘：(√(x²+1)+√(x²+2))·(√(x²+1)−√(x²+2)) = (x²+1) − (x²+2) = −1
    const RadicalExtension conjugated = (both * mixed).unwrap();
    CHECK_TRUE(conjugated.isRadicalFree());
    CHECK_TRUE(conjugated.toRationalFunction().unwrap() == expression("-1"));

    // 平方：展开出 y₁y₂ 项
    const RadicalExtension squared = (both * both).unwrap();
    CHECK_TRUE(squared.coefficient(0) == expression("2x^2+3")); // (x²+1) + (x²+2)
    CHECK_TRUE(squared.coefficient(1).isZero());
    CHECK_TRUE(squared.coefficient(2).isZero());
    CHECK_TRUE(squared.coefficient(3) == expression("2")); // 2·y₁y₂ 的系数是 2

    // 除法：取倒数再乘回去应当得 1
    const Result<RadicalExtension> inverse = both.inverse();
    CHECK_OK(inverse);
    const RadicalExtension back = (both * inverse.unwrap()).unwrap();
    CHECK_TRUE(back.isRadicalFree());
    CHECK_TRUE(back.toRationalFunction().unwrap() == expression("1"));

    // 求值：x=0 时 √1+√2 = 1+√2
    Scope zero;
    CHECK_OK(zero.assign(Variable("x"), Fraction(0, 1)));
    CHECK_TRUE(both.evaluate(zero).unwrap() == RealAlgebraicNumber::parse("1+\\sqrt{2}").unwrap());

    // 求值：x=3 时 √10 + √11
    Scope three;
    CHECK_OK(three.assign(Variable("x"), Fraction(3, 1)));
    CHECK_TRUE(both.evaluate(three).unwrap() == RealAlgebraicNumber::parse("\\sqrt{10}+\\sqrt{11}").unwrap());

    // 定义域：x=0 时 √(x²−1) 无实值
    const RadicalExtension rootXSquaredMinusOne = RadicalExtension::make(expression("x^2-1")).unwrap();
    CHECK_ERR(rootXSquaredMinusOne.evaluate(zero), MathsError::NegativeEvenRoot);
  }

  // ---------- 求值：单根号 ----------
  {
    const RadicalExtension rootX = radicalX();

    Scope four;
    CHECK_OK(four.assign(Variable("x"), Fraction(4, 1)));
    CHECK_TRUE(rootX.evaluate(four).unwrap() == Fraction(2, 1)); // √4 = 2

    Scope negative;
    CHECK_OK(negative.assign(Variable("x"), Fraction(-1, 1)));
    CHECK_ERR(rootX.evaluate(negative), MathsError::NegativeEvenRoot);

    Scope two;
    CHECK_OK(two.assign(Variable("x"), Fraction(2, 1)));
    const RadicalExtension onePlusRootX =
        RadicalExtension::make({expression("x")}, {expression("1"), expression("1")}).unwrap();
    CHECK_TRUE(onePlusRootX.evaluate(two).unwrap() == RealAlgebraicNumber::parse("1+\\sqrt{2}").unwrap());

    // 未绑定的变量：求值应当失败而不是猜
    Scope other;
    CHECK_OK(other.assign(Variable("y"), Fraction(1, 1)));
    CHECK_TRUE(rootX.evaluate(other).isErr());
  }

  // ---------- 渲染 ----------
  {
    CHECK_EQ(radicalX().latex(), std::string("\\sqrt{x}"));
    CHECK_EQ(radicalX().str(), std::string("sqrt(x)"));

    const RadicalExtension onePlus =
        RadicalExtension::make({expression("x")}, {expression("1"), expression("1")}).unwrap();
    CHECK_EQ(onePlus.latex(), std::string("1 + \\sqrt{x}"));

    const RadicalExtension twoRootX =
        RadicalExtension::make({expression("x")}, {expression("0"), expression("2")}).unwrap();
    CHECK_EQ(twoRootX.latex(), std::string("2\\sqrt{x}"));

    const RadicalExtension negativeRootX =
        RadicalExtension::make({expression("x")}, {expression("0"), expression("-1")}).unwrap();
    CHECK_EQ(negativeRootX.latex(), std::string("-\\sqrt{x}"));

    // 两个根号：\sqrt{x^{2} + 1} + \sqrt{x^{2} + 2}
    CHECK_EQ(twoRadicals().latex(), std::string("\\sqrt{x^2 + 1} + \\sqrt{x^2 + 2}"));
  }

  TEST_SUMMARY();
}
