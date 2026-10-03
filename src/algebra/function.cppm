export module maths.algebra:function;

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
import :rule;
import :tower;

export namespace maths {

namespace function_detail {

// 取一个落在 (low, high) 里的**有理**点，两端都是有限实代数数。
//
// 做法与 `RealSet::sampleIn` 相同：把两个隔离区间精化到分离，再取中间的有理数。
// 「有理」是关键 —— 代入后是精确有理运算，判号不会因为近似而错。
inline std::optional<Fraction> rationalBetween(RealAlgebraicNumber low, RealAlgebraicNumber high) {
  for (int attempt = 0; attempt < 256; ++attempt) {
    if (low.upperBound() < high.lowerBound()) {
      return (low.upperBound() + high.lowerBound()) * Fraction(1, 2);
    }
    low.refine();
    high.refine();
  }
  return std::nullopt;
}

// 一段连通块上的像集信息。三类值必须分开记，因为最后那个区间的端点**取到与否**
// 全靠这个区分：临界点与闭端点上的值是取到的，开端点上的单侧极限不是，
// 而 ±∞ 连值都不是。
struct PieceImage {
  std::vector<RealAlgebraicNumber> attained; // 取到的值
  std::vector<RealAlgebraicNumber> limits;   // 开端点的有限极限（不取到）
  bool toPositiveInfinity{false};
  bool toNegativeInfinity{false};
};

} // namespace function_detail

// 一元实函数 `f: S ⊆ ℝ → ℝ`：**规则 + 定义域**。
//
// 这是「集合 → 实数」的落点：定义域本身就是一个 `RealSet`（区间的有限并），端点是实代数数，
// 所以 `√(x²−1)` 的定义域 `(−∞,−1] ∪ [1,+∞)` 是精确的，不是浮点近似出来的区间。
//
// 规则用 `RadicalExtension` 承载，于是这一个类型同时覆盖三类函数：
//
//   f(x) = 3x² − 1              纯有理函数
//   f(x) = 1/(x−1)              分式（定义域自动去掉 x = 1）
//   f(x) = √(x²+1) + √(x²+2)    根号里带变量（本库的招牌形状）
//
// 定义域是**存下来的**，不是每次现算：`restrict` 可以把它收窄（把 f 限制在 [0,1] 上），
// 之后求值、四则、复合都按收窄后的定义域走 —— 所以「同一个规则 + 不同定义域」是两个函数。
//
// ======================= 四条刻意的边界 =======================
//
// 1) **一元**。规则里出现两个以上自变量时报 `NotUnivariate`：多元函数是另一件事
//    （定义域是多维点集，走 AtomConstraint / ConstraintSystem）。要「多元」就先用
//    `Scope` 部分代入把自变量一个个消掉，剩下的自然接上本类型。
//
// 2) **系数是有理函数**（`RadicalExtension` 的系数类型就是 ℚ(x)）。所以 `√2·x` 这类
//    「代数数当系数」的函数不在本类型里，那是 `AlgebraicRationalFunction` 的领域；
//    本类型管的是「**根号里带变量**」。反过来取值结果用 `RealAlgebraicNumber`，
//    所以 √2 这种值完全装得下。
//
// 3) **复合的内层不能含根号**（`compose` 要求内层是有理函数），外层含根号没问题：
//    f(y) = √(y+1) 配 g(x) = x²+1 直接得 √(x²+2)。内层含根号就是「根式套根式」，
//    需要多重二次扩张的张量积，本库不做。
//
// 4) **`preimage` 要求规则不含根号，且目标集合的端点是有理数**。前者因为
//    「√f(x) ∈ [a,b]」得逐根号做单调性推理；后者因为解不等式时端点要进多项式系数
//    （`Polynomial` 的系数是 ℚ）。两条都不满足时明确报错，不近似。

class RealFunction {
public:
  // ==================== 构造 ====================

