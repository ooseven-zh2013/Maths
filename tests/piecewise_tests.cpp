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

RealSet interval(long long low, long long high) { return RealSet::closedInterval(number(low), number(high)).unwrap(); }

} // namespace

int main() {
  std::cout << "=== 分段函数 PiecewiseFunction（绝对值等）测试 ===" << '\n';
  std::cout << std::unitbuf; // 崩溃时也能看到已输出的断言结果

  // ---------- |x| 的构造与结构 ----------
  {
    const PiecewiseFunction absolute = absoluteValue().unwrap();
    CHECK_TRUE(absolute.branchCount() == std::size_t(2));
    CHECK_TRUE(absolute.variable() == Variable("x"));
    CHECK_TRUE(absolute.domain().isRealLine());

    // 正的一支在 [0,+∞)，负的一支在 (−∞,0) —— 两两不交
    CHECK_TRUE(absolute.branch(0).domain().contains(number(0)));
    CHECK_TRUE(absolute.branch(0).domain().contains(number(5)));
    CHECK_TRUE(!absolute.branch(1).domain().contains(number(0)));
    CHECK_TRUE(absolute.branch(1).domain().contains(number(-5)));

    CHECK_EQ(absolute.str(), std::string("{x , x in [0, +inf); -x , x in (-inf, 0)}"));
    CHECK_TRUE(absolute.latex().find("\\begin{cases}") == 0);
    CHECK_TRUE(absolute.latex().find("\\end{cases}") != std::string::npos);

    // 求值：拐点在 0 上，两侧都对
    CHECK_TRUE(absolute.at(number(5)).unwrap() == number(5));
    CHECK_TRUE(absolute.at(number(-5)).unwrap() == number(5));
    CHECK_TRUE(absolute.at(number(0)).unwrap() == number(0));
    CHECK_TRUE(absolute.at(Fraction(-3, 2)).unwrap() == number(3, 2));
  }

  // ---------- |x| 的像集与定义域切割 ----------
  {
    const PiecewiseFunction absolute = absoluteValue().unwrap();
    CHECK_EQ(absolute.range().unwrap().latex(), std::string("[0, +\\infty)"));

    // 在 [−2,1] 上：两支的像分别是 [0,2] 与 (0,1]，并起来是 [0,2]
    CHECK_EQ(absolute.image(interval(-2, 1)).unwrap().latex(), std::string("[0, 2]"));

    // 收窄定义域：|x| 只在 x ≥ 3 上看
    const PiecewiseFunction tail = absolute.restrict(interval(3, 5)).unwrap();
    CHECK_TRUE(tail.branchCount() == std::size_t(1)); // 负的那支被完全切掉了
    CHECK_TRUE(tail.at(number(4)).unwrap() == number(4));
    CHECK_ERR(tail.at(number(-4)), MathsError::OutsideDomain);
    // 单支可降回普通函数；规则是 x，定义域还带着 [3,5] 这个收窄
    CHECK_EQ(tail.toRealFunction().unwrap().ruleLatex(), std::string("x"));
    CHECK_TRUE(tail.toRealFunction().unwrap().domain() == interval(3, 5));
  }

  // ---------- 逐点取大 / 取小 ----------
  {
    // max(x, 1)：x ≥ 1 处取 x，其余取 1
    const PiecewiseFunction upper = maximumOf(functionOf("x"), functionOf("1")).unwrap();
    CHECK_TRUE(upper.branchCount() == std::size_t(2));
    CHECK_TRUE(upper.at(number(3)).unwrap() == number(3));
    CHECK_TRUE(upper.at(number(0)).unwrap() == number(1));
    CHECK_TRUE(upper.at(number(1)).unwrap() == number(1)); // 端点两支给同一个值，无歧义

    // min(x², x)：分界在 x² − x = 0 即 x ∈ {0,1}，在 [0,1] 上取 x²，其余取 x
    const PiecewiseFunction lower = minimumOf(functionOf("x^2"), functionOf("x")).unwrap();
    CHECK_TRUE(lower.at(number(1, 2)).unwrap() == number(1, 4));
    CHECK_TRUE(lower.at(number(3)).unwrap() == number(3));
    CHECK_TRUE(lower.at(number(-2)).unwrap() == number(-2));
    CHECK_EQ(lower.image(interval(0, 1)).unwrap().latex(), std::string("[0, 1]"));

    // max(x, −x) 与 |x| 是同一个函数（两条独立的路径给同一个结果）
    const PiecewiseFunction bigger = maximumOf(functionOf("x"), functionOf("-x")).unwrap();
    CHECK_TRUE(bigger.at(number(-7)).unwrap() == number(7));
    CHECK_TRUE(bigger.at(number(7)).unwrap() == number(7));
    CHECK_TRUE(bigger.domain() == absoluteValue().unwrap().domain());

    // 含两个以上生成元的差要解代数数不等式，不做
    CHECK_ERR(maximumOf(RealFunction::make(radicalOf("x")).unwrap(), RealFunction::make(radicalOf("x+1")).unwrap()),
              MathsError::NotARational);
  }

  // ---------- 逐点取大 / 取小：一边带**一个**根号 ----------
  {
    // √x ≥ x ⟺ x ≤ 1（在 x ≥ 0 上），所以 max 在 [0,1] 上取 √x，之后取 x
    const RealFunction root = RealFunction::make(radicalOf("x")).unwrap();
    const RealFunction ramp = functionOf("x");

    const PiecewiseFunction upper = maximumOf(root, ramp).unwrap();
    CHECK_TRUE(upper.branchCount() == std::size_t(2));
    CHECK_TRUE(upper.at(number(1, 4)).unwrap() == number(1, 2)); // √0.25 = 0.5 > 0.25
    CHECK_TRUE(upper.at(number(4)).unwrap() == number(4));       // 4 > 2
    CHECK_TRUE(upper.at(number(0)).unwrap() == number(0));

    // 两边都有定义才成立：x < 0 时 √x 没定义，max 也没有
    CHECK_ERR(upper.at(number(-1)), MathsError::OutsideDomain);
    CHECK_TRUE(!upper.domain().contains(number(-1)));

    // 取小则反过来
    const PiecewiseFunction lower = minimumOf(root, ramp).unwrap();
    CHECK_TRUE(lower.at(number(1, 4)).unwrap() == number(1, 4)); // 0.25 < 0.5
    CHECK_TRUE(lower.at(number(4)).unwrap() == number(2));
    CHECK_ERR(lower.at(number(-1)), MathsError::OutsideDomain);

    // max(√x, x) · 它自己 = (max)²，顺手验证两支拼得上
    CHECK_TRUE((upper * upper).unwrap().at(number(4)).unwrap() == number(16));
  }

  // ---------- 回归：第二支必须落在「两边都有定义」的地方 ----------
  {
    // max(1/x, x)：x 自己的定义域是整条实轴，但 1/x 在 0 上没有定义 ——
    // 不拦住的话 x = 0 会冒出一个值来
    const PiecewiseFunction hyperbolaMax = maximumOf(functionOf("1/x"), functionOf("x")).unwrap();
    CHECK_ERR(hyperbolaMax.at(number(0)), MathsError::OutsideDomain);
    CHECK_TRUE(!hyperbolaMax.domain().contains(number(0)));
    CHECK_TRUE(hyperbolaMax.domain().contains(number(2)));
    CHECK_TRUE(hyperbolaMax.domain().contains(number(-2)));
  }

  // ---------- 复合 ----------
  {
    const PiecewiseFunction absolute = absoluteValue().unwrap();

    // √|x|：外层带根号也可以，因为 |x| 的两支都是有理函数
    const RealFunction outerRoot = RealFunction::make(radicalOf("y")).unwrap();
    const PiecewiseFunction rootOfAbsolute = compose(outerRoot, absolute).unwrap();
    CHECK_TRUE(rootOfAbsolute.branchCount() == std::size_t(2));
    CHECK_TRUE(rootOfAbsolute.at(number(9)).unwrap() == number(3));
    CHECK_TRUE(rootOfAbsolute.at(number(-9)).unwrap() == number(3));
    CHECK_TRUE(rootOfAbsolute.at(number(0)).unwrap() == number(0));
    // 像集只支持有理规则（√x 的像要逐根号做单调性推理），所以这里明确报错而不是猜
    CHECK_ERR(rootOfAbsolute.range(), MathsError::NotARational);

    // x² ∘ |x| = x²：两支算出来是同一条规则
    const PiecewiseFunction squareOfAbsolute = compose(functionOf("x^2"), absolute).unwrap();
    CHECK_TRUE(squareOfAbsolute.at(number(-3)).unwrap() == number(9));

    // |x| ∘ (x−1) = |x−1|：内层是单规则函数
    const PiecewiseFunction shiftedAbsolute = absolute.compose(functionOf("x-1")).unwrap();
    CHECK_TRUE(shiftedAbsolute.at(number(4)).unwrap() == number(3));
    CHECK_TRUE(shiftedAbsolute.at(number(-2)).unwrap() == number(3));
    CHECK_TRUE(shiftedAbsolute.at(number(1)).unwrap() == number(0));

    // |x| ∘ |x| = |x|
    const PiecewiseFunction nestedAbsolute = absolute.compose(absolute).unwrap();
    CHECK_TRUE(nestedAbsolute.at(number(-7)).unwrap() == number(7));

    // 逐支复合：√(y+1) ∘ |x| = √(|x|+1)，两支分别给 √(x+1) 与 √(−x+1)
    const RealFunction outerShifted = RealFunction::make(radicalOf("y+1")).unwrap();
    const PiecewiseFunction shiftedRoot = compose(outerShifted, absolute).unwrap();
    CHECK_TRUE(shiftedRoot.branchCount() == std::size_t(2));
    CHECK_EQ(shiftedRoot.at(number(3)).unwrap().latex(), std::string("2"));
    CHECK_EQ(shiftedRoot.at(number(-3)).unwrap().latex(), std::string("2"));

    // 根式套根式：内层含根号，不做
    CHECK_ERR(compose(outerRoot, PiecewiseFunction::make({RealFunction::make(radicalOf("x")).unwrap()}).unwrap()),
              MathsError::NotARational);
  }

  // ---------- 四则：分支两两配对 ----------
  {
    const PiecewiseFunction absolute = absoluteValue().unwrap();

    // |x| + 1：仍在两支上
    const PiecewiseFunction shifted = (absolute + functionOf("1")).unwrap();
    CHECK_TRUE(shifted.branchCount() == std::size_t(2));
    CHECK_TRUE(shifted.at(number(-4)).unwrap() == number(5));
    CHECK_TRUE(shifted.at(number(4)).unwrap() == number(5));

    // |x| · |x| = x²：两支的规则都化成 x²，于是求值时负半轴也给正数
    const PiecewiseFunction squared = (absolute * absolute).unwrap();
    CHECK_TRUE(squared.at(number(-3)).unwrap() == number(9));
    CHECK_TRUE(squared.at(number(3)).unwrap() == number(9));

    // |x| − |x| = 0：差在每一支上都是零
    const PiecewiseFunction cancelled = (absolute - absolute).unwrap();
    CHECK_TRUE(cancelled.at(number(-3)).unwrap() == number(0));
    CHECK_TRUE(cancelled.at(number(3)).unwrap() == number(0));

    // |x| / |x|：x = 0 那点分母为零，被剔出定义域
    const PiecewiseFunction ratio = (absolute / absolute).unwrap();
    CHECK_TRUE(ratio.at(number(3)).unwrap() == number(1));
    CHECK_ERR(ratio.at(number(0)), MathsError::OutsideDomain);

    // 取负与数乘
    CHECK_TRUE(absolute.negate().at(number(3)).unwrap() == number(-3));
    CHECK_TRUE(absolute.scaledBy(Fraction(2, 1)).unwrap().at(number(-3)).unwrap() == number(6));
  }

  // ---------- 归一化：先写的分支优先 ----------
  {
    // 后写的分支被前面整条盖住 → 只剩一支
    const PiecewiseFunction covered = PiecewiseFunction::make({functionOf("x"), functionOf("x^2")}).unwrap();
    CHECK_TRUE(covered.branchCount() == std::size_t(1));
    CHECK_TRUE(covered.at(number(-2)).unwrap() == number(-2));

    // 部分重叠：先写 [0,2] 上的 1，再写 [1,3] 上的 2 → 第二支只剩 (2,3]
    const PiecewiseFunction partial =
        PiecewiseFunction::make({RealFunction::make(expression("1"), interval(0, 2)).unwrap(),
                                 RealFunction::make(expression("2"), interval(1, 3)).unwrap()})
            .unwrap();
    CHECK_TRUE(partial.branchCount() == std::size_t(2));
    CHECK_TRUE(partial.at(number(1)).unwrap() == number(1)); // 先写的赢
    CHECK_TRUE(partial.at(number(3)).unwrap() == number(2));
    CHECK_TRUE(!partial.domain().contains(number(4)));

    // 空定义域的分支直接丢掉；全被丢掉就不是函数
    CHECK_ERR(PiecewiseFunction::make({RealFunction::make(expression("x"), RealSet::empty()).unwrap()}),
              MathsError::InvalidExpression);
  }

  // ---------- 拒绝的输入 ----------
  {
    // 分支之间自变量不同名
    CHECK_ERR(PiecewiseFunction::make({functionOf("x"), functionOf("y^2")}), MathsError::NotUnivariate);

    // 常函数分支不参与自变量约束，可以混
    const PiecewiseFunction mixed =
        PiecewiseFunction::make(
            {RealFunction::make(expression("y"), interval(0, 1)).unwrap(), RealFunction::constant(Fraction(7, 1))})
            .unwrap();
    CHECK_TRUE(mixed.branchCount() == std::size_t(2));
    CHECK_TRUE(mixed.at(number(5)).unwrap() == number(7));

    // 多分支不能降回普通函数
    CHECK_ERR(absoluteValue().unwrap().toRealFunction(), MathsError::InvalidExpression);

    // 空定义域的「函数」不能参与构造
    CHECK_ERR(PiecewiseFunction::make({}), MathsError::InvalidExpression);
  }

  TEST_SUMMARY();
}
