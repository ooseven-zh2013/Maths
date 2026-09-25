// 实代数数：有理系数一元多项式的实根。
//
// 表示法 —— 「最小多项式 + 隔离区间」：
//   α 由 (p, [lo, hi]) 确定，其中 p ∈ ℚ[x] 无平方因子，区间内恰好含有 p 的一个实根。
// 这是 Sage 的 QQbar、Mathematica 的 Root[] 采用的办法，好处是
//   · 判零、判等、比较大小**都是可判定的**，且全程精确；
//   · 加、减、乘、除、开 n 次方仍得到代数数（代数数域是代数闭域），因此运算封闭。
//
// 刻意不碰 sin / exp / log 这类超越函数：它们的值大多不是代数数
// （由 Lindemann–Weierstrass，ln 2 就是超越数），混进来整个体系会塌掉。
//
// 精度的立身之本：区间端点一律是有理数（Fraction），**全程无浮点**。

export module maths.algebraic_number;

import std;
import maths.error;
import maths.result;
import maths.numbers;

// 内部工具：只依赖 Fraction，因此放在最前面。
// 刻意放在 maths 里但不导出 —— 这些是实现细节，不是对外的接口。
namespace maths {
namespace algebraic_detail {

// Fraction 对外以 long long 取值，超过 2^63 会翻成负数。
// Sturm 序列 / 结式这类迭代会把系数推得很高，因此每一步主动拦截：
// 宁可明确报溢出，也不要静默算出错误结果。
inline unsigned long long magnitudeOf(long long value) {
  return value < 0 ? static_cast<unsigned long long>(0) - static_cast<unsigned long long>(value)
                   : static_cast<unsigned long long>(value);
}

inline void guard(const Fraction &value) {
  constexpr unsigned long long limit = static_cast<unsigned long long>(std::numeric_limits<long long>::max() / 4);
  if (magnitudeOf(value.getNumerator()) > limit || static_cast<unsigned long long>(value.getDenominator()) > limit) {
    throw MathsException(MathsError::NumericOverflow);
  }
}

inline long long toSigned(unsigned long long value) {
  if (value > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
    throw MathsException(MathsError::NumericOverflow);
  }
  return static_cast<long long>(value);
}

inline Fraction powInt(const Fraction &base, unsigned exponent) {
  Fraction result(1, 1);
  for (unsigned step = 0; step < exponent; ++step) {
    result = result * base;
    guard(result);
  }
  return result;
}

inline std::string fractionText(const Fraction &value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

inline std::string fractionLatex(const Fraction &value) {
  if (value.getDenominator() == 1) {
    return std::to_string(value.getNumerator());
  }
  const long long numerator = value.getNumerator();
  const long long magnitude = numerator < 0 ? -numerator : numerator;
  const std::string body = "\\frac{" + std::to_string(magnitude) + "}{" + std::to_string(value.getDenominator()) + "}";
  return value.isNegative() ? "-" + body : body;
}

// base^exponent ≤ limit ？（整数开方二分时用，中途超过就提前退出以免溢出）
inline bool powerWithin(unsigned long long base, unsigned exponent, unsigned long long limit) {
  unsigned long long result = 1;
  for (unsigned step = 0; step < exponent; ++step) {
    if (base != 0 && base > limit / result) {
      return false;
    }
    result *= base;
  }
  return result <= limit;
}

// ⌊value^(1/exponent)⌋，要求 value ≥ 0
inline long long integerRootFloor(unsigned long long value, unsigned exponent) {
  if (exponent == 0) {
    throw MathsException(MathsError::InvalidRange);
  }
  if (value <= 1ULL) {
    return static_cast<long long>(value);
  }
  unsigned long long high = 1;
  while (powerWithin(high, exponent, value) && high <= (1ULL << 62)) {
    high *= 2;
  }
  unsigned long long low = high / 2;
  while (low + 1 < high) {
    const unsigned long long middle = low + (high - low) / 2;
    if (powerWithin(middle, exponent, value)) {
      low = middle;
    } else {
      high = middle;
    }
  }
  return toSigned(low);
}

// value^(1/exponent) 的有理数上下界 —— 全程精确，不引入浮点。
// value = a/b 时 (a/b)^(1/n) = (a·b^(n-1))^(1/n) / b，于是只需算整数开方。
inline std::pair<Fraction, Fraction> nthRootBounds(const Fraction &value, unsigned exponent) {
  if (exponent == 0) {
    throw MathsException(MathsError::InvalidRange);
  }
  const long long numerator = value.getNumerator();
  const long long denominator = value.getDenominator(); // Fraction 恒返回正分母
  const bool negativeResult = (exponent % 2 == 1) && numerator < 0;

  Fraction scaled(toSigned(magnitudeOf(numerator)), 1LL);
  for (unsigned step = 1; step < exponent; ++step) {
    scaled = scaled * Fraction(denominator, 1LL);
    guard(scaled);
  }

  const long long root = integerRootFloor(magnitudeOf(scaled.getNumerator()), exponent);
  const Fraction lower(root, denominator);
  const Fraction upper(root + 1, denominator);
  if (negativeResult) {
    return {-upper, -lower}; // 取负号后区间方向要翻过来
  }
  return {lower, upper};
}

inline unsigned long long binomial(unsigned total, unsigned choose) {
  if (choose > total) {
    return 0;
  }
  const unsigned smaller = std::min(choose, total - choose);
  unsigned long long result = 1;
  for (unsigned step = 1; step <= smaller; ++step) {
    result = result * (total - smaller + step) / step;
  }
  return result;
}

} // namespace algebraic_detail
} // namespace maths

export namespace maths {

// ==================== 一元多项式（ℚ 上） ====================
//
// 按次数升序存系数：coeffs_[i] 是 x^i 的系数，高次零系数自动去掉。
// 为什么不复用现成的 Polynomial：那个是多元存储（map<VarPowers, Fraction>），
// 反复做 Euclid 除法 / Sturm 序列会很别扭也慢。一元运算需要紧凑表示。
class UnivariatePolynomial {
public:
  UnivariatePolynomial() = default; // 零多项式

  explicit UnivariatePolynomial(std::vector<Fraction> ascending) : coeffs_(std::move(ascending)) { trim(); }

  static UnivariatePolynomial constant(const Fraction &value) {
    return UnivariatePolynomial(std::vector<Fraction>{value});
  }

  // 以 value 为根的一次多项式：x - value
  static UnivariatePolynomial linearRoot(const Fraction &value) {
    return UnivariatePolynomial(std::vector<Fraction>{-value, Fraction(1, 1)});
  }

  // x^exponent - value
  static UnivariatePolynomial powerMinus(const Fraction &value, unsigned exponent) {
    std::vector<Fraction> coefficients(static_cast<std::size_t>(exponent) + 1, Fraction(0, 1));
    coefficients.front() = -value;
    coefficients.back() = Fraction(1, 1);
    return UnivariatePolynomial(std::move(coefficients));
  }

  // ==================== 观察 ====================

  bool isZero() const { return coeffs_.empty(); }
  bool isConstant() const { return coeffs_.size() <= 1; }
  std::size_t degree() const { return coeffs_.empty() ? 0 : coeffs_.size() - 1; }
  const std::vector<Fraction> &coefficients() const { return coeffs_; }

  Fraction coefficient(std::size_t power) const { return power < coeffs_.size() ? coeffs_[power] : Fraction(0, 1); }
  Fraction leadingCoefficient() const { return coeffs_.empty() ? Fraction(0, 1) : coeffs_.back(); }
  Fraction constantTerm() const { return coeffs_.empty() ? Fraction(0, 1) : coeffs_.front(); }

  // ==================== 求值 ====================

  Fraction evaluate(const Fraction &value) const {
    Fraction result(0, 1);
    for (std::size_t index = coeffs_.size(); index-- > 0;) {
      result = result * value + coeffs_[index];
      algebraic_detail::guard(result);
    }
    return result;
  }

  // ==================== 四则运算 ====================

  UnivariatePolynomial operator+(const UnivariatePolynomial &rhs) const {
    const std::size_t width = std::max(coeffs_.size(), rhs.coeffs_.size());
    std::vector<Fraction> result(width, Fraction(0, 1));
    for (std::size_t index = 0; index < width; ++index) {
      result[index] = coefficient(index) + rhs.coefficient(index);
      algebraic_detail::guard(result[index]);
    }
    return UnivariatePolynomial(std::move(result));
  }