  // 从规则推出自变量与天然定义域：分母的零点、被开方数 < 0 的地方都自动剔掉
  // 规则可以是根式，也可以是一条塔（`FunctionRule` 包装两者）
  static Result<RealFunction> make(const FunctionRule &rule) {
    const Result<Variable> variable = inferVariable(rule);
    if (variable.isErr()) {
      return std::unexpected(variable.unwrapErr());
    }
    const Result<RealSet> natural = definitionDomainOf(rule, variable.unwrap());
    if (natural.isErr()) {
      return std::unexpected(natural.unwrapErr());
    }
    return RealFunction(variable.unwrap(), rule, natural.unwrap());
  }

  static Result<RealFunction> make(const RationalFunction &rule) { return make(FunctionRule::rational(rule)); }

  static Result<RealFunction> make(const RadicalExtension &rule) { return make(FunctionRule::radicalOf(rule)); }

  // 显式给定义域：与规则的天然定义域**取交**，不是覆盖 ——
  // 规则算不出来的点永远不属于函数。f(x) = 1/x 配 [0,2] 得到 (0,2]。
  static Result<RealFunction> make(const FunctionRule &rule, const RealSet &domain) {
    const Result<RealFunction> base = make(rule);
    if (base.isErr()) {
      return std::unexpected(base.unwrapErr());
    }
    return base.unwrap().restrict(domain);
  }

  static Result<RealFunction> make(const RationalFunction &rule, const RealSet &domain) {
    return make(FunctionRule::rational(rule), domain);
  }

  static Result<RealFunction> make(const RadicalExtension &rule, const RealSet &domain) {
    return make(FunctionRule::radicalOf(rule), domain);
  }

  // 常函数。自变量名必须显式给（可以走默认值）：规则里没有变量，推不出来。
  static RealFunction constant(const Fraction &value, const Variable &variable = Variable("x")) {
    return RealFunction(variable, FunctionRule::rational(RationalFunction(value)), RealSet::realLine());
  }

  // ==================== 查询 ====================

  const Variable &variable() const { return variable_; }
  const FunctionRule &rule() const { return rule_; }
  const RealSet &domain() const { return domain_; }

  // 规则里真正出现的自变量（常函数时为空集）
  std::set<Variable> variables() const { return rule_.variables(); }

  bool isConstant() const { return rule_.variables().empty(); }

  // 规则里没有根号 —— 只有这时 `preimage` 可用
  bool isRational() const { return rule_.isRadicalFree(); }

  // ==================== 求值 ====================

  // 点在定义域外报 OutsideDomain，不是「变量未定义」—— 那是没给值，这是给了值但该点不属于函数
  Result<RealAlgebraicNumber> at(const RealAlgebraicNumber &point) const {
    if (!domain_.contains(point)) {
      return std::unexpected(MathsError::OutsideDomain);
    }
    return valueAt(point);
  }

  Result<RealAlgebraicNumber> at(const Fraction &point) const { return at(RealAlgebraicNumber(point)); }

  // ==================== 定义域 ====================

  // 把定义域收窄到 subset（取交）。结果是新函数，原函数不变。
  Result<RealFunction> restrict(const RealSet &subset) const {
    const Result<RealSet> narrowed = domain_.intersect(subset);
    if (narrowed.isErr()) {
      return std::unexpected(narrowed.unwrapErr());
    }
    return RealFunction(variable_, rule_, narrowed.unwrap());
  }

  // ==================== 四则 ====================
  // 定义域取交：`√x · √(x−1)` 只在 x ≥ 1 上有定义，即使乘积规则恰好在别处也能算。

  Result<RealFunction> operator+(const RealFunction &rhs) const { return combine(rhs, rule_ + rhs.rule_); }
  Result<RealFunction> operator-(const RealFunction &rhs) const { return combine(rhs, rule_ - rhs.rule_); }
  Result<RealFunction> operator*(const RealFunction &rhs) const { return combine(rhs, rule_ * rhs.rule_); }
  Result<RealFunction> operator/(const RealFunction &rhs) const { return combine(rhs, rule_ / rhs.rule_); }

