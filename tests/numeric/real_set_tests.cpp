#include "../check.hpp"

#include <iostream>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

// 由系数构造一元多项式：a0 + a1 x + a2 x^2 + …
UnivariatePolynomial polynomialOf(const std::vector<long long> &coefficients) {
  std::vector<Fraction> values;
  for (const long long coefficient : coefficients) {
    values.emplace_back(coefficient, 1LL);
  }
  return UnivariatePolynomial(std::move(values));
}

bool isNegativeInfinity(const Bound &bound) { return bound.isInfinite() && bound.infiniteDirection() < 0; }

bool isPositiveInfinity(const Bound &bound) { return bound.isInfinite() && bound.infiniteDirection() > 0; }

} // namespace

int main() {
  std::cout << "=== 实点集 RealSet 测试 ===" << '\n';
  std::cout << std::unitbuf;

  // ---------- 实根的精确隔离 ----------
  {
    const RealAlgebraicNumber rootTwo = RealAlgebraicNumber::parse("\\sqrt{2}").unwrap();

    CHECK_TRUE(RealAlgebraicNumber::realRoots(polynomialOf({-1, 0, 1})).size() == std::size_t(2)); // x²−1
    const std::vector<RealAlgebraicNumber> rootsOfX2Minus2 = RealAlgebraicNumber::realRoots(polynomialOf({-2, 0, 1}));
    CHECK_TRUE(rootsOfX2Minus2.size() == std::size_t(2));
    CHECK_TRUE(rootsOfX2Minus2[0] == -rootTwo); // 升序：先 −√2
    CHECK_TRUE(rootsOfX2Minus2[1] == rootTwo);

    CHECK_TRUE(RealAlgebraicNumber::realRoots(polynomialOf({1, 0, 1})).empty()); // x²+1 无实根
    CHECK_TRUE(RealAlgebraicNumber::realRoots(polynomialOf({5})).empty());       // 常数
    CHECK_TRUE(RealAlgebraicNumber::realRoots(UnivariatePolynomial()).empty());  // 零多项式

    // 重根只算一个（平方自由化先做掉了）
    const std::vector<RealAlgebraicNumber> square = RealAlgebraicNumber::realRoots(polynomialOf({1, -2, 1})); // (x−1)²
    CHECK_TRUE(square.size() == std::size_t(1));
    CHECK_TRUE(square[0] == Fraction(1, 1));

    // 混合：x³−x = x(x−1)(x+1)，含一个有理根 0
    const std::vector<RealAlgebraicNumber> cubic = RealAlgebraicNumber::realRoots(polynomialOf({0, -1, 0, 1}));
    CHECK_TRUE(cubic.size() == std::size_t(3));
    CHECK_TRUE(cubic[0] == Fraction(-1, 1));
    CHECK_TRUE(cubic[1] == Fraction(0, 1));
    CHECK_TRUE(cubic[2] == Fraction(1, 1));

    // 无理根也精确：x²−x−1 的两个根是 (1±√5)/2
    const std::vector<RealAlgebraicNumber> golden = RealAlgebraicNumber::realRoots(polynomialOf({-1, -1, 1}));
    CHECK_TRUE(golden.size() == std::size_t(2));
    CHECK_TRUE(golden[0] == RealAlgebraicNumber::parse("\\frac{1-\\sqrt{5}}{2}").unwrap());
    CHECK_TRUE(golden[1] == RealAlgebraicNumber::parse("\\frac{1+\\sqrt{5}}{2}").unwrap());
  }

  // ---------- 集合的规范化 ----------
  {
    // 重叠合并
    const RealSet merged = RealSet::make({Interval{Bound::finite(RealAlgebraicNumber(Fraction(0, 1)), true),
                                                   Bound::finite(RealAlgebraicNumber(Fraction(2, 1)), true)},
                                          Interval{Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), true),
                                                   Bound::finite(RealAlgebraicNumber(Fraction(3, 1)), true)}})
                               .unwrap();
    CHECK_TRUE(merged.intervals().size() == std::size_t(1));
    CHECK_TRUE(merged.intervals()[0].lower.value() == Fraction(0, 1));
    CHECK_TRUE(merged.intervals()[0].upper.value() == Fraction(3, 1));

    // 相接且两端都取到 → 合并成一个
    const RealSet touching = RealSet::make({Interval{Bound::finite(RealAlgebraicNumber(Fraction(0, 1)), true),
                                                     Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), true)},
                                            Interval{Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), true),
                                                     Bound::finite(RealAlgebraicNumber(Fraction(2, 1)), true)}})
                                 .unwrap();
    CHECK_TRUE(touching.intervals().size() == std::size_t(1));

    // 相接但有一端没取到 → 不能合并（否则会把那个点算进去）
    const RealSet separated = RealSet::make({Interval{Bound::finite(RealAlgebraicNumber(Fraction(0, 1)), false),
                                                      Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), false)},
                                             Interval{Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), false),
                                                      Bound::finite(RealAlgebraicNumber(Fraction(2, 1)), false)}})
                                  .unwrap();
    CHECK_TRUE(separated.intervals().size() == std::size_t(2));
    CHECK_TRUE(!separated.contains(RealAlgebraicNumber(Fraction(1, 1)))); // 1 不在集合里

    // 非法区间
    CHECK_ERR(RealSet::make({Interval{Bound::finite(RealAlgebraicNumber(Fraction(2, 1)), true),
                                      Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), true)}}),
              MathsError::InvalidRange);
    CHECK_ERR(RealSet::make({Interval{Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), false),
                                      Bound::finite(RealAlgebraicNumber(Fraction(1, 1)), false)}}),
              MathsError::InvalidRange); // 退化成一点必须两端都取到
  }

  // ---------- 交、并、补 ----------
  {
    const RealSet first =
        RealSet::closedInterval(RealAlgebraicNumber(Fraction(0, 1)), RealAlgebraicNumber(Fraction(2, 1))).unwrap();
    const RealSet second =
        RealSet::closedInterval(RealAlgebraicNumber(Fraction(1, 1)), RealAlgebraicNumber(Fraction(3, 1))).unwrap();

    const RealSet intersection = first.intersect(second).unwrap();
    CHECK_TRUE(intersection.intervals().size() == std::size_t(1));
    CHECK_TRUE(intersection.intervals()[0].lower.value() == Fraction(1, 1));
    CHECK_TRUE(intersection.intervals()[0].upper.value() == Fraction(2, 1));

    const RealSet united = first.unite(second);
    CHECK_TRUE(united.intervals().size() == std::size_t(1));
    CHECK_TRUE(united.intervals()[0].lower.value() == Fraction(0, 1));
    CHECK_TRUE(united.intervals()[0].upper.value() == Fraction(3, 1));

    CHECK_TRUE(
        first
            .intersect(RealSet::closedInterval(RealAlgebraicNumber(Fraction(5, 1)), RealAlgebraicNumber(Fraction(6, 1)))
                           .unwrap())
            .unwrap()
            .isEmpty());

    // 补集：[0,2] 的补是 (−∞,0) ∪ (2,+∞)
    const RealSet complement = first.complement().unwrap();
    CHECK_TRUE(complement.intervals().size() == std::size_t(2));
    CHECK_TRUE(isNegativeInfinity(complement.intervals()[0].lower));
    CHECK_TRUE(!complement.intervals()[0].upper.isClosed());
    CHECK_TRUE(complement.intervals()[0].upper.value() == Fraction(0, 1));
    CHECK_TRUE(isPositiveInfinity(complement.intervals()[1].upper));
    CHECK_TRUE(!complement.intervals()[1].lower.isClosed());

    // 补两次回到自己
    const RealSet restored = complement.complement().unwrap();
    CHECK_TRUE(restored.intervals().size() == std::size_t(1));
    CHECK_TRUE(restored.intervals()[0].lower.value() == Fraction(0, 1));
    CHECK_TRUE(restored.intervals()[0].upper.value() == Fraction(2, 1));

    // 单点的补：ℝ \ {0}
    const RealSet withoutZero = RealSet::point(RealAlgebraicNumber(Fraction(0, 1))).unwrap().complement().unwrap();
    CHECK_TRUE(withoutZero.intervals().size() == std::size_t(2));
    CHECK_TRUE(!withoutZero.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(withoutZero.contains(RealAlgebraicNumber(Fraction(1, 2))));
    CHECK_TRUE(withoutZero.contains(RealAlgebraicNumber(Fraction(-100, 1))));

    // 全集的补是空集
    CHECK_TRUE(RealSet::realLine().complement().unwrap().isEmpty());
    CHECK_TRUE(RealSet::empty().complement().unwrap().isRealLine());
  }

  // ---------- 解不等式 ----------
  {
    const UnivariatePolynomial xSquaredMinusOne = polynomialOf({-1, 0, 1});

    // x²−1 > 0 → (−∞,−1) ∪ (1,+∞)
    const RealSet greater = RealSet::solve(xSquaredMinusOne, Relation::Greater).unwrap();
    CHECK_EQ(greater.latex(), std::string("(-\\infty, -1) \\cup (1, +\\infty)"));
    CHECK_TRUE(greater.contains(RealAlgebraicNumber(Fraction(2, 1))));
    CHECK_TRUE(!greater.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(!greater.contains(RealAlgebraicNumber(Fraction(1, 1)))); // 端点不取

    // x²−1 ≥ 0 → 端点收进来
    const RealSet greaterEqual = RealSet::solve(xSquaredMinusOne, Relation::GreaterEqual).unwrap();
    CHECK_TRUE(greaterEqual.contains(RealAlgebraicNumber(Fraction(1, 1))));
    CHECK_TRUE(greaterEqual.contains(RealAlgebraicNumber(Fraction(-1, 1))));

    // x²−1 < 0 → (−1,1)
    const RealSet less = RealSet::solve(xSquaredMinusOne, Relation::Less).unwrap();
    CHECK_EQ(less.latex(), std::string("(-1, 1)"));
    CHECK_TRUE(less.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(!less.contains(RealAlgebraicNumber(Fraction(1, 1))));

    // x²−1 ≤ 0 → [−1,1]
    CHECK_EQ(RealSet::solve(xSquaredMinusOne, Relation::LessEqual).unwrap().latex(), std::string("[-1, 1]"));

    // x²−1 = 0 → {−1} ∪ {1}
    const RealSet equal = RealSet::solve(xSquaredMinusOne, Relation::Equal).unwrap();
    CHECK_TRUE(equal.intervals().size() == std::size_t(2));
    CHECK_EQ(equal.latex(), std::string("\\{-1\\} \\cup \\{1\\}"));

    // x²−1 ≠ 0 → ℝ 去掉两个点，共三段（中间那一段也在）
    const RealSet notEqual = RealSet::solve(xSquaredMinusOne, Relation::NotEqual).unwrap();
    CHECK_TRUE(notEqual.intervals().size() == std::size_t(3));
    CHECK_TRUE(notEqual.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(!notEqual.contains(RealAlgebraicNumber(Fraction(1, 1))));
    CHECK_TRUE(!notEqual.contains(RealAlgebraicNumber(Fraction(-1, 1))));

    // 恒真 / 恒假
    CHECK_TRUE(RealSet::solve(polynomialOf({1, 0, 1}), Relation::Greater).unwrap().isRealLine());     // x²+1 > 0
    CHECK_TRUE(RealSet::solve(polynomialOf({1, 0, 1}), Relation::Less).unwrap().isEmpty());           // x²+1 < 0
    CHECK_TRUE(RealSet::solve(polynomialOf({2}), Relation::Greater).unwrap().isRealLine());           // 2 > 0
    CHECK_TRUE(RealSet::solve(polynomialOf({-2}), Relation::Greater).unwrap().isEmpty());             // −2 > 0
    CHECK_TRUE(RealSet::solve(UnivariatePolynomial(), Relation::GreaterEqual).unwrap().isRealLine()); // 0 ≥ 0
    CHECK_TRUE(RealSet::solve(UnivariatePolynomial(), Relation::Less).unwrap().isEmpty());            // 0 < 0

    // 重根：(x−1)² > 0 → ℝ \ {1}
    const RealSet squareGreater = RealSet::solve(polynomialOf({1, -2, 1}), Relation::Greater).unwrap();
    CHECK_EQ(squareGreater.latex(), std::string("(-\\infty, 1) \\cup (1, +\\infty)"));

    // 三次：x³−x ≥ 0 → [−1,0] ∪ [1,+∞)
    const RealSet cubic = RealSet::solve(polynomialOf({0, -1, 0, 1}), Relation::GreaterEqual).unwrap();
    CHECK_EQ(cubic.latex(), std::string("[-1, 0] \\cup [1, +\\infty)"));
    CHECK_TRUE(cubic.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(cubic.contains(RealAlgebraicNumber(Fraction(-1, 2)))); // x³−x 在 (−1,0) 上为正
    CHECK_TRUE(!cubic.contains(RealAlgebraicNumber(Fraction(1, 2)))); // 在 (0,1) 上为负
    CHECK_TRUE(cubic.contains(RealAlgebraicNumber(Fraction(2, 1))));

    // 无理端点：x²−2 ≥ 0 → (−∞,−√2] ∪ [√2,+∞)，端点必须是精确的代数数
    const RealSet irrational = RealSet::solve(polynomialOf({-2, 0, 1}), Relation::GreaterEqual).unwrap();
    const RealAlgebraicNumber rootTwo = RealAlgebraicNumber::parse("\\sqrt{2}").unwrap();
    CHECK_TRUE(irrational.contains(rootTwo));
    CHECK_TRUE(irrational.contains(-rootTwo));
    CHECK_TRUE(!irrational.contains(RealAlgebraicNumber(Fraction(1, 1))));

    // 黄金比：x²−x−1 > 0 → (−∞,(1−√5)/2) ∪ ((1+√5)/2,+∞)
    const RealSet golden = RealSet::solve(polynomialOf({-1, -1, 1}), Relation::Greater).unwrap();
    CHECK_TRUE(golden.intervals().size() == std::size_t(2));
    CHECK_TRUE(!golden.contains(RealAlgebraicNumber(Fraction(0, 1))));
    CHECK_TRUE(golden.contains(RealAlgebraicNumber(Fraction(2, 1))));
    CHECK_TRUE(golden.contains(RealAlgebraicNumber(Fraction(-1, 1))));
  }

  // ---------- 交由：端点值相同但开闭不同（曾经的取错界的 bug）----------
  {
    const RealAlgebraicNumber zero(Fraction(0, 1));
    const RealAlgebraicNumber two(Fraction(2, 1));

    // [0,2] ∩ (0,2] = (0,2]：并列端点上「不取到」更紧，不能把 0 放回来
    const RealSet closedFromZero = RealSet::closedInterval(zero, two).unwrap();
    const RealSet openFromZero =
        RealSet::make({Interval{Bound::finite(zero, false), Bound::finite(two, true)}}).unwrap();
    const RealSet intersected = closedFromZero.intersect(openFromZero).unwrap();
    CHECK_TRUE(!intersected.contains(zero));
    CHECK_TRUE(intersected.contains(two));
    CHECK_EQ(intersected.latex(), std::string("(0, 2]"));

    // 反方向也要对：(0,2] ∩ [0,2] 同上
    CHECK_TRUE(!openFromZero.intersect(closedFromZero).unwrap().contains(zero));

    // 两侧都取到时才保留端点：[0,2] ∩ [0,2] = [0,2]
    CHECK_TRUE(closedFromZero.intersect(closedFromZero).unwrap().contains(zero));

    // 函数定义域那一路最容易撞到：1/x 的天然定义域 ℝ\{0} = (−∞,0) ∪ (0,+∞)，
    // 与 [0,2] 取交必须得到 (0,2]，端点 0 只能来自「不取到」的那一侧
    const RealSet punctured = RealSet::point(zero).unwrap().complement().unwrap();
    const RealSet clipped = punctured.intersect(closedFromZero).unwrap();
    CHECK_TRUE(!clipped.contains(zero));
    CHECK_TRUE(clipped.contains(two));
  }

  // ---------- 相接区间的合并：只看接触点有没有被覆盖 ----------
  {
    const RealAlgebraicNumber zero(Fraction(0, 1));
    const RealAlgebraicNumber one(Fraction(1, 1));

    // (−∞,0) ∪ [0,+∞) 就是整条实轴：接触点 0 被后一侧取到，中间没有洞。
    // 这里曾经因为「要求两侧都取到」而过强地拒绝合并，于是同一个集合与 ℝ 判不等。
    const RealSet negativeOpen =
        RealSet::make({Interval{Bound::negativeInfinity(), Bound::finite(zero, false)}}).unwrap();
    const RealSet positiveClosed =
        RealSet::make({Interval{Bound::finite(zero, true), Bound::positiveInfinity()}}).unwrap();
    CHECK_TRUE(negativeOpen.unite(positiveClosed).isRealLine());
    CHECK_TRUE(negativeOpen.unite(positiveClosed) == RealSet::realLine());

    // (0,1) ∪ (1,2)：1 两侧都不取 → 中间有个洞，**不能**并成 (0,2)
    const RealSet leftOpen = RealSet::make({Interval{Bound::finite(zero, false), Bound::finite(one, false)}}).unwrap();
    const RealSet rightOpen =
        RealSet::make({Interval{Bound::finite(one, false), Bound::finite(RealAlgebraicNumber(Fraction(2, 1)), false)}})
            .unwrap();
    CHECK_EQ(leftOpen.unite(rightOpen).latex(), std::string("(0, 1) \\cup (1, 2)"));

    // (0,1] ∪ (1,2)：1 被前一侧取到 → 并成 (0,2)
    const RealSet leftClosed = RealSet::make({Interval{Bound::finite(zero, false), Bound::finite(one, true)}}).unwrap();
    CHECK_EQ(leftClosed.unite(rightOpen).latex(), std::string("(0, 2)"));

    // (0,1) ∪ [1,2]：1 被后一侧取到 → 并成 (0,2]（下端仍继承左区间的不取到）
    const RealSet rightClosed =
        RealSet::make({Interval{Bound::finite(one, true), Bound::finite(RealAlgebraicNumber(Fraction(2, 1)), true)}})
            .unwrap();
    CHECK_EQ(leftOpen.unite(rightClosed).latex(), std::string("(0, 2]"));

    // 孤立的点并进开区间：合并时**下端也要修**，否则 {0} 会被吃掉
    // （排序只比数值，`[0,0]` 与 `(0,1)` 的先后不确定，轮到点在后面时就会漏）
    CHECK_EQ(RealSet::point(zero).unwrap().unite(leftOpen).latex(), std::string("[0, 1)"));
    CHECK_EQ(leftOpen.unite(RealSet::point(zero).unwrap()).latex(), std::string("[0, 1)"));
  }

  TEST_SUMMARY();
}