  UnivariatePolynomial operator-(const UnivariatePolynomial &rhs) const {
    const std::size_t width = std::max(coeffs_.size(), rhs.coeffs_.size());
    std::vector<Fraction> result(width, Fraction(0, 1));
    for (std::size_t index = 0; index < width; ++index) {
      result[index] = coefficient(index) - rhs.coefficient(index);
      algebraic_detail::guard(result[index]);
    }
    return UnivariatePolynomial(std::move(result));
  }

  UnivariatePolynomial operator-() const { return scale(Fraction(-1, 1)); }

  UnivariatePolynomial operator*(const UnivariatePolynomial &rhs) const {
    if (isZero() || rhs.isZero()) {
      return UnivariatePolynomial();
    }
    std::vector<Fraction> result(coeffs_.size() + rhs.coeffs_.size() - 1, Fraction(0, 1));
    for (std::size_t left = 0; left < coeffs_.size(); ++left) {
      for (std::size_t right = 0; right < rhs.coeffs_.size(); ++right) {
        result[left + right] = result[left + right] + coeffs_[left] * rhs.coeffs_[right];
        algebraic_detail::guard(result[left + right]);
      }
    }
    return UnivariatePolynomial(std::move(result));
  }

  UnivariatePolynomial scale(const Fraction &factor) const {
    if (isZero() || factor == 0LL) {
      return UnivariatePolynomial();
    }
    std::vector<Fraction> result;
    result.reserve(coeffs_.size());
    for (const Fraction &value : coeffs_) {
      const Fraction scaled = value * factor;
      algebraic_detail::guard(scaled);
      result.push_back(scaled);
    }
    return UnivariatePolynomial(std::move(result));
  }

  // ==================== 变形 ====================

  UnivariatePolynomial derivative() const {
    if (coeffs_.size() <= 1) {
      return UnivariatePolynomial();
    }
    std::vector<Fraction> result(coeffs_.size() - 1);
    for (std::size_t power = 1; power < coeffs_.size(); ++power) {
      result[power - 1] = coeffs_[power] * Fraction(algebraic_detail::toSigned(power), 1LL);
      algebraic_detail::guard(result[power - 1]);
    }
    return UnivariatePolynomial(std::move(result));
  }

  // p(-x)：奇数次项取反
  UnivariatePolynomial negateVariable() const {
    std::vector<Fraction> result = coeffs_;
    for (std::size_t power = 1; power < result.size(); power += 2) {
      result[power] = -result[power];
    }
    return UnivariatePolynomial(std::move(result));
  }

  // p(x^exponent)：把「α 的开 n 次方」变成 p(x^n) 的根
  UnivariatePolynomial composePower(unsigned exponent) const {
    if (isZero() || exponent == 0) {
      return *this;
    }
    std::vector<Fraction> result(degree() * static_cast<std::size_t>(exponent) + 1, Fraction(0, 1));
    for (std::size_t power = 0; power < coeffs_.size(); ++power) {
      result[power * exponent] = coeffs_[power];
    }
    return UnivariatePolynomial(std::move(result));
  }

  // x^degree · p(1/x)：根变成原来各根的倒数，用于取倒数
  UnivariatePolynomial reverse() const {
    std::vector<Fraction> result(coeffs_.rbegin(), coeffs_.rend());
    return UnivariatePolynomial(std::move(result));
  }

  // poly(value - x)，按 x 展开。
  // x^term 的系数 = Σ_power a_power · C(power, term) · (-1)^term · value^(power-term)
  static UnivariatePolynomial reversedShift(const UnivariatePolynomial &poly, const Fraction &value) {
    if (poly.isZero()) {
      return UnivariatePolynomial();
    }
    std::vector<Fraction> result(poly.coeffs_.size(), Fraction(0, 1));
    for (std::size_t power = 0; power < poly.coeffs_.size(); ++power) {
      const Fraction original = poly.coeffs_[power];
      for (std::size_t term = 0; term <= power; ++term) {
        Fraction contribution = original * Fraction(algebraic_detail::toSigned(algebraic_detail::binomial(
                                                        static_cast<unsigned>(power), static_cast<unsigned>(term))),
                                                    1LL);
        contribution = contribution * algebraic_detail::powInt(value, static_cast<unsigned>(power - term));
        if (term % 2 == 1) {
          contribution = -contribution;
        }
        result[term] = result[term] + contribution;
        algebraic_detail::guard(result[term]);
      }
    }
    return UnivariatePolynomial(std::move(result));
  }

  // ==================== 规范化 ====================

  // 本原部分：通分成整数系数后除掉所有系数的 gcd。
  // 缩放因子恒为正，不改变多项式在各点的符号 —— Sturm 序列依赖这一点。
  UnivariatePolynomial primitivePart() const {
    if (isZero()) {
      return UnivariatePolynomial();
    }
    long long common = 1;
    for (const Fraction &value : coeffs_) {
      common = std::lcm(common, value.getDenominator());
      algebraic_detail::guard(Fraction(common, 1LL));
    }
    const UnivariatePolynomial integerScaled = scale(Fraction(common, 1LL));

    unsigned long long divisor = 0;
    for (const Fraction &value : integerScaled.coeffs_) {
      divisor = std::gcd(divisor, algebraic_detail::magnitudeOf(value.getNumerator()));
    }
    if (divisor == 0ULL) {
      return integerScaled;
    }
    const Result<Fraction> inverse = Fraction(1, 1) / Fraction(algebraic_detail::toSigned(divisor), 1LL);
    if (inverse.isErr()) {
      return integerScaled;
    }
    return integerScaled.scale(inverse.unwrap());
  }

  // 首项系数化为 1
  UnivariatePolynomial monic() const {
    if (isZero()) {
      return UnivariatePolynomial();
    }
    const Result<Fraction> quotient = Fraction(1, 1) / leadingCoefficient();
    if (quotient.isErr()) {
      return *this;
    }
    return scale(quotient.unwrap());
  }

  // ==================== 除法 / gcd / 平方自由化 ====================

  // 返回 {商, 余式}：被除式 = 商 × 除式 + 余式，deg(余式) < deg(除式)
  static Result<std::pair<UnivariatePolynomial, UnivariatePolynomial>> divide(const UnivariatePolynomial &dividend,
                                                                              const UnivariatePolynomial &divisor) {
    if (divisor.isZero()) {
      return std::unexpected(MathsError::DivisionByZero);
    }
    if (dividend.isZero()) {
      return std::pair<UnivariatePolynomial, UnivariatePolynomial>{UnivariatePolynomial(), UnivariatePolynomial()};
    }

    const std::size_t divisorDegree = divisor.degree();
    const Fraction leading = divisor.leadingCoefficient();
    std::vector<Fraction> working = dividend.coeffs_;
    std::vector<Fraction> quotient;

    if (dividend.degree() >= divisorDegree) {
      quotient.assign(dividend.degree() - divisorDegree + 1, Fraction(0, 1));
      for (;;) {
        while (!working.empty() && working.back() == 0LL) {
          working.pop_back();
        }
        if (working.size() <= divisorDegree) {
          break; // 余式次数已低于除式
        }
        const std::size_t currentDegree = working.size() - 1;
        const Result<Fraction> factor = working[currentDegree] / leading;
        if (factor.isErr()) {
          return std::unexpected(factor.unwrapErr());
        }
        quotient[currentDegree - divisorDegree] = factor.unwrap();
        for (std::size_t offset = 0; offset <= divisorDegree; ++offset) {
          const std::size_t index = currentDegree - offset;
          working[index] = working[index] - factor.unwrap() * divisor.coeffs_[divisorDegree - offset];
          algebraic_detail::guard(working[index]);
        }
      }
    }

    return std::pair<UnivariatePolynomial, UnivariatePolynomial>{UnivariatePolynomial(std::move(quotient)),
                                                                 UnivariatePolynomial(std::move(working))};
  }

