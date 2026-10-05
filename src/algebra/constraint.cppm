export module maths.algebra:constraint;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import maths.real_set;
import :expression;
import :rational;

export namespace maths {

// 变量取值范围的约束：`variable ∈ allowed`。
//
// 两件事在这里合流：
//
// 1. **解不等式**（`solveInequality`）—— 结果是「x 能取哪些值」，正好是 `RealSet`。
// 2. **化简时丢掉的约束**（`constraintsOf`）—— 约分约掉 x 等价于默认 x ≠ 0，
//    语义就是 `x ∈ ℝ \ {0}`，与不等式解集是同一种东西。
//
// 于是「函数定义域」「条件的取值范围」「不等式的解」共用一套表示，
// 不需要为每种场景另造一个类型。

namespace constraint_detail {

// 有理函数是否只含一个变量：不含变量返回 nullopt，多于一个变量报错
inline Result<std::optional<Variable>> singleVariableOf(const RationalFunction &function) {
  const std::set<Variable> variables = function.variables();
  if (variables.empty()) {
    return std::optional<Variable>();
  }
  if (variables.size() > 1) {
    return std::unexpected(MathsError::InvalidExpression);
  }
  return std::optional<Variable>(*variables.begin());
}

// 不含变量的分式的值。这里不借助 Scope（那会让本分区与 :scope 相互依赖成环），
// 分子分母都是常数项时直接相除即可。
inline Result<Fraction> constantValueOf(const RationalFunction &function) {
  const Result<Monomial> numerator = function.getNumerator().toMonomial();
  const Result<Monomial> denominator = function.getDenominator().toMonomial();
  if (numerator.isErr() || !numerator.unwrap().isConstant() || denominator.isErr() ||
      !denominator.unwrap().isConstant()) {
    return std::unexpected(MathsError::UndefinedVariable); // 含变量，不是常数
  }
  return numerator.unwrap().getCoefficient() / denominator.unwrap().getCoefficient();
}

// 常数分式对某个关系的解集：处处同号，所以要么整条实轴要么空
inline Result<RealSet> solveConstantInequality(const RationalFunction &function, Relation relation) {
  const Result<Fraction> value = constantValueOf(function);
  if (value.isErr()) {
    return std::unexpected(value.unwrapErr());
  }
  const Fraction constant = value.unwrap();
  const bool positive = constant > 0LL;
  const bool isZero = constant == 0LL;

  switch (relation) {
  case Relation::Equal:
    return isZero ? RealSet::realLine() : RealSet::empty();
  case Relation::NotEqual:
    return isZero ? RealSet::empty() : RealSet::realLine();
  case Relation::Less:
    return !positive && !isZero ? RealSet::realLine() : RealSet::empty();
  case Relation::LessEqual:
    return !positive ? RealSet::realLine() : RealSet::empty();
  case Relation::Greater:
    return positive && !isZero ? RealSet::realLine() : RealSet::empty();
  case Relation::GreaterEqual:
    return positive || isZero ? RealSet::realLine() : RealSet::empty(); // 0 ≥ 0 恒真
  }
  return std::unexpected(MathsError::InvalidExpression);
}

} // namespace constraint_detail

// 一条取值范围约束。
//
// 名字带 Range 是有意的：多维点集的原子约束（多项式 ⋈ 0）将来叫 AtomConstraint，
// 两者不是一回事；`Constraint` 这个名字太通用，容易和调用方的同名类型撞。
class RangeConstraint {
public:
  RangeConstraint() = default;
  RangeConstraint(Variable variable, RealSet allowed) : variable_(std::move(variable)), allowed_(std::move(allowed)) {}

  const Variable &variable() const { return variable_; }
  const RealSet &allowed() const { return allowed_; }

  bool admits(const RealAlgebraicNumber &value) const { return allowed_.contains(value); }

  bool operator==(const RangeConstraint &rhs) const { return variable_ == rhs.variable_ && allowed_ == rhs.allowed_; }

  std::string latex() const { return variable_.str() + " \\in " + allowed_.latex(); }
  std::string str() const { return variable_.str() + " in " + allowed_.str(); }

private:
  Variable variable_{"x"};
  RealSet allowed_;
};

// 前置声明：下面解不等式要与定义域取交
inline Result<RealSet> domainOf(const RationalFunction &function);

// 一元有理函数的不等式解集：f(x) ⋈ 0。
//
// 化到多项式：sign(p/q) = sign(p·q)（因为 q² > 0），所以不严格不等号用 p·q；
// `=` 与 `≠` 看分子 p。最后与定义域取交 —— 分母的零点虽然也是 p·q 的根，
// 但那里 f 无定义，不属于解集。
//
// 只支持**单变量**：多变量的解集是多维点集，不在本模块范围。
inline Result<RealSet> solveInequality(const RationalFunction &function, Relation relation) {
  const Result<std::optional<Variable>> variable = constraint_detail::singleVariableOf(function);
  if (variable.isErr()) {
    return std::unexpected(variable.unwrapErr());
  }
  if (!variable.unwrap().has_value()) {
    return constraint_detail::solveConstantInequality(function, relation);
  }

  const Variable target = *variable.unwrap();
  const bool byNumerator = relation == Relation::Equal || relation == Relation::NotEqual;
  const Result<Polynomial> combined =
      byNumerator ? Result<Polynomial>(function.getNumerator()) : (function.getNumerator() * function.getDenominator());
  if (combined.isErr()) {
    return std::unexpected(combined.unwrapErr());
  }
  const std::optional<UnivariatePolynomial> polynomial = toUnivariatePolynomial(combined.unwrap(), target);
  if (!polynomial) {
    return std::unexpected(MathsError::InvalidExpression); // 多变量
  }

  Result<RealSet> solution = RealSet::solve(*polynomial, relation);
  if (solution.isErr()) {
    return solution;
  }
  Result<RealSet> domain = domainOf(function);
  if (domain.isErr()) {
    return domain;
  }
  return solution.unwrap().intersect(domain.unwrap());
}

// 一元有理函数的定义域：分母不为零。分母是常数时是整条实轴。
inline Result<RealSet> domainOf(const RationalFunction &function) {
  const Result<std::optional<Variable>> variable = constraint_detail::singleVariableOf(function.getDenominator());
  if (variable.isErr()) {
    return std::unexpected(variable.unwrapErr());
  }
  if (!variable.unwrap().has_value()) {
    return RealSet::realLine();
  }
  const std::optional<UnivariatePolynomial> denominator =
      toUnivariatePolynomial(function.getDenominator(), *variable.unwrap());
  if (!denominator) {
    return std::unexpected(MathsError::InvalidExpression); // 多变量
  }
  Result<RealSet> zeros = RealSet::solve(*denominator, Relation::Equal);
  if (zeros.isErr()) {
    return zeros;
  }
  return zeros.unwrap().complement(); // ℝ 去掉分母的零点
}

// 化简丢掉的定义域约束，转成集合约束：约掉 x 等价于默认 x ≠ 0。
inline std::vector<RangeConstraint> constraintsOf(const RationalFunction &function) {
  std::vector<RangeConstraint> result;
  const Result<RealSet> nonzero = RealSet::point(RealAlgebraicNumber(Fraction(0, 1))).unwrap().complement();
  if (nonzero.isErr()) {
    return result;
  }
  for (const Variable &variable : function.discardedConstraints()) {
    result.emplace_back(variable, nonzero.unwrap());
  }
  return result;
}

} // namespace maths