  // 取负不会失败（规则取负是逐系数变号，不涉及扩域）
  RealFunction negate() const { return RealFunction(variable_, -rule_, domain_); }

  Result<RealFunction> scaledBy(const Fraction &factor) const {
    const Result<FunctionRule> scaled = rule_ * FunctionRule::rational(RationalFunction(factor));
    if (scaled.isErr()) {
      return std::unexpected(scaled.unwrapErr());
    }
    return make(scaled.unwrap(), domain_);
  }

  // ==================== 复合 ====================
  //
  // this ∘ inner：`f(y) = √(y+1)` 配 `g(x) = x²+1` 得 `√(x²+2)`。
  //
  // 定义域是 `{x ∈ dom(g) : g(x) ∈ dom(f)}`。这个集合有两种算法：
  //   - 一般情形走 `inner.preimage(domain_)`（精确，但要求端点是有理数）
  //   - f 没有被 restrict 过时，复合规则的**天然定义域**恰好等于 `g⁻¹(dom f)`
  //     （定义域怎么定义的，代入后就是怎么定义的），走这条便宜的等价路径
  // 两条都不通时明确报错，不拿「天然定义域」蒙混 —— 那会把定义域悄悄放大。
  Result<RealFunction> compose(const RealFunction &inner) const {
    if (!inner.rule_.isRadicalFree()) {
      return std::unexpected(MathsError::NotARational); // 根式套根式：不做
    }
    const Result<RationalFunction> innerValue = inner.rule_.toRationalFunction();
    if (innerValue.isErr()) {
      return std::unexpected(innerValue.unwrapErr());
    }

    // 代入的目标变量。**内外同名时必须先给外层换个临时名**：
    // `Scope::assign(x, 含 x 的式子)` 是方程 x = x² 而不是赋值，被明确拒收 ——
    // 而 `f(x) = √(x+1)` 配 `g(x) = x²+1` 这种内外同名恰恰是最常见的复合。
    // 换个名字再代，语义完全一样，最后结果的自变量由复合后的规则自己推出来（就是内层的）。
    Variable target = variable_;
    FunctionRule outerRule = rule_;
    if (innerValue.unwrap().containsVariable(variable_)) {
      std::set<Variable> taken = innerValue.unwrap().variables();
      for (const Variable &variable : variables()) {
        taken.insert(variable);
      }
      const Variable temporary = freshVariable(taken);
      Scope rename;
      const Result<void> renamed = rename.assign(variable_, RationalFunction(variablePolynomial(temporary)));
      if (renamed.isErr()) {
        return std::unexpected(renamed.unwrapErr());
      }
      const Result<FunctionRule> renamedRule = rule_.substitute(rename);
      if (renamedRule.isErr()) {
        return std::unexpected(renamedRule.unwrapErr());
      }
      outerRule = renamedRule.unwrap();
      target = temporary;
    }

    Scope scope;
    const Result<void> assigned = scope.assign(target, innerValue.unwrap());
    if (assigned.isErr()) {
      return std::unexpected(assigned.unwrapErr());
    }
    const Result<FunctionRule> composed = outerRule.substitute(scope);
    if (composed.isErr()) {
      return std::unexpected(composed.unwrapErr());
    }

    const Result<RealSet> pulled = inner.preimage(domain_);
    if (pulled.isErr()) {
      const Result<RealSet> natural = definitionDomainOf(rule_, variable_);
      if (natural.isErr() || !(natural.unwrap() == domain_)) {
        return std::unexpected(pulled.unwrapErr());
      }
      return make(composed.unwrap(), inner.domain_);
    }
    const Result<RealSet> effective = inner.domain_.intersect(pulled.unwrap());
    if (effective.isErr()) {
      return std::unexpected(effective.unwrapErr());
    }
    // 结果的自变量由 make 从复合后的规则推出来，就是内层的那个
    return make(composed.unwrap(), effective.unwrap());
  }