  static UnivariatePolynomial gcd(const UnivariatePolynomial &lhs, const UnivariatePolynomial &rhs) {
    if (lhs.isZero()) {
      return rhs.monic();
    }
    if (rhs.isZero()) {
      return lhs.monic();
    }
    UnivariatePolynomial current = lhs.primitivePart();
    UnivariatePolynomial next = rhs.primitivePart();
    for (;;) {
      const Result<std::pair<UnivariatePolynomial, UnivariatePolynomial>> step = divide(current, next);
      if (step.isErr()) {
        break;
      }
      const UnivariatePolynomial remaining = step.unwrap().second;
      if (remaining.isZero()) {
        break;
      }
      current = next;
      next = remaining.primitivePart(); // 保持整数系数，抑制 Euclid 的系数膨胀
    }
    return next.monic();
  }

  // 平方自由部分：p / gcd(p, p')。
  // 重根会让 Sturm 计数失效，也会白白抬高次数 —— 每个新多项式进来都要先过这一层。
  UnivariatePolynomial squareFreePart() const {
    if (isZero()) {
      return UnivariatePolynomial();
    }
    const UnivariatePolynomial derived = derivative();
    if (derived.isZero()) {
      return monic(); // 常数多项式
    }
    const UnivariatePolynomial common = gcd(*this, derived);
    if (common.isZero() || common.isConstant()) {
      return monic();
    }
    const Result<std::pair<UnivariatePolynomial, UnivariatePolynomial>> step = divide(*this, common);
    if (step.isErr()) {
      return *this;
    }
    return step.unwrap().first.primitivePart();
  }

  // ==================== 实根计数（Sturm 定理） ====================
  //
  // 要求入参平方自由、端点不是根；返回 (low, high] 内实根的个数。
  // 这是整个表示的判据：隔离区间里恰好一个根，才唯一地确定一个数。

  static int countRealRootsIn(const UnivariatePolynomial &squareFree, const Fraction &low, const Fraction &high) {
    if (squareFree.isZero() || squareFree.isConstant()) {
      return 0;
    }
    const std::vector<UnivariatePolynomial> chain = sturmChain(squareFree);
    if (chain.size() < 2) {
      return 0;
    }
    return signVariations(chain, low) - signVariations(chain, high);
  }

  // ==================== 把两个代数数捏成一个 ====================
  //
  // 需要的是「一个以 α+β（或 αβ）为根的多项式」。教科书做法是结式：
  // Res_y(p(y), q(x-y)) 的根正好取遍所有 α_i + β_j。但符号结式会把系数推到天上，
  // 换成「采样 + 插值」又会被插值本身放大（16 次多项式要 17 个点，中间量能到 10^27 量级，
  // 直接顶穿 Fraction 的表示范围 —— 实测过）。
  //
  // 这里改走另一条路：在商环 ℚ[x,y]/(p(x), q(y)) 里做线性代数。
  // 这个环的维数是 deg p · deg q，元素 x+y（或 x·y）的幂序列 1, t, t², … 必然在某一步
  // 线性相关，那个依赖关系 t^k = Σ c_i t^i 就给出一个零化多项式 x^k - Σ c_i x^i。
  // 好处：次数是**这个数真正的次数**（常常远小于 deg p · deg q），且中间量不会被放大。

  // 一个以 lhs 的根与 rhs 的根之和为根的多项式
  static UnivariatePolynomial annihilatorOfSum(const UnivariatePolynomial &lhs, const UnivariatePolynomial &rhs) {
    return annihilatorOfCombination(lhs, rhs, false);
  }

  // 一个以 lhs 的根与 rhs 的根之积为根的多项式
  static UnivariatePolynomial annihilatorOfProduct(const UnivariatePolynomial &lhs, const UnivariatePolynomial &rhs) {
    return annihilatorOfCombination(lhs, rhs, true);
  }

  // 一个以 lhs 的根的**平方**为根的多项式。单独开这条路是因为「同一个数自乘」太常见
  // （平方、幂、开方验算），而通用乘积路线要到 dim = deg² 的环里做线性代数 ——
  // 4 次的多项式就是 16 维，消元时中间量直接顶穿 Fraction 的表示范围。
  //
  // 这里把它压回 deg 维：p 拆成偶部与奇部 p(x) = e(x²) + x·o(x²)。
  // 令 γ = α²，则 e(γ) + α·o(γ) = 0；两边乘 o(γ) 并用 γ = α² 消去 α，
  // 得到 e(γ)² - γ·o(γ)² = 0 —— 正是以 α² 为根的多项式，次数不超过 deg p。
  static UnivariatePolynomial annihilatorOfSquare(const UnivariatePolynomial &lhs) {
    std::vector<Fraction> evenCoefficients;
    std::vector<Fraction> oddCoefficients;
    for (std::size_t power = 0; power < lhs.coefficients().size(); ++power) {
      if (power % 2 == 0) {
        evenCoefficients.push_back(lhs.coefficient(power));
      } else {
        oddCoefficients.push_back(lhs.coefficient(power));
      }
    }
    const UnivariatePolynomial evenPart(std::move(evenCoefficients));
    const UnivariatePolynomial oddPart(std::move(oddCoefficients));

    const UnivariatePolynomial oddSquared = oddPart * oddPart;
    // 乘 x 即整体升一次幂
    std::vector<Fraction> shifted(oddSquared.coefficients().size() + 1, Fraction(0, 1));
    for (std::size_t index = 0; index < oddSquared.coefficients().size(); ++index) {
      shifted[index + 1] = oddSquared.coefficient(index);
    }
    return (evenPart * evenPart) - UnivariatePolynomial(std::move(shifted));
  }

  // ==================== 输出 ====================

  std::string str() const {
    if (isZero()) {
      return "0";
    }
    std::string result;
    bool first = true;
    for (std::size_t power = coeffs_.size(); power-- > 0;) {
      const Fraction value = coeffs_[power];
      if (value == 0LL) {
        continue;
      }
      const bool negative = value.isNegative();
      const Fraction magnitude = negative ? -value : value;
      if (first) {
        if (negative) {
          result += '-';
        }
      } else {
        result += negative ? " - " : " + ";
      }
      if (power == 0 || !(magnitude == 1LL)) {
        result += algebraic_detail::fractionText(magnitude);
      }
      if (power > 0) {
        result += 'x';
        if (power > 1) {
          result += '^';
          result += std::to_string(power);
        }
      }
      first = false;
    }
    return result;
  }

  std::string latex() const {
    if (isZero()) {
      return "0";
    }
    std::string result;
    bool first = true;
    for (std::size_t power = coeffs_.size(); power-- > 0;) {
      const Fraction value = coeffs_[power];
      if (value == 0LL) {
        continue;
      }
      const bool negative = value.isNegative();
      const Fraction magnitude = negative ? -value : value;
      if (first) {
        if (negative) {
          result += '-';
        }
      } else {
        result += negative ? " - " : " + ";
      }
      if (power == 0 || !(magnitude == 1LL)) {
        result += algebraic_detail::fractionLatex(magnitude);
      }
      if (power > 0) {
        result += 'x';
        if (power > 1) {
          result += "^{";
          result += std::to_string(power);
          result += '}';
        }
      }
      first = false;
    }
    return result;
  }

private:
  void trim() {
    while (!coeffs_.empty() && coeffs_.back() == 0LL) {
      coeffs_.pop_back();
    }
  }

  // Sturm 链：p0 = p, p1 = p', p_{k+1} = -rem(p_{k-1}, p_k)
  static std::vector<UnivariatePolynomial> sturmChain(const UnivariatePolynomial &poly) {
    std::vector<UnivariatePolynomial> chain;
    chain.push_back(poly.primitivePart());
    chain.push_back(poly.derivative().primitivePart());
    while (chain.back().degree() > 0) {
      const UnivariatePolynomial previous = chain[chain.size() - 2];
      const UnivariatePolynomial current = chain.back();
      const Result<std::pair<UnivariatePolynomial, UnivariatePolynomial>> step = divide(previous, current);
      if (step.isErr()) {
        return chain;
      }
      const UnivariatePolynomial remaining = step.unwrap().second;
      if (remaining.isZero()) {
        break; // gcd(p, p') 是常数，链到底了
      }
      chain.push_back((-remaining).primitivePart());
    }
    return chain;
  }

