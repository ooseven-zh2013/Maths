export module maths.algebra:quadratic;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import :expression;
import :rational;
import :scope;

export namespace maths {

// 二次扩张 ℚ(x)[y]/(y² − f)：把「根号下是有理函数」这件事表示成精确的代数元素。
//
//   a + b·y      （a, b ∈ ℚ(x)，y² = f）
//
// 用它就能把 `\sqrt{x}`、`\sqrt{x^2 - 1}` 这类**含有变量的根号**当成精确对象来算：
// 加法、乘法、除法都封闭（y² = f 就是那条约束），代入有理数还能落到实代数数。
//
// ============================ 三条刻意的限制 ============================
//
// 1) **被开方数必须是非平方的有理函数**。f 是 ℚ(x) 里的平方时（`√(x²)`、`√((x+1)²)`），
//    y² − f 在 ℚ(x)[y] 里可约 —— 环不是域、y 是零因子、± 两个根都成立，
//    「√(x²)」根本没有单值元素。本库不引入 |x| 节点，所以**明确拒收**
//    （RadicandIsSquare），而不是猜一个分支：那正是「静默给错」。
//    判定用「分子分母各自无重根」——这是平方性的**充分否证**（squarefree 且非常数
//    就不可能是平方），保守但不误判。
//
// 2) **不做最简根式化**：f 与 f·c² 是不同的表示（√8 与 2√2 不相等），
//    与实代数数那边既有决策一致 —— 精确优先，不为了好看引入整数分解。
//
// 3) **只支持单变量**。多变量的根号要更大的代数结构，留待后续。
//
// 与既有类型的关系：`AlgebraicRationalFunction` 是「系数取代数数」（√2 是系数），
// 本类型是「根号包里带变量」（√x 是元素）—— 两件事，别混。

namespace quadratic_detail {

// 把（单变量的）多项式转成 ℚ[x] 上的一元多项式，好复用那边已经测过的平方自由化。
// 含多于一个变量时返回 nullopt。
inline std::optional<UnivariatePolynomial> toUnivariate(const Polynomial &polynomial, const Variable &variable) {
  std::vector<Fraction> coefficients(polynomial.degree() + 1, Fraction(0, 1));
  for (const auto &[factors, coefficient] : polynomial.getTerms()) {
    unsigned long long exponent = 0;
    for (const auto &factor : factors) {
      if (factor.first != variable) {
        return std::nullopt; // 多变量：本类型不支持
      }
      exponent = factor.second;
    }
    if (exponent >= coefficients.size()) {
      return std::nullopt;
    }
    coefficients[exponent] = coefficient;
  }
  return UnivariatePolynomial(std::move(coefficients));
}

} // namespace quadratic_detail

class QuadraticExtension {
public:
  QuadraticExtension() = default; // 零元：0 + 0·y

  // √f
  static Result<QuadraticExtension> make(const RationalFunction &radicand) {
    return make(RationalFunction(Fraction(0, 1)), RationalFunction(Fraction(1, 1)), radicand);
  }

  // a + b√f。b = 0 是合法的（这时元素就是有理函数 a，例如 √x·√x = x、y − y = 0），
  // 想取回它用 toRationalFunction()。
  static Result<QuadraticExtension> make(const RationalFunction &rationalPart, const RationalFunction &radicalPart,
                                         const RationalFunction &radicand) {
    const Result<std::optional<Variable>> variable = singleVariableOf(radicand);
    if (variable.isErr()) {
      return std::unexpected(variable.unwrapErr());
    }
    if (!variable.unwrap().has_value()) {
      return std::unexpected(MathsError::InvalidExpression); // 常数被开方数请走实代数数
    }

    QuadraticExtension result;
    result.rationalPart_ = rationalPart;
    result.radicalPart_ = radicalPart;
    result.radicand_ = radicand;
    result.variable_ = *variable.unwrap();
    return result;
  }

  const RationalFunction &radicand() const { return radicand_; }
  const RationalFunction &rationalPart() const { return rationalPart_; }
  const RationalFunction &radicalPart() const { return radicalPart_; }
  const Variable &variable() const { return variable_; }

  bool operator==(const QuadraticExtension &rhs) const {
    // 元素在基 {1, y} 下的坐标唯一，逐坐标比即可（不需要因式分解）。
    // 注意：f 不同的两个元素分处不同的域，这里一律判不等 —— 那是「不同域」而不是「不等」。
    return radicand_ == rhs.radicand_ && rationalPart_ == rhs.rationalPart_ && radicalPart_ == rhs.radicalPart_;
  }

  // ==================== 四则 ====================
  // 同 f 才能相加相乘；f 不同就得升到更大的域，本类型做不到，明确报错。

