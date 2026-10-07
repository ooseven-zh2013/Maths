// Expression：调用方看到的那一个类型。
//
// 这个文件的作用是**把原来住在 apps/simplify 里的解析级联钉在库这一层** ——
// 那段「什么式子该走哪一档」的规则以前是 app 的 if 链，判断错一次就是一类 bug
// （单变量 |x| 被多元档抢走、同一句话在两条路上含义不同）。

#include "check.hpp"

#include <initializer_list>
#include <set>
#include <string>
#include <utility>

import maths;

using namespace maths;

namespace {

Scope point(std::initializer_list<std::pair<const char *, long long>> values) {
  Scope scope;
  for (const auto &[name, value] : values) {
    scope.assign(Variable(name), Fraction(value, 1)).unwrap();
  }
  return scope;
}

} // namespace

int main() {
  // ---------- 单一入口：每种表示都被认出来 ----------
  {
    CHECK_TRUE(Expression::parse("x+1").unwrap().representation() == Expression::Representation::Rational);
    CHECK_TRUE(Expression::parse("1/2").unwrap().representation() ==
               Expression::Representation::Rational); // 有理数该走有理式那档
    CHECK_TRUE(Expression::parse("\\sqrt{2}").unwrap().representation() == Expression::Representation::AlgebraicNumber);
    CHECK_TRUE(Expression::parse("\\sqrt{2}*x").unwrap().representation() ==
               Expression::Representation::AlgebraicRational);
    CHECK_TRUE(Expression::parse("\\sqrt{x}").unwrap().representation() == Expression::Representation::Radical);
    CHECK_TRUE(Expression::parse("\\sqrt{1+\\sqrt{x}}").unwrap().representation() == Expression::Representation::Tower);
    CHECK_TRUE(Expression::parse("\\sqrt{x^2+y^2}").unwrap().representation() ==
               Expression::Representation::MultiTower);
    CHECK_TRUE(Expression::parse("|x|").unwrap().representation() == Expression::Representation::Piecewise);
    CHECK_TRUE(Expression::parse("|x-y|").unwrap().representation() == Expression::Representation::MultiPiecewise);
  }

  // ---------- 档位顺序：单变量不被多元档抢走 ----------
  //
  // ⚠️ 这两条是今天踩过的坑：`|x|` 与 `|x-y|` 的**表示**必须与它们的变量数匹配。
  // 判据错（少了 `variables().size() > 1`）时单变量绝对值会被要求给「一次给全的点」。
  {
    const Expression univariate = Expression::parse("|x|").unwrap();
    CHECK_TRUE(univariate.variables() == std::set<Variable>({Variable("x")}));
    const Expression multivariate = Expression::parse("|x-y|").unwrap();
    CHECK_TRUE(multivariate.variables() == std::set<Variable>({Variable("x"), Variable("y")}));
    // 多变量输入不该退化成单变量表示
    CHECK_TRUE(Expression::parse("\\sqrt{x^2+y^2}").unwrap().variables().size() == std::size_t(2));
    CHECK_TRUE(Expression::parse("\\sqrt{x^2}").unwrap().variables().size() == std::size_t(1));
  }

  // ---------- 变量清单 ----------
  {
    CHECK_TRUE(Expression::parse("1/2").unwrap().variables().empty()); // 纯数值没有变量
    CHECK_TRUE(Expression::parse("\\sqrt{2}*x+y").unwrap().variables() ==
               std::set<Variable>({Variable("x"), Variable("y")}));
  }

  // ---------- 渲染 ----------
  {
    CHECK_TRUE(Expression::parse("x+1").unwrap().latex() == "x + 1");
    // `|x|` 这种写法由**调用方**的绝对值回显负责（库给的是分段形式）
    CHECK_TRUE(Expression::parse("|x|").unwrap().str().find("x in") != std::string::npos);
    // 完全平方的多元绝对值：短路成单支，那支是整空间
    const Expression squared = Expression::parse("|x^2+y^2-2xy|").unwrap();
    CHECK_TRUE(squared.representation() == Expression::Representation::MultiPiecewise);
    CHECK_TRUE(squared.latex().find("mathbb") != std::string::npos);
  }

  // ---------- 绝对值标记（调用方据此回显成 |…| 而不是 √ 写法） ----------
  {
    CHECK_TRUE(Expression::parse("|x|").unwrap().writtenAsAbsoluteValue());
    CHECK_TRUE(Expression::parse("|x-y|").unwrap().writtenAsAbsoluteValue());
    CHECK_TRUE(Expression::parse("\\sqrt{x^2}").unwrap().writtenAsAbsoluteValue());
    CHECK_TRUE(!Expression::parse("x+1").unwrap().writtenAsAbsoluteValue());
    CHECK_TRUE(!Expression::parse("\\sqrt{x}").unwrap().writtenAsAbsoluteValue());
  }

  // ---------- 代入 ----------
  {
    const Expression substituted = Expression::parse("x+1").unwrap().substitute(point({{"x", 4}})).unwrap();
    CHECK_TRUE(substituted.variables().empty());
    CHECK_TRUE(substituted.evaluate(Scope()).unwrap() == RealAlgebraicNumber(Fraction(5, 1)));
    // 分段 / 塔不提供 substitute —— 对它们该做的是 evaluate（别静默走成别的路）
    CHECK_TRUE(Expression::parse("|x|").unwrap().substitute(point({{"x", 4}})).isErr());
    CHECK_TRUE(Expression::parse("\\sqrt{1+\\sqrt{x}}").unwrap().substitute(point({{"x", 4}})).isErr());
  }

  // ---------- 求值：八种表示全都要能用 ----------
  {
    CHECK_TRUE(Expression::parse("x+1").unwrap().evaluate(point({{"x", 4}})).unwrap() ==
               RealAlgebraicNumber(Fraction(5, 1)));
    CHECK_TRUE(Expression::parse("1/2").unwrap().evaluate(Scope()).unwrap() == RealAlgebraicNumber(Fraction(1, 2)));
    CHECK_TRUE(Expression::parse("\\sqrt{2}").unwrap().evaluate(Scope()).unwrap() ==
               RealAlgebraicNumber::parse("\\sqrt{2}").unwrap());
    // \sqrt{2}*x 在 x=3 上是 3\sqrt{2}
    CHECK_TRUE(Expression::parse("\\sqrt{2}*x").unwrap().evaluate(point({{"x", 3}})).unwrap() ==
               RealAlgebraicNumber::parse("3*\\sqrt{2}").unwrap());
    CHECK_TRUE(Expression::parse("\\sqrt{x}").unwrap().evaluate(point({{"x", 4}})).unwrap() ==
               RealAlgebraicNumber(Fraction(2, 1)));
    CHECK_TRUE(Expression::parse("|x|").unwrap().evaluate(point({{"x", 5}})).unwrap() ==
               RealAlgebraicNumber(Fraction(5, 1)));
    // ⚠️ x=3 给出 √(1+√3)，**不是有理数** —— 用 x=0 得 1
    CHECK_TRUE(Expression::parse("\\sqrt{1+\\sqrt{x}}").unwrap().evaluate(point({{"x", 0}})).unwrap() ==
               RealAlgebraicNumber(Fraction(1, 1)));
    CHECK_TRUE(Expression::parse("\\sqrt{x^2+y^2}").unwrap().evaluate(point({{"x", 3}, {"y", 4}})).unwrap() ==
               RealAlgebraicNumber(Fraction(5, 1)));
    CHECK_TRUE(Expression::parse("|x-y|").unwrap().evaluate(point({{"x", 3}, {"y", 4}})).unwrap() ==
               RealAlgebraicNumber(Fraction(1, 1)));
    CHECK_TRUE(Expression::parse("|x*y-1|").unwrap().evaluate(point({{"x", 3}, {"y", 4}})).unwrap() ==
               RealAlgebraicNumber(Fraction(11, 1)));
  }

  // ---------- 化简：原样返回（各表示已是规范形） ----------
  {
    CHECK_TRUE(Expression::parse("x+x").unwrap().simplify().latex() == "2x");
  }

  // ---------- 诊断：失败时报**最具体**的那条原因 ----------
  {
    // 纯数值的错误只有代数数那档给得出具体码
    CHECK_ERR(Expression::parse("(-4)^{1/2}"), MathsError::NegativeEvenRoot);
    CHECK_ERR(Expression::parse("1/0"), MathsError::DivisionByZero);
    // 语法错：有理解析器的诊断最准
    CHECK_ERR(Expression::parse("x+"), MathsError::InvalidExpression);
    // ⚠️ 曾想用 `\sqrt{x}+\sqrt{y}` 触发 RadicandsNotIndependent，实测**它能解析**
    // （2026-07 实测：加法与乘法都不报这个码）。所以这里不断言它 ——
    // **写不出触发条件的断言等于没断言**，不如不写。
  }

  TEST_SUMMARY();
}