  // 某点处 Sturm 链的符号变化数（跳过取值为 0 的项）
  static int signVariations(const std::vector<UnivariatePolynomial> &chain, const Fraction &point) {
    int variations = 0;
    int previousSign = 0;
    for (const UnivariatePolynomial &member : chain) {
      const Fraction value = member.evaluate(point);
      int sign = 0;
      if (value < 0LL) {
        sign = -1;
      } else if (value > 0LL) {
        sign = 1;
      }
      if (sign == 0) {
        continue;
      }
      if (previousSign != 0 && sign != previousSign) {
        ++variations;
      }
      previousSign = sign;
    }
    return variations;
  }

  // 在商环 ℚ[x,y]/(p(x), q(y)) 中求元素 t = x + y（product 为 false）或 t = x·y（为 true）
  // 的零化多项式。做法：把 t 的各次幂在环里展开成坐标向量，找到第一个线性相关的幂次，
  // 那个依赖系数 t^k = Σ c_i·t^i 反过来就给出多项式 x^k - Σ c_i·x^i。
  static UnivariatePolynomial annihilatorOfCombination(const UnivariatePolynomial &lhs, const UnivariatePolynomial &rhs,
                                                       bool product) {
    const UnivariatePolynomial left = lhs.monic();
    const UnivariatePolynomial right = rhs.monic();
    const std::size_t lhsDegree = left.degree();
    const std::size_t rhsDegree = right.degree();
    if (lhsDegree == 0 || rhsDegree == 0) {
      return UnivariatePolynomial(); // 常数多项式不定义根，交给调用方判错
    }
    const std::size_t dimension = lhsDegree * rhsDegree;

    // 把 x^powerX · y^powerY 按商环关系化回标准基（下标 = powerX * rhsDegree + powerY）。
    // 首项已化为 1，所以 x^m = -Σ_{k<m} a_k·x^k，y 同理。
    auto addMonomial = [&](auto &&self, std::size_t powerX, std::size_t powerY, const Fraction &factor,
                           std::vector<Fraction> &target) -> void {
      if (powerX >= lhsDegree) {
        for (std::size_t offset = 0; offset < lhsDegree; ++offset) {
          const Fraction shifted = factor * -left.coefficient(offset);
          algebraic_detail::guard(shifted);
          self(self, powerX - lhsDegree + offset, powerY, shifted, target);
        }
        return;
      }
      if (powerY >= rhsDegree) {
        for (std::size_t offset = 0; offset < rhsDegree; ++offset) {
          const Fraction shifted = factor * -right.coefficient(offset);
          algebraic_detail::guard(shifted);
          self(self, powerX, powerY - rhsDegree + offset, shifted, target);
        }
        return;
      }
      const std::size_t index = powerX * rhsDegree + powerY;
      target[index] = target[index] + factor;
      algebraic_detail::guard(target[index]);
    };

    auto multiply = [&](const std::vector<Fraction> &first, const std::vector<Fraction> &second) {
      std::vector<Fraction> result(dimension, Fraction(0, 1));
      for (std::size_t firstPower = 0; firstPower < lhsDegree; ++firstPower) {
        for (std::size_t firstIndex = 0; firstIndex < rhsDegree; ++firstIndex) {
          const Fraction firstValue = first[firstPower * rhsDegree + firstIndex];
          if (firstValue == 0LL) {
            continue;
          }
          for (std::size_t secondPower = 0; secondPower < lhsDegree; ++secondPower) {
            for (std::size_t secondIndex = 0; secondIndex < rhsDegree; ++secondIndex) {
              const Fraction secondValue = second[secondPower * rhsDegree + secondIndex];
              if (secondValue == 0LL) {
                continue;
              }
              const Fraction term = firstValue * secondValue;
              algebraic_detail::guard(term);
              addMonomial(addMonomial, firstPower + secondPower, firstIndex + secondIndex, term, result);
            }
          }
        }
      }
      return result;
    };

    std::vector<Fraction> element(dimension, Fraction(0, 1));
    if (product) {
      addMonomial(addMonomial, 1, 1, Fraction(1, 1), element);
    } else {
      addMonomial(addMonomial, 1, 0, Fraction(1, 1), element);
      addMonomial(addMonomial, 0, 1, Fraction(1, 1), element);
    }

    // 环的维数是 dimension，所以幂序列 1, t, t², … 至多 dimension 步必然线性相关。
    // 每算出一个新幂次就压回「整数且互素」（否则分母会随幂次累积到溢出），
    // 因此存下来的 powers[i] 其实是 scale[i]·t^i —— 记账用的 scale 不能省。
    std::vector<std::vector<Fraction>> powers;
    std::vector<Fraction> scales; // powers[i] = scales[i] · t^i
    std::vector<Fraction> current(dimension, Fraction(0, 1));
    current[0] = Fraction(1, 1); // t^0 = 1
    Fraction scale(1, 1);
    for (std::size_t power = 0; power <= dimension; ++power) {
      if (const std::optional<std::vector<Fraction>> coefficients = expressAsCombination(powers, current)) {
        std::vector<Fraction> polynomial(power + 1, Fraction(0, 1));
        for (std::size_t index = 0; index < power; ++index) {
          // v_k = Σ c_i·v_i 且 v_i = λ_i·t^i，于是 t^k = Σ (c_i·λ_i / λ_k)·t^i
          const Result<Fraction> scaled = ((*coefficients)[index] * scales[index]) / scale;
          if (scaled.isErr()) {
            return UnivariatePolynomial();
          }
          polynomial[index] = -scaled.unwrap();
        }
        polynomial[power] = Fraction(1, 1);
        return UnivariatePolynomial(std::move(polynomial));
      }
      powers.push_back(current);
      scales.push_back(scale);
      current = multiply(current, element); // 结果仍是 scale·t^(power+1)
      scale = scale * scaleToPrimitive(current);
    }
    return UnivariatePolynomial(); // 理论上到不了这里
  }
  // 把一行缩放到「整数且互素」：先通分去掉分母，再除掉所有分子的 gcd。
  // 高斯消元与幂序列里分母会不断相乘，不这样压一道，几十维的方程组很快就顶穿 Fraction 的表示范围。
  //
  // **返回实际乘上的倍数（恒为正数）**。缩放不改变线性相关关系，但会改变系数，
  // 调用方若要还原成真实的多项式，必须把这个倍数记账下来。
  static Fraction scaleToPrimitive(std::vector<Fraction> &row) {
    long long common = 1;
    for (const Fraction &value : row) {
      common = std::lcm(common, value.getDenominator());
    }
    Fraction multiplier(common, 1LL);
    if (common != 1) {
      for (Fraction &value : row) {
        value = value * Fraction(common, 1LL);
        algebraic_detail::guard(value);
      }
    }
    unsigned long long divisor = 0;
    for (const Fraction &value : row) {
      divisor = std::gcd(divisor, algebraic_detail::magnitudeOf(value.getNumerator()));
    }
    if (divisor > 1ULL) {
      const Result<Fraction> inverse = Fraction(1, 1) / Fraction(algebraic_detail::toSigned(divisor), 1LL);
      if (inverse.isErr()) {
        return multiplier;
      }
      multiplier = multiplier * inverse.unwrap();
      for (Fraction &value : row) {
        value = value * inverse.unwrap();
      }
    }
    return multiplier;
  }

