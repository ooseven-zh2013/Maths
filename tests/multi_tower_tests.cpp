#include "check.hpp"

#include <iostream>
#include <set>
#include <string>

import maths;

using namespace maths;

namespace {

MultiRationalFunction mrf(const char *numerator, const char *denominator = "1") {
  return MultiRationalFunction::make(parseExpression(numerator).unwrap().getNumerator(),
                                     parseExpression(denominator).unwrap().getNumerator())
      .unwrap();
}

using Flat = MultiTowerExtension::Flat;

// 深度 1：y₁² = x²+y²
const std::vector<Flat> kRootSumSquares = {Flat{mrf("x^2+y^2")}};

// 深度 2：y₁² = x²+y²、y₂² = 1 + y₁
const std::vector<Flat> kNested = {Flat{mrf("x^2+y^2")}, Flat{mrf("1"), mrf("1")}};

MultiTowerExtension element(const std::vector<Flat> &relations, const Flat &flat) {
  return MultiTowerExtension::fromMasks(relations, flat).unwrap();
}

MultiTowerExtension rootOfSumSquares() { return element(kRootSumSquares, Flat{mrf("0"), mrf("1")}); }

MultiTowerExtension rootOfNested() { return element(kNested, Flat{mrf("0"), mrf("0"), mrf("1"), mrf("0")}); }

Scope point(long long x, long long y) {
  Scope scope;
  scope.assign(Variable("x"), Fraction(x, 1)).unwrap();
  scope.assign(Variable("y"), Fraction(y, 1)).unwrap();
  return scope;
}

RealAlgebraicNumber valueAt(const MultiTowerExtension &value, long long x, long long y) {
  return value.evaluate(point(x, y)).unwrap();
}

} // namespace

int main() {
  std::cout << "=== 多元塔 MultiTowerExtension 测试 ===" << std::endl;
  std::cout << std::unitbuf;

  // ---------- 深度 1：√(x²+y²) ----------
  {
    const MultiTowerExtension root = rootOfSumSquares();
    CHECK_TRUE(root.depth() == std::size_t(1));
    CHECK_TRUE(!root.isZero());
    CHECK_TRUE(root.variables() == std::set<Variable>({Variable("x"), Variable("y")}));
    // 在 (3,4) 上是 5
    CHECK_TRUE(valueAt(root, 3, 4) == RealAlgebraicNumber(Fraction(5, 1)));
    // (√(x²+y²))² = x²+y² —— 乘法要把 y₁² 换回 f₁
    const MultiTowerExtension squared = (root * root).unwrap();
    CHECK_TRUE(squared == element(kRootSumSquares, Flat{mrf("x^2+y^2"), mrf("0")}));
    CHECK_TRUE(valueAt(squared, 3, 4) == RealAlgebraicNumber(Fraction(25, 1)));
  }

  // ---------- 深度 2：√(1+√(x²+y²)) ----------
  {
    const MultiTowerExtension nested = rootOfNested();
    CHECK_TRUE(nested.depth() == std::size_t(2));
    // y₂·y₂ = 1 + y₁ —— 塔与「独立生成元」唯一真正的差别
    CHECK_TRUE((nested * nested).unwrap() == element(kNested, Flat{mrf("1"), mrf("1"), mrf("0"), mrf("0")}));
    // 在 (0,0) 上：√(1+0) = 1
    CHECK_TRUE(valueAt(nested, 0, 0) == RealAlgebraicNumber(Fraction(1, 1)));
    // 在 (3,4) 上：√(1+5) = √6
    const RealAlgebraicNumber six = RealAlgebraicNumber::nthRootOf(Fraction(6, 1), 2).unwrap();
    CHECK_TRUE(valueAt(nested, 3, 4) == six);
  }

  // ---------- 加减与取负 ----------
  {
    const MultiTowerExtension root = rootOfSumSquares();
    const MultiTowerExtension twice = (root + root).unwrap();
    CHECK_TRUE(twice == element(kRootSumSquares, Flat{mrf("0"), mrf("2")}));
    CHECK_TRUE((root + root.negate()).unwrap() == element(kRootSumSquares, Flat{mrf("0"), mrf("0")}));
    CHECK_TRUE(valueAt(root.negate(), 3, 4) == RealAlgebraicNumber(Fraction(-5, 1)));
  }

  // ---------- 判等与同塔 ----------
  {
    const MultiTowerExtension a = rootOfSumSquares();
    const MultiTowerExtension b = rootOfSumSquares();
    CHECK_TRUE(a == b);
    CHECK_TRUE(a.sameTower(b));
    // 不同的塔不能运算
    const MultiTowerExtension other = element(kNested, Flat{mrf("0"), mrf("0"), mrf("1"), mrf("0")});
    CHECK_ERR(a + other, MathsError::InvalidExpression);
    CHECK_TRUE(!a.sameTower(other));
  }

  // ---------- 往上接一层 ----------
  {
    const MultiTowerExtension base = rootOfSumSquares();
    // y₂² = 1 + y₁
    const MultiTowerExtension two = base.adjoining(Flat{mrf("1"), mrf("1")}).unwrap(); // 长度 2^1 = 2 ✓
    CHECK_TRUE(two.depth() == std::size_t(2));
    // 长度不对 → 拒收
    CHECK_ERR(base.adjoining(Flat{mrf("1")}), MathsError::NestedRadical);
    // 整条关系全零 → √0 之后那层退化
    CHECK_ERR(MultiTowerExtension::make(mrf("0"), {Flat{mrf("0")}}), MathsError::InvalidRange);
    // relations[1] 长度必须是 2^1
    CHECK_ERR(MultiTowerExtension::make(mrf("0"), {Flat{mrf("x")}, Flat{mrf("1")}}), MathsError::NestedRadical);
  }

  // ---------- ℚ(x,y) 里的元素放进塔 ----------
  {
    const MultiTowerExtension base = rootOfSumSquares();
    const MultiTowerExtension lifted = base.lifting(mrf("x")).unwrap();
    CHECK_TRUE(lifted == element(kRootSumSquares, Flat{mrf("x"), mrf("0")}));
    CHECK_TRUE(valueAt(lifted, 3, 4) == RealAlgebraicNumber(Fraction(3, 1)));
  }

  // ---------- 渲染 ----------
  {
    const MultiTowerExtension root = rootOfSumSquares();
    CHECK_TRUE(root.latex().find("sqrt") != std::string::npos);
    CHECK_TRUE(rootOfNested().latex().find("sqrt") != std::string::npos);
  }

  TEST_SUMMARY();
}
