export module maths.algebra:rational;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import :expression;

export namespace maths {

// 分式（有理函数）：两个多项式之比 P/Q，Q 恒不为零多项式。
//
// 化简策略：只做能保证正确的部分，不做多项式因式分解。
//   L1 数值内容约分 —— 分子分母所有系数的 gcd。常数非零恒成立，因此无任何副作用。
//   L2 单项式公因子约分 —— 变量指数逐项取 min 后同时除掉，如 6x^2y / 4xy^2 → 3x / 2y。
//   L3 分母符号归一 —— 分母首项系数取正，消除 1/(-x-1) 与 -1/(x+1) 两种写法。
//
// L2 是有代价的：约掉变量 x 等价于默认 x ≠ 0，也就是丢掉了「原式在 x = 0 处无定义」
// 这一信息。被丢掉的约束记录在 discardedConstraints() 里，语义是
// 「以上化简在该变量非零的前提下成立」。若变量取值可能为 0，调用方应先检查它。
//
// 刻意不做多项式 GCD：多元 GCD 实现复杂，且辗转相除存在系数爆炸风险，
// 收益不足以抵消成本。需要判断两个分式是否相等时，用交叉相乘即可绕开 GCD。

// Substitute / evaluate 需要 Scope 的完整定义，因此实现放在 scope.hpp。
// 这里只用前向声明，依赖保持单向：scope.hpp → rational_function.hpp
class Scope;

namespace detail {

// 取 long long 的无符号绝对值，规避 LLONG_MIN 取负的未定义行为
inline unsigned long long magnitudeOf(long long value) {
  return value < 0 ? static_cast<unsigned long long>(0) - static_cast<unsigned long long>(value)
                   : static_cast<unsigned long long>(value);
}

// 有理数 gcd：gcd(a/b, c/d) = gcd(a, c) / lcm(b, d)，结果非负。
// 必须走无符号版本：std::gcd 在内部对操作数调用 std::abs，
// 遇到 LLONG_MIN 会直接断言失败（Integer(LLONG_MIN) 转成分数就会踩到）。
inline Fraction gcdFraction(const Fraction &lhs, const Fraction &rhs) {
  if (lhs == 0LL) {
    return rhs.isNegative() ? -rhs : rhs;
  }
  if (rhs == 0LL) {
    return lhs.isNegative() ? -lhs : lhs;
  }
  const unsigned long long numeratorGcd = std::gcd(magnitudeOf(lhs.getNumerator()), magnitudeOf(rhs.getNumerator()));
  return Fraction(static_cast<long long>(numeratorGcd), std::lcm(lhs.getDenominator(), rhs.getDenominator()));
}

// 多项式的数值内容：所有系数的 gcd
inline Fraction contentOf(const Polynomial &polynomial) {
  Fraction result(0, 1);
  for (const auto &entry : polynomial.getTerms()) {
    result = gcdFraction(result, entry.second);
  }
  return result;
}

// 所有项共有的变量因子：每个变量取它在各出现项中的最小指数
inline VarPowers commonVariables(const Polynomial &polynomial) {
  VarPowers result;
  bool first = true;
  for (const auto &entry : polynomial.getTerms()) {
    if (first) {
      result = entry.first;
      first = false;
      continue;
    }
    const VarPowers &factors = entry.first;
    VarPowers intersection;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < result.size() && j < factors.size()) {
      if (result[i].first == factors[j].first) {
        intersection.emplace_back(result[i].first, std::min(result[i].second, factors[j].second));
        ++i;
        ++j;
      } else if (result[i].first < factors[j].first) {
        ++i; // 该变量没有出现在本项里，公共部分不含它
      } else {
        ++j;
      }
    }
    result = std::move(intersection);
  }
  return result;
}

// 两个有序因子表的交集，同变量取指数较小者
inline VarPowers intersectVariables(const VarPowers &lhs, const VarPowers &rhs) {
  VarPowers result;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < lhs.size() && j < rhs.size()) {
    if (lhs[i].first == rhs[j].first) {
      result.emplace_back(lhs[i].first, std::min(lhs[i].second, rhs[j].second));
      ++i;
      ++j;
    } else if (lhs[i].first < rhs[j].first) {
      ++i;
    } else {
      ++j;
    }
  }
  return result;
}

} // namespace detail