  // 判断 target 能否由 columns 线性表出：能则给出一组系数，不能则返回 nullopt
  static std::optional<std::vector<Fraction>> expressAsCombination(const std::vector<std::vector<Fraction>> &columns,
                                                                   const std::vector<Fraction> &target) {
    const std::size_t rows = target.size();
    const std::size_t width = columns.size();
    if (width == 0 || rows == 0) {
      return std::nullopt;
    }

    std::vector<std::vector<Fraction>> matrix(rows, std::vector<Fraction>(width + 1, Fraction(0, 1)));
    for (std::size_t row = 0; row < rows; ++row) {
      for (std::size_t column = 0; column < width; ++column) {
        matrix[row][column] = columns[column][row];
      }
      matrix[row][width] = target[row]; // 增广列
    }

    std::vector<std::size_t> pivotColumn(rows, width);
    std::size_t pivotRow = 0;
    for (std::size_t column = 0; column < width && pivotRow < rows; ++column) {
      std::size_t selected = pivotRow;
      while (selected < rows && matrix[selected][column] == 0LL) {
        ++selected;
      }
      if (selected == rows) {
        continue; // 整列为零 → 自由变量
      }
      std::swap(matrix[selected], matrix[pivotRow]);
      const Fraction pivot = matrix[pivotRow][column];
      for (std::size_t row = 0; row < rows; ++row) {
        if (row == pivotRow || matrix[row][column] == 0LL) {
          continue;
        }
        const Result<Fraction> factorResult = matrix[row][column] / pivot;
        if (factorResult.isErr()) {
          return std::nullopt;
        }
        const Fraction factor = factorResult.unwrap();
        for (std::size_t index = column; index <= width; ++index) {
          matrix[row][index] = matrix[row][index] - factor * matrix[pivotRow][index];
          algebraic_detail::guard(matrix[row][index]);
        }
        scaleToPrimitive(matrix[row]);
      }
      scaleToPrimitive(matrix[pivotRow]);
      pivotColumn[pivotRow] = column;
      ++pivotRow;
    }

    // 出现「0 = 非 0」的行说明无解，也就是这一阶幂次与前面的线性无关
    for (std::size_t row = 0; row < rows; ++row) {
      bool allZero = true;
      for (std::size_t column = 0; column < width; ++column) {
        if (!(matrix[row][column] == 0LL)) {
          allZero = false;
          break;
        }
      }
      if (allZero && !(matrix[row][width] == 0LL)) {
        return std::nullopt;
      }
    }

    std::vector<Fraction> solution(width, Fraction(0, 1));
    for (std::size_t row = 0; row < rows; ++row) {
      const std::size_t column = pivotColumn[row];
      if (column == width) {
        continue;
      }
      const Result<Fraction> value = matrix[row][width] / matrix[row][column];
      if (value.isErr()) {
        return std::nullopt;
      }
      solution[column] = value.unwrap();
    }
    return solution;
  }

  std::vector<Fraction> coeffs_;
};

inline std::ostream &operator<<(std::ostream &os, const UnivariatePolynomial &value) { return os << value.str(); }

// ==================== 实代数数 ====================

class RealAlgebraicNumber {
public:
  RealAlgebraicNumber() : RealAlgebraicNumber(Fraction(0, 1)) {}

  // 有理数是代数数的特例：x - value 的实根
  RealAlgebraicNumber(const Fraction &value)
      : poly_(UnivariatePolynomial::linearRoot(value)), low_(value), high_(value) {}

  // ==================== 构造 ====================

  // 由「多项式 + 隔离区间」确定一个数：区间内必须恰好含一个实根。
  // 多项式会被平方自由化；若端点恰好是根，会向外挪一点以满足 Sturm 计数的前提。
  static Result<RealAlgebraicNumber> create(UnivariatePolynomial polynomial, const Fraction &low,
                                            const Fraction &high) {
    if (high < low) {
      return std::unexpected(MathsError::InvalidRange);
    }
    const UnivariatePolynomial squareFree = polynomial.squareFreePart();
    if (squareFree.isZero() || squareFree.isConstant()) {
      return std::unexpected(MathsError::InvalidExpression); // 常数或零多项式不定义任何根
    }
    if (auto isolated = tryIsolate(squareFree, low, high)) {
      return *isolated;
    }
    return std::unexpected(MathsError::InvalidRange);
  }

  // 有理数的 n 次实根（偶数次要求被开方数非负）
  static Result<RealAlgebraicNumber> nthRootOf(const Fraction &value, unsigned exponent) {
    if (exponent == 0) {
      return std::unexpected(MathsError::InvalidRange);
    }
    if (exponent == 1 || value == 0LL) {
      return RealAlgebraicNumber(value);
    }
    if (exponent % 2 == 0 && value < 0LL) {
      return std::unexpected(MathsError::InvalidRange); // 偶数次根下为负，实根不存在
    }

    // 完全 n 次方直接落回有理数，省掉后面一整套构造
    const std::pair<Fraction, Fraction> bounds = algebraic_detail::nthRootBounds(value, exponent);
    if (algebraic_detail::powInt(bounds.first, exponent) == value) {
      return RealAlgebraicNumber(bounds.first);
    }
    if (algebraic_detail::powInt(bounds.second, exponent) == value) {
      return RealAlgebraicNumber(bounds.second);
    }

    const UnivariatePolynomial candidate = UnivariatePolynomial::powerMinus(value, exponent).squareFreePart();
    std::pair<Fraction, Fraction> current = bounds;
    for (int iteration = 0; iteration < kRefinementLimit; ++iteration) {
      if (auto isolated = tryIsolate(candidate, current.first, current.second)) {
        return *isolated;
      }
      // x^n 单调：比较中点处的幂与被开方数即可判断落在哪一半
      const Fraction middle = (current.first + current.second) * Fraction(1, 2);
      if (algebraic_detail::powInt(middle, exponent) > value) {
        current.second = middle;
      } else {
        current.first = middle;
      }
    }
    throw MathsException(MathsError::InvalidRange);
  }

  static Result<RealAlgebraicNumber> squareRootOf(const Fraction &value) { return nthRootOf(value, 2); }

  // 从文本解析出实代数数。
  // 根号**只认 LaTeX 写法**：\sqrt{…}（二次根）、\sqrt[n]{…}（n 次根）。
  // 刻意不提供 sqrt(…) 这类 ASCII 写法 —— 同一件事两套记法迟早会分叉。
  // 其它运算沿用库里既有的书写习惯：+ - * / ^ 与 \frac \cdot \times \div 都接受。
  static Result<RealAlgebraicNumber> parse(std::string_view text);

  // ==================== 观察 ====================

  const UnivariatePolynomial &polynomial() const { return poly_; }
  Fraction lowerBound() const { return low_; }
  Fraction upperBound() const { return high_; }
  std::size_t degree() const { return poly_.degree(); }

  // 是否已退化为精确有理数（构造时端点重合）
  bool isRational() const { return low_ == high_; }

  // 尝试降一阶：值是有理数时返回它，否则 MathsError::NotARational。
  // 与 Polynomial::toMonomial / RationalFunction::toPolynomial / Fraction::toInteger
  // 是同一套「toXxx 尝试降一阶」的命名。
  //
  // 注意：这里不做多项式因式分解。构造时就已经收成一点（low == high）的必然成功；
  // 否则按有理根定理在隔离区间里找候选（p/q，p 整除常数项、q 整除首项）。
  // 系数分解走试除且有上限，系数极大时可能漏判而返回 Err —— 只会漏，不会错。
  Result<Fraction> toFraction() const {
    if (low_ == high_) {
      return low_;
    }
    if (const std::optional<Fraction> root = findRationalRoot()) {
      return *root;
    }
    return std::unexpected(MathsError::NotARational);
  }

  bool isZero() const { return compareToRational(Fraction(0, 1)) == std::strong_ordering::equal; }

  // 把隔离区间折半一次，端点更贴近真正的根
  void refine() {
    if (low_ == high_) {
      return;
    }
    const Fraction middle = (low_ + high_) * Fraction(1, 2);
    if (poly_.evaluate(middle) == 0LL) {
      collapseTo(middle);
      return;
    }
    // 区间里只有一个单根，所以两端符号是否跨越就决定了根在哪一半
    const bool straddlesLeft = (poly_.evaluate(low_) < 0LL) != (poly_.evaluate(middle) < 0LL);
    if (straddlesLeft) {
      high_ = middle;
    } else {
      low_ = middle;
    }
  }

  // ==================== 比较 ====================
  //
  // 两边同时精化（也就是不断二分逼近），直到隔离区间互不相交。
  // 纯二分在两者相等时永不终止，因此先用 gcd 做相等的短路判定：
  // 互素的两个多项式不可能有公共根；若存在公共因子，再数一数交点区间里
  // 恰好有几个公共因子的根 —— 是 1 就说明这两个数就是同一个根。

  std::strong_ordering compareTo(const RealAlgebraicNumber &rhs) const {
    RealAlgebraicNumber left = *this;
    RealAlgebraicNumber right = rhs;
    for (int iteration = 0; iteration < kRefinementLimit; ++iteration) {
      if (left.isRational() || right.isRational()) {
        if (left.isRational()) {
          return reverse(right.compareToRational(left.low_));
        }
        return left.compareToRational(right.low_);
      }
      if (left.high_ < right.low_) {
        return std::strong_ordering::less;
      }
      if (right.high_ < left.low_) {
        return std::strong_ordering::greater;
      }

      const UnivariatePolynomial common = UnivariatePolynomial::gcd(left.poly_, right.poly_);
      if (!common.isConstant()) {
        const Fraction sharedLow = std::max(left.low_, right.low_);
        const Fraction sharedHigh = std::min(left.high_, right.high_);
        if (sharedLow < sharedHigh && UnivariatePolynomial::countRealRootsIn(common, sharedLow, sharedHigh) == 1) {
          return std::strong_ordering::equal;
        }
      }

      left.refine();
      right.refine();
    }
    throw MathsException(MathsError::InvalidRange);
  }

