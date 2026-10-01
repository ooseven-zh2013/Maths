export module maths.algebra:piecewise;

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
import :radical;
import :function;

export namespace maths {

// 分段函数：**有限个分支**的并，每个分支就是一个 `RealFunction`。
//
// 为什么必须有这个类型：`RealFunction` 的规则只能是 `RadicalExtension`，而那种元素的
// 每个成员都是**在定义域上有理（因而连续）**的函数。`|x|` 不是 —— 它在 0 处拐弯，
// 不可能是任何一条规则。所以「绝对值」这一整类函数需要一个分支容器来装。
//
//   |x| = {  x , x ≥ 0
//         { −x , x < 0
//
// ⚠️ 这**不违反**「本库不引入 |x|」那条红线。红线的原话是：`√(x²)` 在代数函数域
// ℚ(x)[y]/(y²−x²) 里绕不过去（环可约、y 是零因子、±x 都是根，没有单值元素），
// 所以 `RadicalExtension` **拒收**它 —— 那条限制管的是**规则之内**。
// `|x|` 作为 ℝ → ℝ 的函数本身当然定义良好，只是表示不了成单条规则而已。
//
// ============================ 三条约定 ============================
//
// 1) **先写的分支优先**。构造时后面的分支会把前面已经覆盖的部分减掉，所以
//    内部存下来的分支一定**两两不交**、且按给定顺序排列。于是求值就是
//    「找到第一个包含该点的分支」。
//
// 2) **自变量必须一致**。常函数分支不参与约束（它没有自变量），其余分支的
//    自变量名必须相同，否则报 `NotUnivariate`。
//
// 3) **像集是各分支像之并**。分支不交 + 每支的像是一个区间，所以并起来就是真像，
//    不需要再做别的。这是分段表示最划算的一条：`RealFunction::image` 直接复用。
//
// 与 `RealFunction` 的关系：只有一个分支时可以用 `toRealFunction()` 降回去。

namespace piecewise_detail {

// 自变量本身作为一个多项式（`x`）。用来搭 |x| 这类分段函数。
inline Polynomial variablePolynomial(const Variable &variable) {
  return Polynomial(Monomial(Fraction(1, 1), VarPowers{{variable, 1ULL}}));
}

} // namespace piecewise_detail

class PiecewiseFunction {
public:
  // ==================== 构造 ====================

  // 归一化：丢掉空定义域的分支、把后面分支与前面对不交的部分减掉。
  static Result<PiecewiseFunction> make(std::vector<RealFunction> cases) {
    std::vector<RealFunction> accepted;
    std::optional<Variable> variable;
    for (const RealFunction &candidate : cases) {
      if (candidate.domain().isEmpty()) {
        continue; // 空分支直接丢：它不贡献任何点
      }
      if (!candidate.isConstant()) {
        if (!variable.has_value()) {
          variable = candidate.variable();
        } else if (!(*variable == candidate.variable())) {
          return std::unexpected(MathsError::NotUnivariate); // 分支之间自变量不同名
        }
      }
      // 减掉已经被前面分支覆盖的部分 —— 这就是「先写的优先」
      RealSet remaining = candidate.domain();
      for (const RealFunction &earlier : accepted) {
        const Result<RealSet> uncovered = earlier.domain().complement();
        if (uncovered.isErr()) {
          return std::unexpected(uncovered.unwrapErr());
        }
        const Result<RealSet> left = remaining.intersect(uncovered.unwrap());
        if (left.isErr()) {
          return std::unexpected(left.unwrapErr());
        }
        remaining = left.unwrap();
      }
      if (remaining.isEmpty()) {
        continue; // 被前面完全盖住了
      }
      const Result<RealFunction> clipped = candidate.restrict(remaining);
      if (clipped.isErr()) {
        return std::unexpected(clipped.unwrapErr());
      }
      accepted.push_back(clipped.unwrap());
    }
    if (accepted.empty()) {
      return std::unexpected(MathsError::InvalidExpression); // 处处无定义的东西不是函数
    }
    return PiecewiseFunction(std::move(accepted), variable.value_or(Variable("x")));
  }

  // ==================== 查询 ====================

  const std::vector<RealFunction> &cases() const { return cases_; }
  std::size_t branchCount() const { return cases_.size(); }
  const RealFunction &branch(std::size_t index) const { return cases_[index]; }
  const Variable &variable() const { return variable_; }
  bool isSingleBranch() const { return cases_.size() == 1; }

  // 各分支定义域之并。分支两两不交，所以 `unite` 不会真的合并出什么新东西
  RealSet domain() const {
    RealSet result = RealSet::empty();
    for (const RealFunction &branchFunction : cases_) {
      result = result.unite(branchFunction.domain());
    }
    return result;
  }

