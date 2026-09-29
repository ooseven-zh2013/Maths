export module maths.algebra:constraint_system;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import maths.real_set;
import :expression;
import :rational;
import :scope;
import :algebraic;
import :constraint;

export namespace maths {

// 多维点集的**约束式（隐式）表示**。
//
// 为什么不展开成区域：n ≥ 2 的半代数集要做柱形代数分解，代价对变量数**双指数** ——
// 本库明确不做。改成保留一组原子约束，按需判定与化简；只有一维是特例
// （那里能解成规范形 `RealSet`，见 `:constraint`）。
//
// 这个分区之所以单独存在：它要用完整的 `Scope` 与 `toAlgebraic`（`:scope` / `:algebraic`），
// 而那两个分区都依赖 `:constraint` —— 放一起会成环。这里是最上层，可以同时 import 三者。

// ==================== 多维点集：约束式（隐式）表示 ====================
//
// n ≥ 2 时一般半代数集**不展开成区域**（那要柱形代数分解，代价双指数），
// 而是保留一组原子约束，按需做判定与化简：
//
//   x − y = z        → 原子 `x − y − z = 0`
//   x² + y² ≤ 1      → 原子 `x² + y² − 1 ≤ 0`
//
// 这样「成员判定」是精确代入判号（快），「解方程组」交给将来的 Gröbner 消元。
// 一维情形才是特例：那时能解成规范形 `RealSet`（见 `solveInequality`）。

// 一个原子约束：多项式 ⋈ 0
class AtomConstraint {
public:
  AtomConstraint() = default;
  AtomConstraint(Polynomial expression, Relation relation) : expression_(std::move(expression)), relation_(relation) {}

  const Polynomial &expression() const { return expression_; }
  Relation relation() const { return relation_; }

  std::set<Variable> variables() const { return expression_.variables(); }

  // 点是否满足约束：把点代进去、精确判符号。
  // 代入后仍含未绑定变量 → UndefinedVariable；代入后分母为零 → 该点不在定义域内。
  Result<bool> admits(const Scope &point) const {
    const RationalFunction substituted = expression_.substitute(point);
    const Result<Fraction> constant = constraint_detail::constantValueOf(substituted);
    if (constant.isErr()) {
      return std::unexpected(constant.unwrapErr());
    }
    return satisfies(relation_, constant.unwrap());
  }

  // 取值可以是实代数数（例如把 x 代成 √2）
  Result<bool> admits(const AlgebraicScope &point) const {
    const Result<AlgebraicRationalFunction> substituted = toAlgebraic(expression_).substitute(point);
    if (substituted.isErr()) {
      return std::unexpected(substituted.unwrapErr());
    }
    const Result<RealAlgebraicNumber> value = substituted.unwrap().evaluate(point);
    if (value.isErr()) {
      return std::unexpected(value.unwrapErr());
    }
    const RealAlgebraicNumber number = value.unwrap();
    const RealAlgebraicNumber zero(Fraction(0, 1));
    switch (relation_) {
    case Relation::Equal:
      return number == zero;
    case Relation::NotEqual:
      return !(number == zero);
    case Relation::Less:
      return number < zero;
    case Relation::LessEqual:
      return !(zero < number);
    case Relation::Greater:
      return zero < number;
    case Relation::GreaterEqual:
      return !(number < zero);
    }
    return std::unexpected(MathsError::InvalidExpression);
  }

  bool operator==(const AtomConstraint &rhs) const {
    return expression_ == rhs.expression_ && relation_ == rhs.relation_;
  }

  // 原子是 `expression ⋈ 0`，右边那个 0 也写出来，读起来才完整
  std::string latex() const { return expression_.latex() + " " + relationLatex(relation_) + " 0"; }

private:
  static bool satisfies(Relation relation, const Fraction &value) {
    const bool positive = value > 0LL;
    const bool zero = value == 0LL;
    switch (relation) {
    case Relation::Equal:
      return zero;
    case Relation::NotEqual:
      return !zero;
    case Relation::Less:
      return !positive && !zero;
    case Relation::LessEqual:
      return !positive || zero;
    case Relation::Greater:
      return positive && !zero;
    case Relation::GreaterEqual:
      return positive || zero;
    }
    return false;
  }