  // ==================== 集合：拉回 ====================
  //
  // `g⁻¹(T) ∩ 定义域`：把目标集合沿函数拉回去。一元有理函数是精确可做的 ——
  // `g(x) ∈ [a,b]` 就是两条不等式 `g − a ≥ 0` 与 `g − b ≤ 0`，`solveInequality` 现成的。
  //
  // 目标集合是若干个**互不相交**的区间，所以逐区间求拉回再并起来就是全集
  // （拉回保持并集运算，区间不交则各拉回之间也不会重复）。
  Result<RealSet> preimage(const RealSet &target) const {
    if (!isRational()) {
      return std::unexpected(MathsError::NotARational); // 含根号的规则：逐根号单调性推理，不做
    }
    if (target.isRealLine()) {
      return domain_;
    }
    const Result<RationalFunction> rule = rule_.toRationalFunction();
    if (rule.isErr()) {
      return std::unexpected(rule.unwrapErr());
    }

    RealSet result = RealSet::empty();
    for (const Interval &interval : target.intervals()) {
      Result<RealSet> piece = domain_;
      for (const bool isLower : {true, false}) {
        const Bound &bound = isLower ? interval.lower : interval.upper;
        if (bound.isInfinite()) {
          continue; // 这一侧没有条件
        }
        const Result<Fraction> endpoint = bound.value().toFraction();
        if (endpoint.isErr()) {
          return std::unexpected(MathsError::NotARational); // 端点不是有理数，进不了多项式系数
        }
        const Relation relation = isLower ? (bound.isClosed() ? Relation::GreaterEqual : Relation::Greater)
                                          : (bound.isClosed() ? Relation::LessEqual : Relation::Less);
        const Result<RealSet> condition = solveInequality(rule.unwrap() - endpoint.unwrap(), relation);
        if (condition.isErr()) {
          return std::unexpected(condition.unwrapErr());
        }
        const Result<RealSet> narrowed = piece.unwrap().intersect(condition.unwrap());
        if (narrowed.isErr()) {
          return std::unexpected(narrowed.unwrapErr());
        }
        piece = narrowed.unwrap();
      }
      result = result.unite(piece.unwrap());
    }
    return result;
  }

