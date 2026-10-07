#include "../check.hpp"

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
  std::cout << "=== 多维点集：约束式表示测试 ===" << '\n';
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

  // ---------- 线性情形的投影：Fourier–Motzkin ----------
  {
    const auto atom = [](const char *latex, Relation relation) { return AtomConstraint(polynomial(latex), relation); };

    // {x ≥ y, x ≤ z} 消去 x → y ≤ z（(y,z) 能被某个 x 补全的条件）
    const ConstraintSystem between({atom("x-y", Relation::GreaterEqual), atom("x-z", Relation::LessEqual)});
    const auto projected = projectLinear(between, {Variable("x")});
    CHECK_OK(projected);
    CHECK_TRUE(projected.unwrap().has_value());
    const ConstraintSystem &reduced = *projected.unwrap();
    CHECK_TRUE(reduced.variables().size() == std::size_t(2)); // 只剩 y、z
    CHECK_TRUE(!reduced.atoms().empty());
    CHECK_TRUE(reduced.admits(point({{"y", 1}, {"z", 2}})).unwrap());  // 可取 x ∈ [1,2]
    CHECK_TRUE(reduced.admits(point({{"y", 1}, {"z", 1}})).unwrap());  // 取等也允许（非严格）
    CHECK_TRUE(!reduced.admits(point({{"y", 2}, {"z", 1}})).unwrap()); // 2 ≤ 1 不成立

    // 严格性要保留：{x > y, x < z} → y < z（等号不行）
    const ConstraintSystem strictSystem({atom("x-y", Relation::Greater), atom("x-z", Relation::Less)});
    const auto strictProjected = projectLinear(strictSystem, {Variable("x")});
    CHECK_OK(strictProjected);
    CHECK_TRUE(strictProjected.unwrap().has_value());
    const ConstraintSystem &strictReduced = *strictProjected.unwrap();
    CHECK_TRUE(strictReduced.admits(point({{"y", 1}, {"z", 2}})).unwrap());
    CHECK_TRUE(!strictReduced.admits(point({{"y", 1}, {"z", 1}})).unwrap()); // y = z 不行

    // 无解：{x ≥ 1, x ≤ 0} 消去 x → 1 ≤ 0
    const ConstraintSystem contradictory({atom("x-1", Relation::GreaterEqual), atom("x", Relation::LessEqual)});
    const auto impossible = projectLinear(contradictory, {Variable("x")});
    CHECK_OK(impossible);
    CHECK_TRUE(!impossible.unwrap().has_value()); // nullopt = 无解

    // 恒真：{x ≥ 1, x ≤ 3} 消去 x 后对剩余变量没有任何限制
    const ConstraintSystem bounded({atom("x-1", Relation::GreaterEqual), atom("3-x", Relation::GreaterEqual)});
    const auto tautology = projectLinear(bounded, {Variable("x")});
    CHECK_OK(tautology);
    CHECK_TRUE(tautology.unwrap().has_value());
    CHECK_TRUE(tautology.unwrap()->isTrivial());

    // 等式：{x = y, x = z} → y = z
    const ConstraintSystem equalities({atom("x-y", Relation::Equal), atom("x-z", Relation::Equal)});
    const auto equalProjected = projectLinear(equalities, {Variable("x")});
    CHECK_OK(equalProjected);
    CHECK_TRUE(equalProjected.unwrap().has_value());
    const ConstraintSystem &equalReduced = *equalProjected.unwrap();
    CHECK_TRUE(equalReduced.admits(point({{"y", 3}, {"z", 3}})).unwrap());
    CHECK_TRUE(!equalReduced.admits(point({{"y", 3}, {"z", 4}})).unwrap());

    // 一次消两个：{x ≥ 0, y ≥ x, z ≥ y} → z ≥ 0
    const ConstraintSystem chain(
        {atom("x", Relation::GreaterEqual), atom("y-x", Relation::GreaterEqual), atom("z-y", Relation::GreaterEqual)});
    const auto chainProjected = projectLinear(chain, {Variable("x"), Variable("y")});
    CHECK_OK(chainProjected);
    CHECK_TRUE(chainProjected.unwrap().has_value());
    const ConstraintSystem &chainReduced = *chainProjected.unwrap();
    CHECK_TRUE(chainReduced.variables().size() == std::size_t(1)); // 只剩 z
    CHECK_TRUE(chainReduced.admits(point({{"z", 0}})).unwrap());
    CHECK_TRUE(!chainReduced.admits(point({{"z", -1}})).unwrap());

    // 不适用：非线性原子、以及「不等于」（非凸）
    CHECK_ERR(projectLinear(ConstraintSystem({atom("x^2+y", Relation::LessEqual)}), {Variable("x")}),
              MathsError::InvalidExpression);
    CHECK_ERR(projectLinear(ConstraintSystem({atom("x-y", Relation::NotEqual)}), {Variable("x")}),
              MathsError::InvalidExpression);
  }

  TEST_SUMMARY();
}