  std::strong_ordering compareToRational(const Fraction &value) const {
    RealAlgebraicNumber self = *this;
    for (int iteration = 0; iteration < kRefinementLimit; ++iteration) {
      if (self.high_ < value) {
        return std::strong_ordering::less;
      }
      if (self.low_ > value) {
        return std::strong_ordering::greater;
      }
      if (self.low_ == self.high_) {
        return self.low_ <=> value;
      }
      if (self.poly_.evaluate(value) == 0LL) {
        return std::strong_ordering::equal; // value 落在隔离区间内且恰好是根
      }
      self.refine();
    }
    throw MathsException(MathsError::InvalidRange);
  }

  bool operator==(const RealAlgebraicNumber &rhs) const { return compareTo(rhs) == std::strong_ordering::equal; }
  std::strong_ordering operator<=>(const RealAlgebraicNumber &rhs) const { return compareTo(rhs); }
  bool operator==(const Fraction &value) const { return compareToRational(value) == std::strong_ordering::equal; }
  std::strong_ordering operator<=>(const Fraction &value) const { return compareToRational(value); }

  // ==================== 四则运算 ====================

  RealAlgebraicNumber operator-() const {
    return RealAlgebraicNumber(poly_.negateVariable(), -high_, -low_, Validated{});
  }

  friend RealAlgebraicNumber operator+(const RealAlgebraicNumber &lhs, const RealAlgebraicNumber &rhs) {
    if (lhs.isRational() && rhs.isRational()) {
      return RealAlgebraicNumber(lhs.low_ + rhs.low_);
    }
    // 候选多项式的根取遍所有「α_i + β_j」，再用区间加法定位到目标那一个
    const UnivariatePolynomial candidate =
        UnivariatePolynomial::annihilatorOfSum(lhs.poly_, rhs.poly_).squareFreePart();
    RealAlgebraicNumber left = lhs;
    RealAlgebraicNumber right = rhs;
    for (int iteration = 0; iteration < kRefinementLimit; ++iteration) {
      if (left.isRational() && right.isRational()) {
        return RealAlgebraicNumber(left.low_ + right.low_);
      }
      if (auto isolated = tryIsolate(candidate, left.low_ + right.low_, left.high_ + right.high_)) {
        return *isolated;
      }
      left.refine();
      right.refine();
    }
    throw MathsException(MathsError::InvalidRange);
  }

  friend RealAlgebraicNumber operator-(const RealAlgebraicNumber &lhs, const RealAlgebraicNumber &rhs) {
    return lhs + (-rhs);
  }

  friend RealAlgebraicNumber operator*(const RealAlgebraicNumber &lhs, const RealAlgebraicNumber &rhs) {
    if (lhs.isRational() && rhs.isRational()) {
      return RealAlgebraicNumber(lhs.low_ * rhs.low_);
    }
    // 同一个数自乘走平方专用路线：环的维数从 deg² 降到 deg，避免消元中途溢出
    const bool squaring = (lhs == rhs);
    const UnivariatePolynomial candidate = (squaring ? UnivariatePolynomial::annihilatorOfSquare(lhs.poly_)
                                                     : UnivariatePolynomial::annihilatorOfProduct(lhs.poly_, rhs.poly_))
                                               .squareFreePart();
    RealAlgebraicNumber left = lhs;
    RealAlgebraicNumber right = rhs;
    for (int iteration = 0; iteration < kRefinementLimit; ++iteration) {
      if (left.isRational() && right.isRational()) {
        return RealAlgebraicNumber(left.low_ * right.low_);
      }
      const std::pair<Fraction, Fraction> bounds = productBounds(left, right);
      if (auto isolated = tryIsolate(candidate, bounds.first, bounds.second)) {
        return *isolated;
      }
      left.refine();
      right.refine();
    }
    throw MathsException(MathsError::InvalidRange);
  }

  Result<RealAlgebraicNumber> inverse() const {
    if (isZero()) {
      return std::unexpected(MathsError::DivisionByZero);
    }
    if (isRational()) {
      const Result<Fraction> reciprocal = Fraction(1, 1) / low_;
      if (reciprocal.isErr()) {
        return std::unexpected(reciprocal.unwrapErr());
      }
      return RealAlgebraicNumber(reciprocal.unwrap());
    }

    // 倒数多项式的根就是原来各根的倒数
    const UnivariatePolynomial candidate = poly_.reverse().squareFreePart();
    RealAlgebraicNumber self = *this;
    for (int iteration = 0; iteration < kRefinementLimit; ++iteration) {
      if (self.isRational()) {
        const Result<Fraction> reciprocal = Fraction(1, 1) / self.low_;
        if (reciprocal.isErr()) {
          return std::unexpected(reciprocal.unwrapErr());
        }
        return RealAlgebraicNumber(reciprocal.unwrap());
      }
      if (self.low_ > 0LL || self.high_ < 0LL) { // 0 不在区间里，倒数定向才安全
        const Result<Fraction> inverseLow = Fraction(1, 1) / self.high_;
        const Result<Fraction> inverseHigh = Fraction(1, 1) / self.low_;
        if (inverseLow.isErr() || inverseHigh.isErr()) {
          return std::unexpected(MathsError::DivisionByZero);
        }
        if (auto isolated = tryIsolate(candidate, inverseLow.unwrap(), inverseHigh.unwrap())) {
          return *isolated;
        }
      }
      self.refine();
    }
    throw MathsException(MathsError::InvalidRange);
  }

  friend Result<RealAlgebraicNumber> operator/(const RealAlgebraicNumber &lhs, const RealAlgebraicNumber &rhs) {
    if (lhs.isRational() && rhs.isRational()) {
      const Result<Fraction> quotient = lhs.low_ / rhs.low_;
      if (quotient.isErr()) {
        return std::unexpected(quotient.unwrapErr());
      }
      return RealAlgebraicNumber(quotient.unwrap());
    }
    const Result<RealAlgebraicNumber> reciprocal = rhs.inverse();
    if (reciprocal.isErr()) {
      return reciprocal;
    }
    return lhs * reciprocal.unwrap();
  }

  // ==================== 开 n 次方 ====================

  Result<RealAlgebraicNumber> nthRoot(unsigned exponent) const {
    if (exponent == 0) {
      return std::unexpected(MathsError::InvalidRange);
    }
    if (exponent == 1) {
      return *this;
    }
    if (isZero()) {
      return RealAlgebraicNumber();
    }
    if (exponent % 2 == 0 && compareToRational(Fraction(0, 1)) == std::strong_ordering::less) {
      return std::unexpected(MathsError::InvalidRange);
    }
    if (isRational()) {
      return nthRootOf(low_, exponent);
    }

    // γ = α^(1/n) 满足 p(γ^n) = 0，于是候选多项式就是 p(x^n)
    const UnivariatePolynomial candidate = poly_.composePower(exponent).squareFreePart();
    RealAlgebraicNumber self = *this;
    for (int iteration = 0; iteration < kRefinementLimit; ++iteration) {
      if (self.isRational()) {
        return nthRootOf(self.low_, exponent);
      }
      if (exponent % 2 == 0 && self.low_ < 0LL) {
        self.refine(); // 偶数次根先把下界推到非负
        continue;
      }
      const std::pair<Fraction, Fraction> lowBounds = algebraic_detail::nthRootBounds(self.low_, exponent);
      const std::pair<Fraction, Fraction> highBounds = algebraic_detail::nthRootBounds(self.high_, exponent);
      if (auto isolated = tryIsolate(candidate, lowBounds.first, highBounds.second)) {
        return *isolated;
      }
      self.refine();
    }
    throw MathsException(MathsError::InvalidRange);
  }

  Result<RealAlgebraicNumber> sqrt() const { return nthRoot(2); }