  // ==================== 集合：像集 ====================
  //
  // `f(S) = {f(x) : x ∈ S}`，S 先与定义域取交。这是「集合 → 实数」的另一半
  // （另一半是 `preimage`，把集合沿函数拉回去）。
  //
  // 算法：有理函数处处可导，临界点（g′ 的实根）把它切成单调段，于是
  // **每个连通块的像都是一个区间**，两端取遍
  //   {临界点上的值} ∪ {闭端点的值} ∪ {开端点的单侧极限}
  // 的 min / max；端点取到与否，就看这个极值是「取到的」还是「只是极限」——
  // 所以那三类值必须分开收集，不能一视同仁地丢进一个 vector。
  //
  // 全程精确：临界点是隔离出的实代数数，极限都有精确表达式
  // （次数比 → 有理数；开端点有定义 → 就是该点的值；极点 → ±∞）。
  Result<RealSet> image(const RealSet &subset) const {
    if (!isRational()) {
      return std::unexpected(MathsError::NotARational); // 含根号的规则要先做逐根号单调性推理，不做
    }
    const Result<RealSet> source = domain_.intersect(subset);
    if (source.isErr()) {
      return std::unexpected(source.unwrapErr());
    }
    if (source.unwrap().isEmpty()) {
      return RealSet::empty();
    }
    if (isConstant()) {
      // 常函数：像集就是那一个点。规则里没有变量，空作用域就能求值
      const Result<RealAlgebraicNumber> value = rule_.evaluate(AlgebraicScope());
      if (value.isErr()) {
        return std::unexpected(value.unwrapErr());
      }
      return RealSet::point(value.unwrap());
    }

    const Result<RationalFunction> rule = rule_.toRationalFunction();
    if (rule.isErr()) {
      return std::unexpected(rule.unwrapErr());
    }
    const std::optional<UnivariatePolynomial> numerator =
        toUnivariatePolynomial(rule.unwrap().getNumerator(), variable_);
    const std::optional<UnivariatePolynomial> denominator =
        toUnivariatePolynomial(rule.unwrap().getDenominator(), variable_);
    if (!numerator || !denominator) {
      return std::unexpected(MathsError::InvalidExpression);
    }

    // g′ = (p′q − pq′) / q²，分母是平方恒不为负，所以临界点就是分子的实根
    const UnivariatePolynomial stationary =
        numerator->derivative() * *denominator - *numerator * denominator->derivative();
    const std::vector<RealAlgebraicNumber> criticalPoints =
        stationary.isZero() ? std::vector<RealAlgebraicNumber>() : RealAlgebraicNumber::realRoots(stationary);

    // 符号格的边界：g 只在 p、q 的零点处变号。极点一侧的极限是 +∞ 还是 −∞，
    // 靠「该侧相邻格内取一个有理样本判号」得到
    std::vector<RealAlgebraicNumber> boundaries = RealAlgebraicNumber::realRoots(*numerator);
    for (const RealAlgebraicNumber &root : RealAlgebraicNumber::realRoots(*denominator)) {
      boundaries.push_back(root);
    }
    std::sort(boundaries.begin(), boundaries.end(), [](const RealAlgebraicNumber &lhs, const RealAlgebraicNumber &rhs) {
      return lhs.compareTo(rhs) == std::strong_ordering::less;
    });

    RealSet result = RealSet::empty();
    for (const Interval &piece : source.unwrap().intervals()) {
      const Result<RealSet> rendered =
          imageOfPiece(piece, rule.unwrap(), *numerator, *denominator, criticalPoints, boundaries);
      if (rendered.isErr()) {
        return std::unexpected(rendered.unwrapErr());
      }
      result = result.unite(rendered.unwrap());
    }
    return result;
  }

  // 值域：`f(定义域)`
  Result<RealSet> range() const { return image(domain_); }

  // ==================== 输出 ====================

  std::string ruleStr() const { return rule_.str(); }
  std::string ruleLatex() const { return rule_.latex(); }
  std::string domainStr() const { return domain_.str(); }
  std::string domainLatex() const { return domain_.latex(); }

  // 定义域是整条实轴时不啰嗦；被收窄过的才把 `x ∈ …` 缀上 ——
  // 那正是这个函数与「同规则、无限制」的那个函数的区别所在
  std::string str() const {
    if (domain_.isRealLine()) {
      return rule_.str();
    }
    return rule_.str() + " , " + variable_.str() + " in " + domain_.str();
  }

  std::string latex() const {
    if (domain_.isRealLine()) {
      return rule_.latex();
    }
    return rule_.latex() + ",\\quad " + variable_.str() + " \\in " + domain_.latex();
  }

private:
  RealFunction(Variable variable, FunctionRule rule, RealSet domain)
      : variable_(std::move(variable)), rule_(std::move(rule)), domain_(std::move(domain)) {}

  // 自变量：规则里有几个变量就取那个；没有变量（常函数）时给默认名 x；
  // 多于一个就是多元函数，本类型不收
  static Result<Variable> inferVariable(const FunctionRule &rule) {
    const std::set<Variable> variables = rule.variables();
    if (variables.size() > 1) {
      return std::unexpected(MathsError::NotUnivariate);
    }
    if (variables.empty()) {
      return Variable("x");
    }
    return *variables.begin();
  }

