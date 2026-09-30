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

export namespace maths {

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
  static Result<RealFunction> make(const RationalFunction &rule) { return make(RadicalExtension(rule)); }

  static Result<RealFunction> make(const RadicalExtension &rule) {
    const Result<Variable> variable = inferVariable(rule);
    if (variable.isErr()) {
      return std::unexpected(variable.unwrapErr());
    }
    const Result<RealSet> natural = domainOf(rule);
    if (natural.isErr()) {
      return std::unexpected(natural.unwrapErr());
    }
    return RealFunction(variable.unwrap(), rule, natural.unwrap());
  }

  // 显式给定义域：与规则的天然定义域**取交**，不是覆盖 ——
  // 规则算不出来的点永远不属于函数。f(x) = 1/x 配 [0,2] 得到 (0,2]。
  static Result<RealFunction> make(const RationalFunction &rule, const RealSet &domain) {
    return make(RadicalExtension(rule), domain);
  }

  static Result<RealFunction> make(const RadicalExtension &rule, const RealSet &domain) {
    const Result<RealFunction> base = make(rule);
    if (base.isErr()) {
      return std::unexpected(base.unwrapErr());
    }
    return base.unwrap().restrict(domain);
  }

  // 常函数。自变量名必须显式给（可以走默认值）：规则里没有变量，推不出来。
  static RealFunction constant(const Fraction &value, const Variable &variable = Variable("x")) {
    return RealFunction(variable, RadicalExtension(value), RealSet::realLine());
  }

  // ==================== 查询 ====================

  const Variable &variable() const { return variable_; }
  const RadicalExtension &rule() const { return rule_; }
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
    AlgebraicScope scope;
    const Result<void> assigned = scope.assign(variable_, point);
    if (assigned.isErr()) {
      return std::unexpected(assigned.unwrapErr());
    }
    return rule_.evaluate(scope);
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
    const Result<RadicalExtension> scaled = rule_ * RadicalExtension(factor);
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
    Scope scope;
    const Result<void> assigned = scope.assign(variable_, innerValue.unwrap());
    if (assigned.isErr()) {
      return std::unexpected(assigned.unwrapErr());
    }
    const Result<RadicalExtension> composed = rule_.substitute(scope);
    if (composed.isErr()) {
      return std::unexpected(composed.unwrapErr());
    }

    const Result<RealSet> pulled = inner.preimage(domain_);
    if (pulled.isErr()) {
      const Result<RealSet> natural = domainOf(rule_);
      if (natural.isErr() || !(natural.unwrap() == domain_)) {
        return std::unexpected(pulled.unwrapErr());
      }
      return make(composed.unwrap(), inner.domain_);
    }
    const Result<RealSet> effective = inner.domain_.intersect(pulled.unwrap());
    if (effective.isErr()) {
      return std::unexpected(effective.unwrapErr());
    }
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
  RealFunction(Variable variable, RadicalExtension rule, RealSet domain)
      : variable_(std::move(variable)), rule_(std::move(rule)), domain_(std::move(domain)) {}

  // 自变量：规则里有几个变量就取那个；没有变量（常函数）时给默认名 x；
  // 多于一个就是多元函数，本类型不收
  static Result<Variable> inferVariable(const RadicalExtension &rule) {
    const std::set<Variable> variables = rule.variables();
    if (variables.size() > 1) {
      return std::unexpected(MathsError::NotUnivariate);
    }
    if (variables.empty()) {
      return Variable("x");
    }
    return *variables.begin();
  }

  // 四则的公共骨架：定义域取交，规则由调用方合好。
  // 合出来的规则会带来自己的天然定义域，由 make 再取一次交 ——
  // 那一层不是多余的：定义域理论上应该等于两侧天然定义域之交，让 make 复核一遍最省心。
  Result<RealFunction> combine(const RealFunction &rhs, const Result<RadicalExtension> &merged) const {
    if (merged.isErr()) {
      return std::unexpected(merged.unwrapErr());
    }
    const Result<RealSet> domain = domain_.intersect(rhs.domain_);
    if (domain.isErr()) {
      return std::unexpected(domain.unwrapErr());
    }
    return make(merged.unwrap(), domain.unwrap());
  }

  Variable variable_{"x"};
  RadicalExtension rule_{Fraction(0, 1)};
  RealSet domain_;
};

inline std::ostream &operator<<(std::ostream &os, const RealFunction &value) { return os << value.str(); }

} // namespace maths
