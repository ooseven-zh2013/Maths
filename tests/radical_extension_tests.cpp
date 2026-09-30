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

    // 不同被开方数**不再是错误**：运算时自动扩域到共同域（见下面「自动扩域」一节）。
    // 只有「同一个平方类」（√x 与 √(4x)）和「不同变量」才拒收。
    const RadicalExtension other = RadicalExtension::make(expression("x+1")).unwrap();
    CHECK_OK(rootX + other);
    CHECK_OK(rootX * other);

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

  // ---------- 自动扩域：两个不同的根号能放在一个域里算 ----------
  {
    const RadicalExtension rootX = radicalX();
    const RadicalExtension rootXPlusOne = RadicalExtension::make(expression("x+1")).unwrap();

    // √x · √(x+1)：两边生成元不同，运算时自动取并集扩域
    const RadicalExtension product = (rootX * rootXPlusOne).unwrap();
    CHECK_TRUE(product.radicands().size() == std::size_t(2));
    CHECK_TRUE(product.termCount() == std::size_t(4));
    CHECK_TRUE(product.coefficient(3) == expression("1")); // y₁y₂ 的系数是 1
    CHECK_TRUE(product.coefficient(0).isZero());

    // √x + √(x+1)：同理
    const RadicalExtension sum = (rootX + rootXPlusOne).unwrap();
    CHECK_TRUE(sum.radicands().size() == std::size_t(2));
    CHECK_TRUE(sum.coefficient(1) == expression("1"));
    CHECK_TRUE(sum.coefficient(2) == expression("1"));

    // 平方：(√x + √(x+1))² = 2x + 1 + 2√x√(x+1)
    const RadicalExtension squared = (sum * sum).unwrap();
    CHECK_TRUE(squared.coefficient(0) == expression("2x+1"));
    CHECK_TRUE(squared.coefficient(3) == expression("2"));

    // 取逆再乘回去得 1
    const RadicalExtension back = (sum * sum.inverse().unwrap()).unwrap();
    CHECK_TRUE(back.isRadicalFree());
    CHECK_TRUE(back.toRationalFunction().unwrap() == expression("1"));

    // 相加顺序不影响结果
    CHECK_TRUE((rootXPlusOne + rootX).unwrap() == sum);

    // 同一个平方类（√x 与 √(4x) 其实是一个根号）仍然拒收：合并需要最简根式化
    const RadicalExtension rootFourX = RadicalExtension::make(expression("4x")).unwrap();
    CHECK_ERR(rootX + rootFourX, MathsError::RadicandsNotIndependent);
    CHECK_ERR(rootX * rootFourX, MathsError::RadicandsNotIndependent);

    // 不同变量：并集里出现两个变量，同样拒收
    CHECK_ERR(rootX + RadicalExtension::make(expression("y")).unwrap(), MathsError::InvalidExpression);
  }

  // ---------- 纯有理元素（零生成元）：为「当系数类型用」做准备 ----------
  {
    const RadicalExtension two(Fraction(2, 1));
    CHECK_TRUE(two.isRadicalFree());
    CHECK_TRUE(two.termCount() == std::size_t(1));
    CHECK_EQ(two.latex(), std::string("2"));

    const RadicalExtension rootX = radicalX();

    // 与根式元素运算时自动扩域
    const RadicalExtension sum = (two + rootX).unwrap();
    CHECK_TRUE(sum.radicands().size() == std::size_t(1));
    CHECK_TRUE(sum.coefficient(0) == expression("2"));
    CHECK_TRUE(sum.coefficient(1) == expression("1"));
    CHECK_TRUE(sum == RadicalExtension::make({expression("x")}, {expression("2"), expression("1")}).unwrap());

    // 判等按值比：√x·√x 与 x 是同一种东西的两种写法（下面这条）
    // 注意：常数被开方数（√2）属于实代数数，本类型明确拒收
    CHECK_ERR(RadicalExtension::make(Fraction(2, 1)), MathsError::InvalidExpression);

    // √x·√x 与 x 也是同一个值
    const RadicalExtension squared = (rootX * rootX).unwrap();
    CHECK_TRUE(squared == RadicalExtension(expression("x")));

    // 有理元素之间的运算仍是有理元素
    CHECK_TRUE((two + RadicalExtension(Fraction(3, 1))).unwrap() == RadicalExtension(Fraction(5, 1)));
    CHECK_TRUE((two * RadicalExtension(Fraction(3, 1))).unwrap() == RadicalExtension(Fraction(6, 1)));
  }

  // ---------- 部分代入：把变量换成有理函数（函数套函数需要它） ----------
  {
    // √(x+1) 代入 x = w²  →  √(w²+1)
    const RadicalExtension shifted = RadicalExtension::make(expression("x+1")).unwrap();
    Scope scope;
    CHECK_OK(scope.assign(Variable("x"), expression("w^2")));
    const Result<RadicalExtension> composed = shifted.substitute(scope);
    CHECK_OK(composed);
    CHECK_EQ(composed.unwrap().latex(), std::string("\\sqrt{w^2 + 1}"));
    CHECK_TRUE(composed.unwrap().variables().count(Variable("x")) == 0); // x 已被换掉
    CHECK_TRUE(composed.unwrap().containsVariable(Variable("w")));

    // √x 代入 x = w²+1  →  √(w²+1)：被开方数整条换掉
    Scope widened;
    CHECK_OK(widened.assign(Variable("x"), expression("w^2+1")));
    const Result<RadicalExtension> substituted = radicalX().substitute(widened);
    CHECK_OK(substituted);
    CHECK_EQ(substituted.unwrap().latex(), std::string("\\sqrt{w^2 + 1}"));
    CHECK_TRUE(!substituted.unwrap().containsVariable(Variable("x")));

    // 系数也会被替换：y√x 代入 y = w²  →  w²√x（x 仍在）
    const RadicalExtension withCoefficient =
        RadicalExtension::make({expression("x")}, {expression("0"), expression("y")}).unwrap();
    CHECK_EQ(withCoefficient.latex(), std::string("y\\sqrt{x}"));
    Scope onlyY;
    CHECK_OK(onlyY.assign(Variable("y"), expression("w^2")));
    const Result<RadicalExtension> coefficientReplaced = withCoefficient.substitute(onlyY);
    CHECK_OK(coefficientReplaced);
    CHECK_EQ(coefficientReplaced.unwrap().latex(), std::string("w^2\\sqrt{x}"));
    CHECK_TRUE(coefficientReplaced.unwrap().containsVariable(Variable("w")));
    CHECK_TRUE(coefficientReplaced.unwrap().containsVariable(Variable("x"))); // x 没被代入

    // 没被代入的变量照旧：只代入 w 时 √(x+1) 原样不动
    Scope other;
    CHECK_OK(other.assign(Variable("w"), expression("t^2")));
    const Result<RadicalExtension> untouched = shifted.substitute(other);
    CHECK_OK(untouched);
    CHECK_EQ(untouched.unwrap().latex(), std::string("\\sqrt{x + 1}"));

    // 边界一：替换后被开方数变成常数 → 本类型装不下它的根（√5 属于实代数数）
    Scope number;
    CHECK_OK(number.assign(Variable("x"), expression("4")));
    CHECK_ERR(radicalX().substitute(number), MathsError::InvalidExpression);

    // 边界二：替换后被开方数变成完全平方 → 主根是 |w|，按规矩拒收
    CHECK_ERR(radicalX().substitute(scope), MathsError::RadicandIsSquare);
  }

  TEST_SUMMARY();
}
