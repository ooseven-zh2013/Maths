export module maths.algebra:rational_multi;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import :expression;
import :scope;

// ==================== 多元有理函数 ====================
//
//     P(x₁, …, xₙ) / Q(x₁, …, xₙ)
//
// 一元那边是 `RationalFunction = RationalFunctionOf<Fraction>`（建在 `UnivariatePolynomial`
// 上，带天然定义域与 `discardedConstraints`）。多元这一层**只管分式本身** —— 定义域
// 是「{x : Q ≠ 0} ∩ 各自的根式条件」，那属于多元函数层的活（见 memory 里的多元计划）。
//
// ---- 不做规范形 ----
//
// 判等靠**交叉相乘**（P₁Q₂ == P₂Q₁），所以不需要约分到最简。只做两件廉价的事：
//
//   1. 约掉**公共单项式**（每个变量取最小指数）—— 便宜且安全
//   2. 分母首项系数为负时把分子分母同时取负 —— 让 `str()`/`latex()` 的符号稳定
//
// 不做完整多元 GCD（那也���硬骨头）。代价：反复运算会让 P/Q 变大、渲染变长 ——
// 与一元那边「不做最简根式化」的取舍一致。能算对，不保证最好看。
//
// ---- 求值只收有理赋值 ----
//
// `Polynomial::evaluate` 收 `Scope`（有理值），所以本类也是。要在代数点上求值得
// 另建 `Polynomial<RealAlgebraicNumber>` 那一路（跟塔现在的限制同源）。

export namespace maths {

class MultiRationalFunction {
public:
  MultiRationalFunction() : numerator_(Polynomial()), denominator_(one()) {}

  // 整数 / 分数常量
  explicit MultiRationalFunction(const Fraction &value) : numerator_(Monomial(value)), denominator_(one()) {}

  // 多项式（分母取 1）
  explicit MultiRationalFunction(const Polynomial &value) : numerator_(value), denominator_(one()) {}

  static Result<MultiRationalFunction> make(Polynomial numerator, Polynomial denominator) {
    if (denominator.isZero()) {
      return Result<MultiRationalFunction>::err(MathsError::ZeroDenominator);
    }
    MultiRationalFunction value(std::move(numerator), std::move(denominator));
    // 约掉的公共因子是「除掉过东西」，x/x → 1 要记住 x ≠ 0
    const std::set<Variable> canceled = value.cancelCommonMonomial();
    value.normalizeSign();
    return Result<MultiRationalFunction>(value.notingDiscarded(canceled));
  }

  const Polynomial &numerator() const { return numerator_; }
  const Polynomial &denominator() const { return denominator_; }
  const std::set<Variable> &discardedVariables() const { return discarded_; }

  std::set<Variable> variables() const {
    std::set<Variable> all = numerator_.variables();
    const std::set<Variable> fromDenominator = denominator_.variables();
    all.insert(fromDenominator.begin(), fromDenominator.end());
    return all;
  }

  bool isZero() const { return numerator_.isZero(); }
  bool isOne() const { return numerator_ == denominator_; }
  // 没有分母（或分母是常数）→ 能降回多项式
  bool isPolynomial() const { return denominator_.variables().empty(); }

  // ==================== 算术 ====================

  MultiRationalFunction operator+(const MultiRationalFunction &rhs) const {
    const Result<Polynomial> left = numerator_ * rhs.denominator_;
    if (left.isErr()) {
      return MultiRationalFunction(); // 溢出属于极端边界，不作为常规错误路径
    }
    const Result<Polynomial> right = rhs.numerator_ * denominator_;
    if (right.isErr()) {
      return MultiRationalFunction();
    }
    const Result<Polynomial> bottom = denominator_ * rhs.denominator_;
    if (bottom.isErr()) {
      return MultiRationalFunction();
    }
    const Result<MultiRationalFunction> sum = make(left.unwrap() + right.unwrap(), bottom.unwrap());
    return sum.isOk() ? sum.unwrap() : MultiRationalFunction();
  }

  MultiRationalFunction operator-(const MultiRationalFunction &rhs) const { return *this + (-rhs); }

  MultiRationalFunction operator-() const { return MultiRationalFunction(Polynomial() - numerator_, denominator_); }

  Result<MultiRationalFunction> operator*(const MultiRationalFunction &rhs) const {
    const Result<Polynomial> top = numerator_ * rhs.numerator_;
    if (top.isErr()) {
      return std::unexpected(top.unwrapErr());
    }
    const Result<Polynomial> bottom = denominator_ * rhs.denominator_;
    if (bottom.isErr()) {
      return std::unexpected(bottom.unwrapErr());
    }
    Result<MultiRationalFunction> product = make(top.unwrap(), bottom.unwrap());
    if (product.isErr()) {
      return product;
    }
    return Result<MultiRationalFunction>(product.unwrap().notingDiscarded(rhs.discardedVariables()));
  }

  // 记下「约掉过的变量」，供定义域用
  MultiRationalFunction notingDiscarded(const std::set<Variable> &variables) const {
    if (variables.empty()) {
      return *this;
    }
    MultiRationalFunction copy = *this;
    copy.discarded_.insert(variables.begin(), variables.end());
    return copy;
  }

