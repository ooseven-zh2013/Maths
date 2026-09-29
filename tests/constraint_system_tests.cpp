#include "check.hpp"

#include <iostream>
#include <map>
#include <string>

import maths;

using namespace maths;

namespace {

Polynomial polynomial(const char *latex) { return parseExpression(latex).unwrap().getNumerator(); }

Scope point(std::initializer_list<std::pair<const char *, long long>> values) {
  Scope scope;
  for (const auto &[name, value] : values) {
    (void)scope.assign(Variable(name), Fraction(value, 1LL));
  }
  return scope;
}

} // namespace

int main() {
  std::cout << "=== 多维点集：约束式表示测试 ===" << std::endl;
  std::cout << std::unitbuf;

  // ---------- 原子约束的成员判定 ----------
  {
    // 用户的例子：x − y = z 写成 x − y − z = 0
    const AtomConstraint plane(polynomial("x-y-z"), Relation::Equal);
    CHECK_TRUE(plane.variables().size() == std::size_t(3));
    CHECK_TRUE(plane.admits(point({{"x", 3}, {"y", 1}, {"z", 2}})).unwrap());  // 3−1−2 = 0
    CHECK_TRUE(!plane.admits(point({{"x", 1}, {"y", 1}, {"z", 1}})).unwrap()); // 1−1−1 = −1
    CHECK_EQ(plane.latex(), std::string("x - y - z = 0"));

    // 变量没绑全 → 判断不了，不猜
    CHECK_ERR(plane.admits(point({{"x", 3}})), MathsError::UndefinedVariable);

    // 不等式：x² + y² ≤ 1
    const AtomConstraint disk(polynomial("x^2+y^2-1"), Relation::LessEqual);
    CHECK_TRUE(disk.admits(point({{"x", 0}, {"y", 0}})).unwrap());
    CHECK_TRUE(!disk.admits(point({{"x", 2}, {"y", 2}})).unwrap());

    // 代数点：x² − 2 = 0 在 x = √2 处成立
    const AtomConstraint rootTwoAtom(polynomial("x^2-2"), Relation::Equal);
    AlgebraicScope algebraicPoint;
    CHECK_OK(algebraicPoint.assign(Variable("x"), RealAlgebraicNumber::parse("\\sqrt{2}").unwrap()));
    CHECK_TRUE(rootTwoAtom.admits(algebraicPoint).unwrap());
    CHECK_OK(algebraicPoint.assign(Variable("x"), RealAlgebraicNumber::parse("\\sqrt{3}").unwrap()));
    CHECK_TRUE(!rootTwoAtom.admits(algebraicPoint).unwrap());
  }

  // ---------- 合取 ----------
  {
    const AtomConstraint first(polynomial("x-1"), Relation::GreaterEqual); // x ≥ 1
    const AtomConstraint second(polynomial("x-3"), Relation::LessEqual);   // x ≤ 3

    ConstraintSystem system;
    system = system.andWith(ConstraintSystem({first}));
    system = system.andWith(ConstraintSystem({second}));
    system = system.andWith(ConstraintSystem({first})); // 重复的会被去掉
    CHECK_TRUE(system.atoms().size() == std::size_t(2));

    CHECK_TRUE(system.admits(point({{"x", 2}})).unwrap());
    CHECK_TRUE(!system.admits(point({{"x", 0}})).unwrap());
    CHECK_TRUE(!system.admits(point({{"x", 4}})).unwrap());
    CHECK_EQ(system.latex(), std::string("x - 1 \\ge 0 \\land x - 3 \\le 0"));

    // 空合取恒真
    CHECK_TRUE(ConstraintSystem().isTrivial());
    CHECK_TRUE(ConstraintSystem().admits(point({{"x", 123}})).unwrap());
  }

  // ---------- 可分离情形：各维一维实集的笛卡尔积 ----------
  {
    // 1 ≤ x ≤ 2 且 y ≥ 0
    ConstraintSystem system;
    system = system.andWith(ConstraintSystem({AtomConstraint(polynomial("x-1"), Relation::GreaterEqual)}));
    system = system.andWith(ConstraintSystem({AtomConstraint(polynomial("2-x"), Relation::GreaterEqual)}));
    system = system.andWith(ConstraintSystem({AtomConstraint(polynomial("y"), Relation::GreaterEqual)}));

    const auto separable = system.asSeparable();
    CHECK_OK(separable);
    CHECK_TRUE(separable.unwrap().has_value());
    const std::map<Variable, RealSet> &ranges = *separable.unwrap();
    CHECK_TRUE(ranges.size() == std::size_t(2));

    // x ∈ [1,2]
    const RealSet &xRange = ranges.at(Variable("x"));
    CHECK_EQ(xRange.latex(), std::string("[1, 2]"));
    CHECK_TRUE(xRange.contains(RealAlgebraicNumber(Fraction(1, 1))));
    CHECK_TRUE(xRange.contains(RealAlgebraicNumber(Fraction(2, 1))));
    CHECK_TRUE(!xRange.contains(RealAlgebraicNumber(Fraction(3, 1))));

    // y ∈ [0,+∞)
    CHECK_TRUE(ranges.at(Variable("y")).contains(RealAlgebraicNumber(Fraction(0, 1))));

    // 两步取交的结果与逐点判定一致
    CHECK_TRUE(ranges.at(Variable("x")).contains(RealAlgebraicNumber(Fraction(3, 2))));
    CHECK_TRUE(system.admits(point({{"x", 1}, {"y", 5}})).unwrap());
    CHECK_TRUE(!system.admits(point({{"x", 4}, {"y", 5}})).unwrap());
  }

  // ---------- 可分离的两种退化 ----------
  {
    // 常数条件恒假 → 整个系统不可满足
    const ConstraintSystem impossible({AtomConstraint(polynomial("1"), Relation::Equal)}); // 1 = 0
    const auto separable = impossible.asSeparable();
    CHECK_OK(separable);
    CHECK_TRUE(!separable.unwrap().has_value()); // nullopt = 恒假
    CHECK_TRUE(!impossible.admits(point({{"x", 1}})).unwrap());

    // 常数条件恒真 → 不产生任何维度的限制
    const ConstraintSystem trivial({AtomConstraint(polynomial("0"), Relation::Equal)}); // 0 = 0
    const auto ranges = trivial.asSeparable();
    CHECK_OK(ranges);
    CHECK_TRUE(ranges.unwrap().has_value());
    CHECK_TRUE(ranges.unwrap()->empty());

    // 一个原子含两个变量 → 不可分离（要靠消元，不是这里能给的）
    const ConstraintSystem coupled({AtomConstraint(polynomial("x-y"), Relation::Equal)});
    CHECK_ERR(coupled.asSeparable(), MathsError::InvalidExpression);
  }

  TEST_SUMMARY();
}
