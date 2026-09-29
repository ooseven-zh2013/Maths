export module maths.algebra:radical;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import :expression;
import :rational;
import :scope;
import :constraint;

export namespace maths {

// 根式扩张 ℚ(x)[y₁,…,y_k]/(y₁²−f₁, …, y_k²−f_k)：把「根号下是有理函数」的多重根式
// 表示成精确的代数元素。
//
//   Σ_S c_S · ∏_{i∈S} y_i        （c_S ∈ ℚ(x)，y_i² = f_i，S 取遍子集）
//
// k = 1 时就是 a + b·y（√f 的二次扩张）；k = 2 时基是 {1, y₁, y₂, y₁y₂}，
// 于是 `√(x²+1) + √(x²+2)` 这种**两个根号同时出现**的式子有了精确表示。
// 四则运算在这个基下封闭，代入有理数还能落到实代数数上。
//
// ============================ 四条刻意的限制 ============================
//
// 1) **被开方数必须是非平方的有理函数**。f 是 ℚ(x) 里的平方时（`√(x²)`、`√((x+1)²)`），
//    y² − f 在 ℚ(x)[y] 里可约 —— 环不是域、y 是零因子、± 两个根都成立，
//    「√(x²)」根本没有单值元素。本库不引入 |x| 节点，所以**明确拒收**
//    （RadicandIsSquare），而不是猜一个分支。
//    判定用「分子分母各自无重根」，是平方性的**充分否证**：保守但不误判。
//
// 2) **各根号必须互相独立**（不在同一平方类）。`√x + √(4x)` 里两个根号其实是同一个
//    （√(4x) = 2√x），要用单生成元表示。不独立的组合会明确拒收
//    （RadicandsNotIndependent）：折叠需要最简根式化，本库明确不做。
//    判据是 Kummer 意义下的平方类独立：任何非空子集的乘积都不能是平方。
//
// 3) **不做最简根式化**：f 与 f·c² 是不同的表示（√8 与 2√2 不相等）。
//
// 4) **单变量、不嵌套**：所有被开方数必须含同一个变量，且不支持 √(1+√x) 这类嵌套。
//
// 与其它类型的关系：`AlgebraicRationalFunction` 是「系数取代数数」（√2 是系数），
// 本类型是「根号包里带变量」（√x 是元素）—— 两件事。

namespace radical_detail {

// 非负整数的精确平方根判定（std::sqrt 可能因舍入给出错判，故做一次校正）
inline bool isPerfectSquare(unsigned long long value) {
  if (value == 0ULL) {
    return true;
  }
  unsigned long long root = static_cast<unsigned long long>(std::sqrt(static_cast<double>(value)));
  while (root > 0ULL && root * root > value) {
    --root;
  }
  while ((root + 1ULL) * (root + 1ULL) <= value) {
    ++root;
  }
  return root * root == value;
}

} // namespace radical_detail

class RadicalExtension {
public:
  // 生成元个数的上限：维度 2^k，k = 4 时基向量 16 个，运算量仍然很小
  static constexpr unsigned kMaxRadicands = 4;

  // 纯有理元素（还没有任何生成元）。这是「零生成元」的域，也是让本类型能当
  // 「系数类型」用的前提：模板里的单位元构造方式就是 `Coefficient(Fraction)`。
  // 与真正的根式元素做运算时会自动扩域。
  RadicalExtension(const Fraction &value)
      : coefficients_{RationalFunction(value)} {} // NOLINT(google-explicit-constructor)

  // 同上，但值可以是含变量的有理函数（√x·√x 就等于 x，它也是本类型的一个元素）
  RadicalExtension(const RationalFunction &value) : coefficients_{value} {} // NOLINT(google-explicit-constructor)

