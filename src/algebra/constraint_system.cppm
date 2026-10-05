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

  // 恒真吗：表达式是常数、且那个常数满足这条关系（`1 ≠ 0`、`0 ≥ 0`……）
  //
  // ⚠️ `holdsFor` 在 domain_multi 里（本模块看不见它），所以这里内联一份 ——
  // 六条关系逐一写，别去引入跨模块的依赖。
  bool isTautology() const {
    if (!expression_.variables().empty()) {
      return false;
    }
    const Result<Fraction> value = expression_.evaluate(Scope());
    if (value.isErr()) {
      return false;
    }
    const Fraction constant = value.unwrap();
    switch (relation_) {
    case Relation::Equal:
      return constant == Fraction(0, 1);
    case Relation::NotEqual:
      return constant != Fraction(0, 1);
    case Relation::Less:
      return constant < Fraction(0, 1);
    case Relation::LessEqual:
      return constant <= Fraction(0, 1);
    case Relation::Greater:
      return constant > Fraction(0, 1);
    case Relation::GreaterEqual:
      return constant >= Fraction(0, 1);
    }
    return false;
  }

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
    // ⚠️ 恒真的原子不印出来。
    //
    // 符号条件天然会带一条 `分母 ≠ 0`，而分母是 1 的情形（`|x-y|`）就是 `1 ≠ 0` ——
    // 恒真，印出来只是噪音。判法：表达式是常数、且那个常数确实满足这条关系。
    std::vector<const AtomConstraint *> shown;
    for (const AtomConstraint &atom : atoms_) {
      if (atom.isTautology()) {
        continue;
      }
      shown.push_back(&atom);
    }
    if (shown.empty()) {
      return "\\text{恒真}";
    }
    std::string result;
    for (std::size_t index = 0; index < shown.size(); ++index) {
      if (index != 0) {
        result += " \\land ";
      }
      result += shown[index]->latex();
    }
    return result;
  }

private:
  std::vector<AtomConstraint> atoms_;
};

// ==================== 线性情形的投影：Fourier–Motzkin ====================
//
// 全是线性约束时，「消掉若干变量、看剩下哪些点可达」有精确的有限算法：
// 逐个变量消去，每轮把「这个变量有上界」与「有下界」的行两两组合，
// 消掉该变量得到一条不含它的新约束。这就是 Fourier–Motzkin 消元。
//
// 它给出的是**投影**：`{x ≥ y, x ≤ z}` 消掉 x 之后是 `y ≤ z` ——
// 也就是「(y,z) 平面上哪些点能被某个 x 补全成解」。
//
// 代价随约束条数增长（每轮组合可能平方级），但对本库的规模足够；一般半代数集
// 那层（非线性、含析取）仍要靠柱形代数分解，本库不做。

namespace linear_detail {

// 一条线性不等式：Σ aᵢxᵢ ≤ constant（strict 为真时是严格小于）
struct LinearRow {
  std::map<Variable, Fraction> coefficients;
  Fraction constant{0, 1};
  bool strict{false};

