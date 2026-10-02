export module maths.algebra:expression;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;

export namespace maths {

class Name {
public:
  Name() {}
  Name(std::string_view name_) { *this = name_; }

  std::string str() const { return name; }

  Name &operator=(std::string_view name_) {
    if (check(name_)) {
      name = std::string(name_);
      return *this;
    }
    throw MathsException(MathsError::InvalidName);
  }

  auto operator<=>(const Name &oth) const { return name <=> oth.name; }

private:
  std::string name;
  bool check(std::string_view name_) {
    if (name_.empty())
      return false;
    bool allAlpha = true;
    bool allDigits = true;
    for (char c : name_) {
      unsigned char uc = static_cast<unsigned char>(c);
      if (!std::isalpha(uc))
        allAlpha = false;
      if (!std::isdigit(uc))
        allDigits = false;
      if (!allAlpha && !allDigits)
        return false;
    }
    return allAlpha || allDigits;
  }
};

class Variable {
public:
  Variable(std::string_view name_) { convertToName(name_, false); }
  Variable(std::string_view name_, bool allowNumeric) { convertToName(name_, allowNumeric); }

  Variable &operator=(std::string_view name_) {
    convertToName(name_, false);
    return *this;
  }

  bool hasIndex() const { return !index.empty(); }

  std::string str() const {
    std::string res = name.str();
    if (hasIndex()) {
      res += '_';
      if (index.size() == 1) {
        if (index.begin()->hasIndex()) {
          res += '{';
          res += index.begin()->str();
          res += '}';
        } else {
          res += index.begin()->str();
        }
      } else {
        res += '{';
        bool first = true;
        for (const Variable &idx : index) {
          if (first) {
            first = false;
          } else {
            res += ',';
          }
          res += idx.str();
        }
        res += '}';
      }
    }
    return res;
  }

  auto operator<=>(const Variable &oth) const {
    if (auto cmp = name <=> oth.name; cmp != 0)
      return cmp;
    for (std::size_t i = 0, size_ = std::min(index.size(), oth.index.size()); i < size_; ++i) {
      if (auto cmp = index[i] <=> oth.index[i]; cmp != 0)
        return cmp;
    }
    return index.size() <=> oth.index.size();
  }

  bool operator==(const Variable &oth) const { return (*this <=> oth) == 0; }

private:
  Name name;                   // variable name
  std::vector<Variable> index; // variable index
  void convertToName(std::string_view name_, bool allowNumeric) {
    // convert LaTex expression to NameType
    Name tmpName;
    std::vector<Variable> tmpIdx;
    auto pos = name_.find('_');
    if (pos == std::string_view::npos) {
      // 不允许纯数字作为变量名（纯数字应作为常数或索引），除非 allowNumeric 为 true
      bool allDigits = !name_.empty() && std::all_of(name_.begin(), name_.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c));
      });
      if (allDigits && !allowNumeric) {
        throw MathsException(MathsError::InvalidName);
      }
      tmpName = name_;
    } else {
      // 基名必须为字母（不能是纯数字或包含其它字符）
      auto base = name_.substr(0, pos);
      if (!std::all_of(base.begin(), base.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)); })) {
        throw MathsException(MathsError::InvalidName);
      }
      tmpName = base;
      name_ = name_.substr(pos + 1);

      if (name_.empty()) {
        throw MathsException(MathsError::InvalidName);
      }

      if (name_.front() != '{') {
        // 单索引形式，例如 a_1 或 a_x
        // 遵循 LaTeX 规范：_ 后不带 {} 时只接受单个字符（字母或数字）
        if (name_.size() != 1) {
          throw MathsException(MathsError::InvalidName);
        }
        if (name_.find('_') != std::string_view::npos) {
          throw MathsException(MathsError::InvalidName);
        }
        tmpIdx.emplace_back(name_, true);
      } else {
        // 大括号形式 a_{...}
        if (name_.back() != '}') {
          throw MathsException(MathsError::InvalidName);
        }
        name_ = name_.substr(1, name_.size() - 2);
        std::size_t last = 0;
        while (true) {
          pos = name_.find(',', last);
          if (pos == std::string_view::npos) {
            tmpIdx.emplace_back(name_.substr(last), true);
            break;
          } else {
            tmpIdx.emplace_back(name_.substr(last, pos - last), true);
            last = pos + 1;
          }
        }
      }
    }
    name = std::move(tmpName);
    index = std::move(tmpIdx);
  }
};