  // 规则自带的定义域（分母零点、被开方数 ≥ 0），**再加上化简时丢掉的那些约束**。
  //
  // `x/x` 会被化简成 `1`，但 `discardedConstraints` 记着「约掉过 x」—— 也就是 x ≠ 0。
  // 不把它算进来，函数就会在 x = 0 上给出 1：那是静默给错，本库最不能接受的一类错误。
  // 只收**与自变量同名**的约束；不同名说明那个变量已经被约得不再是自变量
  // （如 `√x·(y/y)` 在 x 上看），那种参数上的条件留给调用方按约束处理。
  static Result<RealSet> definitionDomainOf(const FunctionRule &rule, const Variable &variable) {
    const Result<RealSet> natural = rule.domain();
    if (natural.isErr()) {
      return std::unexpected(natural.unwrapErr());
    }
    if (!dropsVariable(rule, variable)) {
      return natural;
    }
    const Result<RealSet> atZero = RealSet::point(RealAlgebraicNumber(Fraction(0, 1)));
    if (atZero.isErr()) {
      return std::unexpected(atZero.unwrapErr());
    }
    const Result<RealSet> punctured = atZero.unwrap().complement();
    if (punctured.isErr()) {
      return std::unexpected(punctured.unwrapErr());
    }
    return natural.unwrap().intersect(punctured.unwrap());
  }

  // 保证不重名的临时变量名：只由字母组成，逐个加长直到没被用过。
  // 复合时外层要先改名成它，才能避开 `Scope::assign` 的「右边不得含被赋值变量」这条。
  static Variable freshVariable(const std::set<Variable> &taken) {
    std::string name = "t";
    while (taken.count(Variable(name)) != 0) {
      name += "t";
    }
    return Variable(name);
  }

  static bool dropsVariable(const FunctionRule &rule, const Variable &variable) {
    const auto dropped = [&variable](const RationalFunction &value) {
      return value.discardedConstraints().count(variable) != 0;
    };
    if (rule.holdsTower()) {
      // 塔：每一项的系数都可能带着「约掉过某个变量」的痕迹
      for (const RationalFunction &coefficient : rule.asTower().coefficients()) {
        if (dropped(coefficient)) {
          return true;
        }
      }
      return false;
    }
    for (const RationalFunction &coefficient : rule.asRadical().coefficients()) {
      if (dropped(coefficient)) {
        return true;
      }
    }
    for (const RationalFunction &radicand : rule.asRadical().radicands()) {
      if (dropped(radicand)) {
        return true;
      }
    }
    return false;
  }

  // 四则的公共骨架：定义域取交，规则由调用方合好。
  // 合出来的规则会带来自己的天然定义域，由 make 再取一次交 ——
  // 那一层不是多余的：定义域理论上应该等于两侧天然定义域之交，让 make 复核一遍最省心。
  Result<RealFunction> combine(const RealFunction &rhs, const Result<FunctionRule> &merged) const {
    if (merged.isErr()) {
      return std::unexpected(merged.unwrapErr());
    }
    const Result<RealSet> domain = domain_.intersect(rhs.domain_);
    if (domain.isErr()) {
      return std::unexpected(domain.unwrapErr());
    }
    return make(merged.unwrap(), domain.unwrap());
  }

  // 不做定义域检查的求值：调用方已经确认这个点在定义域内
  Result<RealAlgebraicNumber> valueAt(const RealAlgebraicNumber &point) const {
    AlgebraicScope scope;
    const Result<void> assigned = scope.assign(variable_, point);
    if (assigned.isErr()) {
      return std::unexpected(assigned.unwrapErr());
    }
    return rule_.evaluate(scope);
  }

  // ==================== 像集的内部计算 ====================