class RationalFunction {
public:
  // ==================== 构造 ====================

  RationalFunction() = default; // 0/1

  // 隐式提升：分母为 1，不会失败，因此不需要 Result
  RationalFunction(const Polynomial &numerator_) : numerator(numerator_) {}
  RationalFunction(const Monomial &numerator_) : numerator(numerator_) {}
  // 只提供 Fraction 版本：再提供 Integer 版本会让 RationalFunction(5) 产生歧义
  // （int 到 Fraction 与 int 到 Integer 都是一次用户定义转换）。需要 Integer 时
  // 显式写 RationalFunction(Fraction::fromInteger(value))。
  RationalFunction(Fraction value) : numerator(Monomial(value)) {}

  // 完整构造：分母为零多项式时返回 MathsError::ZeroDenominator
  static Result<RationalFunction> make(Polynomial numerator_, Polynomial denominator_) {
    if (denominator_.isZero()) {
      return std::unexpected(MathsError::ZeroDenominator);
    }
    RationalFunction result;
    result.numerator = std::move(numerator_);
    result.denominator = std::move(denominator_);
    result.simplify();
    return result;
  }

  // ==================== 访问 ====================

  const Polynomial &getNumerator() const { return numerator; }
  const Polynomial &getDenominator() const { return denominator; }

  // 是否含某个变量（分子或分母中出现即算）
  bool containsVariable(const Variable &variable) const {
    return numerator.containsVariable(variable) || denominator.containsVariable(variable);
  }

  // 分子分母中出现的全部变量
  std::set<Variable> variables() const {
    std::set<Variable> result;
    for (const auto &entry : numerator.getTerms()) {
      for (const auto &factor : entry.first) {
        result.insert(factor.first);
      }
    }
    for (const auto &entry : denominator.getTerms()) {
      for (const auto &factor : entry.first) {
        result.insert(factor.first);
      }
    }
    return result;
  }

  const std::set<Variable> &discardedConstraints() const { return discarded; }

  bool isZero() const { return numerator.isZero(); }

  // ==================== 化简 ====================

  // 幂等：已化简的分式再次调用不会产生变化，也不会重复记录约束
  void simplify() {
    reduceNumericContent();
    reduceCommonVariables();
    normalizeSign();
  }

  // ==================== 运算 ====================
  // 加减乘不会失败（分母始终是原有非零分母之积）。
  // 内部的多项式乘法在指数溢出时会抛 MathsException —— 那属于极端边界，不作为常规错误路径。

  RationalFunction operator+(const RationalFunction &rhs) const {
    // a/b + c/d = (a*d + c*b) / (b*d)
    return combine(*this, rhs, (numerator * rhs.denominator).unwrap() + (rhs.numerator * denominator).unwrap());
  }

  RationalFunction operator-(const RationalFunction &rhs) const {
    // a/b - c/d = (a*d - c*b) / (b*d)
    return combine(*this, rhs, (numerator * rhs.denominator).unwrap() - (rhs.numerator * denominator).unwrap());
  }

  RationalFunction operator*(const RationalFunction &rhs) const {
    // a/b * c/d = (a*c) / (b*d)
    return combine(*this, rhs, (numerator * rhs.numerator).unwrap());
  }

  // 除法可能失败：除数的分子为零多项式时，结果的分母会变成零
  Result<RationalFunction> operator/(const RationalFunction &rhs) const {
    if (rhs.numerator.isZero()) {
      return std::unexpected(MathsError::ZeroDenominator);
    }
    // a/b ÷ c/d = (a*d) / (b*c)
    RationalFunction result = fromParts((numerator * rhs.denominator).unwrap(), (denominator * rhs.numerator).unwrap());
    result.discarded.insert(discarded.begin(), discarded.end());
    result.discarded.insert(rhs.discarded.begin(), rhs.discarded.end());
    return result;
  }

