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
  // 判据（Yun）：反复 a := gcd(r, r 的全部偏导)、b := r/a、g := gcd(a,b)、r := r/g²，
  // 结束时 r 是常数则 f = c·h²，平方根是 h 的累乘。
  //
  // ⚠️ **每个断言都必须先 has_value() 再 value()** ——
  // `std::optional::value()` 对空 optional 是 UB，会返回垃圾；拿垃圾比字符串可能
  // **碰巧相等**，于是测试假绿（栽过：`x^4` / `x^2y^2` / `x^4y^2` 的断言全是这么写的，
  // 看起来过了，实际返回的是 nullopt）。
  //
  // 下面的 `CHECK` 宏把两个动作绑在一起，避免再犯。
  const auto isSquareOf = [](const char *text, const std::string &expectedRoot) {
    const std::optional<Polynomial> root = squareRootIfSquare(polynomial(text));
    return root.has_value() && root.value().latex() == expectedRoot;
  };
  {
    CHECK_TRUE(isSquareOf("x^2", "x"));
    CHECK_TRUE(isSquareOf("2*x^2", "x"));
    CHECK_TRUE(isSquareOf("(x+y)^2", "x + y"));
    CHECK_TRUE(isSquareOf("x^2+y^2-2xy", "x - y"));
    CHECK_TRUE(isSquareOf("x^4", "x^2"));
    CHECK_TRUE(isSquareOf("x^2y^2", "xy"));
    CHECK_TRUE(isSquareOf("x^6", "x^3"));
    CHECK_TRUE(isSquareOf("(x-y)^4", "(x - y)^2"));
    // 不是平方的（这些必须**判不出**，否则短路会给出错的结果）
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^2+y^2")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x*y")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^2+2xy")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^3")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^2y")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^3y^2")).has_value());
    // ⚠️ **已知做不到**（写成显式断言，免得日后误以为「已支持」）：
    //   `x^4y^2 = (x²y)²` 的平方根是 x²y。它的一次 gcd 是 a = x³y，
    //   真正的平方根既不等于 a、也不在 a 的幂上（要到 gcd(a, f/a) 里才找得到）。
    //   后果只是「没化简」—— app 会按两支输出，**结果仍然正确**。
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^4y^2")).has_value());
    CHECK_TRUE(!squareRootIfSquare(polynomial("x^2y^4")).has_value());
  }
}
