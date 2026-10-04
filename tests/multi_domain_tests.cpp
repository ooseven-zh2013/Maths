#include "check.hpp"

#include <iostream>
#include <map>
#include <string>

import maths;

using namespace maths;

namespace {

Polynomial poly(const char *latex) { return parseExpression(latex).unwrap().getNumerator(); }

ConstraintSystem system(std::vector<std::pair<std::string, Relation>> atoms) {
  std::vector<AtomConstraint> built;
  for (const auto &[latex, relation] : atoms) {
    built.push_back(AtomConstraint(poly(latex.c_str()), relation));
  }
  return ConstraintSystem(built);
}

MultiRationalFunction frac(const char *numerator, const char *denominator) {
  return MultiRationalFunction::make(poly(numerator), poly(denominator)).unwrap();
}

} // namespace

int main() {
  std::cout << "=== 多元定义域与判空测试 ===" << std::endl;
  std::cout << std::unitbuf;

  // ---------- 定义域 = {Q ≠ 0} ----------
  {
    // 常数分母 → 处处有定义
    CHECK_TRUE(domainOf(frac("x", "2")).unwrap().isTrivial());
    // 分母含变量 → 一条 NotEqual 约束
    const ConstraintSystem domain = domainOf(frac("x", "y")).unwrap();
    CHECK_TRUE(domain.atoms().size() == std::size_t(1));
    CHECK_TRUE(domain.atoms().front().relation() == Relation::NotEqual);
    // 定义域与别的条件取交就是 andWith
    const ConstraintSystem narrowed = domain.andWith(system({{"x", Relation::GreaterEqual}}));
    CHECK_TRUE(narrowed.atoms().size() == std::size_t(2));
  }

  // ---------- whereNonNegativeOverMulti ----------
  {
    CHECK_TRUE(whereNonNegativeOverMulti(poly("0")).unwrap().isTrivial()); // 恒 ≥ 0
    const ConstraintSystem condition = whereNonNegativeOverMulti(poly("x^2-y^2")).unwrap();
    CHECK_TRUE(condition.atoms().size() == std::size_t(1));
    CHECK_TRUE(condition.atoms().front().relation() == Relation::GreaterEqual);
  }

  // ---------- 判空：路 1 逐变量可分离 ----------
  {
    // x ≥ 0 与 x ≤ -1 同时 → 空
    CHECK_TRUE(isEmpty(system({{"x", Relation::GreaterEqual}, {"-1-x", Relation::GreaterEqual}})).unwrap());
    // x ≥ 0 与 y ≥ 0 → 非空
    CHECK_TRUE(!isEmpty(system({{"x", Relation::GreaterEqual}, {"y", Relation::GreaterEqual}})).unwrap());
    // 分母为零处被排除：(x²+y²)/(x−1) 的定义域在该点为空？—— 不，那是一个点，
    // 区域仍非空；这里只验「x ≠ 1」与「x == 1」同时出现时为空
    CHECK_TRUE(isEmpty(system({{"x-1", Relation::NotEqual}, {"x-1", Relation::Equal}})).unwrap());
  }

  // ---------- 判空：路 2 全线性 → Fourier–Motzkin ----------
  {
    // x + y ≥ 0 与 −x − 1 ≥ 0 → x ≤ −1，于是 y ≥ 1，非空
    CHECK_TRUE(!isEmpty(system({{"x+y", Relation::GreaterEqual}, {"-x-1", Relation::GreaterEqual}})).unwrap());
    // x + y ≥ 0 与 −x + 1 ≥ 0 → x ≤ 1，y ≥ 0，非空
    CHECK_TRUE(!isEmpty(system({{"x+y", Relation::GreaterEqual}, {"-x+1", Relation::GreaterEqual}})).unwrap());
    // x ≥ 0 与 −x − 1 ≥ 0 与 y ≥ 0 → x ≤ −1 与 x ≥ 0 矛盾 → 空
    CHECK_TRUE(
        isEmpty(
            system({{"x", Relation::GreaterEqual}, {"-x-1", Relation::GreaterEqual}, {"y", Relation::GreaterEqual}}))
            .unwrap());
    // 无约束 → 非空
    CHECK_TRUE(!isEmpty(ConstraintSystem()).unwrap());
  }

  // ---------- 判空：判不了就报 DomainNotDecidable ----------
  {
    // 真·非线性且跨变量：(x−y²)² ≥ 0 恒成立，但 (x−y²)² ≤ −1 无解 —— FM 用不上
    const Result<bool> undecided = isEmpty(system({{"x-y^2", Relation::LessEqual}}));
    CHECK_ERR(undecided, MathsError::DomainNotDecidable);
  }

  // ---------- separableRanges ----------
  {
    const Result<std::map<Variable, RealSet>> ranges =
        separableRanges(system({{"x", Relation::GreaterEqual}, {"-x+1", Relation::GreaterEqual}}));
    CHECK_TRUE(ranges.isOk());
    CHECK_TRUE(ranges.unwrap().at(Variable("x")).latex() == std::string("[0, 1]"));
    // x ≥ 0 ∧ −1−x ≥ 0 → [0,∞) ∩ (−∞,−1] = ∅
    CHECK_TRUE(separableRanges(system({{"x", Relation::GreaterEqual}, {"-1-x", Relation::GreaterEqual}}))
                   .unwrap()
                   .at(Variable("x"))
                   .isEmpty());
    // 跨变量 → 判不了
    CHECK_ERR(separableRanges(system({{"x+y", Relation::GreaterEqual}})), MathsError::DomainNotDecidable);
  }

  TEST_SUMMARY();
}
