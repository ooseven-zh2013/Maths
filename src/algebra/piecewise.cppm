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

class PiecewiseFunction {
public:
  // ==================== 构造 ====================

  // 归一化：丢掉空定义域的分支、把后面分支与前面对不交的部分减掉。
  static Result<PiecewiseFunction> make(const std::vector<RealFunction> &cases) {
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

  // 若本函数恰好是 `|g|`，返回 g；否则 nullopt。
  //
  // 这是「输出时把 √(g²) 还原成 |g|」那一步。内部一律按符号分段计算 ——
  // 代数函数域装不下 `√(g²)`，那是域论限制，不是表示能力不足 —— 但**外部该显示
  // `|g|` 就显示 `|g|`**，别让一个绝对值在结果里躺成两行 cases。
  std::optional<RationalFunction> asAbsoluteValue() const {
    if (cases_.size() != 2) {
      return std::nullopt;
    }
    for (std::size_t index = 0; index < 2; ++index) { // 两支谁在前都行
      const RealFunction &head = cases_[index];
      const RealFunction &tail = cases_[1 - index];
      if (!head.isRational() || !tail.isRational()) {
        continue; // 规则里带根号：不是 |g| 这种形状
      }
      const Result<RationalFunction> magnitude = head.rule().toRationalFunction();
      if (magnitude.isErr()) {
        continue;
      }
      // 正的那支必须恰好落在「g ≥ 0」上，负的那支恰好落在「g < 0」上。
      //
      // ⚠️ 这里必须**各自解一次**而不是拿互补来凑：`|x/(x−1)|` 的两支定义域是
      // (−∞,0]∪(1,+∞) 与 (0,1)，x = 1 两边都不在（g 那里是极点）——
      // 所以「一支的补集」并不等于另一支，x = 1 会凭空冒出来。
      const Result<RealSet> positive = solveInequality(magnitude.unwrap(), Relation::GreaterEqual);
      const Result<RealSet> negative = solveInequality(magnitude.unwrap(), Relation::Less);
      if (positive.isErr() || negative.isErr() || !(positive.unwrap() == head.domain()) ||
          !(negative.unwrap() == tail.domain())) {
        continue;
      }
      if (!(tail.rule() == FunctionRule::rational(RationalFunction(-magnitude.unwrap())))) {
        continue;
      }
      return magnitude.unwrap();
    }
    return std::nullopt;
  }

  // ==================== 复合 ====================
  // `this ∘ inner`：外层逐支去复合内层。
  //
  // `RealFunction::compose` 会把每支的定义域算成 `inner⁻¹(E_j) ∩ dom(inner)` ——
  // 外层各支不交，复出来的定义域自然也不交，`make` 直接收下。
  // 内层是分段时就是「外层各支 × 内层各支」。
  //
  // 内层含根号会失败（`NotARational`）：那是「根式套根式」，本库不做。
  // 所以 `√|x|` 可以（|x| 的两支都是有理函数），`√(√x)` 不行。

  Result<PiecewiseFunction> compose(const RealFunction &inner) const {
    std::vector<RealFunction> branches;
    for (const RealFunction &outer : cases_) {
      const Result<RealFunction> composed = outer.compose(inner);
      if (composed.isErr()) {
        return std::unexpected(composed.unwrapErr());
      }
      branches.push_back(composed.unwrap());
    }
    return make(std::move(branches));
  }

  Result<PiecewiseFunction> compose(const PiecewiseFunction &inner) const {
    std::vector<RealFunction> branches;
    for (const RealFunction &outer : cases_) {
      for (const RealFunction &innerBranch : inner.cases_) {
        const Result<RealFunction> composed = outer.compose(innerBranch);
        if (composed.isErr()) {
          return std::unexpected(composed.unwrapErr());
        }
        branches.push_back(composed.unwrap());
      }
    }
    return make(std::move(branches));
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
  return RealFunction::make(RationalFunction(variablePolynomial(variable)));
}

// 绝对值 `|x|`：x ≥ 0 时是 x，x < 0 时是 −x。
//
// 这里用 `max(x, −x)` 来写，正好把「拐点在 0」这件事交给 `maximumOf` 去解不等式，
// 不需要手搓分支。
inline Result<PiecewiseFunction> absoluteValue(const Variable &variable = Variable("x"));

// 逐点取大 / 取小。
//
// `max(f, g) = f 在 f ≥ g 的地方，g 在其余地方`，所以只要能把 `f − g ≥ 0` 的解集
// 精确解出来就成立。**差的生成元个数 ≤ 1** 时做得到：
//
//   0 个（纯有理函数）→ `solveInequality` 直接给
//   1 个（`a + b√f`） → 见下面 `whereNonNegative` 的三情形化归
//
// 再多就不做了（报 `NotARational`）：那要更深的代数数不等式推理。
inline Result<PiecewiseFunction> maximumOf(const RealFunction &lhs, const RealFunction &rhs);
inline Result<PiecewiseFunction> minimumOf(const RealFunction &lhs, const RealFunction &rhs);

namespace piecewise_detail {

// 「差 ≥ 0」的解集。三种情形，全程精确，没有一处近似。
inline Result<RealSet> whereNonNegative(const RealFunction &difference) {
  const FunctionRule &rule = difference.rule();
  const Result<RealSet> domain = Result<RealSet>(difference.domain()); // 差本身有定义的地方

  // ---- 纯有理函数：直接解不等式 ----
  if (rule.isRadicalFree()) {
    const Result<RationalFunction> rational = rule.toRationalFunction();
    if (rational.isErr()) {
      return std::unexpected(rational.unwrapErr());
    }
    const Result<RealSet> solved = solveInequality(rational.unwrap(), Relation::GreaterEqual);
    if (solved.isErr()) {
      return std::unexpected(solved.unwrapErr());
    }
    return solved.unwrap().intersect(domain.unwrap());
  }

  // ---- 一个生成元：差 = a + b√f ----
  if (rule.holdsTower() || rule.asRadical().radicands().size() != 1) {
    return std::unexpected(MathsError::NotARational); // 两个以上根号：不做
  }
  const RationalFunction &a = rule.asRadical().coefficient(0);
  const RationalFunction &b = rule.asRadical().coefficient(1);
  const RationalFunction &radicand = rule.asRadical().radicands().front();

  const Result<RationalFunction> quotient = a / b; // a / b，也就是 −h 里的 h 取反
  if (quotient.isErr()) {
    return std::unexpected(quotient.unwrapErr());
  }
  const RationalFunction &ratio = quotient.unwrap();
  const RationalFunction comparison = radicand - ratio * ratio; // f − (a/b)²

  // √f ≥ h ⟺ h ≤ 0 ∨ f ≥ h²；√f ≤ h ⟺ h ≥ 0 ∧ f ≤ h²（h = −a/b）
  const auto atLeastRatio = [&ratio, &comparison]() -> Result<RealSet> {
    const Result<RealSet> ratioNonNegative = solveInequality(ratio, Relation::GreaterEqual); // −(a/b) ≤ 0
    const Result<RealSet> squared = solveInequality(comparison, Relation::GreaterEqual);     // f ≥ (a/b)²
    if (ratioNonNegative.isErr()) {
      return std::unexpected(ratioNonNegative.unwrapErr());
    }
    if (squared.isErr()) {
      return std::unexpected(squared.unwrapErr());
    }
    return RealSet(ratioNonNegative.unwrap().unite(squared.unwrap()));
  };
  const auto atMostRatio = [&ratio, &comparison]() -> Result<RealSet> {
    const Result<RealSet> ratioNonPositive = solveInequality(ratio, Relation::LessEqual);
    const Result<RealSet> squared = solveInequality(comparison, Relation::LessEqual);
    if (ratioNonPositive.isErr()) {
      return std::unexpected(ratioNonPositive.unwrapErr());
    }
    if (squared.isErr()) {
      return std::unexpected(squared.unwrapErr());
    }
    return ratioNonPositive.unwrap().intersect(squared.unwrap());
  };

  // b 是 x 的函数，符号随 x 变 —— 三种情形都要留着，最后取并
  const Result<RealSet> bPositive = solveInequality(b, Relation::Greater);
  const Result<RealSet> bNegative = solveInequality(b, Relation::Less);
  const Result<RealSet> bZero = solveInequality(b, Relation::Equal);
  const Result<RealSet> bNonNegative = solveInequality(a, Relation::GreaterEqual); // b = 0 时退化成 a ≥ 0
  for (const Result<RealSet> *step : {&bPositive, &bNegative, &bZero, &bNonNegative}) {
    if (step->isErr()) {
      return std::unexpected(step->unwrapErr());
    }
  }

  const Result<RealSet> first = atLeastRatio();
  const Result<RealSet> second = atMostRatio();
  if (first.isErr()) {
    return std::unexpected(first.unwrapErr());
  }
  if (second.isErr()) {
    return std::unexpected(second.unwrapErr());
  }
  const Result<RealSet> positiveBranch = bPositive.unwrap().intersect(first.unwrap());
  const Result<RealSet> negativeBranch = bNegative.unwrap().intersect(second.unwrap());
  const Result<RealSet> zeroBranch = bZero.unwrap().intersect(bNonNegative.unwrap());
  for (const Result<RealSet> *step : {&positiveBranch, &negativeBranch, &zeroBranch}) {
    if (step->isErr()) {
      return std::unexpected(step->unwrapErr());
    }
  }

  const RealSet combined = positiveBranch.unwrap().unite(negativeBranch.unwrap()).unite(zeroBranch.unwrap());
  return combined.intersect(domain.unwrap());
}

// max / min 的共同部分：解出「lhs ⋈ rhs 成立」的那半边，两边各自取一支。
//
// ⚠️ 两支都必须落在 `difference.domain()` 里 —— max 只在**两边都有定义**的地方有意义。
// 只与 `lhs.domain()` / `rhs.domain()` 取交是不够的：`max(1/x, x)` 的两支分别是
// `1/x` 与 `x`，而 `x` 自己的定义域是整条实轴，不拦住的话 x = 0 上会冒出一个值。
inline Result<PiecewiseFunction> splitBy(const RealFunction &lhs, const RealFunction &rhs, bool takeLhsWhenGreater) {
  const Result<RealFunction> difference = lhs - rhs;
  if (difference.isErr()) {
    return std::unexpected(difference.unwrapErr());
  }
  const RealSet both = difference.unwrap().domain();
  const Result<RealSet> first = whereNonNegative(difference.unwrap());
  if (first.isErr()) {
    return std::unexpected(first.unwrapErr());
  }
  const Result<RealSet> outside = first.unwrap().complement();
  if (outside.isErr()) {
    return std::unexpected(outside.unwrapErr());
  }
  const Result<RealSet> second = both.intersect(outside.unwrap());
  if (second.isErr()) {
    return std::unexpected(second.unwrapErr());
  }

  const Result<RealSet> main = takeLhsWhenGreater ? first : second;
  const Result<RealSet> other = takeLhsWhenGreater ? second : first;
  const Result<RealFunction> mainCase = lhs.restrict(main.unwrap());
  if (mainCase.isErr()) {
    return std::unexpected(mainCase.unwrapErr());
  }
  const Result<RealFunction> otherCase = rhs.restrict(other.unwrap());
  if (otherCase.isErr()) {
    return std::unexpected(otherCase.unwrapErr());
  }
  return PiecewiseFunction::make({mainCase.unwrap(), otherCase.unwrap()});
}

} // namespace piecewise_detail

inline Result<PiecewiseFunction> maximumOf(const RealFunction &lhs, const RealFunction &rhs) {
  return piecewise_detail::splitBy(lhs, rhs, true); // 取 lhs 的地方：lhs ≥ rhs
}

inline Result<PiecewiseFunction> minimumOf(const RealFunction &lhs, const RealFunction &rhs) {
  return piecewise_detail::splitBy(lhs, rhs, false); // 取 lhs 的地方：lhs ≤ rhs
}

// f ∘ g：外层是单规则函数，内层是分段函数。逐支复合即可 ——
// 内层第 i 支的定义域就是外层的取值区间，复出来的定义域 `g⁻¹(dom f) ∩ D_i` 由
// `RealFunction::compose` 自己算，分支之间天然不交。
inline Result<PiecewiseFunction> compose(const RealFunction &outer, const PiecewiseFunction &inner) {
  std::vector<RealFunction> branches;
  for (const RealFunction &innerBranch : inner.cases()) {
    const Result<RealFunction> composed = outer.compose(innerBranch);
    if (composed.isErr()) {
      return std::unexpected(composed.unwrapErr());
    }
    branches.push_back(composed.unwrap());
  }
  return PiecewiseFunction::make(std::move(branches));
}

inline Result<PiecewiseFunction> absoluteValue(const Variable &variable) {
  const Result<RealFunction> identity = identityFunction(variable);
  if (identity.isErr()) {
    return std::unexpected(identity.unwrapErr());
  }
  return maximumOf(identity.unwrap(), identity.unwrap().negate());
}

} // namespace maths