// 单项式的变量因子：变量 + 次数。
// 规范化后按变量升序排列、不含零次幂、同底数已合并，因此同一个单项式只有一种表示。
using VarPowers = std::vector<std::pair<Variable, unsigned long long>>;

namespace detail {

inline std::string renderFraction(const Fraction &value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

// 分数的 LaTeX 形式：整数直接输出，真分数写成 \frac{分子}{分母}，负号提到最前
inline std::string renderFractionLatex(const Fraction &value) {
  if (value.getDenominator() == 1) {
    return std::to_string(value.getNumerator());
  }
  const long long numerator = value.getNumerator();
  const long long magnitude = numerator < 0 ? -numerator : numerator;
  const std::string body = "\\frac{" + std::to_string(magnitude) + "}{" + std::to_string(value.getDenominator()) + "}";
  return value.isNegative() ? "-" + body : body;
}

// ==================== 系数的通用操作 ====================
//
// 下面这组函数让「系数」可以从 Fraction 换成别的精确数类型（例如实代数数）。
// 都用 if constexpr 分流：有对应成员就走成员，否则退回到从 Fraction 构造的等价写法。
// 这样写的好处是既有的 Fraction 路径一行行为都不变，新类型也不必特化整条代数栈。

template <class Coefficient> inline Coefficient coefficientZero() { return Coefficient(Fraction(0, 1)); }
template <class Coefficient> inline Coefficient coefficientOne() { return Coefficient(Fraction(1, 1)); }
template <class Coefficient> inline Coefficient coefficientMinusOne() { return Coefficient(Fraction(-1, 1)); }

// 系数取幂。系数类型自带 pow 时走它 —— 例如 RealAlgebraicNumber::pow 对纯根式有
// O(deg) 特例（α = ±ⁿ√r 时 α^k 直接给出）。**不要**在调用方用线性乘法代替：
// α^k 会被退化成 k−1 次通用乘积（环维数 deg²），中间量迅速顶穿 Fraction。
template <class Coefficient>
inline Result<Coefficient> coefficientPower(const Coefficient &base, unsigned long long exponent) {
  if (exponent == 0ULL) {
    return coefficientOne<Coefficient>();
  }
  if constexpr (requires(const Coefficient &value, unsigned power) { value.pow(power); }) {
    return base.pow(static_cast<unsigned>(exponent));
  } else {
    Coefficient result = coefficientOne<Coefficient>();
    Coefficient current = base;
    unsigned long long remaining = exponent;
    while (remaining > 0) {
      if (remaining % 2 == 1) {
        result = result * current;
      }
      remaining /= 2;
      if (remaining > 0) {
        current = current * current;
      }
    }
    return result;
  }
}

template <class Coefficient> inline bool isZeroCoefficient(const Coefficient &value) {
  if constexpr (requires { value.isZero(); }) {
    return value.isZero();
  } else {
    return value == coefficientZero<Coefficient>();
  }
}

template <class Coefficient> inline bool isNegativeCoefficient(const Coefficient &value) {
  if constexpr (requires { value.isNegative(); }) {
    return value.isNegative();
  } else {
    return value < coefficientZero<Coefficient>();
  }
}

template <class Coefficient> inline std::string coefficientText(const Coefficient &value) {
  std::ostringstream os;
  os << value;
  return os.str();
}

template <class Coefficient> inline std::string coefficientLatex(const Coefficient &value) {
  if constexpr (requires { value.latex(); }) {
    return value.latex();
  } else {
    return renderFractionLatex(value); // 目前只有 Fraction 会走这条
  }
}

inline unsigned long long degreeOf(const VarPowers &factors) {
  unsigned long long total = 0;
  for (const auto &factor : factors) {
    total += factor.second;
  }
  return total;
}

// 仅用于 str() 的展示顺序：次数降序 → 变量名升序 → 同变量指数降序，
// 使输出贴近手写习惯（x^2 + 2 x y + y^2 而不是 y^2 + x^2 + 2 x y）
inline bool displayOrderLess(const VarPowers &lhs, const VarPowers &rhs) {
  const auto lhsDegree = degreeOf(lhs);
  const auto rhsDegree = degreeOf(rhs);
  if (lhsDegree != rhsDegree) {
    return lhsDegree > rhsDegree;
  }
  for (std::size_t i = 0; i < std::min(lhs.size(), rhs.size()); ++i) {
    if (lhs[i].first != rhs[i].first) {
      return lhs[i].first < rhs[i].first;
    }
    if (lhs[i].second != rhs[i].second) {
      return lhs[i].second > rhs[i].second;
    }
  }
  return lhs.size() > rhs.size();
}

// 字典序（lex）单项式序：按变量名升序逐个比较指数，某一侧缺失的变量视为指数 0。
//
// 必须用这个序，不能用 VarPowers 的默认比较 —— 后者不是合法的单项式序，
// 因为它不满足乘法相容性：默认比较下 x < y，两边同乘 x 后
// x^2 的首项 (x,2) 大于 xy 的首项 (x,1)，得到 x^2 > xy，与 x < y 矛盾。
// 项序不相容会导致多项式除法不终止。
inline std::strong_ordering compareLex(const VarPowers &lhs, const VarPowers &rhs) {
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < lhs.size() || j < rhs.size()) {
    unsigned long long lhsExponent = 0;
    unsigned long long rhsExponent = 0;

    if (j >= rhs.size() || (i < lhs.size() && lhs[i].first < rhs[j].first)) {
      lhsExponent = lhs[i].second; // 该变量只在左侧出现，右侧视为 0
      ++i;
    } else if (i >= lhs.size() || rhs[j].first < lhs[i].first) {
      rhsExponent = rhs[j].second;
      ++j;
    } else {
      lhsExponent = lhs[i].second;
      rhsExponent = rhs[j].second;
      ++i;
      ++j;
    }

    if (lhsExponent != rhsExponent) {
      return lhsExponent > rhsExponent ? std::strong_ordering::greater : std::strong_ordering::less;
    }
  }
  return std::strong_ordering::equal;
}

} // namespace detail