  // 只有一个分支时降回普通函数
  Result<RealFunction> toRealFunction() const {
    if (!isSingleBranch()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    return cases_.front();
  }

  // ==================== 求值 ====================
  // 分支两两不交，所以最多命中一个；一个都不命中就是该点无定义。

  Result<RealAlgebraicNumber> at(const RealAlgebraicNumber &point) const {
    for (const RealFunction &branchFunction : cases_) {
      if (branchFunction.domain().contains(point)) {
        return branchFunction.at(point);
      }
    }
    return std::unexpected(MathsError::OutsideDomain);
  }

  Result<RealAlgebraicNumber> at(const Fraction &point) const { return at(RealAlgebraicNumber(point)); }

  // ==================== 定义域 ====================

  Result<PiecewiseFunction> restrict(const RealSet &subset) const {
    std::vector<RealFunction> narrowed;
    for (const RealFunction &branchFunction : cases_) {
      const Result<RealFunction> clipped = branchFunction.restrict(subset);
      if (clipped.isErr()) {
        return std::unexpected(clipped.unwrapErr());
      }
      narrowed.push_back(clipped.unwrap());
    }
    return make(std::move(narrowed));
  }

  // ==================== 像集 ====================
  // 各分支像之并。每支的像是一个区间（`RealFunction::image` 已经保证），
  // 分支又不交，所以并起来就是真像。

  Result<RealSet> image(const RealSet &subset) const {
    RealSet result = RealSet::empty();
    for (const RealFunction &branchFunction : cases_) {
      const Result<RealSet> rendered = branchFunction.image(subset);
      if (rendered.isErr()) {
        return std::unexpected(rendered.unwrapErr());
      }
      result = result.unite(rendered.unwrap());
    }
    return result;
  }

  Result<RealSet> range() const { return image(RealSet::realLine()); }

  // ==================== 四则 ====================
  // 分支两两配对：点落在第 i 支且第 j 支上的区域就是 D_i ∩ E_j，
  // 而 D_i、E_j 各自两两不交，所以配出来的块也两两不交 ——
  // 于是合并规则就是逐块套用单规则的四则，不需要额外的划分逻辑。

  Result<PiecewiseFunction> operator+(const PiecewiseFunction &rhs) const {
    return pairwise(rhs, [](const RealFunction &lhsRule, const RealFunction &rhsRule) { return lhsRule + rhsRule; });
  }
  Result<PiecewiseFunction> operator-(const PiecewiseFunction &rhs) const {
    return pairwise(rhs, [](const RealFunction &lhsRule, const RealFunction &rhsRule) { return lhsRule - rhsRule; });
  }
  Result<PiecewiseFunction> operator*(const PiecewiseFunction &rhs) const {
    return pairwise(rhs, [](const RealFunction &lhsRule, const RealFunction &rhsRule) { return lhsRule * rhsRule; });
  }
  Result<PiecewiseFunction> operator/(const PiecewiseFunction &rhs) const {
    return pairwise(rhs, [](const RealFunction &lhsRule, const RealFunction &rhsRule) { return lhsRule / rhsRule; });
  }

  // 与单规则函数（或常数）运算：右操作数当作只有一个分支
  Result<PiecewiseFunction> operator+(const RealFunction &rhs) const { return *this + single(rhs); }
  Result<PiecewiseFunction> operator-(const RealFunction &rhs) const { return *this - single(rhs); }
  Result<PiecewiseFunction> operator*(const RealFunction &rhs) const { return *this * single(rhs); }
  Result<PiecewiseFunction> operator/(const RealFunction &rhs) const { return *this / single(rhs); }

  // 逐分支取负 / 数乘，不涉及分支合并
  PiecewiseFunction negate() const {
    std::vector<RealFunction> flipped;
    flipped.reserve(cases_.size());
    for (const RealFunction &branchFunction : cases_) {
      flipped.push_back(branchFunction.negate());
    }
    return PiecewiseFunction(std::move(flipped), variable_);
  }

  Result<PiecewiseFunction> scaledBy(const Fraction &factor) const {
    std::vector<RealFunction> scaled;
    scaled.reserve(cases_.size());
    for (const RealFunction &branchFunction : cases_) {
      const Result<RealFunction> value = branchFunction.scaledBy(factor);
      if (value.isErr()) {
        return std::unexpected(value.unwrapErr());
      }
      scaled.push_back(value.unwrap());
    }
    return make(std::move(scaled));
  }

  // ==================== 输出 ====================

  // 终端用一行表示，分支之间用 `;` 隔开
  std::string str() const {
    std::string result = "{";
    for (std::size_t index = 0; index < cases_.size(); ++index) {
      if (index != 0) {
        result += "; ";
      }
      result += cases_[index].ruleStr() + " , " + variable_.str() + " in " + cases_[index].domainStr();
    }
    result += "}";
    return result;
  }

  // 排版走 LaTeX 的 cases 环境
  std::string latex() const {
    std::string result = "\\begin{cases}";
    for (std::size_t index = 0; index < cases_.size(); ++index) {
      if (index != 0) {
        result += " \\\\ ";
      }
      result += cases_[index].ruleLatex() + " & " + variable_.str() + " \\in " + cases_[index].domainLatex();
    }
    result += "\\end{cases}";
    return result;
  }

private:
  PiecewiseFunction(std::vector<RealFunction> cases, Variable variable)
      : cases_(std::move(cases)), variable_(std::move(variable)) {}

  static PiecewiseFunction single(const RealFunction &rule) { return PiecewiseFunction({rule}, rule.variable()); }

  template <class Combine> Result<PiecewiseFunction> pairwise(const PiecewiseFunction &rhs, Combine operation) const {
    std::vector<RealFunction> combined;
    for (const RealFunction &lhsRule : cases_) {
      for (const RealFunction &rhsRule : rhs.cases_) {
        const Result<RealFunction> piece = operation(lhsRule, rhsRule);
        if (piece.isErr()) {
          return std::unexpected(piece.unwrapErr());
        }
        combined.push_back(piece.unwrap()); // 空定义域的块由 make 丢掉
      }
    }
    return make(std::move(combined));
  }

  std::vector<RealFunction> cases_;
  Variable variable_{"x"};
};

inline std::ostream &operator<<(std::ostream &os, const PiecewiseFunction &value) { return os << value.str(); }

// ==================== 由分段派生出来的常用函数 ====================

// 自变量本身的函数（`f(x) = x`）
inline Result<RealFunction> identityFunction(const Variable &variable = Variable("x")) {
  return RealFunction::make(RationalFunction(piecewise_detail::variablePolynomial(variable)));
}

// 绝对值 `|x|`：x ≥ 0 时是 x，x < 0 时是 −x。
//
// 这里用 `max(x, −x)` 来写，正好把「拐点在 0」这件事交给 `maximumOf` 去解不等式，
// 不需要手搓分支。
inline Result<PiecewiseFunction> absoluteValue(const Variable &variable = Variable("x"));

// 逐点取大 / 取小。
//
// `max(f, g) = f 在 f ≥ g 的地方，g 在其余地方` —— 分界点就是 `f − g = 0` 的根，
// 交给 `solveInequality` 精确解出。所以**要求两条规则都是有理函数**：
// 含根号时要解含根号的不等式，那是另一套推理，本库不做。
inline Result<PiecewiseFunction> maximumOf(const RealFunction &lhs, const RealFunction &rhs);
inline Result<PiecewiseFunction> minimumOf(const RealFunction &lhs, const RealFunction &rhs);

namespace piecewise_detail {

// max / min 的共同部分：解出 `lhs ⋈ rhs` 成立的那半边，两边各自取一支
inline Result<PiecewiseFunction> splitBy(const RealFunction &lhs, const RealFunction &rhs, Relation relation) {
  if (!lhs.isRational() || !rhs.isRational()) {
    return std::unexpected(MathsError::NotARational); // 含根号的不等式：不做
  }
  const Result<RealFunction> difference = lhs - rhs;
  if (difference.isErr()) {
    return std::unexpected(difference.unwrapErr());
  }
  const Result<RationalFunction> rule = difference.unwrap().rule().toRationalFunction();
  if (rule.isErr()) {
    return std::unexpected(rule.unwrapErr());
  }
  const Result<RealSet> first = solveInequality(rule.unwrap(), relation);
  if (first.isErr()) {
    return std::unexpected(first.unwrapErr());
  }
  const Result<RealSet> second = first.unwrap().complement();
  if (second.isErr()) {
    return std::unexpected(second.unwrapErr());
  }

  const Result<RealFunction> firstCase = lhs.restrict(first.unwrap());
  if (firstCase.isErr()) {
    return std::unexpected(firstCase.unwrapErr());
  }
  const Result<RealFunction> secondCase = rhs.restrict(second.unwrap());
  if (secondCase.isErr()) {
    return std::unexpected(secondCase.unwrapErr());
  }
  return PiecewiseFunction::make({firstCase.unwrap(), secondCase.unwrap()});
}

} // namespace piecewise_detail

inline Result<PiecewiseFunction> maximumOf(const RealFunction &lhs, const RealFunction &rhs) {
  return piecewise_detail::splitBy(lhs, rhs, Relation::GreaterEqual); // 取 lhs 的地方：lhs ≥ rhs
}

inline Result<PiecewiseFunction> minimumOf(const RealFunction &lhs, const RealFunction &rhs) {
  return piecewise_detail::splitBy(lhs, rhs, Relation::LessEqual); // 取 lhs 的地方：lhs ≤ rhs
}

inline Result<PiecewiseFunction> absoluteValue(const Variable &variable) {
  const Result<RealFunction> identity = identityFunction(variable);
  if (identity.isErr()) {
    return std::unexpected(identity.unwrapErr());
  }
  return maximumOf(identity.unwrap(), identity.unwrap().negate());
}

} // namespace maths