  // Σ_S c_S ∏_{i∈S} √f_i。coefficients 的长度必须正好是 2^k；
  // radicands 可以为空，此时就是纯有理元素（coefficients 长度 1）。
  static Result<RadicalExtension> make(std::vector<RationalFunction> radicands,
                                       std::vector<RationalFunction> coefficients) {
    if (radicands.size() > kMaxRadicands) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    if (coefficients.size() != (std::size_t(1) << radicands.size())) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    if (const Result<void> valid = validateRadicands(radicands); valid.isErr()) {
      return std::unexpected(valid.unwrapErr());
    }
    if (const Result<void> independent = validateIndependence(radicands); independent.isErr()) {
      return std::unexpected(independent.unwrapErr());
    }
    return RadicalExtension(std::move(radicands), std::move(coefficients), Validated{});
  }

  // √f
  static Result<RadicalExtension> make(const RationalFunction &radicand) {
    return make(std::vector<RationalFunction>{radicand},
                std::vector<RationalFunction>{RationalFunction(Fraction(0, 1)), RationalFunction(Fraction(1, 1))});
  }

  // √f₁ + √f₂ + … ：每个根号系数为 1，这就是「多个根号相加」的常用形状
  static Result<RadicalExtension> sumOfRadicals(std::vector<RationalFunction> radicands) {
    std::vector<RationalFunction> coefficients(std::size_t(1) << radicands.size(), RationalFunction(Fraction(0, 1)));
    for (std::size_t bit = 0; bit < radicands.size(); ++bit) {
      coefficients[std::size_t(1) << bit] = RationalFunction(Fraction(1, 1));
    }
    return make(std::move(radicands), std::move(coefficients));
  }

  const std::vector<RationalFunction> &radicands() const { return radicands_; }
  const std::vector<RationalFunction> &coefficients() const { return coefficients_; }
  std::size_t termCount() const { return coefficients_.size(); }
  const RationalFunction &coefficient(std::size_t mask) const { return coefficients_[mask]; }

  // 元素里出现过的全部变量：被开方数与所有系数里出现的并集
  std::set<Variable> variables() const {
    std::set<Variable> result;
    for (const RationalFunction &radicand : radicands_) {
      for (const Variable &variable : radicand.variables()) {
        result.insert(variable);
      }
    }
    for (const RationalFunction &coefficient : coefficients_) {
      for (const Variable &variable : coefficient.variables()) {
        result.insert(variable);
      }
    }
    return result;
  }

  bool containsVariable(const Variable &variable) const { return variables().count(variable) == 1; }

  bool isZero() const {
    for (const RationalFunction &coefficient : coefficients_) {
      if (!coefficient.isZero()) {
        return false;
      }
    }
    return true;
  }

  // 是否已经没有根号部分（所有非常数项系数为零）
  bool isRadicalFree() const {
    for (std::size_t mask = 1; mask < coefficients_.size(); ++mask) {
      if (!coefficients_[mask].isZero()) {
        return false;
      }
    }
    return true;
  }

  bool operator==(const RadicalExtension &rhs) const {
    // 都退化成有理函数时直接比值（于是 √x·√x 与 x 相等、√2·√2 与 2 相等）
    const std::optional<RationalFunction> lhsValue = radicalFreeValue();
    const std::optional<RationalFunction> rhsValue = rhs.radicalFreeValue();
    if (lhsValue.has_value() && rhsValue.has_value()) {
      return *lhsValue == *rhsValue;
    }
    // 否则提升到共同的域后逐坐标比（基下坐标唯一，不需要因式分解）；
    // 升不上去（不同平方类 / 多变量）说明它们没法放在同一个域里，判不等
    const Result<std::pair<RadicalExtension, RadicalExtension>> unified = unify(*this, rhs);
    if (unified.isErr()) {
      return false;
    }
    return unified.unwrap().first.coefficients_ == unified.unwrap().second.coefficients_;
  }

  // ==================== 四则 ====================
  // 同生成元才能相加相乘；生成元不同就得升到更大的域，本类型做不到，明确报错。

