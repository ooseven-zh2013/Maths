export module maths.algebra:domain_multi;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import :expression;
import :scope;
import maths.real_set;
import :constraint_system;
import :rational_multi;

// ==================== 多元定义域 ====================
//
// 一元那边定义域是 `RealSet`（ℝ 的区间 + 有限点集）。多元没有那个表示 ——
// `{x ≥ 0 ∧ y ≥ 0}` 这种半代数区域装不进「区间」的模型。
//
// **直接用 `ConstraintSystem`**：它已经是「若干 `Polynomial + Relation` 的合取」，
// 正好是多元定义域该有的形状。函数 `P/Q` 的定义域就是 `{Q ≠ 0}`，带根号时再 ∧ 上
// 各个 `{Pᵢ ≥ 0}` —— 全是合取，不需要新类型、不需要布尔析取。
//
// 代价是**区域本身不是对象**：拿到的是一个约束式，不是一片「点集」。
// 要判「这个区域空不空」「每个变量各自能取哪些值」得另外问，见下面两个函数。

export namespace maths {

namespace multi_domain_detail {

// 常数 c 是否满足 relation（`AtomConstraint::satisfies` 是私有的，这里自己判一份）
inline bool holdsFor(Relation relation, const Fraction &value) {
  switch (relation) {
  case Relation::Greater:
    return value > Fraction(0, 1);
  case Relation::GreaterEqual:
    return value >= Fraction(0, 1);
  case Relation::Less:
    return value < Fraction(0, 1);
  case Relation::LessEqual:
    return value <= Fraction(0, 1);
  case Relation::Equal:
    return value == Fraction(0, 1);
  case Relation::NotEqual:
    return value != Fraction(0, 1);
  }
  return false;
}

} // namespace multi_domain_detail

// P(x₁,…,xₙ)/Q(x₁,…,xₙ) 的定义域 = {Q ≠ 0} ∧ {约掉过的变量 ≠ 0}
//
// 第二项是**必须有**的：`x/x` 化简成 1，但 x ≠ 0 这条约束跟着化简一起消失了。
// 少了它，函数会在 x = 0 处静默给出 1 —— 那是本库最不能接受的一类错误。
// 一元那边靠 `discardedConstraints` 记着，多元这边是 `discardedVariables()`。
inline Result<ConstraintSystem> domainOf(const MultiRationalFunction &value) {
  std::vector<AtomConstraint> atoms;
  if (!value.denominator().variables().empty()) {
    atoms.push_back(AtomConstraint(value.denominator(), Relation::NotEqual));
  }
  for (const Variable &variable : value.discardedVariables()) {
    atoms.push_back(AtomConstraint(Monomial(Fraction(1, 1), VarPowers{{variable, 1}}), Relation::NotEqual));
  }
  return Result<ConstraintSystem>(ConstraintSystem(atoms));
}

// {x : P(x₁,…,xₙ) ≥ 0}，P 是多元多项式。
//
// 判得了就给出可分离的逐变量条件；判不了就报 DomainNotDecidable —— 一元那边
// `solveInequality` 覆盖不全时也是这个态度，不猜。
inline Result<ConstraintSystem> whereNonNegativeOverMulti(const Polynomial &value) {
  if (value.isZero()) {
    return Result<ConstraintSystem>(ConstraintSystem()); // 恒 ≥ 0，不加约束
  }
  return Result<ConstraintSystem>(ConstraintSystem({AtomConstraint(value, Relation::GreaterEqual)}));
}

// ==================== 判空 ====================
//
// 三条路，按代价从低到高：
//
//   1. **逐变量可分离** —— 每个原子只涉及一个变量。那就是若干个一元条件相乘，
//      任意一个空则整体空。全对则整体非空 ✓ 精确
//   2. **全线性** —— 用 Fourier–Motzkin 逐个消元，消到只剩常数约束后逐条判 ✓ 精确
//   3. 其余（真·非线性、跨变量）→ `DomainNotDecidable`
//
// 一元那边有完整的「符号表 + 区间选根」，多元的对应物是柱状代数分解（CAD）——
// 那是研究级算法，本库不做。所以多元的判空能力**明显弱于一元**。
inline Result<bool> isEmpty(const ConstraintSystem &system) {
  if (system.isTrivial()) {
    return Result<bool>(false); // 无约束 → 整空间非空
  }
  // ---- 路 1：逐变量可分离 ----
  // ⚠️ 原子真正多元时 `asSeparable()` 会**报错**（内部去解一元不等式失败）。
  // 那不叫「判空判不了」，只叫「这条路走不通」→ 继续往 Fourier–Motzkin 那条路走。
  const Result<std::optional<std::map<Variable, RealSet>>> split = system.asSeparable();
  if (split.isOk() && split.unwrap().has_value()) {
    for (const auto &[variable, allowed] : *split.unwrap()) {
      if (allowed.isEmpty()) {
        return Result<bool>(true);
      }
    }
    return Result<bool>(false);
  }
  // ---- 路 2：全线性 → Fourier–Motzkin 消元 ----
  ConstraintSystem reduced = system;
  while (!reduced.atoms().empty()) {
    std::set<Variable> remaining = reduced.variables();
    if (remaining.empty()) {
      break;
    }
    const Result<std::optional<ConstraintSystem>> projected = projectLinear(reduced, remaining);
    if (projected.isErr() || !projected.unwrap().has_value()) {
      return Result<bool>::err(MathsError::DomainNotDecidable); // 有非线性，FM 用不上
    }
    reduced = projected.unwrap().value();
  }
  // 消完只剩常数约束，逐条判
  for (const AtomConstraint &atom : reduced.atoms()) {
    if (!atom.expression().variables().empty()) {
      return Result<bool>::err(MathsError::DomainNotDecidable);
    }
    const Result<Fraction> constant = atom.expression().evaluate(Scope());
    if (constant.isErr()) {
      return std::unexpected(constant.unwrapErr());
    }
    if (!multi_domain_detail::holdsFor(atom.relation(), constant.unwrap())) {
      return Result<bool>(true);
    }
  }
  return Result<bool>(false);
}

// ==================== 逐变量能取哪些值 ====================
//
// 能分离时给出每个变量的 `RealSet`（精确）；不能分离报 DomainNotDecidable。
// 上面 `isEmpty` 的路 1 用的就是它。
inline Result<std::map<Variable, RealSet>> separableRanges(const ConstraintSystem &system) {
  const Result<std::optional<std::map<Variable, RealSet>>> split = system.asSeparable();
  if (split.isErr() || !split.unwrap().has_value()) {
    return Result<std::map<Variable, RealSet>>::err(MathsError::DomainNotDecidable);
  }
  return Result<std::map<Variable, RealSet>>(*split.unwrap());
}

} // namespace maths
