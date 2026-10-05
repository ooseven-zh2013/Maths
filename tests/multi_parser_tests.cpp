#include "check.hpp"

#include <iostream>
#include <string>

import maths;

using namespace maths;

namespace {

Scope point(long long x, long long y) {
  Scope scope;
  scope.assign(Variable("x"), Fraction(x, 1)).unwrap();
  scope.assign(Variable("y"), Fraction(y, 1)).unwrap();
  return scope;
}

} // namespace

int main() {
  std::cout << "=== 多元解析器 parseMultiTowerExpression 测试 ===" << '\n';
  std::cout << std::unitbuf;

  // ---------- 单层：√(x²+y²) ----------
  {
    const MultiTowerExtension root = parseMultiTowerExpression("\\sqrt{x^2+y^2}").unwrap();
    CHECK_TRUE(root.depth() == std::size_t(1));
    // (3,4) → 5
    CHECK_TRUE(root.evaluate(point(3, 4)).unwrap().str() == std::string("5"));
    // (0,0) → 0
    CHECK_TRUE(root.evaluate(point(0, 0)).unwrap().str() == std::string("0"));
  }

  // ---------- 两层：√(1+√(x²+y²)) ----------
  {
    const MultiTowerExtension nested = parseMultiTowerExpression("\\sqrt{1+\\sqrt{x^2+y^2}}").unwrap();
    CHECK_TRUE(nested.depth() == std::size_t(2));
    // (3,4) → √(1+5) = √6
    CHECK_TRUE(nested.evaluate(point(3, 4)).unwrap().str() ==
               RealAlgebraicNumber::nthRootOf(Fraction(6, 1), 2).unwrap().str());
  }

  // ---------- 三层 ----------
  {
    const MultiTowerExtension deep = parseMultiTowerExpression("\\sqrt{1+\\sqrt{2+\\sqrt{x^2+y^2}}}").unwrap();
    CHECK_TRUE(deep.depth() == std::size_t(3));
  }

  // ---------- 带系数 ----------
  {
    const MultiTowerExtension scaled = parseMultiTowerExpression("2*\\sqrt{x^2+y^2}").unwrap();
    CHECK_TRUE(scaled.depth() == std::size_t(1));
    CHECK_TRUE(scaled.evaluate(point(3, 4)).unwrap().str() == std::string("10"));
  }

  // ---------- 拒绝的输入 ----------
  {
    // 没有根号 → 走别的入口
    CHECK_ERR(parseMultiTowerExpression("x^2+y^2"), MathsError::InvalidExpression);
    // 高次根不做
    CHECK_ERR(parseMultiTowerExpression("\\sqrt[3]{x}"), MathsError::InvalidExpression);
  }

  TEST_SUMMARY();
}
