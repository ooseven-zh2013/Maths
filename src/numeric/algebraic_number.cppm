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

// 非负整数的精确平方根（`std::sqrt` 可能因舍入给出错判，所以做一次上下校正）
inline std::optional<unsigned long long> exactIntegerSquareRoot(unsigned long long value) {
  unsigned long long root = static_cast<unsigned long long>(std::sqrt(static_cast<double>(value)));
  while (root > 0ULL && root * root > value) {
    --root;
  }
  while ((root + 1ULL) * (root + 1ULL) <= value) {
    ++root;
  }
  return root * root == value ? std::optional<unsigned long long>(root) : std::nullopt;
}

// 有理数的精确平方根：分子分母都得是完全平方，负数在 ℝ 上没有平方根。
// 用于「这个多项式的首项系数能不能开方」以及多项式开平方的入口判断。
inline std::optional<Fraction> rationalSquareRoot(const Fraction &value) {
  const long long numerator = value.getNumerator();
  if (numerator < 0) {
    return std::nullopt;
  }
  const std::optional<unsigned long long> numeratorRoot =
      exactIntegerSquareRoot(static_cast<unsigned long long>(numerator));
  const std::optional<unsigned long long> denominatorRoot =
      exactIntegerSquareRoot(static_cast<unsigned long long>(value.getDenominator()));
  if (!numeratorRoot.has_value() || !denominatorRoot.has_value() || *denominatorRoot == 0ULL) {
    return std::nullopt;
  }
  return Fraction(toSigned(*numeratorRoot), toSigned(*denominatorRoot));
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

// 解析「整串都是数字」的非负整数，失败返回 nullopt。
//
// **必须校验整串消费完**：std::stoull 遇到非法字符只解析前缀、而且不抛异常
// （"1/2" 直接返回 1），于是 2^{1/2} 会静默算成 2 —— 不报错、给错答案。
// 各处一律走这两个函数，不要再裸调 stoull。
inline std::optional<unsigned long long> parseWholeUnsigned(std::string_view text) {
  std::string digits;
  for (const char character : text) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
      return std::nullopt; // 出现非数字即非法，绝不"读到哪算哪"
    }
    digits += character;
  }
  if (digits.empty() || digits.size() > 19) {
    return std::nullopt;
  }
  try {
    return static_cast<unsigned long long>(std::stoull(digits));
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

// 同上，允许一个前导正负号
inline std::optional<long long> parseWholeSigned(std::string_view text) {
  std::string digits;
  bool negative = false;
  bool sawSign = false;
  for (const char character : text) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      continue;
    }
    if (!sawSign && (character == '+' || character == '-')) {
      negative = character == '-';
      sawSign = true;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
      return std::nullopt;
    }
    digits += character;
  }
  if (digits.empty() || digits.size() > 18) {
    return std::nullopt;
  }
  try {
    const long long magnitude = std::stoll(digits);
    return negative ? -magnitude : magnitude;
  } catch (const std::exception &) {
    return std::nullopt;
  }
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

  // 按次数升序的系数表逐项相等（高次零系数在构造时已去掉，所以直接比表就行）
  bool operator==(const UnivariatePolynomial &rhs) const { return coeffs_ == rhs.coeffs_; }

  // ==================== 开平方 ====================

  // 若存在 g ∈ ℚ[x] 使 g² = *this，返回 g；否则 nullopt。
  //
  // 用途：`√(g²)` 在代数函数域里不是单值元素（它是 |g|），所以根式扩张明确拒收 ——
  // 但要把它拆成分段函数（`√(x²)` → ±x 按符号分情形）就得先把 g 解出来。
  //
  // 做法是逐项定系数：g = Σ bᵢxⁱ，最高次 b_m 由首项系数开有理平方得到；
  // 再看 x^{m+i} 的系数 —— 它等于 2·b_m·b_i 加上已知的高次项两两乘积，
  // 于是 bᵢ 一步解出。最后**验一遍** g² == *this，不靠推导过程中的假设作数。
  std::optional<UnivariatePolynomial> squareRoot() const {
    if (isZero()) {
      return UnivariatePolynomial(); // 0 = 0²
    }
    if (degree() % 2 != 0) {
      return std::nullopt; // 奇次多项式不可能是平方
    }
    const std::size_t half = degree() / 2;
    const std::optional<Fraction> leading = algebraic_detail::rationalSquareRoot(leadingCoefficient());
    if (!leading.has_value()) {
      return std::nullopt;
    }

    std::vector<Fraction> root(half + 1, Fraction(0, 1));
    root[half] = *leading;
    // 除数 2·b_m 固定（b_m ≠ 0），把它的倒数挪到循环外，顺便避开 Fraction 除法返回 Result
    const Result<Fraction> inverseDivisor = Fraction(1, 1) / (*leading * Fraction(2, 1));
    if (inverseDivisor.isErr()) {
      return std::nullopt;
    }
    for (std::size_t index = half; index-- > 0;) {
      // x^{half+index} 的系数里，下标都大于 index 的那些项已经全部定下来了
      Fraction known(0, 1);
      for (std::size_t left = index + 1; left < half; ++left) {
        const std::size_t right = half + index - left;
        if (right > index && right < half) {
          known = known + root[left] * root[right];
        }
      }
      root[index] = (coefficient(half + index) - known) * inverseDivisor.unwrap();
    }

    const UnivariatePolynomial candidate(std::move(root));
    if (!(candidate * candidate == *this)) {
      return std::nullopt;
    }
    return candidate;
  }

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

  // poly(x - offset)，按 x 展开：x^k 的系数 = Σ_j a_j · C(j, k) · (-offset)^(j-k)。
  // 用途：α 的零化多项式是 p 时，α + offset 的零化多项式就是 p(x - offset) ——
  // 平移显然保持「无平方因子」与根的隔离性，所以可以直接拿来用。
  static UnivariatePolynomial shifted(const UnivariatePolynomial &poly, const Fraction &offset) {
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
        contribution = contribution * algebraic_detail::powInt(-offset, static_cast<unsigned>(power - term));
        result[term] = result[term] + contribution;
        algebraic_detail::guard(result[term]);
      }
    }
    // 展开会带出公共整数因子，约掉它免得系数越滚越大（只影响外观与规模，不改根）
    return UnivariatePolynomial(std::move(result)).primitivePart();
  }

  // poly(x / factor)：α 是 p 的根时，factor·α 就是它的根（factor ≠ 0）。
  static UnivariatePolynomial scaledVariable(const UnivariatePolynomial &poly, const Fraction &factor) {
    if (poly.isZero() || factor == 0LL) {
      return UnivariatePolynomial();
    }
    std::vector<Fraction> result(poly.coeffs_.size(), Fraction(0, 1));
    Fraction power(1, 1);
    for (std::size_t index = 0; index < poly.coeffs_.size(); ++index) {
      const Result<Fraction> scaled = poly.coeffs_[index] / power;
      if (scaled.isErr()) {
        return UnivariatePolynomial();
      }
      result[index] = scaled.unwrap();
      power = power * factor;
      algebraic_detail::guard(power);
    }
    return UnivariatePolynomial(std::move(result)).primitivePart();
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

  // 多项式的全部实根，**按升序**排列（互不相同，各自带精确隔离区间）。
  // 零多项式与常数多项式没有实根，返回空。
  // 解不等式的第一步就是它：把实数轴按这些临界点切开，每段符号恒定。
  static std::vector<RealAlgebraicNumber> realRoots(const UnivariatePolynomial &polynomial) {
    std::vector<RealAlgebraicNumber> result;
    const UnivariatePolynomial squareFree = polynomial.squareFreePart();
    if (squareFree.isZero() || squareFree.isConstant()) {
      return result;
    }
    const Fraction bound = cauchyBound(squareFree); // 所有实根都严格落在 (-bound, bound) 内
    isolateRootsRecursively(squareFree, -bound, bound,
                            UnivariatePolynomial::countRealRootsIn(squareFree, -bound, bound), result);
    return result;
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
      // 负数开偶次根在实数域上无定义 —— 单列错误码，别跟「区间参数非法」混为一谈
      return std::unexpected(MathsError::NegativeEvenRoot);
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
    // 与有理数相加是廉价且不会失败的特例：α + c 是 p(x − c) 的根，区间整体平移。
    // 走专用路线既省掉环上的线性代数，也避开高次多项式在那里顶穿表示范围 ——
    // 而「加一个有理数」在代入、通分里出现得极其频繁。
    if (rhs.isRational()) {
      return RealAlgebraicNumber(UnivariatePolynomial::shifted(lhs.poly_, rhs.low_), lhs.low_ + rhs.low_,
                                 lhs.high_ + rhs.low_, Validated{});
    }
    if (lhs.isRational()) {
      return RealAlgebraicNumber(UnivariatePolynomial::shifted(rhs.poly_, lhs.low_), rhs.low_ + lhs.low_,
                                 rhs.high_ + lhs.low_, Validated{});
    }
    // 同一个数自加就是 2α，直接走缩放路线。
    // 通用路线的候选多项式在这里是 x·(x²−8) 这种带个多余 0 根的东西，
    // 它本身已无平方因子（去掉 0 根要靠因式分解，库里明确不做），
    // 于是 asSingleRadical 认不出单根式，渲染退化成 RootOf。
    if (lhs == rhs) {
      return lhs.scaledByRational(Fraction(2, 1));
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
    // 同一个数相减恒为 0。走通用路线会得到一个「零但不是规范零」的表示
    // （多余 0 根留在多项式里），渲染出来是 RootOf 而不是 0。
    if (lhs == rhs) {
      return RealAlgebraicNumber(Fraction(0, 1));
    }
    return lhs + (-rhs);
  }

  friend RealAlgebraicNumber operator*(const RealAlgebraicNumber &lhs, const RealAlgebraicNumber &rhs) {
    if (lhs.isRational() && rhs.isRational()) {
      return RealAlgebraicNumber(lhs.low_ * rhs.low_);
    }
    // 与有理数相乘同理：α·c 是 p(x / c) 的根，区间按 c 缩放（c < 0 时方向翻转）
    if (rhs.isRational()) {
      return lhs.scaledByRational(rhs.low_);
    }
    if (lhs.isRational()) {
      return rhs.scaledByRational(lhs.low_);
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
    // 除以有理数：α / c = α · (1/c)，同样不必进环上的线性代数
    if (rhs.isRational()) {
      const Result<Fraction> factor = Fraction(1, 1) / rhs.low_;
      if (factor.isErr()) {
        return std::unexpected(MathsError::DivisionByZero);
      }
      return lhs.scaledByRational(factor.unwrap());
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
      return std::unexpected(MathsError::NegativeEvenRoot);
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

  // 非负整数次幂。代数数对乘法封闭，所以幂不会失败；
  // 返回 Result 只是为了和其它可能失败的入口保持一致的形状。
  Result<RealAlgebraicNumber> pow(unsigned exponent) const {
    if (exponent == 0) {
      return RealAlgebraicNumber(Fraction(1, 1));
    }
    if (exponent == 1) {
      return *this;
    }
    // 纯根式先走 O(deg) 特例：x^6 配 x = √[6]{2} 直接得 2，
    // 而不是让 x^6 = x³·x³ 掉进「环维数 deg²」的通用乘积里溢出。
    if (const std::optional<RealAlgebraicNumber> fast = radicalPower(exponent)) {
      return *fast;
    }

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
    // 值本身是有理数就直接写分数：表示里可能还挂着更高次的多项式
    // （例如 x²−1 的根 1），没必要给它套一层根号写成 \sqrt{1}
    if (const Result<Fraction> rational = toFraction(); rational.isOk()) {
      return algebraic_detail::fractionLatex(rational.unwrap());
    }
    // 能还原成单个根式就写根式：\sqrt{2} 比 RootOf(x^2-2, [...]) 好读太多
    if (const std::optional<std::pair<unsigned, Fraction>> radical = asSingleRadical()) {
      const unsigned degree = radical->first;
      Fraction radicand = radical->second;
      bool negativePrefix = false;
      if (degree % 2 == 1) {
        // 奇次实根唯一，符号已经由被开方数承载；提到根号外只是更好读。
        // 这里**不能**再看值的符号 —— 否则 -∛2 会被写成 -\sqrt[3]{-2}（那是正数）。
        if (radicand < 0LL) {
          radicand = -radicand;
          negativePrefix = true;
        }
      } else {
        // 偶次：被开方数恒正，± 分不出来，必须靠隔离区间实测值本身的符号
        negativePrefix = compareToRational(Fraction(0, 1)) == std::strong_ordering::less;
      }

      std::string body = "\\sqrt";
      if (degree != 2) {
        body += "[" + std::to_string(degree) + "]";
      }
      body += "{" + algebraic_detail::fractionLatex(radicand) + "}";
      return negativePrefix ? "-" + body : body;
    }
    return "\\operatorname{RootOf}(" + poly_.latex() + ", [" + algebraic_detail::fractionLatex(low_) + ", " +
           algebraic_detail::fractionLatex(high_) + "])";
  }

private:
  struct Validated {}; // 标记：参数已由调用方验证，不再重复检查

  // 所有根都严格落在 (-bound, bound) 内的有理界：bound = 1 + max|a_i / a_n|（Cauchy 界）。
  // 「严格」这点用得上：取 ±bound 作端点时不会是根，Sturm 计数才不需要额外规避。
  static Fraction cauchyBound(const UnivariatePolynomial &polynomial) {
    const Fraction leading = polynomial.leadingCoefficient();
    Fraction largest(0, 1);
    for (std::size_t power = 0; power < polynomial.degree(); ++power) {
      const Result<Fraction> ratio = polynomial.coefficient(power) / leading;
      if (ratio.isErr()) {
        continue; // leading 非零由调用方保证
      }
      const Fraction magnitude = ratio.unwrap() < 0LL ? -ratio.unwrap() : ratio.unwrap();
      if (largest < magnitude) {
        largest = magnitude;
      }
    }
    return Fraction(1, 1) + largest;
  }

  // 把 [low, high] 里恰好 count 个实根逐个隔离出来，按升序追加到 out。
  //
  // 分裂点落在根上（有理根）时先把它除掉再分两段递归：Sturm 计数要求端点不是根，
  // 而直接往端点挪会造成重复计数，除掉是最干净的做法。
  static void isolateRootsRecursively(const UnivariatePolynomial &polynomial, const Fraction &low, const Fraction &high,
                                      int count, std::vector<RealAlgebraicNumber> &out) {
    if (count <= 0) {
      return;
    }
    if (count == 1) {
      if (const std::optional<RealAlgebraicNumber> root = tryIsolate(polynomial, low, high)) {
        out.push_back(*root);
      }
      return;
    }

    const Fraction middle = (low + high) * Fraction(1, 2);
    if (polynomial.evaluate(middle) == 0LL) {
      const Result<std::pair<UnivariatePolynomial, UnivariatePolynomial>> division =
          UnivariatePolynomial::divide(polynomial, UnivariatePolynomial::linearRoot(middle));
      if (division.isErr()) {
        return;
      }
      const UnivariatePolynomial quotient = division.unwrap().first; // 商仍平方自由
      isolateRootsRecursively(quotient, low, middle, UnivariatePolynomial::countRealRootsIn(quotient, low, middle),
                              out);
      out.push_back(RealAlgebraicNumber(middle));
      isolateRootsRecursively(quotient, middle, high, UnivariatePolynomial::countRealRootsIn(quotient, middle, high),
                              out);
      return;
    }

    isolateRootsRecursively(polynomial, low, middle, UnivariatePolynomial::countRealRootsIn(polynomial, low, middle),
                            out);
    isolateRootsRecursively(polynomial, middle, high, UnivariatePolynomial::countRealRootsIn(polynomial, middle, high),
                            out);
  }

  // α 恰好是 ±ⁿ√r 时，α^k 是 x^(n/g) − r^(k/g) 的根（g = gcd(n, k)）——
  // 因为 (α^k)^(n/g) = (α^n)^(k/g) = r^(k/g)。
  // 这条比通用乘积（环维数 n²）便宜得多，也让高次幂不再顶穿表示范围。
  // 拿不到根式（例如 √2+√3）时返回 nullopt，由调用方回退到通用路线。
  std::optional<RealAlgebraicNumber> radicalPower(unsigned exponent) const {
    if (exponent == 0 || isRational()) {
      return std::nullopt;
    }
    const std::optional<std::pair<unsigned, Fraction>> radical = asSingleRadical();
    if (!radical) {
      return std::nullopt;
    }
    const unsigned degree = radical->first;
    const unsigned common = std::gcd(degree, exponent);
    const unsigned reducedDegree = degree / common;
    const unsigned reducedPower = exponent / common;
    if (reducedDegree == 0 || reducedPower == 0) {
      return std::nullopt;
    }

    // 结果直接交给 nthRootOf 这个**规范构造**去落地，而不是自己拼多项式 + tryIsolate：
    // tryIsolate 对"区间是不是刚好只罩住一个根"很敏感，α 存的区间往往很宽
    // （⁴√2 是 [3/4, 9/4]），取幂后区间会同时罩住 ± 两个根，于是特例白白放弃、
    // 掉进通用乘积（α·α² 那条路还会抛 ZeroDenominator）。
    // nthRootOf 走 nthRootBounds + 单调二分，对区间宽度不敏感。
    const Result<RealAlgebraicNumber> root =
        nthRootOf(algebraic_detail::powInt(radical->second, reducedPower), reducedDegree);
    if (root.isErr()) {
      return std::nullopt; // 例如偶次根下为负，交回通用路线
    }

    // 偶次根有两个实根，符号要靠 α 自己定；奇次根唯一，符号已由被开方数承载。
    if (reducedDegree % 2 == 0 && exponent % 2 == 1 &&
        compareToRational(Fraction(0, 1)) == std::strong_ordering::less) {
      return -root.unwrap();
    }
    return root.unwrap();
  }

  // 最小多项式只有首项与常数项非零时（a·x^n + c），这个数就是 ±ⁿ√(-c/a)。
  // 返回 (阶数, 被开方数)；nullopt 表示还原不成单个根式
  // （例如 √2+√3，它的多项式 x^4-10x^2+1 有中间项）。
  std::optional<std::pair<unsigned, Fraction>> asSingleRadical() const {
    if (isRational()) {
      return std::nullopt;
    }
    const std::size_t degree = poly_.degree();
    if (degree < 2) {
      return std::nullopt;
    }
    for (std::size_t power = 1; power < degree; ++power) {
      if (poly_.coefficient(power) != Fraction(0, 1)) {
        return std::nullopt;
      }
    }
    const Result<Fraction> radicand = (Fraction(0, 1) - poly_.constantTerm()) / poly_.leadingCoefficient();
    if (radicand.isErr()) {
      return std::nullopt; // 首项系数非零，实际到不了这里，兜底
    }
    return std::make_pair(static_cast<unsigned>(degree), radicand.unwrap());
  }

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

  // α·c（c 为有理数）：多项式换成 p(x / c)，区间按 c 缩放。
  // c < 0 时区间的两个端点会交换大小，必须重新排序。
  RealAlgebraicNumber scaledByRational(const Fraction &factor) const {
    if (factor == 0LL || isZero()) {
      return RealAlgebraicNumber(Fraction(0, 1));
    }
    const UnivariatePolynomial scaled = UnivariatePolynomial::scaledVariable(poly_, factor);
    if (scaled.isZero() || scaled.isConstant()) {
      return RealAlgebraicNumber(Fraction(0, 1)); // 不该发生，兜底
    }
    if (factor.isNegative()) {
      return RealAlgebraicNumber(scaled, high_ * factor, low_ * factor, Validated{});
    }
    return RealAlgebraicNumber(scaled, low_ * factor, high_ * factor, Validated{});
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
    const Result<std::pair<long long, long long>> exponent = parseRationalExponent();
    if (exponent.isErr()) {
      return std::unexpected(exponent.unwrapErr());
    }
    return powerWithRationalExponent(base.unwrap(), exponent.unwrap().first, exponent.unwrap().second);
  }

  // a^(p/q)：先取 q 次根再取 p 次幂；p 为负时取倒数。
  // 实数域上的定义域限制交给 nthRoot 判（偶次根要求非负），这里只管负指数与零。
  static Result<RealAlgebraicNumber> powerWithRationalExponent(const RealAlgebraicNumber &base, long long numerator,
                                                               long long denominator) {
    if (denominator == 0) {
      return std::unexpected(MathsError::ZeroDenominator);
    }
    const bool negativeExponent = numerator < 0;
    unsigned long long magnitude =
        negativeExponent ? algebraic_detail::magnitudeOf(numerator) : static_cast<unsigned long long>(numerator);
    unsigned long long reducedDenominator = static_cast<unsigned long long>(denominator);

    // 先把 p/q 约到最简：2^{100/50} 没必要先开 50 次根（那要构造 50 次多项式，直接溢出）
    const unsigned long long common = std::gcd(magnitude, reducedDenominator);
    if (common > 1ULL) {
      magnitude /= common;
      reducedDenominator /= common;
    }
    if (magnitude == 0ULL) {
      return RealAlgebraicNumber(Fraction(1, 1)); // p = 0 → a^0 = 1（含 a = 0，0^0 按 1 处理）
    }

    const Result<RealAlgebraicNumber> root = base.nthRoot(static_cast<unsigned>(reducedDenominator));
    if (root.isErr()) {
      return std::unexpected(root.unwrapErr());
    }
    const Result<RealAlgebraicNumber> powered = root.unwrap().pow(static_cast<unsigned>(magnitude));
    if (powered.isErr()) {
      return std::unexpected(powered.unwrapErr());
    }
    if (numerator >= 0) {
      return powered.unwrap();
    }
    if (powered.unwrap().isZero()) {
      return std::unexpected(MathsError::DivisionByZero); // 0 的负次幂无定义
    }
    return powered.unwrap().inverse();
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
        // 同样必须整串校验：`\sqrt[1/2]{2}` 曾是 stoul 前缀解析成 1 次根 → 静默给出 2
        const std::optional<unsigned long long> parsedDegree = algebraic_detail::parseWholeUnsigned(digits);
        if (!parsedDegree) {
          return std::unexpected(MathsError::InvalidExpression);
        }
        if (*parsedDegree == 0ULL) {
          return std::unexpected(MathsError::InvalidRange);
        }
        if (*parsedDegree > static_cast<unsigned long long>(std::numeric_limits<unsigned>::max())) {
          return std::unexpected(MathsError::InvalidRange);
        }
        degree = static_cast<unsigned>(*parsedDegree);
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

  // 从一段独立文本里读花括号分组（逻辑同成员版，但不依赖 position_）
  static bool takeBracedGroupAt(std::string_view source, std::size_t &position, std::string &out) {
    while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position])) != 0) {
      ++position;
    }
    if (position >= source.size() || source[position] != '{') {
      return false;
    }
    int depth = 0;
    const std::size_t start = position + 1;
    while (position < source.size()) {
      if (source[position] == '{') {
        ++depth;
      } else if (source[position] == '}') {
        --depth;
        if (depth == 0) {
          out = std::string(source.substr(start, position - start));
          ++position;
          return true;
        }
      }
      ++position;
    }
    return false;
  }

  // 读一个可带符号的整数字面量（不含空白），返回消费到的文本；读不到则返回空
  std::string_view takeIntegerToken() {
    skipSpaces();
    const std::size_t start = position_;
    if (!atEnd() && (peek() == '+' || peek() == '-')) {
      ++position_;
    }
    const std::size_t digitsStart = position_;
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
      ++position_;
    }
    if (position_ == digitsStart) {
      position_ = start;
      return {};
    }
    return text_.substr(start, position_ - start);
  }

  // 把分子分母两段文本合成一个有理数（分母必须非零）
  static Result<std::pair<long long, long long>> combineRationalText(std::string_view numeratorText,
                                                                     std::string_view denominatorText) {
    const std::optional<long long> numerator = algebraic_detail::parseWholeSigned(numeratorText);
    const std::optional<unsigned long long> denominator = algebraic_detail::parseWholeUnsigned(denominatorText);
    if (!numerator || !denominator) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    if (*denominator == 0ULL) {
      return std::unexpected(MathsError::ZeroDenominator);
    }
    if (*denominator > static_cast<unsigned long long>(std::numeric_limits<long long>::max())) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    return std::make_pair(*numerator, static_cast<long long>(*denominator));
  }

  // 一段文本形式的有理数指数：p、p/q、\frac{p}{q}
  static Result<std::pair<long long, long long>> parseRationalText(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
      text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
      text.remove_suffix(1);
    }
    if (text.empty()) {
      return std::unexpected(MathsError::InvalidExpression);
    }

    if (text.compare(0, 5, "\\frac") == 0) {
      std::size_t position = 5;
      std::string numeratorText;
      std::string denominatorText;
      if (!takeBracedGroupAt(text, position, numeratorText) || !takeBracedGroupAt(text, position, denominatorText)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      if (position != text.size()) {
        return std::unexpected(MathsError::InvalidExpression); // \frac 之后还有多余字符
      }
      return combineRationalText(numeratorText, denominatorText);
    }

    const std::size_t slash = text.find('/');
    if (slash == std::string_view::npos) {
      const std::optional<long long> numerator = algebraic_detail::parseWholeSigned(text);
      if (!numerator) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      return std::make_pair(*numerator, 1LL);
    }
    return combineRationalText(text.substr(0, slash), text.substr(slash + 1));
  }

  // 指数：p、p/q、\frac{p}{q}，也接受外层花括号（^{…}）。
  // 返回 (分子, 分母)，分母恒正。**内容有多余字符一律报错** ——
  // 早先这里裸调 stoull，`2^{1/2}` 被前缀解析成指数 1，静默得到 2。
  Result<std::pair<long long, long long>> parseRationalExponent() {
    skipSpaces();
    if (peek() == '{') {
      std::string group;
      if (!takeBracedGroup(group)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      return parseRationalText(group);
    }
    if (takeToken("\\frac")) {
      std::string numeratorText;
      std::string denominatorText;
      if (!takeBracedGroup(numeratorText) || !takeBracedGroup(denominatorText)) {
        return std::unexpected(MathsError::InvalidExpression);
      }
      return combineRationalText(numeratorText, denominatorText);
    }

    const std::string_view numeratorText = takeIntegerToken();
    if (numeratorText.empty()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    skipSpaces();
    if (peek() != '/') {
      const Result<std::pair<long long, long long>> single = combineRationalText(numeratorText, std::string_view("1"));
      return single;
    }
    ++position_;
    const std::string_view denominatorText = takeIntegerToken();
    if (denominatorText.empty()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    return combineRationalText(numeratorText, denominatorText);
  }

  std::string_view text_;
  std::size_t position_{0};
};

} // namespace algebraic_detail

inline Result<RealAlgebraicNumber> RealAlgebraicNumber::parse(std::string_view text) {
  return algebraic_detail::AlgebraicParser(text).parse();
}

} // namespace maths
