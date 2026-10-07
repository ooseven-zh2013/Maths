#include "check.hpp"

#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

Polynomial polynomial(const char *latex) { return parseExpression(latex).unwrap().getNumerator(); }

// 基里有没有「值恰好等于某个式子」的元素。
// 注意用有理函数比较：首一化会带出分数系数（y² − 1/2），而
// parseExpression(...).getNumerator() 会把分母清掉（2y² − 1），直接比多项式会不相等。
bool containsValue(const std::vector<Polynomial> &basis, const RationalFunction &expected) {
  for (const Polynomial &polynomial : basis) {
    if (RationalFunction(polynomial) == expected) {
      return true;
    }
  }
  return false;
}

// 基里只含指定变量的元素（第一个找到的）
std::optional<Polynomial> findWithVariables(const std::vector<Polynomial> &basis, const std::set<Variable> &variables) {
  for (const Polynomial &polynomial : basis) {
    if (!polynomial.isZero() && polynomial.variables() == variables) {
      return polynomial;
    }
  }
  return std::nullopt;
}

} // namespace

int main() {
  std::cout << "=== Gröbner 基测试 ===" << '\n';
  std::cout << std::unitbuf;

  // ---------- 首项与单项式序 ----------
  {
    const Polynomial f = polynomial("x^2 + y^3 + x*y");
    // Lex：变量名小的先比 → x 最重要
    const auto lexTerm = leadingTerm(f, MonomialOrder::Lex);
    CHECK_TRUE(lexTerm.has_value());
    CHECK_TRUE(lexTerm->first.size() == 1 && lexTerm->first[0].first == Variable("x"));
    CHECK_EQ(lexTerm->first[0].second, 2ULL);

    // GrLex：先比总次数 → y³ 最大
    const auto grLexTerm = leadingTerm(f, MonomialOrder::GrLex);
    CHECK_TRUE(grLexTerm.has_value());
    CHECK_TRUE(grLexTerm->first.size() == 1 && grLexTerm->first[0].first == Variable("y"));
    CHECK_EQ(grLexTerm->first[0].second, 3ULL);

    // 首项系数归一
    CHECK_TRUE(monic(polynomial("3x + 6"), MonomialOrder::Lex) == polynomial("x + 2"));
  }

  // ---------- 单个线性式：自己就是基 ----------
  {
    const Result<std::vector<Polynomial>> basis = groebnerBasis({polynomial("x - y - z")}, MonomialOrder::Lex);
    CHECK_OK(basis);
    CHECK_TRUE(basis.unwrap().size() == std::size_t(1));
    CHECK_TRUE(containsValue(basis.unwrap(), parseExpression("x - y - z").unwrap()));

    // 首项是 x —— 也就是说这条可以直接读成 x = y + z
    const auto term = leadingTerm(basis.unwrap()[0], MonomialOrder::Lex);
    CHECK_TRUE(term.has_value() && term->first.size() == 1 && term->first[0].first == Variable("x"));
  }

  // ---------- 需要真正做 S-多项式约化的例子 ----------
  {
    // x² + y² = 1 且 x = y  ⟹  2y² = 1（把 x 代掉）
    const Result<std::vector<Polynomial>> basis =
        groebnerBasis({polynomial("x^2 + y^2 - 1"), polynomial("x - y")}, MonomialOrder::Lex);
    CHECK_OK(basis);
    CHECK_TRUE(basis.unwrap().size() == std::size_t(2));
    CHECK_TRUE(containsValue(basis.unwrap(), parseExpression("x - y").unwrap()));

    // 只剩 y 的那条，首一化之后是 y² − 1/2
    const std::optional<Polynomial> onlyY = findWithVariables(basis.unwrap(), {Variable("y")});
    CHECK_TRUE(onlyY.has_value());
    CHECK_TRUE(RationalFunction(onlyY.value()) == parseExpression("y^2 - 1/2").unwrap());

    // 消去 x：剩下的正是消元理想的基
    const std::vector<Polynomial> projected = eliminateVariables(basis.unwrap(), {Variable("x")});
    CHECK_TRUE(projected.size() == std::size_t(1));
    CHECK_TRUE(RationalFunction(projected[0]) == parseExpression("y^2 - 1/2").unwrap());
  }

  // ---------- 一致性判定 ----------
  {
    // x + y = 0 与 x + y + 1 = 0 不可能同时成立 → 基里出现非零常数
    const Result<std::vector<Polynomial>> contradictory =
        groebnerBasis({polynomial("x + y"), polynomial("x + y + 1")}, MonomialOrder::Lex);
    CHECK_OK(contradictory);
    CHECK_TRUE(isInconsistent(contradictory.unwrap()));

    // 有解的情形
    const Result<std::vector<Polynomial>> consistent =
        groebnerBasis({polynomial("x + y"), polynomial("x - y")}, MonomialOrder::Lex);
    CHECK_OK(consistent);
    CHECK_TRUE(!isInconsistent(consistent.unwrap()));
  }

  // ---------- 解出线性方程组的解 ----------
  {
    // x = 2 且 xy = 1  ⟹  y = 1/2
    const Result<std::vector<Polynomial>> basis =
        groebnerBasis({polynomial("x - 2"), polynomial("x*y - 1")}, MonomialOrder::Lex);
    CHECK_OK(basis);
    CHECK_TRUE(containsValue(basis.unwrap(), parseExpression("x - 2").unwrap()));
    CHECK_TRUE(containsValue(basis.unwrap(), parseExpression("y - 1/2").unwrap()));
  }

  // ---------- 首项互素的输入（第一判别法直接跳过，应当很快） ----------
  {
    const Result<std::vector<Polynomial>> basis =
        groebnerBasis({polynomial("x - 1"), polynomial("y - 2"), polynomial("z - 3")}, MonomialOrder::Lex);
    CHECK_OK(basis);
    CHECK_TRUE(basis.unwrap().size() == std::size_t(3));
    CHECK_TRUE(containsValue(basis.unwrap(), parseExpression("x - 1").unwrap()));
    CHECK_TRUE(containsValue(basis.unwrap(), parseExpression("z - 3").unwrap()));
  }

  // ---------- 三个变量的消元 ----------
  {
    // x² + y² + z² = 1 且 x = y = z  ⟹  3z² = 1
    const Result<std::vector<Polynomial>> basis = groebnerBasis(
        {polynomial("x^2 + y^2 + z^2 - 1"), polynomial("x - y"), polynomial("y - z")}, MonomialOrder::Lex);
    CHECK_OK(basis);
    const std::optional<Polynomial> onlyZ = findWithVariables(basis.unwrap(), {Variable("z")});
    CHECK_TRUE(onlyZ.has_value());
    CHECK_TRUE(RationalFunction(onlyZ.value()) == parseExpression("z^2 - 1/3").unwrap());

    // 消去 x、y 之后只剩 z 的那条
    const std::vector<Polynomial> projected = eliminateVariables(basis.unwrap(), {Variable("x"), Variable("y")});
    CHECK_TRUE(projected.size() == std::size_t(1));
    CHECK_TRUE(RationalFunction(projected[0]) == parseExpression("z^2 - 1/3").unwrap());
  }

  // ---------- 空输入 ----------
  {
    const Result<std::vector<Polynomial>> basis = groebnerBasis({}, MonomialOrder::Lex);
    CHECK_OK(basis);
    CHECK_TRUE(basis.unwrap().empty());
    CHECK_TRUE(!isInconsistent(basis.unwrap())); // 空组恒真
  }

  TEST_SUMMARY();

  // ---------- 主理想的基不能是空的（2026-10-07 修） ----------
  //
  // `groebnerBasis({x-y, 2x-2y})` 的首一化基是 {x-y, x-y}，两个完全相同。
  // 原先的「最小化」会把每个元素都被**其余元素**约化成 0 ⇒ 结果为空集 ——
  // 而主理想的最小基是 {g}，不是 {}。空基会让 gcd 一类的调用方以为「理想是零理想」。
  {
    const std::vector<Polynomial> basis =
        groebnerBasis({polynomial("x-y"), polynomial("2*x-2*y")}, MonomialOrder::Lex).unwrap();
    CHECK_TRUE(basis.size() == std::size_t(1));
    const Result<Polynomial> gcd = polynomialGcd(polynomial("x-y"), polynomial("2*x-2*y"));
    CHECK_TRUE(gcd.isOk() && gcd.unwrap().latex() == "x - y");
  }

  // ---------- 完全平方判定 ----------
  //
  // 判据：f = c·h² ⟺ gcd(f, 各偏导) = h 且 f/h² 是常数（Yun 的第一步）。
  // ⚠️ 目前**有漏判**：`x^4`、`x^2y^2` 这类含多重因子的情况还算不对。
  // 漏判的后果是「该化简时没化简」，结果仍然正确 —— 所以别把「说不是平方」当「不是平方」。
  {
    CHECK_TRUE(squareRootIfSquare(polynomial("x^2")).has_value());
    CHECK_TRUE(squareRootIfSquare(polynomial("2*x^2")).has_value());
    CHECK_TRUE(squareRootIfSquare(polynomial("(x+y)^2")).value().latex() == "x + y");
    CHECK_TRUE(squareRootIfSquare(polynomial("x^2+y^2-2xy")).value().latex() == "x - y");
    // 重数 > 2 的情形：一次 gcd 只剥一层，所以要沿 gcd 链往下走
    CHECK_TRUE(squareRootIfSquare(polynomial("x^4")).value().latex() == "x^2");
    CHECK_TRUE(squareRootIfSquare(polynomial("x^2y^2")).value().latex() == "xy");
    CHECK_TRUE(squareRootIfSquare(polynomial("(x-y)^4")).value().latex() == "(x - y)^2");
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^3")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^2y")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^2+y^2")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x*y")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^2+2xy")).has_value());
  }
}