template <class Coefficient> class MonomialOf;
template <class Coefficient> class PolynomialOf;
template <class Coefficient> class RationalFunctionOf;
template <class Coefficient> class ScopeOf;

template <class Coefficient> class MonomialOf {
public:
  using ull = unsigned long long;

  MonomialOf() = default; // 零单项式

  // 系数按值传参即可（Fraction 与实代数数都是廉价可拷贝的），std::move 不会带来收益
  MonomialOf(Coefficient _coeff) : coeff(_coeff) { normalize(); }

  MonomialOf(Coefficient _coeff, VarPowers _factors) : coeff(_coeff), factors(std::move(_factors)) { normalize(); }

  const Coefficient &getCoefficient() const { return coeff; }
  const VarPowers &getFactors() const { return factors; }

  bool isZero() const { return detail::isZeroCoefficient(coeff); }
  bool isConstant() const { return factors.empty(); }
  ull degree() const { return detail::degreeOf(factors); }

  // 乘法结果仍是单项式，但同底数幂合并可能溢出，故返回 Result
  Result<MonomialOf> operator*(const MonomialOf &rhs) const {
    if (isZero() || rhs.isZero()) {
      return MonomialOf();
    }
    VarPowers merged = factors;
    merged.insert(merged.end(), rhs.factors.begin(), rhs.factors.end());
    try {
      return MonomialOf(coeff * rhs.coeff, std::move(merged));
    } catch (const MathsException &error) {
      return std::unexpected(error.code());
    }
  }

  MonomialOf &operator*=(const MonomialOf &rhs) {
    *this = (*this * rhs).unwrap();
    return *this;
  }

  MonomialOf operator-() const { return MonomialOf(-coeff, factors); }

  // 把 Scope 中已绑定的变量替换为其值，未绑定的变量原样保留。
  // 绑定值本身可以是分式（如 s = v*t、x = a/b），所以结果用 RationalFunctionOf 承载（定义见 scope.cppm）
  RationalFunctionOf<Coefficient> substitute(const ScopeOf<Coefficient> &scope) const;

  // 完全求值：所有变量都必须已绑定且结果化为常数，否则返回 MathsError::UndefinedVariable
  Result<Coefficient> evaluate(const ScopeOf<Coefficient> &scope) const;

  // 加减的结果不保证仍是单项式，因此返回 PolynomialOf（定义见 PolynomialOf 之后）
  PolynomialOf<Coefficient> operator+(const MonomialOf &rhs) const;
  PolynomialOf<Coefficient> operator-(const MonomialOf &rhs) const;

  // 先比变量部分（字典序）再比系数；变量部分的顺序由 normalizeFactors 保证
  std::strong_ordering operator<=>(const MonomialOf &rhs) const {
    if (auto cmp = factors <=> rhs.factors; cmp != 0) {
      return cmp;
    }
    return coeff <=> rhs.coeff;
  }

  bool operator==(const MonomialOf &rhs) const { return factors == rhs.factors && coeff == rhs.coeff; }

  std::string str() const {
    if (isZero()) {
      return "0";
    }
    if (factors.empty()) {
      return detail::coefficientText(coeff);
    }
    std::string result;
    if (coeff == detail::coefficientMinusOne<Coefficient>()) {
      result += '-';
    } else if (!(coeff == detail::coefficientOne<Coefficient>())) {
      result += detail::coefficientText(coeff);
      result += ' ';
    }
    for (std::size_t i = 0; i < factors.size(); ++i) {
      if (i != 0) {
        result += ' ';
      }
      result += factors[i].first.str();
      if (factors[i].second != 1) {
        result += '^';
        result += std::to_string(factors[i].second);
      }
    }
    return result;
  }

  // LaTeX 形式：分数系数写成 \frac{}{}，变量之间直接相连（LaTeX 隐含乘法），
  // 指数统一加花括号
  std::string latex() const {
    if (isZero()) {
      return "0";
    }
    if (factors.empty()) {
      return detail::coefficientLatex(coeff);
    }

    std::string result;
    if (coeff == detail::coefficientMinusOne<Coefficient>()) {
      result += '-';
    } else if (!(coeff == detail::coefficientOne<Coefficient>())) {
      result += detail::coefficientLatex(coeff);
    }
    for (const auto &factor : factors) {
      result += factor.first.str();
      if (factor.second != 1) {
        result += '^';
        const std::string exponent = std::to_string(factor.second);
        result += exponent.size() == 1 ? exponent : "{" + exponent + "}"; // 单字符指数不用花括号
      }
    }
    return result;
  }

  // 规范化：去掉零次幂 → 按变量升序排序 → 合并同底数幂
  static void normalizeFactors(VarPowers &factors) {
    factors.erase(std::remove_if(factors.begin(), factors.end(),
                                 [](const std::pair<Variable, ull> &factor) { return factor.second == 0; }),
                  factors.end());
    std::sort(
        factors.begin(), factors.end(),
        [](const std::pair<Variable, ull> &lhs, const std::pair<Variable, ull> &rhs) { return lhs.first < rhs.first; });

    VarPowers merged;
    merged.reserve(factors.size());
    for (const auto &factor : factors) {
      if (!merged.empty() && merged.back().first == factor.first) {
        // 同底数幂相加可能溢出，宁可报错也不要静默回绕成错误次数
        if (merged.back().second > std::numeric_limits<ull>::max() - factor.second) {
          throw MathsException(MathsError::ExponentOverflow);
        }
        merged.back().second += factor.second;
      } else {
        merged.push_back(factor);
      }
    }
    factors = std::move(merged);
  }

private:
  void normalize() {
    if (isZero()) {
      factors.clear(); // 零单项式统一表示为 0，不携带变量
      return;
    }
    normalizeFactors(factors);
  }

  Coefficient coeff;
  VarPowers factors;
};