  Result<RadicalExtension> operator+(const RadicalExtension &rhs) const {
    const Result<std::pair<RadicalExtension, RadicalExtension>> unified = unify(*this, rhs);
    if (unified.isErr()) {
      return std::unexpected(unified.unwrapErr());
    }
    const RadicalExtension &left = unified.unwrap().first;
    const RadicalExtension &right = unified.unwrap().second;
    std::vector<RationalFunction> result = left.coefficients_;
    for (std::size_t mask = 0; mask < result.size(); ++mask) {
      result[mask] = result[mask] + right.coefficients_[mask];
    }
    return RadicalExtension(left.radicands_, std::move(result), Validated{});
  }

  Result<RadicalExtension> operator-(const RadicalExtension &rhs) const {
    const Result<std::pair<RadicalExtension, RadicalExtension>> unified = unify(*this, rhs);
    if (unified.isErr()) {
      return std::unexpected(unified.unwrapErr());
    }
    const RadicalExtension &left = unified.unwrap().first;
    const RadicalExtension &right = unified.unwrap().second;
    std::vector<RationalFunction> result = left.coefficients_;
    for (std::size_t mask = 0; mask < result.size(); ++mask) {
      result[mask] = result[mask] - right.coefficients_[mask];
    }
    return RadicalExtension(left.radicands_, std::move(result), Validated{});
  }

  RadicalExtension operator-() const {
    std::vector<RationalFunction> result = coefficients_;
    for (RationalFunction &coefficient : result) {
      coefficient = -coefficient;
    }
    return RadicalExtension(radicands_, std::move(result), Validated{});
  }

  // y_i² = f_i 就是全部的乘法规则：两个子集相交的生成元两两配对后换成对应被开方数之积
  Result<RadicalExtension> operator*(const RadicalExtension &rhs) const {
    const Result<std::pair<RadicalExtension, RadicalExtension>> unified = unify(*this, rhs);
    if (unified.isErr()) {
      return std::unexpected(unified.unwrapErr());
    }
    return unified.unwrap().first.multipliedBy(unified.unwrap().second);
  }

  Result<RadicalExtension> operator/(const RadicalExtension &rhs) const {
    const Result<std::pair<RadicalExtension, RadicalExtension>> unified = unify(*this, rhs);
    if (unified.isErr()) {
      return std::unexpected(unified.unwrapErr());
    }
    const Result<RadicalExtension> reciprocal = unified.unwrap().second.inverse();
    if (reciprocal.isErr()) {
      return reciprocal;
    }
    return unified.unwrap().first.multipliedBy(reciprocal.unwrap());
  }

  // 共轭相乘求逆：α⁻¹ = (∏_{S≠∅} conj_S(α)) / N(α)，其中 N(α) = ∏_{所有 S} conj_S(α) ∈ ℚ(x)
  Result<RadicalExtension> inverse() const {
    if (isZero()) {
      return std::unexpected(MathsError::DivisionByZero);
    }
    std::vector<RationalFunction> cofactor;
    for (std::size_t flipped = 1; flipped < coefficients_.size(); ++flipped) {
      const RadicalExtension conjugate(radicands_, conjugatedCoefficients(flipped), Validated{});
      if (cofactor.empty()) {
        cofactor = conjugate.coefficients_;
        continue;
      }
      cofactor = RadicalExtension(radicands_, cofactor, Validated{}).multipliedBy(conjugate).coefficients_;
    }

    const RadicalExtension normElement = RadicalExtension(radicands_, cofactor, Validated{}).multipliedBy(*this);
    const Result<RationalFunction> norm = normElement.toRationalFunction();
    if (norm.isErr() || norm.unwrap().isZero()) {
      return std::unexpected(MathsError::DivisionByZero); // 生成元独立时到不了这里
    }
    const Result<RationalFunction> inverseNorm = RationalFunction(Fraction(1, 1)) / norm.unwrap();
    if (inverseNorm.isErr()) {
      return std::unexpected(inverseNorm.unwrapErr());
    }
    for (RationalFunction &coefficient : cofactor) {
      coefficient = coefficient * inverseNorm.unwrap();
    }
    return RadicalExtension(radicands_, std::move(cofactor), Validated{});
  }

