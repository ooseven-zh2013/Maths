#include "check.hpp"

#include <iostream>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

RationalFunction expression(const char *latex) { return parseExpression(latex).unwrap(); }

RadicalExtension radicalOf(const char *latex) { return RadicalExtension::make(expression(latex)).unwrap(); }

RealFunction functionOf(const char *latex) { return RealFunction::make(expression(latex)).unwrap(); }

RealAlgebraicNumber number(long long numerator, long long denominator = 1) {
  return RealAlgebraicNumber(Fraction(numerator, denominator));
}

RealSet closed(long long low, long long high) { return RealSet::closedInterval(number(low), number(high)).unwrap(); }

} // namespace

int main() {
  std::cout << "=== 一元实函数 RealFunction（集合 → 实数）测试 ===" << std::endl;
  std::cout << std::unitbuf; // 崩溃时也能看到已输出的断言结果

  // ---------- 构造：自变量与定义域都是推出来的 ----------
  {
    const RealFunction polynomial = functionOf("3x^2-1");
    CHECK_TRUE(polynomial.variable() == Variable("x"));
    CHECK_TRUE(polynomial.domain().isRealLine()); // 多项式处处有定义
    CHECK_TRUE(polynomial.isRational());
    CHECK_TRUE(!polynomial.isConstant());

    // 分式：分母的零点自动剔掉
    const RealFunction reciprocal = functionOf("1/(x-1)");
    CHECK_TRUE(reciprocal.domain().intervals().size() == std::size_t(2));
    CHECK_TRUE(reciprocal.domain().contains(number(0)));
    CHECK_TRUE(!reciprocal.domain().contains(number(1))); // x = 1 不在定义域

    // 根号：被开方数 ≥ 0
    const RealFunction radical = RealFunction::make(radicalOf("x^2-1")).unwrap();
    CHECK_TRUE(!radical.isRational());
    CHECK_TRUE(!radical.domain().contains(number(0)));
    CHECK_TRUE(radical.domain().contains(number(1)));
    CHECK_TRUE(radical.domain().contains(number(-3)));
    CHECK_TRUE(radical.domain().contains(number(-1))); // 端点取到
    CHECK_TRUE(radical.domain().intervals().size() == std::size_t(2));

    // 显式给定义域：与天然定义域取交，不是覆盖
    const RealFunction restricted = RealFunction::make(expression("3x^2"), closed(2, 3)).unwrap();
    CHECK_TRUE(restricted.domain() == closed(2, 3));
    CHECK_EQ(restricted.str(), std::string("3 x^2 , x in [2, 3]"));

    // 1/x 配 [0,2]：x = 0 那点仍然不属于函数（取交是逐端点算的，开闭不同也得对）
    const RealFunction clipped = RealFunction::make(expression("1/x"), closed(0, 2)).unwrap();
    CHECK_TRUE(!clipped.domain().contains(number(0)));
    CHECK_TRUE(clipped.domain().contains(number(2)));
    CHECK_TRUE(clipped.domain().contains(number(1, 2)));
    CHECK_TRUE(!clipped.domain().contains(number(-1)));

    // 常函数：规则里没有变量，自变量名推不出来，走 constant
    const RealFunction five = RealFunction::constant(Fraction(5, 1));
    CHECK_TRUE(five.isConstant());
    CHECK_TRUE(five.domain().isRealLine());
    CHECK_TRUE(five.at(number(100)).unwrap() == number(5));

    // 多元：本类型只收一元
    CHECK_ERR(RealFunction::make(expression("x*y")), MathsError::NotUnivariate);

    // 解析出的式子本身就是合理的函数
    CHECK_OK(RealFunction::make(radicalOf("x")));
  }

  // ---------- 求值：定义域外明确报错 ----------
  {
    const RealFunction polynomial = functionOf("x^2+1");
    CHECK_TRUE(polynomial.at(number(3)).unwrap() == number(10));
    CHECK_TRUE(polynomial.at(Fraction(1, 2)).unwrap() == number(5, 4));

    const RealFunction reciprocal = functionOf("1/(x-1)");
    CHECK_TRUE(reciprocal.at(number(3)).unwrap() == number(1, 2));
    CHECK_ERR(reciprocal.at(number(1)), MathsError::OutsideDomain);

    const RealFunction radical = RealFunction::make(radicalOf("x^2-1")).unwrap();
    CHECK_EQ(radical.at(number(2)).unwrap().latex(), std::string("\\sqrt{3}"));
    CHECK_ERR(radical.at(number(0)), MathsError::OutsideDomain); // √(−1) 不属于函数，不是算不出来

    // 代数点：f(x) = √(x²+1) 在 x = √2 处取 √3
    const RealFunction root = RealFunction::make(radicalOf("x^2+1")).unwrap();
    CHECK_EQ(root.at(RealAlgebraicNumber::nthRootOf(Fraction(2, 1), 2).unwrap()).unwrap().latex(),
             std::string("\\sqrt{3}"));
  }

  // ---------- 四则：定义域取交 ----------
  {
    const RealFunction shifted = functionOf("x+1");
    const RealFunction lowered = functionOf("x-1");
    CHECK_TRUE((shifted + lowered).unwrap().at(number(3)).unwrap() == number(6));
    CHECK_TRUE((shifted - lowered).unwrap().at(number(3)).unwrap() == number(2));
    CHECK_TRUE((shifted * lowered).unwrap().at(number(3)).unwrap() == number(8));
    CHECK_TRUE((shifted / lowered).unwrap().at(number(3)).unwrap() == number(2));

    // √x 与 1/(x−2)：两个限制叠在一起
    const RealFunction rootX = RealFunction::make(radicalOf("x")).unwrap();
    const RealFunction pole = functionOf("1/(x-2)");
    const RealFunction sum = (rootX + pole).unwrap();
    CHECK_TRUE(sum.domain().contains(number(0)));
    CHECK_TRUE(sum.domain().contains(number(3)));
    CHECK_TRUE(!sum.domain().contains(number(2)));  // 分母的零点
    CHECK_TRUE(!sum.domain().contains(number(-1))); // 根号的要求
    CHECK_ERR(sum.at(number(2)), MathsError::OutsideDomain);

    // 自变量不同：不猜，明确报错
    CHECK_ERR(shifted + functionOf("y^2"), MathsError::NotUnivariate);

    // 常函数可与任意变量对齐（规则里没有变量，谈不上冲突）
    const RealFunction plusConstant = (shifted + RealFunction::constant(Fraction(2, 1))).unwrap();
    CHECK_TRUE(plusConstant.at(number(1)).unwrap() == number(4));

    // 取负与数乘
    CHECK_TRUE(shifted.negate().at(number(3)).unwrap() == number(-4));
    CHECK_TRUE(shifted.scaledBy(Fraction(3, 1)).unwrap().at(number(3)).unwrap() == number(12));

    // 除零：规则分母为零
    CHECK_ERR(shifted / functionOf("0"), MathsError::DivisionByZero);
  }

  // ---------- 收窄定义域 ----------
  {
    const RealFunction square = functionOf("x^2");
    const RealFunction onUnit = square.restrict(closed(1, 2)).unwrap();
    CHECK_TRUE(onUnit.at(number(1)).unwrap() == number(1));
    CHECK_ERR(onUnit.at(number(3)), MathsError::OutsideDomain);
    CHECK_TRUE(square.at(number(3)).unwrap() == number(9)); // 原函数不受影响（值类型）

    // 收窄是取交，收不回去
    CHECK_TRUE(onUnit.restrict(closed(1, 2)).unwrap().domain() == closed(1, 2));
    CHECK_TRUE(onUnit.restrict(RealSet::realLine()).unwrap().domain() == closed(1, 2));
  }

  // ---------- 复合：内层有理函数 ----------
  {
    // f(y) = √(y+1)，g(x) = x²+1 → √(x²+2)
    const RealFunction outer = RealFunction::make(radicalOf("y+1")).unwrap();
    const RealFunction inner = functionOf("x^2+1");
    const RealFunction composed = outer.compose(inner).unwrap();
    CHECK_TRUE(composed.variable() == Variable("x")); // 自变量取自内层
    CHECK_TRUE(composed.domain().isRealLine());       // 被开方数恒正
    CHECK_EQ(composed.at(number(1)).unwrap().latex(), std::string("\\sqrt{3}"));

    // 外层被收窄过时，定义域要走拉回：dom f = [0,3]，g(x) = x²+1
    // 于是 {x : 0 ≤ x²+1 ≤ 3} = [−√2, √2]（端点是无理数也照常精确）
    const RealFunction narrowed = outer.restrict(closed(0, 3)).unwrap();
    const RealFunction limited = narrowed.compose(inner).unwrap();
    CHECK_TRUE(limited.domain().contains(number(1)));
    CHECK_ERR(limited.at(number(2)), MathsError::OutsideDomain);
    CHECK_TRUE(limited.domain().latex().find("\\sqrt{2}") != std::string::npos);

    // 内层含根号 = 根式套根式，不做
    CHECK_ERR(outer.compose(RealFunction::make(radicalOf("x")).unwrap()), MathsError::NotARational);

    // 复合后被开方数变成完全平方 → 那正是 |x|，拒收
    const RealFunction bareRoot = RealFunction::make(radicalOf("y")).unwrap();
    CHECK_ERR(bareRoot.compose(functionOf("x^2")), MathsError::RadicandIsSquare);
  }

  // ---------- 拉回：把集合沿函数拉回去 ----------
  {
    const RealFunction square = functionOf("x^2");
    // x² ∈ [1,4] → [−2,−1] ∪ [1,2]
    const Result<RealSet> pulled = square.preimage(closed(1, 4));
    CHECK_OK(pulled);
    CHECK_TRUE(pulled.unwrap().intervals().size() == std::size_t(2));
    CHECK_TRUE(pulled.unwrap().contains(number(-2)));
    CHECK_TRUE(pulled.unwrap().contains(number(-1)));
    CHECK_TRUE(pulled.unwrap().contains(number(0)) == false);
    CHECK_TRUE(pulled.unwrap().contains(number(2)));

    // 单点目标：x² = 4 → {−2, 2}
    const Result<RealSet> roots = square.preimage(RealSet::point(number(4)).unwrap());
    CHECK_OK(roots);
    CHECK_TRUE(roots.unwrap().contains(number(2)));
    CHECK_TRUE(roots.unwrap().contains(number(-2)));
    CHECK_TRUE(!roots.unwrap().contains(number(0)));

    // 拉回整条实轴 = 定义域本身
    CHECK_TRUE(square.preimage(RealSet::realLine()).unwrap() == square.domain());

    // 拉回先与定义域取交：√(x²−1) 的定义域是 (−∞,−1] ∪ [1,+∞)
    // 所以「值 ∈ [0,10]」的拉回不会把 x = 0 拉回来
    const RealFunction radical = RealFunction::make(radicalOf("x^2-1")).unwrap();
    CHECK_ERR(radical.preimage(closed(0, 10)), MathsError::NotARational); // 含根号：不做

    // 端点不是有理数：进不了多项式系数，明确报错
    const RealSet irrationalPoint = RealSet::point(RealAlgebraicNumber::nthRootOf(Fraction(2, 1), 2).unwrap()).unwrap();
    CHECK_ERR(square.preimage(irrationalPoint), MathsError::NotARational);

    // 有理端点的分式拉回：1/(x−1) ∈ [1,2] → x−1 ∈ [1/2,1] → x ∈ [3/2, 2]
    const RealFunction reciprocal = functionOf("1/(x-1)");
    const Result<RealSet> range = reciprocal.preimage(closed(1, 2));
    CHECK_OK(range);
    CHECK_TRUE(range.unwrap().contains(number(3, 2)));
    CHECK_TRUE(range.unwrap().contains(number(2)));
    CHECK_TRUE(!range.unwrap().contains(number(3, 2) - Fraction(1, 100))); // 下界确实被卡住
  }

  // ---------- 像集：f(S) ----------
  {
    // 经典值域题：x/(x²+1) 在 ℝ 上 → [−1/2, 1/2]（临界点 x = ±1 给出两个端点）
    const RealFunction bell = functionOf("x/(x^2+1)");
    CHECK_EQ(bell.range().unwrap().latex(), std::string("[-\\frac{1}{2}, \\frac{1}{2}]"));

    // 1/x 的值域 = ℝ\{0}：两块各自的像分别是 (0,+∞) 与 (−∞,0)，都是开端点
    const RealFunction reciprocal = functionOf("1/x");
    CHECK_EQ(reciprocal.range().unwrap().latex(), std::string("(-\\infty, 0) \\cup (0, +\\infty)"));

    // x² → [0,+∞)：0 是临界点上的值，取到；两侧无穷远都跑向 +∞
    CHECK_EQ(functionOf("x^2").range().unwrap().latex(), std::string("[0, +\\infty)"));
    CHECK_EQ(functionOf("x^2-1").range().unwrap().latex(), std::string("[-1, +\\infty)"));
    CHECK_EQ(functionOf("x^3").range().unwrap().latex(), std::string("\\mathbb{R}")); // 三次：值域是整条实轴

    const RealFunction square = functionOf("x^2");
    CHECK_EQ(square.image(closed(1, 2)).unwrap().latex(), std::string("[1, 4]")); // 闭区间 → 闭
    CHECK_EQ(square.image(RealSet::realLine()).unwrap().latex(), std::string("[0, +\\infty)"));

    // 开区间 → 开：端点只是极限，取不到
    const RealSet openUnit =
        RealSet::make({Interval{Bound::finite(number(1), false), Bound::finite(number(2), false)}}).unwrap();
    CHECK_EQ(square.image(openUnit).unwrap().latex(), std::string("(1, 4)"));

    // 单调函数在闭区间上：像就是两端点之间
    CHECK_EQ(functionOf("x+1").image(closed(0, 1)).unwrap().latex(), std::string("[1, 2]"));

    // 定义域外的那部分自动切掉：1/x 限制在 [−1,1] 上，像是 (−∞,−1] ∪ [1,+∞)
    const RealFunction hyperbola = functionOf("1/x");
    CHECK_EQ(hyperbola.image(closed(-1, 1)).unwrap().latex(), std::string("(-\\infty, -1] \\cup [1, +\\infty)"));

    // 空集进，空集出
    CHECK_TRUE(square.image(RealSet::empty()).unwrap().isEmpty());

    // 常函数：像是单点集
    CHECK_EQ(RealFunction::constant(Fraction(5, 1)).range().unwrap().latex(), std::string("\\{5\\}"));

    // 含根号的规则不做（要么逐根号做单调性推理，要么先把根号消掉）
    CHECK_ERR(RealFunction::make(radicalOf("x")).unwrap().range(), MathsError::NotARational);

    // 1/(x²+1)：极小值只是两端的极限（0，取不到），极大值 1 在临界点 x = 0 上取到
    CHECK_EQ(functionOf("1/(x^2+1)").range().unwrap().latex(), std::string("(0, 1]"));

    // x³−3x 在 [−1,1] 上：两个临界点 ±1 恰好是闭端点
    CHECK_EQ(functionOf("x^3-3x").image(closed(-1, 1)).unwrap().latex(), std::string("[-2, 2]"));

    // x/(x²−1)：中间那块 (−1,1) 从 +∞ 掉到 −∞，没有临界点 ——
    // 「候选值一个都没有、两头都跑向无穷」这条路要能正确地给出整条实轴
    CHECK_EQ(functionOf("x/(x^2-1)").range().unwrap().latex(), std::string("\\mathbb{R}"));
  }

  // ---------- 输出 ----------
  {
    const RealFunction polynomial = functionOf("x^2");
    CHECK_EQ(polynomial.str(), std::string("x^2"));
    CHECK_EQ(polynomial.latex(), std::string("x^2"));

    const RealFunction reciprocal = functionOf("1/(x-1)");
    CHECK_EQ(reciprocal.ruleLatex(), std::string("\\frac{1}{x - 1}"));
    // 定义域不是整条实轴时把 `x ∈ …` 缀上 —— 那正是它与「无限制版本」的区别
    CHECK_TRUE(reciprocal.latex().find("\\in") != std::string::npos);
    CHECK_TRUE(reciprocal.latex().find("\\frac{1}{x - 1}") == 0);
    CHECK_TRUE(reciprocal.domainLatex().find("\\cup") != std::string::npos);
  }

  TEST_SUMMARY();
}