template <class Coefficient> class PolynomialOf {
public:
  using ull = unsigned long long;

  PolynomialOf() = default; // 零多项式

  // 由单项式隐式提升，使 MonomialOf 能直接参与多项式的加减乘
  PolynomialOf(const MonomialOf<Coefficient> &_mono) { addTerm(_mono.getFactors(), _mono.getCoefficient()); }

  const std::map<VarPowers, Coefficient> &getTerms() const { return terms; }

  // 是否含某个变量（任一项的变量因子里出现即算）
  bool containsVariable(const Variable &variable) const {
    for (const auto &entry : terms) {
      for (const auto &factor : entry.first) {
        if (factor.first == variable) {
          return true;
        }
      }
    }
    return false;
  }

  bool isZero() const { return terms.empty(); }

  // 多项式中出现过的全部变量（与 RationalFunction::variables 同名同义）
  std::set<Variable> variables() const {
    std::set<Variable> result;
    for (const auto &entry : terms) {
      for (const auto &factor : entry.first) {
        result.insert(factor.first);
      }
    }
    return result;
  }

  // 化简后只剩不超过一项即为单项式（零多项式视为零单项式）
  bool isMonomial() const { return terms.size() <= 1; }

  // 对每一项调用 MonomialOf::substitute，再统一合并同类项（定义见 scope.cppm）。
  // 结果可能不再是多项式，故返回 RationalFunctionOf
  RationalFunctionOf<Coefficient> substitute(const ScopeOf<Coefficient> &scope) const;

  // 完全求值：所有变量都必须已绑定且结果化为常数，否则返回 MathsError::UndefinedVariable
  Result<Coefficient> evaluate(const ScopeOf<Coefficient> &scope) const;

  // 成功化简为单项式，否则返回 MathsError::NotAMonomial
  Result<MonomialOf<Coefficient>> toMonomial() const {
    if (terms.size() > 1) {
      return std::unexpected(MathsError::NotAMonomial);
    }
    if (terms.empty()) {
      return MonomialOf<Coefficient>();
    }
    const auto &[factors, coeff] = *terms.begin();
    return MonomialOf<Coefficient>(coeff, factors);
  }

  ull degree() const {
    ull highest = 0;
    for (const auto &[factors, coeff] : terms) {
      highest = std::max(highest, detail::degreeOf(factors));
    }
    return highest;
  }

  PolynomialOf operator+(const PolynomialOf &rhs) const {
    PolynomialOf result(*this);
    for (const auto &[factors, coeff] : rhs.terms) {
      result.addTerm(factors, coeff);
    }
    return result;
  }

  PolynomialOf operator-(const PolynomialOf &rhs) const {
    PolynomialOf result(*this);
    for (const auto &[factors, coeff] : rhs.terms) {
      result.addTerm(factors, -coeff);
    }
    return result;
  }

  // 展开时可能因指数合并溢出而失败
  Result<PolynomialOf> operator*(const PolynomialOf &rhs) const {
    PolynomialOf result;
    try {
      for (const auto &[lhsFactors, lhsCoeff] : terms) {
        for (const auto &[rhsFactors, rhsCoeff] : rhs.terms) {
          VarPowers merged = lhsFactors;
          merged.insert(merged.end(), rhsFactors.begin(), rhsFactors.end());
          MonomialOf<Coefficient>::normalizeFactors(merged);
          result.addTerm(merged, lhsCoeff * rhsCoeff);
        }
      }
    } catch (const MathsException &error) {
      return std::unexpected(error.code());
    }
    return result;
  }

  PolynomialOf operator-() const {
    PolynomialOf result;
    for (const auto &[factors, coeff] : terms) {
      result.addTerm(factors, -coeff);
    }
    return result;
  }

  PolynomialOf &operator+=(const PolynomialOf &rhs) {
    *this = *this + rhs;
    return *this;
  }

  PolynomialOf &operator-=(const PolynomialOf &rhs) {
    *this = *this - rhs;
    return *this;
  }

  PolynomialOf &operator*=(const PolynomialOf &rhs) {
    *this = (*this * rhs).unwrap();
    return *this;
  }

  bool operator==(const PolynomialOf &rhs) const = default;

  // 展示顺序见 detail::displayOrderLess
  std::string str() const {
    if (terms.empty()) {
      return "0";
    }
    std::vector<std::pair<VarPowers, Coefficient>> ordered(terms.begin(), terms.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &lhs, const auto &rhs) { return detail::displayOrderLess(lhs.first, rhs.first); });

    std::string result;
    bool first = true;
    for (const auto &[factors, coeff] : ordered) {
      const bool negative = detail::isNegativeCoefficient(coeff);
      if (first) {
        if (negative) {
          result += '-';
        }
        first = false;
      } else {
        result += negative ? " - " : " + ";
      }
      result += MonomialOf<Coefficient>(negative ? -coeff : coeff, factors).str();
    }
    return result;
  }

  // LaTeX 形式：展示顺序与 str() 相同，但分数系数写成 \frac{}{}
  std::string latex() const {
    if (terms.empty()) {
      return "0";
    }
    std::vector<std::pair<VarPowers, Coefficient>> ordered(terms.begin(), terms.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &lhs, const auto &rhs) { return detail::displayOrderLess(lhs.first, rhs.first); });

    std::string result;
    bool first = true;
    for (const auto &[factors, coeff] : ordered) {
      const bool negative = detail::isNegativeCoefficient(coeff);
      if (first) {
        if (negative) {
          result += '-';
        }
        first = false;
      } else {
        result += negative ? " - " : " + ";
      }
      result += MonomialOf<Coefficient>(negative ? -coeff : coeff, factors).latex();
    }
    return result;
  }

  // 加入一项：已存在则合并同类项，系数归零则整项删除。
  // 公开是为了让 RationalFunctionOf 这类需要逐项构建多项式的代码能复用同一套规范化逻辑。
  void addTerm(const VarPowers &key, const Coefficient &value) {
    if (detail::isZeroCoefficient(value)) {
      return;
    }
    auto [iter, inserted] = terms.try_emplace(key, value);
    if (!inserted) {
      iter->second = iter->second + value; // 不依赖 operator+=，换系数类型也能用
      if (detail::isZeroCoefficient(iter->second)) {
        terms.erase(iter);
      }
    }
  }