  // ==================== 化为更低一层 ====================
  // 与 toPolynomial / toMonomial / toFraction 同一套「尝试降一阶」命名

  // 无根号部分时降回有理函数（例如 √x·√x = x、(1+√x)(1−√x) = 1−x）
  Result<RationalFunction> toRationalFunction() const {
    if (!isRadicalFree()) {
      return std::unexpected(MathsError::NotARational);
    }
    return coefficients_[0];
  }

  // ==================== 求值 ====================
  // 所有变量都绑定到有理数时，结果落在一个实代数数上：每个 √f_i 是实代数数，
  // 它们的有理系数组合仍是实代数数。某个被开方数在该点为负 → NegativeEvenRoot。
  Result<RealAlgebraicNumber> evaluate(const Scope &scope) const {
    RealAlgebraicNumber total(Fraction(0, 1));
    std::vector<std::optional<RealAlgebraicNumber>> roots(radicands_.size());
    for (std::size_t mask = 0; mask < coefficients_.size(); ++mask) {
      if (coefficients_[mask].isZero()) {
        continue;
      }
      const Result<Fraction> value = coefficients_[mask].evaluate(scope);
      if (value.isErr()) {
        return std::unexpected(value.unwrapErr());
      }
      RealAlgebraicNumber term(value.unwrap());
      for (std::size_t bit = 0; bit < radicands_.size(); ++bit) {
        if ((mask & (std::size_t(1) << bit)) == 0) {
          continue;
        }
        if (!roots[bit].has_value()) {
          const Result<Fraction> radicand = radicands_[bit].evaluate(scope);
          if (radicand.isErr()) {
            return std::unexpected(radicand.unwrapErr());
          }
          const Result<RealAlgebraicNumber> root = RealAlgebraicNumber::nthRootOf(radicand.unwrap(), 2);
          if (root.isErr()) {
            return std::unexpected(root.unwrapErr());
          }
          roots[bit] = root.unwrap();
        }
        term = term * *roots[bit];
      }
      total = total + term;
    }
    return total;
  }

  // ==================== 输出 ====================

  std::string str() const { return render(false); }
  std::string latex() const { return render(true); }

private:
  struct Validated {};

  RadicalExtension(std::vector<RationalFunction> radicands, std::vector<RationalFunction> coefficients, Validated)
      : radicands_(std::move(radicands)), coefficients_(std::move(coefficients)) {}

  // 被开方数必须含同一个变量，且各自不是 ℚ(x) 中的平方。
  // 空列表是合法的：那是「零生成元」的域，元素就是纯有理函数。
  static Result<void> validateRadicands(const std::vector<RationalFunction> &radicands) {
    std::optional<Variable> common;
    for (const RationalFunction &radicand : radicands) {
      if (radicand.isZero()) {
        return Result<void>::err(MathsError::InvalidExpression); // 零不是扩张元素
      }
      const std::set<Variable> variables = radicand.variables();
      if (variables.empty()) {
        return Result<void>::err(MathsError::InvalidExpression); // 常数被开方数请走实代数数
      }
      if (variables.size() > 1) {
        return Result<void>::err(MathsError::InvalidExpression); // 多变量根号暂不支持
      }
      const auto variable = *variables.begin();
      if (!common.has_value()) {
        common = variable;
      } else if (!(*common == variable)) {
        return Result<void>::err(MathsError::InvalidExpression); // 所有被开方数必须同一个变量
      }

      const std::optional<UnivariatePolynomial> numerator = toUnivariatePolynomial(radicand.getNumerator(), variable);
      const std::optional<UnivariatePolynomial> denominator =
          toUnivariatePolynomial(radicand.getDenominator(), variable);
      if (!numerator || !denominator) {
        return Result<void>::err(MathsError::InvalidExpression);
      }
      // 平方自由 ⟺ 与导数互素 ⟺ squareFreePart 不降次
      if (numerator->squareFreePart().degree() != numerator->degree() ||
          denominator->squareFreePart().degree() != denominator->degree()) {
        return Result<void>::err(MathsError::RadicandIsSquare);
      }
    }
    return Result<void>();
  }