  Result<RealSet> imageOfPiece(const Interval &piece, const RationalFunction &rule,
                               const UnivariatePolynomial &numerator, const UnivariatePolynomial &denominator,
                               const std::vector<RealAlgebraicNumber> &criticalPoints,
                               const std::vector<RealAlgebraicNumber> &boundaries) const {
    function_detail::PieceImage info;

    // 落在这一块上的临界点（闭端点上的临界点也算，`contains` 已经照顾到取到性）
    for (const RealAlgebraicNumber &point : criticalPoints) {
      if (!piece.contains(point)) {
        continue;
      }
      const Result<RealAlgebraicNumber> value = valueAt(point);
      if (value.isErr()) {
        return std::unexpected(value.unwrapErr());
      }
      info.attained.push_back(value.unwrap());
    }

    const Result<RealSet> natural = rule_.domain();
    if (natural.isErr()) {
      return std::unexpected(natural.unwrapErr());
    }

    for (const bool isLower : {true, false}) {
      const Bound &bound = isLower ? piece.lower : piece.upper;
      if (bound.isInfinite()) {
        addLimitAtInfinity(info, numerator, denominator, isLower);
        continue;
      }
      const RealAlgebraicNumber &endpoint = bound.value();
      if (piece.contains(endpoint)) {
        const Result<RealAlgebraicNumber> value = valueAt(endpoint); // 闭端点：值取到
        if (value.isErr()) {
          return std::unexpected(value.unwrapErr());
        }
        info.attained.push_back(value.unwrap());
        continue;
      }
      // 开端点。它是「子集给的开口」还是「极点」，决定了这一头是有限极限还是无穷
      if (natural.unwrap().contains(endpoint)) {
        const Result<RealAlgebraicNumber> value = valueAt(endpoint);
        if (value.isErr()) {
          return std::unexpected(value.unwrapErr());
        }
        info.limits.push_back(value.unwrap()); // 规则在这里有定义 → 单侧极限就是函数值，但不取到
        continue;
      }
      const Result<bool> positive = isPositiveBesidePole(rule, endpoint, isLower, boundaries);
      if (positive.isErr()) {
        return std::unexpected(positive.unwrapErr());
      }
      if (positive.unwrap()) {
        info.toPositiveInfinity = true;
      } else {
        info.toNegativeInfinity = true;
      }
    }
    return assemblePieceImage(info);
  }

  // x → ±∞ 时 g = p/q 的极限。三种情形，都不含近似：
  //   deg p < deg q → 0        deg p = deg q → 首项系数比        deg p > deg q → ±∞
  // 无穷远取不到，所以前两种进 limits（有限但不取到）。
  // 次数更大时的方向：符号 = sign(lc p / lc q)，只有 x → −∞ 且次数差为奇数才翻转。
  static void addLimitAtInfinity(function_detail::PieceImage &info, const UnivariatePolynomial &numerator,
                                 const UnivariatePolynomial &denominator, bool atMinusInfinity) {
    if (numerator.degree() > denominator.degree()) {
      const Result<Fraction> ratio = numerator.leadingCoefficient() / denominator.leadingCoefficient();
      bool positive = ratio.isOk() && ratio.unwrap() > 0LL;
      if (atMinusInfinity && (numerator.degree() - denominator.degree()) % 2 == 1) {
        positive = !positive;
      }
      // 只置位、不清另一位：区间的两头是独立的，x³ 在 −∞ 跑向 −∞ 而在 +∞ 跑向 +∞，
      // 两头都得记下来，不能后一次调用把前一次的结果抹掉
      if (positive) {
        info.toPositiveInfinity = true;
      } else {
        info.toNegativeInfinity = true;
      }
      return;
    }
    const Result<Fraction> ratio = numerator.degree() < denominator.degree()
                                       ? Result<Fraction>(Fraction(0, 1))
                                       : numerator.leadingCoefficient() / denominator.leadingCoefficient();
    if (ratio.isOk()) {
      info.limits.push_back(RealAlgebraicNumber(ratio.unwrap()));
    }
  }