  RationalFunction operator-() const {
    RationalFunction result(*this);
    result.numerator = -result.numerator;
    return result;
  }

  RationalFunction &operator+=(const RationalFunction &rhs) {
    *this = *this + rhs;
    return *this;
  }

  RationalFunction &operator-=(const RationalFunction &rhs) {
    *this = *this - rhs;
    return *this;
  }

  RationalFunction &operator*=(const RationalFunction &rhs) {
    *this = *this * rhs;
    return *this;
  }

  bool operator==(const RationalFunction &rhs) const {
    // 交叉相乘判等，无需化简成正规形式（也就不需要多项式 GCD）
    return (numerator * rhs.denominator).unwrap() == (rhs.numerator * denominator).unwrap();
  }

  // ==================== 代入与求值 ====================
  // 实现放在 scope.hpp（Scope 的完整定义之后），此处只声明，避免循环包含。

  // 反复替换直到不再变化，以处理 s = v*t、v = a*b 这类链式绑定。
  // 代入后分母退化成零多项式时（例如 1/(x-1) 代入 x=1）返回 ZeroDenominator。
  Result<RationalFunction> substitute(const Scope &scope) const;

  // 完全求值：要求代入后分子分母都化为常数，否则 UndefinedVariable
  Result<Fraction> evaluate(const Scope &scope) const;

  // ==================== 化为多项式 ====================

  // 用长除法尝试把分母化掉：当且仅当分母整除分子时成功，返回商；
  // 否则返回 MathsError::NotAPolynomial（此时它确实不是一个多项式）。
  Result<Polynomial> toPolynomial() const {
    const Result<PolynomialDivision> division = divideWithRemainder(numerator, denominator);
    if (division.isErr()) {
      return std::unexpected(division.unwrapErr());
    }
    if (!division.unwrap().remainder.isZero()) {
      return std::unexpected(MathsError::NotAPolynomial);
    }
    return division.unwrap().quotient;
  }

  // ==================== 输出 ====================

  std::string str() const {
    const Result<Monomial> numeratorMonomial = numerator.toMonomial();
    const Result<Monomial> denominatorMonomial = denominator.toMonomial();

    // 分子分母都是常数：按数值分数输出，如 5/6 而不是 (5) / (6)
    if (numeratorMonomial.isOk() && numeratorMonomial.unwrap().isConstant() && denominatorMonomial.isOk() &&
        denominatorMonomial.unwrap().isConstant()) {
      const Fraction value =
          (numeratorMonomial.unwrap().getCoefficient() / denominatorMonomial.unwrap().getCoefficient())
              .unwrap(); // 分母非零由构造保证
      return detail::renderFraction(value);
    }

    // 分母恰为常数 1：按多项式输出。
    // 注意必须同时要求 isConstant()，否则分母是 x 这类系数为 1 的单项式会被误判。
    if (denominatorMonomial.isOk() && denominatorMonomial.unwrap().isConstant() &&
        denominatorMonomial.unwrap().getCoefficient() == 1LL) {
      return numerator.str();
    }

    return "(" + numerator.str() + ") / (" + denominator.str() + ")";
  }