  bool hasVariables() const { return !coefficients.empty(); }
};

// 所有单项式次数 ≤ 1
inline bool isLinear(const Polynomial &polynomial) {
  for (const auto &entry : polynomial.getTerms()) {
    if (detail::degreeOf(entry.first) > 1) {
      return false;
    }
  }
  return true;
}

// 把一个原子化成 ≤ 形式；`=` 会拆成两条（≤ 与 ≥）
inline Result<std::vector<LinearRow>> toRows(const AtomConstraint &atom) {
  if (!isLinear(atom.expression())) {
    return std::unexpected(MathsError::InvalidExpression); // 非线性：FM 处理不了
  }

  LinearRow row;
  for (const auto &entry : atom.expression().getTerms()) {
    if (entry.first.empty()) {
      row.constant = entry.second;
      continue;
    }
    if (entry.first.size() != 1 || entry.first[0].second != 1) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    row.coefficients.emplace(entry.first[0].first, entry.second);
  }

  // p ⋈ 0，其中 p = Σ aᵢxᵢ + a₀，row 里存的是 aᵢ 与 a₀
  //   p ≤ 0  ⟺  Σ aᵢxᵢ ≤ −a₀        → 系数取 aᵢ，常数取 −a₀
  //   p ≥ 0  ⟺  Σ aᵢxᵢ ≥ −a₀ ⟺ −Σ aᵢxᵢ ≤ a₀  → 系数取 −aᵢ，常数取 +a₀
  const auto lessRow = [&row]() {
    LinearRow result = row;
    result.constant = -row.constant;
    return result;
  };
  const auto greaterRow = [&row]() {
    LinearRow result;
    result.constant = row.constant; // 注意是 +a₀：`≥` 移项后常数不过去
    for (const auto &[variable, value] : row.coefficients) {
      result.coefficients.emplace(variable, -value);
    }
    return result;
  };

  switch (atom.relation()) {
  case Relation::Less:
  case Relation::LessEqual: {
    LinearRow upper = lessRow();
    upper.strict = atom.relation() == Relation::Less;
    return std::vector<LinearRow>{upper};
  }
  case Relation::Greater:
  case Relation::GreaterEqual: {
    LinearRow upper = greaterRow();
    upper.strict = atom.relation() == Relation::Greater;
    return std::vector<LinearRow>{upper};
  }
  case Relation::Equal: {
    // 等式 = 两条不等式（方向相反，都不严格）
    return std::vector<LinearRow>{lessRow(), greaterRow()};
  }
  case Relation::NotEqual:
    // 「不等于」不是凸约束，Fourier–Motzkin 处理不了
    return std::unexpected(MathsError::InvalidExpression);
  }
  return std::unexpected(MathsError::InvalidExpression);
}

// 消去一个变量；返回 nullopt 表示这一轮已经能判定**无解**
inline std::optional<std::vector<LinearRow>> eliminateOne(const std::vector<LinearRow> &rows, const Variable &target) {
  std::vector<LinearRow> upper;
  std::vector<LinearRow> lower;
  std::vector<LinearRow> passthrough;
  for (const LinearRow &row : rows) {
    const auto found = row.coefficients.find(target);
    if (found == row.coefficients.end()) {
      passthrough.push_back(row);
      continue;
    }
    if (found->second > 0LL) {
      upper.push_back(row);
    } else {
      lower.push_back(row);
    }
  }

  std::vector<LinearRow> result = passthrough;
  for (const LinearRow &up : upper) {
    const Fraction positive = up.coefficients.at(target);
    for (const LinearRow &down : lower) {
      const Fraction negative = down.coefficients.at(target);
      // 由 A·x ≤ c_u − Σa 与 C·x ≤ c_d − Σc（A > 0 > C）链起来：
      //   x ≤ (c_u − Σa)/A  且  x ≥ (c_d − Σc)/C
      //   ⟹ (c_d − Σc)/C ≤ (c_u − Σa)/A  ⟹ 两边乘 A·C（负数，翻转）
      //   ⟹ A·(c_d − Σc) ≥ C·(c_u − Σa)
      //   ⟹ Σ (A·cᵢ − C·aᵢ) xᵢ ≤ A·c_d − C·c_u
      LinearRow combined;
      combined.strict = up.strict || down.strict; // 有一边严格，链式结论就严格
      for (const auto &[variable, value] : up.coefficients) {
        if (variable == target) {
          continue;
        }
        combined.coefficients[variable] = combined.coefficients[variable] + positive * value;
      }
      for (const auto &[variable, value] : down.coefficients) {
        if (variable == target) {
          continue;
        }
        combined.coefficients[variable] = combined.coefficients[variable] - negative * value;
      }
      combined.constant = positive * down.constant - negative * up.constant;
      combined.coefficients.erase(target);
      result.push_back(combined);
    }
  }

  // 清掉零系数，并检查常数行是否已经矛盾
  std::vector<LinearRow> cleaned;
  const Fraction zero(0, 1);
  for (LinearRow &row : result) {
    std::map<Variable, Fraction> nonzero;
    for (const auto &[variable, value] : row.coefficients) {
      if (value != 0LL) {
        nonzero.emplace(variable, value);
      }
    }
    row.coefficients = std::move(nonzero);
    if (row.hasVariables()) {
      cleaned.push_back(row);
      continue;
    }
    const bool holds = row.strict ? zero < row.constant : !(row.constant < zero);
    if (!holds) {
      return std::nullopt; // 0 ≤ 负数（或 0 < 非正数）：这组约束无解
    }
    // 恒真的常数行直接丢掉
  }
  return cleaned;
}

// 回到原子约束：Σ aᵢxᵢ ≤ c ⟺ Σ aᵢxᵢ − c ≤ 0
inline AtomConstraint toAtom(const LinearRow &row) {
  Polynomial expression;
  for (const auto &[variable, value] : row.coefficients) {
    expression.addTerm(VarPowers{{variable, 1ULL}}, value);
  }
  expression.addTerm(VarPowers{}, -row.constant);
  return AtomConstraint(std::move(expression), row.strict ? Relation::Less : Relation::LessEqual);
}

} // namespace linear_detail

// 线性约束组的投影：按变量名顺序消去指定变量（Fourier–Motzkin）。
//
// - 返回**空系统**表示投影后恒真（原来的约束对剩余的变量没有任何限制）
// - 返回 `nullopt` 表示这组约束**无解**
// - 报错表示不适用：含非线性原子，或含「不等于」（FM 处理不了非凸约束）
//
// 例：`{x ≥ y, x ≤ z}` 消去 x → `y ≤ z`。
inline Result<std::optional<ConstraintSystem>> projectLinear(const ConstraintSystem &system,
                                                             const std::set<Variable> &eliminate) {
  std::vector<linear_detail::LinearRow> rows;
  for (const AtomConstraint &atom : system.atoms()) {
    const Result<std::vector<linear_detail::LinearRow>> converted = linear_detail::toRows(atom);
    if (converted.isErr()) {
      return std::unexpected(converted.unwrapErr());
    }
    rows.insert(rows.end(), converted.unwrap().begin(), converted.unwrap().end());
  }

  for (const Variable &variable : eliminate) { // std::set 按名字升序，顺序确定
    const std::optional<std::vector<linear_detail::LinearRow>> reduced = linear_detail::eliminateOne(rows, variable);
    if (!reduced.has_value()) {
      return std::optional<ConstraintSystem>(); // 无解
    }
    rows = reduced.value();
  }

  std::vector<AtomConstraint> atoms;
  for (const linear_detail::LinearRow &row : rows) {
    if (!row.hasVariables()) {
      continue; // 到这一步剩下的常数行都恒真
    }
    atoms.push_back(linear_detail::toAtom(row));
  }
  return std::optional<ConstraintSystem>(ConstraintSystem(std::move(atoms)));
}

} // namespace maths
