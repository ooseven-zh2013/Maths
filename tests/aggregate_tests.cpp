#include "check.hpp"

#include <iostream>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

RealAlgebraicNumber number(long long numerator, long long denominator = 1) {
  return RealAlgebraicNumber(Fraction(numerator, denominator));
}

RealAlgebraicNumber radicalTwo() { return RealAlgebraicNumber::nthRootOf(Fraction(2, 1), 2).unwrap(); }

// 有限点集：{a, b, c, …}
RealSet pointSet(std::initializer_list<RealAlgebraicNumber> values) {
  std::vector<Interval> intervals;
  for (const RealAlgebraicNumber &value : values) {
    intervals.push_back(Interval{Bound::finite(value, true), Bound::finite(value, true)});
  }
  return RealSet::make(std::move(intervals)).unwrap();
}

RealSet closed(long long low, long long high) { return RealSet::closedInterval(number(low), number(high)).unwrap(); }

} // namespace

int main() {
  std::cout << "=== 聚合 Aggregate（集合 → 数）测试 ===" << std::endl;
  std::cout << std::unitbuf; // 崩溃时也能看到已输出的断言结果

  // ---------- 求和 / 求积 ----------
  {
    const std::vector<RealAlgebraicNumber> data = {number(1), number(2), number(3)};
    CHECK_TRUE(sumOf(data) == number(6));
    CHECK_TRUE(sumOf({number(2), number(3), number(4)}) == number(9));
    CHECK_TRUE(productOf({number(2), number(3), number(4)}) == number(24));

    // 空和与空积按惯例给 0 与 1，不是错误
    CHECK_TRUE(sumOf(std::vector<RealAlgebraicNumber>()) == number(0));
    CHECK_TRUE(productOf(std::vector<RealAlgebraicNumber>()) == number(1));

    // 有重复的元素照样逐个参与 —— 这正是输入用「序列」而不是「集合」的原因
    CHECK_TRUE(sumOf({number(1), number(1), number(2)}) == number(4));

    // 负数与分数
    CHECK_TRUE(sumOf({number(-3), number(1, 2)}) == number(-5, 2));
    CHECK_TRUE(productOf({number(-2), number(3), number(-1)}) == number(6));
  }

  // ---------- 最大 / 最小 ----------
  {
    const std::vector<RealAlgebraicNumber> data = {number(3), number(1), number(2)};
    CHECK_TRUE(maximumOf(data).unwrap() == number(3));
    CHECK_TRUE(minimumOf(data).unwrap() == number(1));

    // 比较是精确的：√2 ≈ 1.414 与 3/2 一比就知道谁大，不需要浮点
    CHECK_TRUE(maximumOf({number(3, 2), radicalTwo()}).unwrap() == number(3, 2));
    CHECK_TRUE(minimumOf({number(3, 2), radicalTwo()}).unwrap() == radicalTwo());

    // 空序列没有最值
    CHECK_ERR(maximumOf(std::vector<RealAlgebraicNumber>()), MathsError::EmptyCollection);
    CHECK_ERR(minimumOf(std::vector<RealAlgebraicNumber>()), MathsError::EmptyCollection);
  }

  // ---------- 平均 / 方差 / 标准差 ----------
  {
    const std::vector<RealAlgebraicNumber> data = {number(1), number(2), number(3)};
    CHECK_TRUE(meanOf(data).unwrap() == number(2));

    // 总体方差 s² = ((1−2)² + 0 + (3−2)²)/3 = 2/3
    CHECK_TRUE(varianceOf(data).unwrap() == number(2, 3));
    // 样本方差 ÷(n−1) = 1
    CHECK_TRUE(sampleVarianceOf(data).unwrap() == number(1));

    // 标准差就是方差开平方，值是精确的代数数
    CHECK_TRUE(standardDeviationOf(data).unwrap() == RealAlgebraicNumber::nthRootOf(Fraction(2, 3), 2).unwrap());
    CHECK_TRUE(sampleStandardDeviationOf(data).unwrap() == number(1));

    // 方差公式对平移不变、对缩放是平方关系 —— 顺手验一下实现没写反
    const std::vector<RealAlgebraicNumber> shifted = {number(101), number(102), number(103)};
    CHECK_TRUE(varianceOf(shifted).unwrap() == number(2, 3));
    const std::vector<RealAlgebraicNumber> scaled = {number(2), number(4), number(6)};
    CHECK_TRUE(varianceOf(scaled).unwrap() == number(8, 3)); // 2² · 2/3

    // 样本方差的观测数要求
    CHECK_ERR(sampleVarianceOf({number(1)}), MathsError::EmptyCollection);
    CHECK_ERR(meanOf(std::vector<RealAlgebraicNumber>()), MathsError::EmptyCollection);
    CHECK_ERR(varianceOf(std::vector<RealAlgebraicNumber>()), MathsError::EmptyCollection);

    // 全是同一个数时方差为零
    CHECK_TRUE(varianceOf({number(7), number(7), number(7)}).unwrap() == number(0));
  }

  // ---------- 代数数元素 ----------
  {
    const RealAlgebraicNumber root = radicalTwo();

    // √2 + √2 = 2√2（走的是 α+α 的廉价特例）
    CHECK_TRUE(sumOf({root, root}) == number(2) * root);

    // √2 · √2 = 2
    CHECK_TRUE(productOf({root, root}) == number(2));

    // 平均数是精确的代数数，不会退化成小数
    const RealAlgebraicNumber rootThree = RealAlgebraicNumber::nthRootOf(Fraction(3, 1), 2).unwrap();
    CHECK_TRUE(meanOf({root, rootThree}).unwrap() == (root + rootThree) / number(2));

    // √2 与 √3 谁大，精确比较
    CHECK_TRUE(maximumOf({root, rootThree}).unwrap() == rootThree);
  }

  // ---------- 集合语义（点集） ----------
  {
    const RealSet points = pointSet({number(-1), number(2)});
    CHECK_TRUE(sumOf(points).unwrap() == number(1));
    CHECK_TRUE(productOf(points).unwrap() == number(-2));
    CHECK_TRUE(meanOf(points).unwrap() == number(1, 2));
    // ((−1 − 1/2)² + (2 − 1/2)²) / 2 = (9/4 + 9/4)/2 = 9/4
    CHECK_TRUE(varianceOf(points).unwrap() == number(9, 4));

    // 独立分量各自正确的单例
    CHECK_TRUE(sumOf(pointSet({number(5)})).unwrap() == number(5));
    CHECK_ERR(meanOf(RealSet::empty()), MathsError::EmptyCollection);

    // 区间块不是有限点集 —— 那要算积分，明确报错而不是近似
    CHECK_ERR(sumOf(closed(0, 1)), MathsError::NotFiniteSet);
    CHECK_ERR(productOf(closed(0, 1)), MathsError::NotFiniteSet);
    CHECK_ERR(meanOf(closed(0, 1)), MathsError::NotFiniteSet);
    CHECK_ERR(varianceOf(closed(0, 1)), MathsError::NotFiniteSet);

    // 混着点与区间也一样拒收（不是「只看点那部分」）
    CHECK_ERR(sumOf(pointSet({number(1)}).unite(closed(3, 4))), MathsError::NotFiniteSet);
  }

  // ---------- 上/下确界 ----------
  {
    // 有限点集：确界就是最值，而且取得到
    const Bound low = infimumOf(pointSet({number(-1), number(2)})).unwrap();
    const Bound high = supremumOf(pointSet({number(-1), number(2)})).unwrap();
    CHECK_TRUE(!low.isInfinite() && !high.isInfinite());
    CHECK_TRUE(low.value() == number(-1));
    CHECK_TRUE(high.value() == number(2));

    // 开端点：上确界 1 不属于 [0,1) —— 所以名字用 sup 而不是 max
    const RealSet halfOpen =
        RealSet::make({Interval{Bound::finite(number(0), true), Bound::finite(number(1), false)}}).unwrap();
    CHECK_TRUE(supremumOf(halfOpen).unwrap().value() == number(1));
    CHECK_TRUE(!halfOpen.contains(number(1))); // 确实取不到

    // 无界：上确界是 +∞，那是个合法答案，用 Bound 装得下
    const Bound infinite = supremumOf(RealSet::realLine()).unwrap();
    CHECK_TRUE(infinite.isInfinite());
    CHECK_TRUE(infinite.infiniteDirection() > 0);
    CHECK_TRUE(infimumOf(RealSet::realLine()).unwrap().infiniteDirection() < 0);

    // 空集没有确界
    CHECK_ERR(supremumOf(RealSet::empty()), MathsError::EmptyCollection);
    CHECK_ERR(infimumOf(RealSet::empty()), MathsError::EmptyCollection);
  }

  TEST_SUMMARY();
}