  Result<MultiRationalFunction> operator/(const MultiRationalFunction &rhs) const {
    if (rhs.numerator_.isZero()) {
      return Result<MultiRationalFunction>::err(MathsError::ZeroDenominator);
    }
    const Result<Polynomial> top = numerator_ * rhs.denominator_;
    if (top.isErr()) {
      return std::unexpected(top.unwrapErr());
    }
    const Result<Polynomial> bottom = denominator_ * rhs.numerator_;
    if (bottom.isErr()) {
      return std::unexpected(bottom.unwrapErr());
    }
    Result<MultiRationalFunction> quotient = make(top.unwrap(), bottom.unwrap());
    if (quotient.isErr()) {
      return quotient;
    }
    // 被除掉的那些变量一个都不能丢：`x/x` → 1 要记住 x ≠ 0
    std::set<Variable> lost = rhs.discardedVariables();
    const std::set<Variable> fromDivisor = rhs.numerator_.variables();
    lost.insert(fromDivisor.begin(), fromDivisor.end());
    return Result<MultiRationalFunction>(quotient.unwrap().notingDiscarded(lost));
  }

  MultiRationalFunction &operator+=(const MultiRationalFunction &rhs) {
    *this = *this + rhs;
    return *this;
  }

  MultiRationalFunction &operator-=(const MultiRationalFunction &rhs) {
    *this = *this - rhs;
    return *this;
  }

  // ==================== 判等 ====================

  // 交叉相乘 —— **不需要规范形**就能判等
  bool operator==(const MultiRationalFunction &rhs) const {
    if (isZero() || rhs.isZero()) {
      return isZero() && rhs.isZero();
    }
    const Result<Polynomial> left = numerator_ * rhs.denominator_;
    const Result<Polynomial> right = rhs.numerator_ * denominator_;
    if (left.isErr() || right.isErr()) {
      return false;
    }
    return left.unwrap() == right.unwrap();
  }

  // ==================== 求值 ====================

  // 分母在这个点上为零时报 ZeroDenominator —— 那是「函数没定义」，不是算错
  Result<Fraction> evaluate(const Scope &scope) const {
    const Result<Fraction> bottom = denominator_.evaluate(scope);
    if (bottom.isErr()) {
      return std::unexpected(bottom.unwrapErr());
    }
    if (bottom.unwrap() == Fraction(0, 1)) {
      return Result<Fraction>::err(MathsError::ZeroDenominator);
    }
    const Result<Fraction> top = numerator_.evaluate(scope);
    if (top.isErr()) {
      return std::unexpected(top.unwrapErr());
    }
    return top.unwrap() / bottom.unwrap();
  }

  // ==================== 输出 ====================

  std::string str() const { return render(false); }
  std::string latex() const { return render(true); }

private:
  MultiRationalFunction(Polynomial numerator, Polynomial denominator)
      : numerator_(std::move(numerator)), denominator_(std::move(denominator)) {}

  static Polynomial one() { return Monomial(Fraction(1, 1)); }

  // 分子分母同除以公共单项式（每个变量取两侧最小指数）。返回被约掉的那些变量。
  std::set<Variable> cancelCommonMonomial() {
    const std::map<VarPowers, Fraction> &top = numerator_.getTerms();
    const std::map<VarPowers, Fraction> &bottom = denominator_.getTerms();
    VarPowers common;
    for (const auto &[factors, coefficient] : top) {
      for (const auto &[variable, power] : factors) {
        unsigned long long least = power;
        for (const auto &[otherFactors, otherCoefficient] : bottom) {
          // VarPowers 是 vector<pair<Variable, unsigned>>，没有 find，手写线性扫
          unsigned otherPower = 0;
          bool present = false;
          for (const auto &[otherVariable, otherExponent] : otherFactors) {
            if (otherVariable == variable) {
              otherPower = otherExponent;
              present = true;
              break;
            }
          }
          if (!present) {
            least = 0;
            break;
          }
          least = std::min<unsigned long long>(least, otherPower);
        }
        if (least > 0) {
          common.push_back({variable, least});
        }
      }
    }
    if (common.empty()) {
      return {};
    }
    const Polynomial divisor = Monomial(Fraction(1, 1), common);
    const Result<PolynomialDivision> topSplit = divideWithRemainder(numerator_, divisor);
    if (topSplit.isErr() || !topSplit.unwrap().remainder.isZero()) {
      return {}; // 除不尽就保持原样，不硬凑
    }
    const Result<PolynomialDivision> bottomSplit = divideWithRemainder(denominator_, divisor);
    if (bottomSplit.isErr() || !bottomSplit.unwrap().remainder.isZero()) {
      return {};
    }
    numerator_ = topSplit.unwrap().quotient;
    denominator_ = bottomSplit.unwrap().quotient;
    std::set<Variable> canceled;
    for (const auto &[variable, power] : common) {
      canceled.insert(variable);
    }
    return canceled;
  }

  // 分母首项系数为负 → 分子分母同时取负，让显示的符号稳定
  void normalizeSign() {
    const std::optional<Monomial> leading = leadingMonomial(denominator_);
    if (leading.has_value() && leading->getCoefficient().isNegative()) {
      numerator_ = Polynomial() - numerator_;
      denominator_ = Polynomial() - denominator_;
    }
  }

  std::string render(bool useLatex) const {
    if (isPolynomial()) {
      return useLatex ? numerator_.latex() : numerator_.str();
    }
    const std::string top = useLatex ? numerator_.latex() : numerator_.str();
    const std::string bottom = useLatex ? denominator_.latex() : denominator_.str();
    return useLatex ? "\\frac{" + top + "}{" + bottom + "}" : "(" + top + ")/(" + bottom + ")";
  }

  Polynomial numerator_;
  Polynomial denominator_;
  // 「约掉过的变量」：`x/x` 化简成 1，但 x ≠ 0 这条约束不能跟着消失 ——
  // 少了它，`at()` 会在 x = 0 处静默给出 1（那是本库最不能接受的一类错误）。
  std::set<Variable> discarded_;
};

} // namespace maths
