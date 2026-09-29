#include "check.hpp"

#include <iostream>
#include <string>

import maths;

using namespace maths;

namespace {

RationalFunction expression(const char *latex) { return parseExpression(latex).unwrap(); }

RealSet withoutZero() { return RealSet::point(RealAlgebraicNumber(Fraction(0, 1))).unwrap().complement().unwrap(); }

// [0, +∞)
RealSet nonNegative() {
  return RealSet::make({Interval{Bound::finite(RealAlgebraicNumber(Fraction(0, 1)), true), Bound::positiveInfinity()}})
      .unwrap();
}

// (−∞, 1]
RealSet atMostOne() {
  return RealSet::make({Interval{Bound::negativeInfinity(), Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), true)}})
      .unwrap();
}

} // namespace

int main() {
  std::cout << "=== 取值范围约束 Constraint / RangeConstraint 测试 ===" << std::endl;
  std::cout << std::unitbuf;

  // ---------- 一元有理函数的不等式 ----------
  {
    // 1/x > 0 → (0, +∞)：分母的零点不在定义域里，也就不是解
    const RealSet positive = solveInequality(expression("1/x"), Relation::Greater).unwrap();
    CHECK_EQ(positive.latex(), std::string("(0, +\\infty)"));
    CHECK_TRUE(positive.contains(RealAlgebraicNumber(Fraction(1, 2))));
    CHECK_TRUE(!positive.contains(RealAlgebraicNumber(Fraction(0, 1)))); // 无定义点
    CHECK_TRUE(!positive.contains(RealAlgebraicNumber(Fraction(-1, 1))));

    // 1/x ≥ 0 → 仍是 (0, +∞)：0 处无定义，不能因为是「取等」就收进来
    CHECK_EQ(solveInequality(expression("1/x"), Relation::GreaterEqual).unwrap().latex(), std::string("(0, +\\infty)"));

    // 1/x < 0 → (−∞, 0)
    CHECK_EQ(solveInequality(expression("1/x"), Relation::Less).unwrap().latex(), std::string("(-\\infty, 0)"));

    // (x−1)/(x+2) ≥ 0 → (−∞,−2) ∪ [1,+∞)
    const RealSet greaterEqual = solveInequality(expression("(x-1)/(x+2)"), Relation::GreaterEqual).unwrap();
    CHECK_TRUE(greaterEqual.intervals().size() == std::size_t(2));
    CHECK_TRUE(greaterEqual.contains(RealAlgebraicNumber(Fraction(1, 1))));   // 分子零点取到
    CHECK_TRUE(!greaterEqual.contains(RealAlgebraicNumber(Fraction(-2, 1)))); // 分母零点不取到
    CHECK_TRUE(greaterEqual.contains(RealAlgebraicNumber(Fraction(-3, 1))));
    CHECK_TRUE(!greaterEqual.contains(RealAlgebraicNumber(Fraction(0, 1))));

    // (x−1)/(x+2) = 0 → {1}（分母零点不是解）
    const RealSet equal = solveInequality(expression("(x-1)/(x+2)"), Relation::Equal).unwrap();
    CHECK_EQ(equal.latex(), std::string("\\{1\\}"));

    // 全实轴恒正：1/(x²+1) > 0 → ℝ
    CHECK_TRUE(solveInequality(expression("1/(x^2+1)"), Relation::Greater).unwrap().isRealLine());

    // 常数式子
    CHECK_TRUE(solveInequality(RationalFunction(Fraction(3, 1)), Relation::Greater).unwrap().isRealLine());
    CHECK_TRUE(solveInequality(RationalFunction(Fraction(-1, 1)), Relation::Greater).unwrap().isEmpty());
    CHECK_TRUE(solveInequality(RationalFunction(Fraction(0, 1)), Relation::GreaterEqual).unwrap().isRealLine());
    CHECK_TRUE(solveInequality(RationalFunction(Fraction(0, 1)), Relation::Greater).unwrap().isEmpty());

    // 多变量：解集是多维点集，本模块不做
    CHECK_ERR(solveInequality(expression("1/(x*y)"), Relation::Greater), MathsError::InvalidExpression);
  }

  // ---------- 定义域 ----------
  {
    const RealSet first = domainOf(expression("1/x")).unwrap();
    CHECK_TRUE(first.intervals().size() == std::size_t(2));
    CHECK_TRUE(!first.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(first.contains(RealAlgebraicNumber(Fraction(7, 3))));

    // 1/(x²−1)：去掉两个点 → 三段
    const RealSet second = domainOf(expression("1/(x^2-1)")).unwrap();
    CHECK_TRUE(second.intervals().size() == std::size_t(3));
    CHECK_TRUE(!second.contains(RealAlgebraicNumber(Fraction(1, 1))));
    CHECK_TRUE(!second.contains(RealAlgebraicNumber(Fraction(-1, 1))));
    CHECK_TRUE(second.contains(RealAlgebraicNumber(Fraction(0, 1))));

    // 分母恒不为零 → 整条实轴
    CHECK_TRUE(domainOf(expression("x/(x^2+1)")).unwrap().isRealLine());
    CHECK_TRUE(domainOf(expression("x^2+1")).unwrap().isRealLine()); // 常数分母

    CHECK_ERR(domainOf(expression("1/(x*y)")), MathsError::InvalidExpression);
  }

  // ---------- 化简丢掉的约束 → 取值范围约束 ----------
  {
    // (x²+x)/(x²−x) 约掉 x 之后隐含 x ≠ 0
    const RationalFunction simplified = expression("(x^2+x)/(x^2-x)");
    CHECK_TRUE(simplified.discardedConstraints().count(Variable("x")) == 1);

    const std::vector<RangeConstraint> constraints = constraintsOf(simplified);
    CHECK_TRUE(constraints.size() == std::size_t(1));
    CHECK_TRUE(constraints[0].variable() == Variable("x"));
    CHECK_TRUE(constraints[0].admits(RealAlgebraicNumber(Fraction(1, 1))));
    CHECK_TRUE(!constraints[0].admits(RealAlgebraicNumber(Fraction(0, 1)))); // 约掉 x 等价于 x ≠ 0
    CHECK_TRUE(constraints[0].allowed().intervals().size() == std::size_t(2));

    // 没有约分的式子没有这类约束
    CHECK_TRUE(constraintsOf(expression("x+1")).empty());

    // 渲染
    CHECK_EQ(constraints[0].latex(), std::string("x \\in (-\\infty, 0) \\cup (0, +\\infty)"));
  }

  // ---------- Scope 接集合约束 ----------
  {
    Scope scope;
    CHECK_OK(scope.restrict(Variable("x"), withoutZero()));
    CHECK_TRUE(scope.constraints().size() == std::size_t(1));

    // 还没绑定 → 判断不了，不许猜
    CHECK_TRUE(scope.admits(Variable("x")) == Admission::Unknown);

    CHECK_OK(scope.assign(Variable("x"), Fraction(2, 1)));
    CHECK_TRUE(scope.admits(Variable("x")) == Admission::Admits);

    CHECK_OK(scope.assign(Variable("x"), Fraction(0, 1)));
    CHECK_TRUE(scope.admits(Variable("x")) == Admission::Violates); // x = 0 违反 x ≠ 0

    // 绑定值含其它变量 → 判断不了
    CHECK_OK(scope.assign(Variable("x"), expression("y+1")));
    CHECK_TRUE(scope.admits(Variable("x")) == Admission::Unknown);

    // 没有约束的变量：直接算满足
    Scope plain;
    CHECK_OK(plain.assign(Variable("y"), Fraction(0, 1)));
    CHECK_TRUE(plain.admits(Variable("y")) == Admission::Admits);
  }

  // ---------- 重复给约束取交集 ----------
  {
    Scope scope;
    CHECK_OK(scope.restrict(Variable("x"), nonNegative())); // [0, +∞)
    CHECK_OK(scope.restrict(Variable("x"), atMostOne()));   // (−∞, 1]

    CHECK_TRUE(scope.constraints().size() == std::size_t(1)); // 合并成一条
    const RealSet &allowed = scope.constraints()[0].allowed();
    CHECK_TRUE(allowed.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(allowed.contains(RealAlgebraicNumber(Fraction(1, 1))));
    CHECK_TRUE(!allowed.contains(RealAlgebraicNumber(Fraction(2, 1))));
    CHECK_TRUE(!allowed.contains(RealAlgebraicNumber(Fraction(-1, 1))));

    CHECK_OK(scope.assign(Variable("x"), Fraction(3, 1)));
    CHECK_TRUE(scope.admits(Variable("x")) == Admission::Violates); // 3 ∉ [0,1]
    CHECK_OK(scope.assign(Variable("x"), Fraction(1, 2)));
    CHECK_TRUE(scope.admits(Variable("x")) == Admission::Admits);

    // clear 把绑定与约束一起清掉
    scope.clear();
    CHECK_TRUE(scope.constraints().empty());
    CHECK_TRUE(scope.empty());
    CHECK_TRUE(scope.admits(Variable("x")) == Admission::Admits);
  }

  // ---------- 根式表达式的定义域 ----------
  //
  // 定义域是**表达式**的性质：√x·√x 的值等于 x 处处有定义，但作为表达式它要求 x ≥ 0。
  {
    // √x → x ≥ 0
    const RealSet rootX = domainOf(parseRadicalExpression("\\sqrt{x}").unwrap()).unwrap();
    CHECK_TRUE(rootX.contains(RealAlgebraicNumber(Fraction(0, 1)))); // 0 处有定义（√0 = 0）
    CHECK_TRUE(rootX.contains(RealAlgebraicNumber(Fraction(4, 1))));
    CHECK_TRUE(!rootX.contains(RealAlgebraicNumber(Fraction(-1, 1))));

    // √(x²+1) → ℝ（被开方数恒正）
    CHECK_TRUE(domainOf(parseRadicalExpression("\\sqrt{x^2+1}").unwrap()).unwrap().isRealLine());

    // √(x²−1) → (−∞,−1] ∪ [1,+∞)
    const RealSet beyondOne = domainOf(parseRadicalExpression("\\sqrt{x^2-1}").unwrap()).unwrap();
    CHECK_TRUE(beyondOne.intervals().size() == std::size_t(2));
    CHECK_TRUE(beyondOne.contains(RealAlgebraicNumber(Fraction(1, 1)))); // 端点是闭的
    CHECK_TRUE(beyondOne.contains(RealAlgebraicNumber(Fraction(-1, 1))));
    CHECK_TRUE(!beyondOne.contains(RealAlgebraicNumber(Fraction(0, 1))));

    // 1/√x → x > 0（被开方数 ≥ 0 与系数分母 ≠ 0 取交，0 被排掉）
    const RealSet positive = domainOf(parseRadicalExpression("\\frac{1}{\\sqrt{x}}").unwrap()).unwrap();
    CHECK_TRUE(positive.contains(RealAlgebraicNumber(Fraction(1, 2))));
    CHECK_TRUE(!positive.contains(RealAlgebraicNumber(Fraction(0, 1))));

    // √x + √(x+1) → x ≥ 0（两个被开方数的条件取交）
    const RealSet sum = domainOf(parseRadicalExpression("\\sqrt{x}+\\sqrt{x+1}").unwrap()).unwrap();
    CHECK_TRUE(sum.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(!sum.contains(RealAlgebraicNumber(Fraction(-1, 2)))); // x+1 ≥ 0 不满足

    // 多变量：定义域是多维点集，本模块不做
    CHECK_ERR(domainOf(parseRadicalExpression("\\sqrt{x}*y").unwrap()), MathsError::InvalidExpression);
  }

  TEST_SUMMARY();
}