  // 多重二次扩张要是域，就要求各被开方数的平方类独立（Kummer，指数 2）：
  // 任何非空子集的乘积都不能是 ℚ(x) 里的平方。
  static Result<void> validateIndependence(const std::vector<RationalFunction> &radicands) {
    const std::size_t size = std::size_t(1) << radicands.size();
    for (std::size_t mask = 1; mask < size; ++mask) {
      RationalFunction product(Fraction(1, 1));
      for (std::size_t bit = 0; bit < radicands.size(); ++bit) {
        if ((mask & (std::size_t(1) << bit)) != 0) {
          product = product * radicands[bit];
        }
      }
      if (!isProvablyNonSquare(product)) {
        return Result<void>::err(MathsError::RadicandsNotIndependent);
      }
    }
    return Result<void>();
  }

  // 有理函数「可以证明不是平方」：含变量时分子分母各自平方自由即为证；
  // 常数时要看它是不是有理数的平方（√2 这类不算平方，仍然是独立的生成元）。
  static bool isProvablyNonSquare(const RationalFunction &value) {
    if (value.isZero()) {
      return false;
    }
    const std::set<Variable> variables = value.variables();
    if (variables.empty()) {
      const Result<Fraction> constant = value.evaluate(Scope());
      if (constant.isErr()) {
        return false;
      }
      const long long numerator = constant.unwrap().getNumerator();
      const long long denominator = constant.unwrap().getDenominator();
      if (numerator < 0) {
        return true; // 负数不可能是平方
      }
      return !(radical_detail::isPerfectSquare(static_cast<unsigned long long>(numerator)) &&
               radical_detail::isPerfectSquare(static_cast<unsigned long long>(denominator)));
    }
    const auto variable = *variables.begin();
    const std::optional<UnivariatePolynomial> numerator = toUnivariatePolynomial(value.getNumerator(), variable);
    const std::optional<UnivariatePolynomial> denominator = toUnivariatePolynomial(value.getDenominator(), variable);
    if (!numerator || !denominator) {
      return false;
    }
    return numerator->squareFreePart().degree() == numerator->degree() &&
           denominator->squareFreePart().degree() == denominator->degree();
  }

  // 纯有理元素（没有根号部分）时给出它的值，否则 nullopt
  std::optional<RationalFunction> radicalFreeValue() const {
    if (!isRadicalFree()) {
      return std::nullopt;
    }
    return coefficients_.empty() ? RationalFunction(Fraction(0, 1)) : coefficients_[0];
  }

  // 把元素放进更大的域。
  //
  // 关键是**位重映射**：元素自己的基按它自己的列表编号，而目标域按合并后的列表编号，
  // 同一个生成元在两个列表里的位置可能不同（例如 √(x+1) 在 [x, x+1] 里是第 2 个），
  // 所以要逐个生成元找出它在目标列表里的位置，再把掩码的位搬过去。
  // 不能假设「自身列表是目标列表的前缀」—— 那只在合并顺序凑巧时才成立。
  static Result<RadicalExtension> embed(const RadicalExtension &element,
                                        const std::vector<RationalFunction> &radicands) {
    std::vector<std::size_t> positions(element.radicands_.size(), 0);
    for (std::size_t index = 0; index < element.radicands_.size(); ++index) {
      bool found = false;
      for (std::size_t target = 0; target < radicands.size(); ++target) {
        if (element.radicands_[index] == radicands[target]) {
          positions[index] = target;
          found = true;
          break;
        }
      }
      if (!found) {
        return std::unexpected(MathsError::InvalidExpression); // 目标域里没有这个生成元
      }
    }

    std::vector<RationalFunction> coefficients(std::size_t(1) << radicands.size(), RationalFunction(Fraction(0, 1)));
    for (std::size_t mask = 0; mask < element.coefficients_.size(); ++mask) {
      if (element.coefficients_[mask].isZero()) {
        continue;
      }
      std::size_t target = 0;
      for (std::size_t index = 0; index < positions.size(); ++index) {
        if ((mask & (std::size_t(1) << index)) != 0) {
          target |= std::size_t(1) << positions[index];
        }
      }
      coefficients[target] = coefficients[target] + element.coefficients_[mask];
    }
    return RadicalExtension(radicands, std::move(coefficients), Validated{});
  }