  // LaTeX 形式。
  // 与 str() 的差别：一律输出 \frac{分子}{分母}，不再把常数分式写成 5/6。
  // 因此 (a/b) + c 这类运算的结果稳定呈现为 \frac{a + b*c}{b} 的统一形式 ——
  // 通分本身由 operator+ 完成，这里只负责不把它拆散。
  std::string latex() const {
    const Result<Monomial> denominatorMonomial = denominator.toMonomial();
    if (denominatorMonomial.isOk() && denominatorMonomial.unwrap().isConstant() &&
        denominatorMonomial.unwrap().getCoefficient() == 1LL) {
      return numerator.latex(); // 分母恰为 1，退化成多项式
    }

    // 分式整体为负时把负号提到 \frac 外：-\frac{3}{4} 而不是 \frac{-3}{4}。
    // 分母首项已由 normalizeSign 归一为正，所以只看分子首项即可。
    const std::optional<Monomial> numeratorLead = leadingMonomial(numerator);
    if (numeratorLead && numeratorLead->getCoefficient().isNegative()) {
      return "-\\frac{" + (-numerator).latex() + "}{" + denominator.latex() + "}";
    }

    return "\\frac{" + numerator.latex() + "}{" + denominator.latex() + "}";
  }

private:
  // 由已确保非零的分母构造，并同步两侧约束
  static RationalFunction fromParts(Polynomial numerator_, Polynomial denominator_) {
    RationalFunction result;
    result.numerator = std::move(numerator_);
    result.denominator = std::move(denominator_);
    result.simplify();
    return result;
  }

  static RationalFunction combine(const RationalFunction &lhs, const RationalFunction &rhs, Polynomial numerator_) {
    RationalFunction result = fromParts(std::move(numerator_), (lhs.denominator * rhs.denominator).unwrap());
    result.discarded.insert(lhs.discarded.begin(), lhs.discarded.end());
    result.discarded.insert(rhs.discarded.begin(), rhs.discarded.end());
    return result;
  }

  // L1：约掉分子分母所有系数的公共因子
  void reduceNumericContent() {
    const Fraction common = detail::gcdFraction(detail::contentOf(numerator), detail::contentOf(denominator));
    if (common == 0LL || common == 1LL) {
      return;
    }
    numerator = divideByConstant(numerator, common);
    denominator = divideByConstant(denominator, common);
  }

  // L2：约掉共有的变量因子，并把被丢掉的「变量非零」约束记录下来
  void reduceCommonVariables() {
    const VarPowers common =
        detail::intersectVariables(detail::commonVariables(numerator), detail::commonVariables(denominator));
    if (common.empty()) {
      return;
    }
    numerator = divideByMonomial(numerator, common);
    denominator = divideByMonomial(denominator, common);
    for (const auto &factor : common) {
      discarded.insert(factor.first); // 约掉 x 等价于默认 x ≠ 0
    }
  }

  // L3：分母首项系数取正
  void normalizeSign() {
    // 首项必须按字典序单项式序取（leadingMonomial），不能用存储序 getTerms().begin()：
    // 存储序下常数项的键最小，会把常数项的符号当成首项符号，
    // 例如 (x^2 - 1)/(x - 1) 会被误判并整体取反。
    const std::optional<Monomial> leading = leadingMonomial(denominator);
    if (!leading) {
      return;
    }
    if (leading->getCoefficient().isNegative()) {
      numerator = -numerator;
      denominator = -denominator;
    }
  }

  static Polynomial divideByConstant(const Polynomial &polynomial, const Fraction &divisor) {
    Polynomial result;
    for (const auto &entry : polynomial.getTerms()) {
      result.addTerm(entry.first, (entry.second / divisor).unwrap()); // divisor 为 gcd，必非零
    }
    return result;
  }

  static Polynomial divideByMonomial(const Polynomial &polynomial, const VarPowers &divisor) {
    Polynomial result;
    for (const auto &entry : polynomial.getTerms()) {
      VarPowers reduced = entry.first;
      for (const auto &factor : divisor) {
        for (auto &candidate : reduced) {
          if (candidate.first == factor.first) {
            candidate.second -= factor.second; // 有公因子保证不会借位
            break;
          }
        }
      }
      Monomial::normalizeFactors(reduced); // 去掉指数降到 0 的因子
      result.addTerm(reduced, entry.second);
    }
    return result;
  }

  Polynomial numerator;
  Polynomial denominator{Monomial(Fraction(1, 1))};
  std::set<Variable> discarded;
};

inline std::ostream &operator<<(std::ostream &os, const RationalFunction &value) { return os << value.str(); }

} // namespace maths