  // 极点一侧的极限符号。
  //
  // g 只在 p、q 的零点处变号，所以相邻两个「符号格边界」之间符号恒定；找出该方向上
  // 最近的一个边界，在格内取一个有理样本判号即可 —— 样本不必落在这块区间里，
  // 因为整格同号，而这一头紧邻极点的部分就在该格里。
  Result<bool> isPositiveBesidePole(const RationalFunction &rule, const RealAlgebraicNumber &pole, bool isLower,
                                    const std::vector<RealAlgebraicNumber> &boundaries) const {
    const bool rightSide = isLower; // 极点在下端 → 考察 x → pole⁺
    std::optional<RealAlgebraicNumber> neighbor;
    for (const RealAlgebraicNumber &boundary : boundaries) {
      const std::strong_ordering order = boundary.compareTo(pole);
      if (order == std::strong_ordering::equal) {
        continue;
      }
      if (rightSide ? order != std::strong_ordering::greater : order != std::strong_ordering::less) {
        continue;
      }
      const bool closer =
          !neighbor.has_value() || (rightSide ? boundary.compareTo(*neighbor) == std::strong_ordering::less
                                              : boundary.compareTo(*neighbor) == std::strong_ordering::greater);
      if (closer) {
        neighbor = boundary;
      }
    }

    Fraction sample(0, 1);
    if (neighbor.has_value()) {
      const std::optional<Fraction> between = rightSide ? function_detail::rationalBetween(pole, *neighbor)
                                                        : function_detail::rationalBetween(*neighbor, pole);
      if (!between.has_value()) {
        return std::unexpected(MathsError::InvalidRange);
      }
      sample = *between;
    } else {
      // 该方向上再没有符号格边界，格一直延伸到无穷：取隔离区间外侧的有理点
      sample = rightSide ? pole.upperBound() + Fraction(1, 1) : pole.lowerBound() - Fraction(1, 1);
    }

    Scope scope;
    const Result<void> assigned = scope.assign(variable_, sample);
    if (assigned.isErr()) {
      return std::unexpected(assigned.unwrapErr());
    }
    const Result<Fraction> value = rule.evaluate(scope);
    if (value.isErr()) {
      return std::unexpected(value.unwrapErr());
    }
    return value.unwrap() > 0LL;
  }

  // 把三类信息拼成一个区间。关键是最后那个 `constantOnPiece`：
  // min == max 时函数在整块上恒等于该值（像是单点集），那时它当然取到；
  // 但如果有一头跑到无穷去了，这就不是「常数」，min == max 只是候选值恰好只有一个，
  // 不能因为相等就把端点标成取到（−1/x 在 (0,∞) 上候选值只有 0，像却是 (−∞,0)）。
  static Result<RealSet> assemblePieceImage(const function_detail::PieceImage &info) {
    std::vector<RealAlgebraicNumber> candidates = info.attained;
    candidates.insert(candidates.end(), info.limits.begin(), info.limits.end());
    if (candidates.empty()) {
      if (!info.toNegativeInfinity || !info.toPositiveInfinity) {
        return std::unexpected(MathsError::InvalidExpression); // 两个方向都没兜住，说明漏了一条分支
      }
      return RealSet::realLine();
    }

    RealAlgebraicNumber minimum = candidates.front();
    RealAlgebraicNumber maximum = candidates.front();
    for (const RealAlgebraicNumber &value : candidates) {
      if (value.compareTo(minimum) == std::strong_ordering::less) {
        minimum = value;
      }
      if (value.compareTo(maximum) == std::strong_ordering::greater) {
        maximum = value;
      }
    }

    const auto isAttained = [&info](const RealAlgebraicNumber &value) {
      for (const RealAlgebraicNumber &attained : info.attained) {
        if (attained == value) {
          return true;
        }
      }
      return false;
    };

    const bool constantOnPiece = minimum == maximum && !info.toNegativeInfinity && !info.toPositiveInfinity;
    const Bound lower = info.toNegativeInfinity ? Bound::negativeInfinity()
                                                : Bound::finite(minimum, constantOnPiece || isAttained(minimum));
    const Bound upper = info.toPositiveInfinity ? Bound::positiveInfinity()
                                                : Bound::finite(maximum, constantOnPiece || isAttained(maximum));
    return RealSet::make({Interval{lower, upper}});
  }

  Variable variable_{"x"};
  FunctionRule rule_{RationalFunction(Fraction(0, 1))};
  RealSet domain_;
};

inline std::ostream &operator<<(std::ostream &os, const RealFunction &value) { return os << value.str(); }

} // namespace maths
