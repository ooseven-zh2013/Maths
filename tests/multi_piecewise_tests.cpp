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

Region region(const std::vector<std::pair<std::string, Relation>> &atoms) {
  std::vector<AtomConstraint> built;
  for (const auto &[latex, relation] : atoms) {
    built.push_back(AtomConstraint(parseExpression(latex.c_str()).unwrap().getNumerator(), relation));
  }
  return Region::fromSystem(ConstraintSystem(built)).unwrap();
}

// √(x²+y²)：深度 1 的多元塔
MultiRule rootOfSumSquares() {
  const std::vector<MultiTowerExtension::Flat> relations = {MultiTowerExtension::Flat{mrf("x^2+y^2")}};
  return MultiRule(MultiTowerExtension::fromMasks(relations, MultiTowerExtension::Flat{mrf("0"), mrf("1")}).unwrap());
}

Scope point(long long x, long long y) {
  Scope scope;
  scope.assign(Variable("x"), Fraction(x, 1)).unwrap();
  scope.assign(Variable("y"), Fraction(y, 1)).unwrap();
  return scope;
}

} // namespace

int main() {
  std::cout << "=== 多元规则与多元分段测试 ===" << std::endl;
  std::cout << std::unitbuf;

  // ---------- MultiRule：两种形态 ----------
  {
    const MultiRule rational(mrf("x", "y"));
    const MultiRule tower = rootOfSumSquares();
    CHECK_TRUE(!rational.holdsTower() && tower.holdsTower());
    CHECK_TRUE(!rational.isZero() && !tower.isZero());
    CHECK_TRUE(rational.variables() == std::set<Variable>({Variable("x"), Variable("y")}));
    CHECK_TRUE(tower.variables() == std::set<Variable>({Variable("x"), Variable("y")}));
    // 有理规则在 (3,4) 是 3/4
    CHECK_TRUE(rational.evaluate(point(3, 4)).unwrap() == RealAlgebraicNumber(Fraction(3, 4)));
    // 塔规则在 (3,4) 是 5
    CHECK_TRUE(tower.evaluate(point(3, 4)).unwrap() == RealAlgebraicNumber(Fraction(5, 1)));
  }

  // ---------- 算术：rational ⊗ rational、rational ⊗ tower、tower ⊗ tower ----------
  {
    const MultiRule a(mrf("x"));
    const MultiRule b(mrf("y"));
    const MultiRule sum = (a + b).unwrap();
    CHECK_TRUE(!sum.holdsTower());
    CHECK_TRUE(sum.evaluate(point(3, 4)).unwrap() == RealAlgebraicNumber(Fraction(7, 1)));

    // 有理 × 塔 → 把有理挂到塔顶，结果仍是塔
    const MultiRule tower = rootOfSumSquares();
    const MultiRule mixed = (tower * a).unwrap();
    CHECK_TRUE(mixed.holdsTower());
    CHECK_TRUE(mixed.evaluate(point(3, 4)).unwrap() == RealAlgebraicNumber(Fraction(15, 1))); // 5·3

    // 塔 + 塔（同一条）可以
    CHECK_TRUE((tower + tower).unwrap().evaluate(point(3, 4)).unwrap() == RealAlgebraicNumber(Fraction(10, 1)));
    // 不同的塔不行
    const std::vector<MultiTowerExtension::Flat> other = {MultiTowerExtension::Flat{mrf("x^2")}};
    const MultiRule different(
        MultiTowerExtension::fromMasks(other, MultiTowerExtension::Flat{mrf("0"), mrf("1")}).unwrap());
    CHECK_ERR(tower + different, MathsError::InvalidExpression);
  }

  // ---------- 多元分段：|x| = { x 当 x≥0 ; −x 当 x<0 } ----------
  {
    const MultiPiecewiseFunction absolute =
        MultiPiecewiseFunction::make(
            {
                MultiPiecewiseFunction::Branch{MultiRule(mrf("x")), region({{"x", Relation::GreaterEqual}})},
                MultiPiecewiseFunction::Branch{MultiRule(mrf("-x")), region({{"-x-1", Relation::GreaterEqual}})},
            })
            .unwrap();
    CHECK_TRUE(absolute.branches().size() == std::size_t(2));
    CHECK_TRUE(absolute.at(point(3, 0)).unwrap() == RealAlgebraicNumber(Fraction(3, 1)));
    CHECK_TRUE(absolute.at(point(-3, 0)).unwrap() == RealAlgebraicNumber(Fraction(3, 1))); // 绝对值
    CHECK_TRUE(absolute.admits(point(1, 0)).unwrap());
  }

  // ---------- 分支规则可以是塔 ----------
  {
    // √(x²+y²) 在「离原点够远」那支… 简版：整空间上一支，规则是塔
    const MultiPiecewiseFunction everywhere =
        MultiPiecewiseFunction::make(
            {MultiPiecewiseFunction::Branch{rootOfSumSquares(), Region::wholeSpace().unwrap()}})
            .unwrap();
    CHECK_TRUE(everywhere.at(point(3, 4)).unwrap() == RealAlgebraicNumber(Fraction(5, 1)));
    CHECK_TRUE(everywhere.at(point(0, 0)).unwrap() == RealAlgebraicNumber(Fraction(0, 1)));
  }

  // ---------- 两支不交 ----------
  {
    // x ≥ 0 与 x ≥ 0 重叠 → 拒收
    CHECK_ERR(MultiPiecewiseFunction::make({
                  MultiPiecewiseFunction::Branch{MultiRule(mrf("x")), region({{"x", Relation::GreaterEqual}})},
                  MultiPiecewiseFunction::Branch{MultiRule(mrf("x")), region({{"x", Relation::GreaterEqual}})},
              }),
              MathsError::InvalidExpression);
    // x ≥ 0 与 x ≤ 0 不交（只在 x=0 相交，而那点两支都不含端点）→ 接受
    CHECK_TRUE(MultiPiecewiseFunction::make(
                   {
                       MultiPiecewiseFunction::Branch{MultiRule(mrf("x")), region({{"x", Relation::GreaterEqual}})},
                       MultiPiecewiseFunction::Branch{MultiRule(mrf("-x")), region({{"-x", Relation::Greater}})},
                   })
                   .isOk());
  }

  // ---------- 点不在任何支里 ----------
  {
    const MultiPiecewiseFunction partial =
        MultiPiecewiseFunction::make(
            {MultiPiecewiseFunction::Branch{MultiRule(mrf("x")), region({{"x", Relation::GreaterEqual}})}})
            .unwrap();
    CHECK_ERR(partial.at(point(-1, 0)), MathsError::OutsideDomain);
    CHECK_TRUE(!partial.admits(point(-1, 0)).unwrap());
  }

  // ---------- 渲染 ----------
  {
    const MultiPiecewiseFunction absolute =
        MultiPiecewiseFunction::make(
            {
                MultiPiecewiseFunction::Branch{MultiRule(mrf("x")), region({{"x", Relation::GreaterEqual}})},
                MultiPiecewiseFunction::Branch{MultiRule(mrf("-x")), region({{"-x-1", Relation::GreaterEqual}})},
            })
            .unwrap();
    CHECK_TRUE(absolute.latex().find("x") != std::string::npos);
    CHECK_TRUE(MultiPiecewiseFunction().latex() == std::string("\\varnothing"));
  }

  TEST_SUMMARY();
}