  // 非负整数次幂（重复平方）。代数数对乘法封闭，所以幂不会失败；
  // 返回 Result 只是为了和其它可能失败的入口保持一致的形状。
  Result<RealAlgebraicNumber> pow(unsigned exponent) const {
    RealAlgebraicNumber result(Fraction(1, 1));
    RealAlgebraicNumber base = *this;
    unsigned remaining = exponent;
    while (remaining > 0) {
      if (remaining % 2 == 1) {
        result = result * base;
      }
      remaining /= 2;
      if (remaining > 0) {
        base = base * base;
      }
    }
    return result;
  }

  // ==================== 输出 ====================

  std::string str() const {
    if (isRational()) {
      return algebraic_detail::fractionText(low_);
    }
    return "RootOf(" + poly_.str() + ", [" + algebraic_detail::fractionText(low_) + ", " +
           algebraic_detail::fractionText(high_) + "])";
  }

  std::string latex() const {
    if (isRational()) {
      return algebraic_detail::fractionLatex(low_);
    }
    return "\\operatorname{RootOf}(" + poly_.latex() + ", [" + algebraic_detail::fractionLatex(low_) + ", " +
           algebraic_detail::fractionLatex(high_) + "])";
  }

private:
  struct Validated {}; // 标记：参数已由调用方验证，不再重复检查

  // 正因子枚举（试除）。超过上限就不分解，只留下 ±1 这类必然因子 ——
  // 宁可漏判（返回 NotARational）也不做可能很慢的完整分解。
  static std::vector<long long> divisorsOf(long long value) {
    if (value == 0) {
      return {0}; // 常数项为 0 时 0 本身就是候选根
    }
    constexpr unsigned long long kFactorLimit = 1ULL << 20;
    const unsigned long long magnitude = algebraic_detail::magnitudeOf(value);
    if (magnitude > kFactorLimit * kFactorLimit) {
      return {1};
    }
    std::vector<long long> result;
    for (unsigned long long candidate = 1; candidate * candidate <= magnitude; ++candidate) {
      if (magnitude % candidate == 0) {
        result.push_back(algebraic_detail::toSigned(candidate));
        const unsigned long long partner = magnitude / candidate;
        if (partner != candidate) {
          result.push_back(algebraic_detail::toSigned(partner));
        }
      }
    }
    return result;
  }

  // 有理根定理：候选根是 ±p/q，其中 p 整除常数项、q 整除首项系数。
  // 只检验落在隔离区间里的候选 —— 区间里只有一个根，命中即为所求。
  std::optional<Fraction> findRationalRoot() const {
    const UnivariatePolynomial primitive = poly_.primitivePart(); // 先整数化
    const std::vector<long long> numerators = divisorsOf(primitive.constantTerm().getNumerator());
    const std::vector<long long> denominators = divisorsOf(primitive.leadingCoefficient().getNumerator());
    for (const long long numerator : numerators) {
      for (const long long denominator : denominators) {
        for (const int sign : {1, -1}) {
          const Fraction candidate(sign * numerator, denominator);
          if (candidate < low_ || candidate > high_) {
            continue;
          }
          if (poly_.evaluate(candidate) == 0LL) {
            return candidate;
          }
        }
      }
    }
    return std::nullopt;
  }

  static constexpr int kRefinementLimit = 512;

  RealAlgebraicNumber(const UnivariatePolynomial &poly, const Fraction &low, const Fraction &high, Validated)
      : poly_(poly), low_(low), high_(high) {
    canonicalizeIfLinear();
  }

  // 一次多项式直接退化成精确有理数，避免"明明是整数却还带着多项式壳"
  void canonicalizeIfLinear() {
    if (poly_.degree() != 1) {
      return;
    }
    const Fraction root = (-poly_.coefficient(0) / poly_.coefficient(1)).unwrap();
    collapseTo(root);
  }

  void collapseTo(const Fraction &root) {
    poly_ = UnivariatePolynomial::linearRoot(root);
    low_ = root;
    high_ = root;
  }

  static std::strong_ordering reverse(std::strong_ordering order) {
    if (order == std::strong_ordering::less) {
      return std::strong_ordering::greater;
    }
    if (order == std::strong_ordering::greater) {
      return std::strong_ordering::less;
    }
    return std::strong_ordering::equal;
  }

  static std::pair<Fraction, Fraction> productBounds(const RealAlgebraicNumber &lhs, const RealAlgebraicNumber &rhs) {
    const Fraction candidates[4] = {lhs.low_ * rhs.low_, lhs.low_ * rhs.high_, lhs.high_ * rhs.low_,
                                    lhs.high_ * rhs.high_};
    Fraction minimum = candidates[0];
    Fraction maximum = candidates[0];
    for (const Fraction &value : candidates) {
      minimum = std::min(minimum, value);
      maximum = std::max(maximum, value);
    }
    return {minimum, maximum};
  }

  // 尝试把 [low, high] 变成只含一个根的隔离区间；成功则返回对应的数。
  // 失败（里面不止一个根，或端点挪不开）返回 nullopt，让调用方继续精化。
  static std::optional<RealAlgebraicNumber> tryIsolate(const UnivariatePolynomial &squareFree, const Fraction &low,
                                                       const Fraction &high) {
    if (squareFree.isZero() || squareFree.isConstant() || low > high) {
      return std::nullopt;
    }
    Fraction step = (high - low) * Fraction(1, 4);
    if (step == 0LL) {
      step = Fraction(1, 1);
    }
    Fraction extendedLow = low - step;
    Fraction extendedHigh = high + step;

    // 端点必须是非零点，否则 Sturm 计数会漏掉开区间端点上的根
    for (int attempt = 0; attempt < 4; ++attempt) {
      const bool touchesLow = squareFree.evaluate(extendedLow) == 0LL;
      const bool touchesHigh = squareFree.evaluate(extendedHigh) == 0LL;
      if (!touchesLow && !touchesHigh) {
        break;
      }
      if (touchesLow) {
        extendedLow = extendedLow - step;
      }
      if (touchesHigh) {
        extendedHigh = extendedHigh + step;
      }
    }
    if (squareFree.evaluate(extendedLow) == 0LL || squareFree.evaluate(extendedHigh) == 0LL) {
      return std::nullopt;
    }
    if (UnivariatePolynomial::countRealRootsIn(squareFree, extendedLow, extendedHigh) != 1) {
      return std::nullopt;
    }
    return RealAlgebraicNumber(squareFree, extendedLow, extendedHigh, Validated{});
  }

  UnivariatePolynomial poly_;
  Fraction low_;
  Fraction high_;
};

inline std::ostream &operator<<(std::ostream &os, const RealAlgebraicNumber &value) { return os << value.str(); }

namespace algebraic_detail {

// 实代数数的文本解析器。
//
// 语法（有意做得和库里既有的表达式解析器一致，只是把「变量」换成了「根式」）：
//   expr    := term (('+' | '-') term)*
//   term    := power (('*' | '/' | \cdot | \times | \div)? power)*   // 省略乘号即隐含乘法
//   power   := unary ('^' 非负整数)?                                  // 指数也接受 x^{2}
//   unary   := ('-' | '+')? primary
//   primary := 整数 | '(' expr ')' | '{' expr '}' | \frac{a}{b} | \sqrt{…} | \sqrt[n]{…}
//
// 只接受数字与根式，不接受变量 —— 解析出的是一个**数**，不是式子。
class AlgebraicParser {
public:
  explicit AlgebraicParser(std::string_view source) : text_(source) {}

