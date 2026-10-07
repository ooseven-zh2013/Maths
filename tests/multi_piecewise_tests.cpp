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
  std::cout << "=== 多元规则与多元分段测试 ===" << '\n';
  std::cout << std::unitbuf;

  // ---------- 多元绝对值：|f| = { f : {f≥0} , −f : {f<0} } ----------
  //
  // ⚠️ 这**不需要 CAD**。两支的定义域就是符号本身写着呢；而 `g = N/D` 的符号条件
  // 精确且廉价：`g ≥ 0` ⟺ `N·D ≥ 0 ∧ D ≠ 0`（同号），全程只有多项式乘法。
  // 以前这个入口根本不存在，一元那边的注释还写着「多变量：分段函数装不下」。
  {
    const MultiPiecewiseFunction difference = parseMultiPiecewiseExpression("|x-y|").unwrap();
    CHECK_TRUE(difference.at(point(3, 1)).unwrap() == RealAlgebraicNumber(Fraction(2, 1)));
    CHECK_TRUE(difference.at(point(1, 3)).unwrap() == RealAlgebraicNumber(Fraction(2, 1)));
    CHECK_TRUE(difference.at(point(2, 2)).unwrap() == RealAlgebraicNumber(Fraction(0, 1))); // 折线上
    // 两支的定义域真的互斥，而且覆盖整个平面
    CHECK_TRUE(difference.branches().size() == std::size_t(2));
    CHECK_TRUE(difference.admits(point(3, 1)).unwrap());
    CHECK_TRUE(difference.admits(point(-3, -1)).unwrap());
    // 恒真的原子不印出来（`|x-y|` 的分母是 1，`1 ≠ 0` 不该出现）
    CHECK_TRUE(difference.latex().find("ne 0") == std::string::npos);
    // ⚠️ 分支分隔符**不能带排版命令**（`\quad` / `\\`）—— app 的输出全进终端。
    // 这条断言当初写反了：那时我刚给分隔符加上 `\quad`，就顺手断言它存在。
    CHECK_TRUE(difference.latex().find("quad") == std::string::npos);
    CHECK_TRUE(difference.latex().find("\\\\") == std::string::npos);

    const MultiPiecewiseFunction product = parseMultiPiecewiseExpression("|x*y-1|").unwrap();
    CHECK_TRUE(product.at(point(3, 1)).unwrap() == RealAlgebraicNumber(Fraction(2, 1)));
    CHECK_TRUE(product.at(point(2, 2)).unwrap() == RealAlgebraicNumber(Fraction(3, 1))); // 正支
    CHECK_TRUE(product.at(point(1, 1)).unwrap() == RealAlgebraicNumber(Fraction(0, 1)));

    // 带分母的：`|x/y-1|` —— N·D ≥ 0 ∧ D ≠ 0 那个技巧
    const MultiPiecewiseFunction quotient = parseMultiPiecewiseExpression("|x/y-1|").unwrap();
    CHECK_TRUE(quotient.at(point(3, 1)).unwrap() == RealAlgebraicNumber(Fraction(2, 1)));
    CHECK_TRUE(quotient.at(point(1, 3)).unwrap() == RealAlgebraicNumber(Fraction(2, 3)));
    CHECK_TRUE(quotient.at(point(2, 2)).unwrap() == RealAlgebraicNumber(Fraction(0, 1)));
    // 分母条件**不是**恒真，必须留着
    CHECK_TRUE(quotient.latex().find("y \\ne 0") != std::string::npos);
    // y = 0 不在定义域里
    CHECK_TRUE(!quotient.admits(point(1, 0)).unwrap());

    // ⚠️ 单变量绝对值**解析得出两支**，所以 app 那一档必须靠 `variables().size() > 1`
    // 挡一下 —— 少了守卫 `|x|` 会被多元档抢走，然后按多元规则要求「一次给全的点」，
    // 而它本来该走一元分段那档、接受 `x = 5`。这条断言钉住这个前提。
    const MultiPiecewiseFunction single = parseMultiPiecewiseExpression("|x|").unwrap();
    CHECK_TRUE(single.variables().size() == std::size_t(1)); // 只有 x
    CHECK_TRUE(single.at(point(3, 0)).unwrap() == RealAlgebraicNumber(Fraction(3, 1)));
    CHECK_TRUE(single.at(point(0, 0)).unwrap() == RealAlgebraicNumber(Fraction(0, 1)));

    // 完全平方的短路：`|x^2+y^2-2xy|` 里分子是完全平方、分母是正的常数，
    // 于是 `|f| = f` 恒成立 —— 一支就够，不必拆 `{f≥0}` / `{f<0}`。
    {
      const MultiPiecewiseFunction squared = parseMultiPiecewiseExpression("|x^2+y^2-2xy|").unwrap();
      CHECK_TRUE(squared.branches().size() == std::size_t(1));
      CHECK_TRUE(squared.branches().front().domain.isWholeSpace());
      CHECK_TRUE(squared.at(point(3, 4)).unwrap() == RealAlgebraicNumber(Fraction(1, 1))); // (x-y)² = 1
      CHECK_TRUE(squared.at(point(1, 1)).unwrap() == RealAlgebraicNumber(Fraction(0, 1)));
      // ⚠️ 变量要算**规则 + 定义域**的并集：整空间区域没有约束原子、只看定义域会得到空集，
      // 而 app 的 `variables().size() > 1` 守卫会把这个输入拒掉
      CHECK_TRUE(squared.variables().size() == std::size_t(2));
    }

    // 没有绝对值就不是这一档
    CHECK_ERR(parseMultiPiecewiseExpression("x-y"), MathsError::InvalidExpression);
  }

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
    CHECK_TRUE(absolute.latex().find('x') != std::string::npos);
    CHECK_TRUE(MultiPiecewiseFunction().latex() == std::string("\\varnothing"));
  }

  TEST_SUMMARY();
}