  Result<QuadraticExtension> operator+(const QuadraticExtension &rhs) const {
    if (const Result<void> compatible = ensureSameField(rhs); compatible.isErr()) {
      return std::unexpected(compatible.unwrapErr());
    }
    return make(rationalPart_ + rhs.rationalPart_, radicalPart_ + rhs.radicalPart_, radicand_);
  }

  Result<QuadraticExtension> operator-(const QuadraticExtension &rhs) const {
    if (const Result<void> compatible = ensureSameField(rhs); compatible.isErr()) {
      return std::unexpected(compatible.unwrapErr());
    }
    return make(rationalPart_ - rhs.rationalPart_, radicalPart_ - rhs.radicalPart_, radicand_);
  }

  QuadraticExtension operator-() const {
    QuadraticExtension result = *this;
    result.rationalPart_ = -result.rationalPart_;
    result.radicalPart_ = -result.radicalPart_;
    return result;
  }

  // (a1 + b1y)(a2 + b2y) = (a1a2 + b1b2·f) + (a1b2 + a2b1)·y
  Result<QuadraticExtension> operator*(const QuadraticExtension &rhs) const {
    if (const Result<void> compatible = ensureSameField(rhs); compatible.isErr()) {
      return std::unexpected(compatible.unwrapErr());
    }
    const RationalFunction rationalPart =
        rationalPart_ * rhs.rationalPart_ + radicalPart_ * rhs.radicalPart_ * radicand_;
    const RationalFunction radicalPart = rationalPart_ * rhs.radicalPart_ + rhs.rationalPart_ * radicalPart_;
    return make(rationalPart, radicalPart, radicand_);
  }

  // 除法用共轭有理化：1/(a + by) = (a − by) / (a² − b²f)，分母落在 ℚ(x) 里。
  Result<QuadraticExtension> operator/(const QuadraticExtension &rhs) const {
    if (const Result<void> compatible = ensureSameField(rhs); compatible.isErr()) {
      return std::unexpected(compatible.unwrapErr());
    }
    if (rhs.rationalPart_.isZero() && rhs.radicalPart_.isZero()) {
      return std::unexpected(MathsError::DivisionByZero);
    }
    // 范数 N(c + dy) = c² − d²f ∈ ℚ(x)
    const RationalFunction norm =
        rhs.rationalPart_ * rhs.rationalPart_ - rhs.radicalPart_ * rhs.radicalPart_ * radicand_;
    if (norm.isZero()) {
      return std::unexpected(MathsError::DivisionByZero); // f 非平方 ⟹ 范数仅在除数为 0 时为零
    }
    const Result<RationalFunction> inverseNorm = RationalFunction(Fraction(1, 1)) / norm;
    if (inverseNorm.isErr()) {
      return std::unexpected(inverseNorm.unwrapErr());
    }
    // (a+by)/(c+dy) = ((ac − bd·f) + (bc − ad)·y) / (c² − d²f)
    const RationalFunction numeratorRational =
        rationalPart_ * rhs.rationalPart_ - radicalPart_ * rhs.radicalPart_ * radicand_;
    const RationalFunction numeratorRadical = radicalPart_ * rhs.rationalPart_ - rationalPart_ * rhs.radicalPart_;
    return make(numeratorRational * inverseNorm.unwrap(), numeratorRadical * inverseNorm.unwrap(), radicand_);
  }

  // ==================== 化为更低一层 ====================
  // 与 toPolynomial / toMonomial / toFraction 同一套「尝试降一阶」命名

  // 无根号部分时降回有理函数（例如 (1+y)(1−y) = 1−f 就是纯粹的有理函数）
  Result<RationalFunction> toRationalFunction() const {
    if (!radicalPart_.isZero()) {
      return std::unexpected(MathsError::NotARational);
    }
    return rationalPart_;
  }

  // ==================== 求值 ====================
  // 所有变量都绑定到有理数时，a + b√f 落在一个实代数数上（√f 可能无理 ⇒ 结果可能是
  // 二次代数数，正好由 RealAlgebraicNumber 承载）。被开方数在取值处为负 → NegativeEvenRoot。
  Result<RealAlgebraicNumber> evaluate(const Scope &scope) const {
    const Result<Fraction> rationalValue = rationalPart_.evaluate(scope);
    if (rationalValue.isErr()) {
      return std::unexpected(rationalValue.unwrapErr());
    }
    const Result<Fraction> radicalValue = radicalPart_.evaluate(scope);
    if (radicalValue.isErr()) {
      return std::unexpected(radicalValue.unwrapErr());
    }
    if (radicalValue.unwrap() == 0LL) {
      return RealAlgebraicNumber(rationalValue.unwrap());
    }
    const Result<Fraction> radicandValue = radicand_.evaluate(scope);
    if (radicandValue.isErr()) {
      return std::unexpected(radicandValue.unwrapErr());
    }
    const Result<RealAlgebraicNumber> root = RealAlgebraicNumber::nthRootOf(radicandValue.unwrap(), 2);
    if (root.isErr()) {
      return std::unexpected(root.unwrapErr()); // 该点处被开方数为负
    }
    return RealAlgebraicNumber(rationalValue.unwrap()) + RealAlgebraicNumber(radicalValue.unwrap()) * root.unwrap();
  }

