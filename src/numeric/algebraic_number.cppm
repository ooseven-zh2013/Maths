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

    // 环的维数是 dimension，所以幂序列 1, t, t², … 至多 dimension 步必然线性相关
    std::vector<std::vector<Fraction>> powers;
    std::vector<Fraction> current(dimension, Fraction(0, 1));
    current[0] = Fraction(1, 1); // t^0 = 1
    for (std::size_t power = 0; power <= dimension; ++power) {
      if (const std::optional<std::vector<Fraction>> coefficients = expressAsCombination(powers, current)) {
        std::vector<Fraction> polynomial(power + 1, Fraction(0, 1));
        for (std::size_t index = 0; index < power; ++index) {
          polynomial[index] = -(*coefficients)[index];
        }
        polynomial[power] = Fraction(1, 1);
        return UnivariatePolynomial(std::move(polynomial));
      }
      powers.push_back(current);
      current = multiply(current, element);
    }
    return UnivariatePolynomial(); // 理论上到不了这里
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
      }
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

  // ==================== 观察 ====================

  const UnivariatePolynomial &polynomial() const { return poly_; }
  Fraction lowerBound() const { return low_; }
  Fraction upperBound() const { return high_; }
  std::size_t degree() const { return poly_.degree(); }

  // 是否已退化为精确有理数（构造时端点重合）
  bool isRational() const { return low_ == high_; }

  std::optional<Fraction> asRational() const { return low_ == high_ ? std::optional<Fraction>(low_) : std::nullopt; }

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
    const UnivariatePolynomial candidate =
        UnivariatePolynomial::annihilatorOfProduct(lhs.poly_, rhs.poly_).squareFreePart();
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

} // namespace maths