  // 把两侧提升到共同的域：被开方数取并（保序去重），并校验并集仍然合法且独立。
  // 失败表示这两个元素没法放在同一个域里算 —— 比如 √x 与 √(4x) 其实是同一个根号
  // （依赖，需要最简根式化才能合并，本库不做），或者被开方数根本不在同一个变量里。
  static Result<std::pair<RadicalExtension, RadicalExtension>> unify(const RadicalExtension &lhs,
                                                                     const RadicalExtension &rhs) {
    std::vector<RationalFunction> merged = lhs.radicands_;
    for (const RationalFunction &radicand : rhs.radicands_) {
      bool present = false;
      for (const RationalFunction &existing : merged) {
        if (existing == radicand) {
          present = true;
          break;
        }
      }
      if (!present) {
        merged.push_back(radicand);
      }
    }
    if (merged.size() > kMaxRadicands) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    if (const Result<void> valid = validateRadicands(merged); valid.isErr()) {
      return std::unexpected(valid.unwrapErr());
    }
    if (const Result<void> independent = validateIndependence(merged); independent.isErr()) {
      return std::unexpected(independent.unwrapErr());
    }
    const Result<RadicalExtension> left = embed(lhs, merged);
    const Result<RadicalExtension> right = embed(rhs, merged);
    if (left.isErr()) {
      return std::unexpected(left.unwrapErr());
    }
    if (right.isErr()) {
      return std::unexpected(right.unwrapErr());
    }
    return std::make_pair(left.unwrap(), right.unwrap());
  }

  // 前提：同域；调用方已核对，不再重复校验
  RadicalExtension multipliedBy(const RadicalExtension &rhs) const {
    std::vector<RationalFunction> result(coefficients_.size(), RationalFunction(Fraction(0, 1)));
    for (std::size_t lhsMask = 0; lhsMask < coefficients_.size(); ++lhsMask) {
      if (coefficients_[lhsMask].isZero()) {
        continue;
      }
      for (std::size_t rhsMask = 0; rhsMask < rhs.coefficients_.size(); ++rhsMask) {
        if (rhs.coefficients_[rhsMask].isZero()) {
          continue;
        }
        RationalFunction term = coefficients_[lhsMask] * rhs.coefficients_[rhsMask];
        const std::size_t paired = lhsMask & rhsMask;
        for (std::size_t bit = 0; bit < radicands_.size(); ++bit) {
          if ((paired & (std::size_t(1) << bit)) != 0) {
            term = term * radicands_[bit]; // y_i² = f_i
          }
        }
        const std::size_t target = lhsMask ^ rhsMask;
        result[target] = result[target] + term;
      }
    }
    return RadicalExtension(radicands_, std::move(result), Validated{});
  }

  // 翻转若干个生成元的符号（多重二次扩张的自同构）：y_T 的符号按 |S∩T| 的奇偶翻转
  std::vector<RationalFunction> conjugatedCoefficients(std::size_t flipped) const {
    std::vector<RationalFunction> result = coefficients_;
    for (std::size_t mask = 0; mask < result.size(); ++mask) {
      if (std::popcount(flipped & mask) % 2 == 1) {
        result[mask] = -result[mask];
      }
    }
    return result;
  }