  Polynomial expression_;
  Relation relation_{Relation::Equal};
};

// 若干原子约束的**合取**（空合取恒真）
class ConstraintSystem {
public:
  ConstraintSystem() = default;

  explicit ConstraintSystem(std::vector<AtomConstraint> atoms) : atoms_(std::move(atoms)) {}

  const std::vector<AtomConstraint> &atoms() const { return atoms_; }
  bool isTrivial() const { return atoms_.empty(); }

  std::set<Variable> variables() const {
    std::set<Variable> result;
    for (const AtomConstraint &atom : atoms_) {
      for (const Variable &variable : atom.variables()) {
        result.insert(variable);
      }
    }
    return result;
  }

  // 合取（交）
  ConstraintSystem andWith(const ConstraintSystem &rhs) const {
    std::vector<AtomConstraint> merged = atoms_;
    for (const AtomConstraint &atom : rhs.atoms_) {
      bool duplicate = false;
      for (const AtomConstraint &existing : merged) {
        if (existing == atom) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        merged.push_back(atom);
      }
    }
    return ConstraintSystem(std::move(merged));
  }

  // 点是否满足全部原子约束
  Result<bool> admits(const Scope &point) const {
    for (const AtomConstraint &atom : atoms_) {
      const Result<bool> satisfied = atom.admits(point);
      if (satisfied.isErr()) {
        return satisfied;
      }
      if (!satisfied.unwrap()) {
        return false;
      }
    }
    return true;
  }

  Result<bool> admits(const AlgebraicScope &point) const {
    for (const AtomConstraint &atom : atoms_) {
      const Result<bool> satisfied = atom.admits(point);
      if (satisfied.isErr()) {
        return satisfied;
      }
      if (!satisfied.unwrap()) {
        return false;
      }
    }
    return true;
  }

  // 可分离情形：每个原子只含一个变量时，点集就是各维一维实集的笛卡尔积。
  //
  // 返回 nullopt 表示**恒假**（某个不含变量的原子不成立）；
  // 报错表示不可分离（某个原子含两个以上变量）—— 那要靠消元，不是这里能给的。
  Result<std::optional<std::map<Variable, RealSet>>> asSeparable() const {
    std::map<Variable, RealSet> ranges;
    for (const AtomConstraint &atom : atoms_) {
      const std::set<Variable> variables = atom.variables();
      if (variables.empty()) {
        // 常数条件：直接判真假
        const Result<bool> satisfied = atom.admits(Scope());
        if (satisfied.isErr()) {
          return std::unexpected(satisfied.unwrapErr());
        }
        if (!satisfied.unwrap()) {
          return std::optional<std::map<Variable, RealSet>>(); // 恒假
        }
        continue;
      }
      if (variables.size() > 1) {
        return std::unexpected(MathsError::InvalidExpression); // 不可分离
      }
      const Variable target = *variables.begin();
      const std::optional<UnivariatePolynomial> polynomial = toUnivariatePolynomial(atom.expression(), target);
      if (!polynomial) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      const Result<RealSet> solved = RealSet::solve(*polynomial, atom.relation());
      if (solved.isErr()) {
        return std::unexpected(solved.unwrapErr());
      }
      const auto found = ranges.find(target);
      if (found == ranges.end()) {
        ranges.emplace(target, solved.unwrap());
        continue;
      }
      const Result<RealSet> narrowed = found->second.intersect(solved.unwrap());
      if (narrowed.isErr()) {
        return std::unexpected(narrowed.unwrapErr());
      }
      found->second = narrowed.unwrap();
    }
    return std::optional<std::map<Variable, RealSet>>(std::move(ranges));
  }

  std::string latex() const {
    if (atoms_.empty()) {
      return "\\text{恒真}";
    }
    std::string result;
    for (std::size_t index = 0; index < atoms_.size(); ++index) {
      if (index != 0) {
        result += " \\land ";
      }
      result += atoms_[index].latex();
    }
    return result;
  }

private:
  std::vector<AtomConstraint> atoms_;
};

} // namespace maths