private:
  std::map<VarPowers, Coefficient> terms;
};

template <class Coefficient>
inline PolynomialOf<Coefficient> MonomialOf<Coefficient>::operator+(const MonomialOf<Coefficient> &rhs) const {
  return PolynomialOf<Coefficient>(*this) + PolynomialOf<Coefficient>(rhs);
}

template <class Coefficient>
inline PolynomialOf<Coefficient> MonomialOf<Coefficient>::operator-(const MonomialOf<Coefficient> &rhs) const {
  return PolynomialOf<Coefficient>(*this) - PolynomialOf<Coefficient>(rhs);
}

// 左操作数为 MonomialOf 时成员运算符不适用，补自由函数保证对称性
template <class Coefficient>
inline PolynomialOf<Coefficient> operator+(const MonomialOf<Coefficient> &lhs, const PolynomialOf<Coefficient> &rhs) {
  return PolynomialOf<Coefficient>(lhs) + rhs;
}

template <class Coefficient>
inline PolynomialOf<Coefficient> operator-(const MonomialOf<Coefficient> &lhs, const PolynomialOf<Coefficient> &rhs) {
  return PolynomialOf<Coefficient>(lhs) - rhs;
}

template <class Coefficient>
inline Result<PolynomialOf<Coefficient>> operator*(const MonomialOf<Coefficient> &lhs,
                                                   const PolynomialOf<Coefficient> &rhs) {
  return PolynomialOf<Coefficient>(lhs) * rhs;
}