  // 与库内其它排版一致：看分子的首项系数（分母首项已由 normalizeSign 归一为正）
  static bool coefficientIsNegative(const RationalFunction &value) {
    const std::optional<Monomial> leading = leadingMonomial(value.getNumerator());
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

  std::string render(bool useLatex) const {
    if (coefficients_.empty() || isZero()) {
      return "0";
    }
    std::string result;
    bool first = true;
    for (std::size_t mask = 0; mask < coefficients_.size(); ++mask) {
      const RationalFunction &coefficient = coefficients_[mask];
      if (coefficient.isZero()) {
        continue;
      }
      const bool negative = coefficientIsNegative(coefficient);
      if (first) {
        if (negative) {
          result += '-';
        }
      } else {
        result += negative ? " - " : " + ";
      }
      first = false;
      result += renderTerm(negative ? -coefficient : coefficient, mask, useLatex);
    }
    return result;
  }

  // 单项：系数（为 1 时省略）+ 该子集里各生成元的根号
  std::string renderTerm(const RationalFunction &magnitude, std::size_t mask, bool useLatex) const {
    std::string radical;
    for (std::size_t bit = 0; bit < radicands_.size(); ++bit) {
      if ((mask & (std::size_t(1) << bit)) == 0) {
        continue;
      }
      radical += useLatex ? "\\sqrt{" + radicands_[bit].latex() + "}" : "sqrt(" + radicands_[bit].str() + ")";
    }
    if (radical.empty()) {
      return useLatex ? magnitude.latex() : magnitude.str();
    }
    if (isUnit(magnitude)) {
      return radical;
    }
    return useLatex ? magnitude.latex() + radical : magnitude.str() + " " + radical;
  }

  std::vector<RationalFunction> radicands_;
  std::vector<RationalFunction> coefficients_;
};

inline std::ostream &operator<<(std::ostream &os, const RadicalExtension &value) { return os << value.str(); }

// 根式表达式的定义域：**所有被开方数 ≥ 0** 与**所有系数的分母 ≠ 0** 取交。
//
// 定义域是「表达式」的性质（√x·√x 的值等于 x、处处有定义，但作为表达式它要求 x ≥ 0），
// 所以这里按被开方数逐个收条件，而不是先把元素化简。
//
//   √x            → x ≥ 0
//   √(x²+1)       → ℝ（被开方数恒正）
//   √(x²−1)       → (−∞,−1] ∪ [1,+∞)
//   1/√x          → x > 0（系数的分母带来的 x ≠ 0 与被开方数的 x ≥ 0 取交）
//
// 只支持**单变量**：多变量时定义域是多维点集（`√x·y` 这类），不在本模块范围。
inline Result<RealSet> domainOf(const RadicalExtension &expression) {
  const std::set<Variable> variables = expression.variables();
  if (variables.empty()) {
    return RealSet::realLine(); // 常数元素
  }
  if (variables.size() > 1) {
    return std::unexpected(MathsError::InvalidExpression); // 多维定义域另说
  }

  Result<RealSet> domain = RealSet::realLine();
  for (const RationalFunction &radicand : expression.radicands()) {
    const Result<RealSet> condition = solveInequality(radicand, Relation::GreaterEqual);
    if (condition.isErr()) {
      return condition;
    }
    const Result<RealSet> intersected = domain.unwrap().intersect(condition.unwrap());
    if (intersected.isErr()) {
      return intersected;
    }
    domain = intersected.unwrap();
  }
  for (const RationalFunction &coefficient : expression.coefficients()) {
    if (coefficient.isZero()) {
      continue;
    }
    const Result<RealSet> condition = domainOf(coefficient);
    if (condition.isErr()) {
      return condition;
    }
    const Result<RealSet> intersected = domain.unwrap().intersect(condition.unwrap());
    if (intersected.isErr()) {
      return intersected;
    }
    domain = intersected.unwrap();
  }
  return domain;
}

} // namespace maths
