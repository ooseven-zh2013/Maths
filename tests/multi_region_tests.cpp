#include "check.hpp"

#include <iostream>
#include <string>

import maths;

using namespace maths;

namespace {

ConstraintSystem branch(const std::vector<std::pair<std::string, Relation>> &atoms) {
  std::vector<AtomConstraint> built;
  for (const auto &[latex, relation] : atoms) {
    built.push_back(AtomConstraint(parseExpression(latex.c_str()).unwrap().getNumerator(), relation));
  }
  return ConstraintSystem(built);
}

Scope point(long long x, long long y) {
  Scope scope;
  scope.assign(Variable("x"), Fraction(x, 1)).unwrap();
  scope.assign(Variable("y"), Fraction(y, 1)).unwrap();
  return scope;
}

} // namespace

int main() {
  std::cout << "=== 多元区域 Region 测试 ===" << '\n';
  std::cout << std::unitbuf;

  // ---------- 整空间 ----------
  {
    const Region whole = Region::wholeSpace().unwrap();
    CHECK_TRUE(whole.isWholeSpace());
    CHECK_TRUE(whole.isTrivial());
    CHECK_TRUE(!whole.isEmptyRegion().unwrap());
    CHECK_TRUE(whole.admits(point(-1, -1)).unwrap()); // 整空间什么都满足
    // 与任何区域相交都还是它
    const Region intersect =
        whole.intersect(Region::fromSystem(branch({{"x", Relation::GreaterEqual}})).unwrap()).unwrap();
    CHECK_TRUE(intersect.branches().size() == std::size_t(1));
    // 整空间的补是空集，DNF 表示不了（支不能为空）→ 明确报错
    CHECK_ERR(whole.complement(), MathsError::InvalidRange);
  }

  // ---------- 交：分配律展开 ----------
  {
    // (x ≥ 0 ∨ x ≥ 1) ∩ (x ≤ 2) → 两支
    const Region left = Region::fromSystem(branch({{"x", Relation::GreaterEqual}}))
                            .unwrap()
                            .unite(Region::fromSystem(branch({{"x-1", Relation::GreaterEqual}})).unwrap())
                            .unwrap();
    const Region right = Region::fromSystem(branch({{"-x+2", Relation::GreaterEqual}})).unwrap();
    CHECK_TRUE(left.branches().size() == std::size_t(2));
    const Region narrowed = left.intersect(right).unwrap();
    CHECK_TRUE(narrowed.branches().size() == std::size_t(2));
    // 交出 x ∈ [1,2] 与 x ∈ [0,2]
    CHECK_TRUE(narrowed.admits(point(1, 0)).unwrap());
    CHECK_TRUE(narrowed.admits(point(2, 0)).unwrap());  // x ≤ 2 的端点算在内
    CHECK_TRUE(!narrowed.admits(point(3, 0)).unwrap()); // x = 3 落在 x ≤ 2 之外
    CHECK_TRUE(!narrowed.admits(point(-1, 0)).unwrap());
  }

  // ---------- 并：拼接 ----------
  {
    const Region a = Region::fromSystem(branch({{"x", Relation::GreaterEqual}})).unwrap();
    const Region b = Region::fromSystem(branch({{"-x", Relation::GreaterEqual}})).unwrap();
    const Region both = a.unite(b).unwrap();
    CHECK_TRUE(both.branches().size() == std::size_t(2));
    // x ≥ 0 ∨ x ≤ 0 → 全体实数（但 DNF 不会去重合并，仍是 2 支）
    CHECK_TRUE(both.admits(point(1, 0)).unwrap());
    CHECK_TRUE(both.admits(point(-1, 0)).unwrap());
    CHECK_TRUE(!both.isEmptyRegion().unwrap());
  }

  // ---------- 补：德摩根 ----------
  {
    // ¬(x ≥ 0 ∧ y ≥ 0) = (x < 0) ∨ (y < 0)
    const Region quadrant =
        Region::fromSystem(branch({{"x", Relation::GreaterEqual}, {"y", Relation::GreaterEqual}})).unwrap();
    const Region outside = quadrant.complement().unwrap();
    CHECK_TRUE(outside.branches().size() == std::size_t(2));
    CHECK_TRUE(!outside.admits(point(1, 1)).unwrap());
    CHECK_TRUE(outside.admits(point(-1, 1)).unwrap());
    CHECK_TRUE(outside.admits(point(1, -1)).unwrap());
    // 补再补回原处（德摩根律）
    CHECK_TRUE(outside.complement().unwrap().admits(point(1, 1)).unwrap());
    CHECK_TRUE(!outside.complement().unwrap().admits(point(-1, 1)).unwrap());
  }

  // ---------- 判空 ----------
  {
    // x ≥ 0 ∧ x ≤ −1 → 空
    const Region empty =
        Region::fromSystem(branch({{"x", Relation::GreaterEqual}, {"-1-x", Relation::GreaterEqual}})).unwrap();
    CHECK_TRUE(empty.isEmptyRegion().unwrap());
    CHECK_TRUE(!Region::fromSystem(branch({{"x", Relation::GreaterEqual}})).unwrap().isEmptyRegion().unwrap());
    // 有一支非空 → 整体非空
    const Region mixed = empty.unite(Region::fromSystem(branch({{"x", Relation::GreaterEqual}})).unwrap()).unwrap();
    CHECK_TRUE(!mixed.isEmptyRegion().unwrap());
  }

  // ---------- 渲染 ----------
  {
    const Region single = Region::fromSystem(branch({{"x", Relation::GreaterEqual}})).unwrap();
    CHECK_TRUE(single.latex().find('x') != std::string::npos);
    const Region both = single.unite(Region::fromSystem(branch({{"y", Relation::GreaterEqual}})).unwrap()).unwrap();
    CHECK_TRUE(both.latex().find("cup") != std::string::npos);
    CHECK_TRUE(Region::wholeSpace().unwrap().latex() == std::string("\\mathbb{R}^{n}"));
  }

  TEST_SUMMARY();
}