template <class Coefficient> inline std::ostream &operator<<(std::ostream &os, const MonomialOf<Coefficient> &value) {
  return os << value.str();
}

template <class Coefficient> inline std::ostream &operator<<(std::ostream &os, const PolynomialOf<Coefficient> &value) {
  return os << value.str();
}

// ==================== 多项式带余除法 ====================

// 按字典序取首项；零多项式返回 nullopt
template <class Coefficient>
inline std::optional<MonomialOf<Coefficient>> leadingMonomial(const PolynomialOf<Coefficient> &polynomial) {
  const auto &terms = polynomial.getTerms();
  if (terms.empty()) {
    return std::nullopt;
  }
  auto best = terms.begin();
  for (auto it = std::next(terms.begin()); it != terms.end(); ++it) {
    if (detail::compareLex(it->first, best->first) == std::strong_ordering::greater) {
      best = it;
    }
  }
  return MonomialOf<Coefficient>(best->second, best->first);
}

template <class Coefficient> struct PolynomialDivisionOf {
  PolynomialOf<Coefficient> quotient;
  PolynomialOf<Coefficient> remainder;
};

// 带余除法：dividend = quotient * divisor + remainder。
// 多元情况下首项可能无法整除（变量指数不足），此时提前终止；余式非零即表示不能整除。
template <class Coefficient>
inline Result<PolynomialDivisionOf<Coefficient>> divideWithRemainder(const PolynomialOf<Coefficient> &dividend,
                                                                     const PolynomialOf<Coefficient> &divisor) {
  if (divisor.isZero()) {
    return std::unexpected(MathsError::DivisionByZero);
  }

  PolynomialOf<Coefficient> quotient;
  PolynomialOf<Coefficient> remainder = dividend;

  while (true) {
    const std::optional<MonomialOf<Coefficient>> remainderLead = leadingMonomial(remainder);
    if (!remainderLead) {
      break; // 余式已为零，整除结束
    }
    const std::optional<MonomialOf<Coefficient>> divisorLead = leadingMonomial(divisor);
    if (!divisorLead) {
      break; // divisor 非零由入口检查保证；显式判空是为了让静态分析也能看到
    }

    // 判断两首项能否整除：被除式必须含有除式首项的每一个变量，且指数足够
    VarPowers reduced = remainderLead->getFactors();
    bool divisible = true;
    for (const auto &factor : divisorLead->getFactors()) {
      bool found = false;
      for (auto &candidate : reduced) {
        if (candidate.first == factor.first) {
          if (candidate.second < factor.second) {
            divisible = false;
          } else {
            candidate.second -= factor.second;
            found = true;
          }
          break;
        }
      }
      if (!found) {
        divisible = false;
      }
      if (!divisible) {
        break;
      }
    }
    if (!divisible) {
      break; // 首项不可整除，当前余式即最终余式
    }
    MonomialOf<Coefficient>::normalizeFactors(reduced);

    // 首项系数必非零，除法不会失败
    const Result<Coefficient> quotientCoefficient = remainderLead->getCoefficient() / divisorLead->getCoefficient();
    if (quotientCoefficient.isErr()) {
      break;
    }
    const MonomialOf<Coefficient> term(quotientCoefficient.unwrap(), std::move(reduced));

    quotient.addTerm(term.getFactors(), term.getCoefficient());
    const Result<PolynomialOf<Coefficient>> product = term * divisor;
    if (product.isErr()) {
      break;
    }
    remainder = remainder - product.unwrap();
  }

  return PolynomialDivisionOf<Coefficient>{std::move(quotient), std::move(remainder)};
}