  Result<RealAlgebraicNumber> parse() {
    skipSpaces();
    if (atEnd()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    Result<RealAlgebraicNumber> value = parseAdditive();
    if (value.isErr()) {
      return value;
    }
    // 允许结尾残留 \left / \right 这类纯排版标记
    while (takeToken("\\right") || takeToken("\\left")) {
    }
    skipSpaces();
    if (!atEnd()) {
      return std::unexpected(MathsError::InvalidExpression); // 有消费不掉的残留字符
    }
    return value;
  }

private:
  bool atEnd() const { return position_ >= text_.size(); }
  char peek() const { return atEnd() ? '\0' : text_[position_]; }

  void skipSpaces() {
    while (!atEnd() && std::isspace(static_cast<unsigned char>(peek())) != 0) {
      ++position_;
    }
  }

  // 尝试吃掉一个固定词（前面允许有空白）；失败时不消耗字符
  bool takeToken(std::string_view token) {
    skipSpaces();
    if (text_.compare(position_, token.size(), token) != 0) {
      return false;
    }
    position_ += token.size();
    return true;
  }

  bool takeChar(char expected) {
    skipSpaces();
    if (peek() != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  // 读取一个花括号分组（支持嵌套），position 停在 '}' 之后
  bool takeBracedGroup(std::string &out) {
    skipSpaces();
    if (peek() != '{') {
      return false;
    }
    int depth = 0;
    const std::size_t start = position_ + 1;
    while (!atEnd()) {
      if (peek() == '{') {
        ++depth;
      } else if (peek() == '}') {
        --depth;
        if (depth == 0) {
          out = std::string(text_.substr(start, position_ - start));
          ++position_;
          return true;
        }
      }
      ++position_;
    }
    return false; // 括号没配平
  }

  // 下一个位置能否开始一个因子 —— 用于识别隐含乘法（2\sqrt{2} 即 2 * √2）
  bool startsPrimary() const {
    if (atEnd()) {
      return false;
    }
    const char character = peek();
    // 这些是结束标记，不能当作新因子的开头，否则 \left(…\right) 会被误判成隐含乘法
    if (character == ')' || character == ']' || character == '}' || character == ',') {
      return false;
    }
    if (character == '\\') {
      // \left 会引出括号分组，算新因子；\right 只是收尾
      return text_.compare(position_, 6, "\\right") != 0;
    }
    return std::isdigit(static_cast<unsigned char>(character)) != 0 || character == '(' || character == '{';
  }

  Result<RealAlgebraicNumber> parseAdditive() {
    Result<RealAlgebraicNumber> left = parseMultiplicative();
    if (left.isErr()) {
      return left;
    }
    for (;;) {
      skipSpaces();
      const char operation = peek();
      if (operation != '+' && operation != '-') {
        return left;
      }
      ++position_;
      Result<RealAlgebraicNumber> right = parseMultiplicative();
      if (right.isErr()) {
        return right;
      }
      left = (operation == '+') ? (left.unwrap() + right.unwrap()) : (left.unwrap() - right.unwrap());
    }
  }

  Result<RealAlgebraicNumber> parseMultiplicative() {
    Result<RealAlgebraicNumber> left = parsePower();
    if (left.isErr()) {
      return left;
    }
    for (;;) {
      skipSpaces();
      char operation = peek();
      if (takeToken("\\cdot") || takeToken("\\times")) {
        operation = '*';
      } else if (takeToken("\\div")) {
        operation = '/';
      } else if (operation == '*' || operation == '/') {
        ++position_;
      } else if (startsPrimary()) {
        operation = '*'; // 隐含乘法
      } else {
        return left;
      }

      Result<RealAlgebraicNumber> right = parsePower();
      if (right.isErr()) {
        return right;
      }
      if (operation == '/') {
        Result<RealAlgebraicNumber> quotient = left.unwrap() / right.unwrap();
        if (quotient.isErr()) {
          return quotient;
        }
        left = quotient;
      } else {
        left = left.unwrap() * right.unwrap();
      }
    }
  }

  Result<RealAlgebraicNumber> parsePower() {
    Result<RealAlgebraicNumber> base = parseUnary();
    if (base.isErr()) {
      return base;
    }
    if (!takeChar('^')) {
      return base;
    }
    Result<unsigned long long> exponent = parseUnsignedInteger();
    if (exponent.isErr()) {
      return std::unexpected(exponent.unwrapErr());
    }
    return base.unwrap().pow(static_cast<unsigned>(exponent.unwrap()));
  }

  Result<RealAlgebraicNumber> parseUnary() {
    skipSpaces();
    if (peek() == '-') {
      ++position_;
      Result<RealAlgebraicNumber> operand = parseUnary();
      if (operand.isErr()) {
        return operand;
      }
      return -operand.unwrap();
    }
    if (peek() == '+') {
      ++position_;
      return parseUnary();
    }
    return parsePrimary();
  }

  Result<RealAlgebraicNumber> parsePrimary() {
    skipSpaces();
    if (atEnd()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    // \left / \right 只是括号修饰符，跳过继续读里面的内容
    if (takeToken("\\left") || takeToken("\\right")) {
      return parsePrimary();
    }
    if (takeToken("\\frac")) {
      std::string numeratorText;
      std::string denominatorText;
      if (!takeBracedGroup(numeratorText) || !takeBracedGroup(denominatorText)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      Result<RealAlgebraicNumber> numerator = AlgebraicParser(numeratorText).parse();
      if (numerator.isErr()) {
        return numerator;
      }
      Result<RealAlgebraicNumber> denominator = AlgebraicParser(denominatorText).parse();
      if (denominator.isErr()) {
        return denominator;
      }
      Result<RealAlgebraicNumber> quotient = numerator.unwrap() / denominator.unwrap();
      if (quotient.isErr()) {
        return quotient;
      }
      return quotient;
    }
    if (takeToken("\\sqrt")) {
      unsigned degree = 2;
      skipSpaces();
      if (peek() == '[') { // \sqrt[n]{…} 的可选次数
        ++position_;
        std::string digits;
        while (!atEnd() && peek() != ']') {
          digits.push_back(peek());
          ++position_;
        }
        if (peek() != ']') {
          return std::unexpected(MathsError::InvalidExpression);
        }
        ++position_;
        std::string cleaned;
        for (const char character : digits) {
          if (std::isspace(static_cast<unsigned char>(character)) == 0) {
            cleaned.push_back(character);
          }
        }
        if (cleaned.empty()) {
          return std::unexpected(MathsError::InvalidExpression);
        }
        try {
          degree = static_cast<unsigned>(std::stoul(cleaned));
        } catch (const std::exception &) {
          return std::unexpected(MathsError::InvalidExpression);
        }
        if (degree == 0) {
          return std::unexpected(MathsError::InvalidRange);
        }
      }

      std::string radicandText;
      if (!takeBracedGroup(radicandText)) {
        return std::unexpected(MathsError::InvalidExpression); // 根号下必须是花括号分组
      }
      Result<RealAlgebraicNumber> radicand = AlgebraicParser(radicandText).parse();
      if (radicand.isErr()) {
        return radicand;
      }
      return radicand.unwrap().nthRoot(degree);
    }
    if (peek() == '(') {
      ++position_;
      Result<RealAlgebraicNumber> inner = parseAdditive();
      if (inner.isErr()) {
        return inner;
      }
      skipSpaces();
      // \left(…\right) 里收尾标记在 ')' **之前**，必须先吃掉再找右括号
      takeToken("\\right");
      skipSpaces();
      if (peek() != ')') {
        return std::unexpected(MathsError::InvalidExpression);
      }
      ++position_;
      return inner;
    }
    if (peek() == '{') {
      std::string group;
      if (!takeBracedGroup(group)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      return AlgebraicParser(group).parse();
    }
    if (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
      return parseNumber();
    }
    return std::unexpected(MathsError::InvalidExpression);
  }

  Result<RealAlgebraicNumber> parseNumber() {
    const std::size_t start = position_;
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
      ++position_;
    }
    try {
      const long long value = std::stoll(std::string(text_.substr(start, position_ - start)));
      return RealAlgebraicNumber(Fraction(value, 1LL));
    } catch (const std::exception &) {
      return std::unexpected(MathsError::InvalidExpression);
    }
  }

  Result<unsigned long long> parseUnsignedInteger() {
    skipSpaces();
    std::string digits;
    if (peek() == '{') { // LaTeX 的 x^{2}
      std::string group;
      if (!takeBracedGroup(group)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      digits = std::move(group);
    } else {
      while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        digits.push_back(peek());
        ++position_;
      }
    }
    if (digits.empty()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    try {
      return static_cast<unsigned long long>(std::stoull(digits));
    } catch (const std::exception &) {
      return std::unexpected(MathsError::InvalidExpression);
    }
  }

  std::string_view text_;
  std::size_t position_{0};
};

} // namespace algebraic_detail

inline Result<RealAlgebraicNumber> RealAlgebraicNumber::parse(std::string_view text) {
  return algebraic_detail::AlgebraicParser(text).parse();
}

} // namespace maths