  // ==================== 输出 ====================

  std::string str() const { return render(false); }
  std::string latex() const { return render(true); }

private:
  // 被开方数必须恰好含一个变量，且不是 ℚ(x) 中的平方（详见文件头第 1 条限制）
  static Result<std::optional<Variable>> singleVariableOf(const RationalFunction &radicand) {
    if (radicand.isZero()) {
      return std::optional<Variable>(); // 零不是扩张元素，交给调用方
    }
    const std::set<Variable> variables = radicand.variables();
    if (variables.empty()) {
      return std::optional<Variable>(); // 常数：请走实代数数
    }
    if (variables.size() > 1) {
      return std::unexpected(MathsError::InvalidExpression); // 多变量根号暂不支持
    }
    const auto variable = *variables.begin();

    const std::optional<UnivariatePolynomial> numerator =
        quadratic_detail::toUnivariate(radicand.getNumerator(), variable);
    const std::optional<UnivariatePolynomial> denominator =
        quadratic_detail::toUnivariate(radicand.getDenominator(), variable);
    if (!numerator || !denominator) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    // 平方自由 ⟺ 与导数互素 ⟺ squareFreePart 不降次。两者都平方自由时
    // f = 分子/分母 不可能是 ℚ(x) 里的平方（除非同为常数，而常数已被上面挡掉）。
    if (numerator->squareFreePart().degree() != numerator->degree() ||
        denominator->squareFreePart().degree() != denominator->degree()) {
      return std::unexpected(MathsError::RadicandIsSquare);
    }
    return std::optional<Variable>(variable);
  }

  Result<void> ensureSameField(const QuadraticExtension &rhs) const {
    if (!(radicand_ == rhs.radicand_)) {
      // 两个不同 f 的元素在两个不同的域里，四则运算无意义
      return Result<void>::err(MathsError::InvalidExpression);
    }
    return Result<void>();
  }

  // 根号部分的排版：b 为 ±1 时省掉系数，负数时把符号提出来
  std::string render(bool useLatex) const {
    if (radicand_.isZero()) {
      return "0"; // 零元（默认构造）
    }
    const std::string radical = useLatex ? "\\sqrt{" + radicand_.latex() + "}" : "sqrt(" + radicand_.str() + ")";
    const bool radicalNegative = radicalPartIsNegative();
    const RationalFunction magnitude = radicalNegative ? -radicalPart_ : radicalPart_;
    const bool unitRadical = isUnit(magnitude);

    std::string result;
    if (radicalPart_.isZero()) {
      return useLatex ? rationalPart_.latex() : rationalPart_.str();
    }
    if (rationalPart_.isZero()) {
      // 只有根号部分：-sqrt(f) / sqrt(f) / b·sqrt(f)
      if (radicalNegative) {
        result += '-';
      }
      if (!unitRadical) {
        result += useLatex ? magnitude.latex() + radical : magnitude.str() + " " + radical;
      } else {
        result += radical;
      }
      return result;
    }

    result += useLatex ? rationalPart_.latex() : rationalPart_.str();
    result += radicalNegative ? " - " : " + ";
    if (!unitRadical) {
      result += useLatex ? magnitude.latex() + radical : magnitude.str() + " " + radical;
    } else {
      result += radical;
    }
    return result;
  }

  // 与库内其它排版一致：看分子的首项系数（分母首项已由 normalizeSign 归一为正）
  bool radicalPartIsNegative() const {
    const std::optional<Monomial> leading = leadingMonomial(radicalPart_.getNumerator());
    return leading.has_value() && leading->getCoefficient().isNegative();
  }

  static bool isUnit(const RationalFunction &value) {
    const Result<Monomial> numerator = value.getNumerator().toMonomial();
    const Result<Monomial> denominator = value.getDenominator().toMonomial();
    if (numerator.isErr() || denominator.isErr()) {
      return false;
    }
    if (!numerator.unwrap().isConstant() || !denominator.unwrap().isConstant()) {
      return false;
    }
    return numerator.unwrap().getCoefficient() == 1LL && denominator.unwrap().getCoefficient() == 1LL;
  }

  RationalFunction rationalPart_{Fraction(0, 1)};
  RationalFunction radicalPart_{Fraction(0, 1)};
  RationalFunction radicand_{Fraction(0, 1)};
  Variable variable_{"x"};
};

inline std::ostream &operator<<(std::ostream &os, const QuadraticExtension &value) { return os << value.str(); }

} // namespace maths