// ==================== 既有名字（系数为有理数） ====================

using Monomial = MonomialOf<Fraction>;
using Polynomial = PolynomialOf<Fraction>;
using PolynomialDivision = PolynomialDivisionOf<Fraction>;

// 自变量本身作为一个多项式（`x`）。构造「f(x) = x」这类元素到处都要用，
// 与其让每个调用方自己拼 `Monomial(Fraction(1,1), {{v, 1}})`，不如放在这里。
inline Polynomial variablePolynomial(const Variable &variable) {
  return Polynomial(Monomial(Fraction(1, 1), VarPowers{{variable, 1ULL}}));
}

// 把**单变量**多项式转成 ℚ[x] 上的一元多项式，好复用实根隔离那一套机器
// （Sturm 计数、平方自由化、符号判断）。含多于一个变量时返回 nullopt。
//
// 之所以放在这里：它是多项式自己的能力，而实根计算在 numeric 层，
// numeric 层不能反向依赖代数层，所以转换放在代数这边。
inline std::optional<UnivariatePolynomial> toUnivariatePolynomial(const Polynomial &polynomial,
                                                                  const Variable &variable) {
  std::vector<Fraction> coefficients(polynomial.degree() + 1, Fraction(0, 1));
  for (const auto &[factors, coefficient] : polynomial.getTerms()) {
    unsigned long long exponent = 0;
    for (const auto &factor : factors) {
      if (factor.first != variable) {
        return std::nullopt; // 多变量：实根隔离那套机器处理不了
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

} // namespace maths
