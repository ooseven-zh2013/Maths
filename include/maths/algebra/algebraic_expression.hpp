#pragma once
#ifndef ALGEBRAIC_EXPRESSION_HPP
#define ALGEBRAIC_EXPRESSION_HPP
#include <algorithm>
#include <cctype>
#include <compare>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <maths/core/result.hpp>
#include <maths/numeric/numbers.hpp>

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
    for (size_t i = 0, size_ = std::min(index.size(), oth.index.size()); i < size_; ++i) {
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
        size_t last = 0;
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

namespace maths_detail {

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
  for (size_t i = 0; i < std::min(lhs.size(), rhs.size()); ++i) {
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
  size_t i = 0;
  size_t j = 0;
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

} // namespace maths_detail

class Polynomial;
class RationalFunction;
class Scope;

class Monomial {
public:
  using ull = unsigned long long;

  Monomial() = default; // 零单项式

  // Fraction 是 trivially copyable，按值传参即可，std::move 不会带来收益
  Monomial(Fraction _coeff) : coeff(_coeff) { normalize(); }

  Monomial(Fraction _coeff, VarPowers _factors) : coeff(_coeff), factors(std::move(_factors)) { normalize(); }

  const Fraction &getCoefficient() const { return coeff; }
  const VarPowers &getFactors() const { return factors; }

  bool isZero() const { return coeff == 0LL; }
  bool isConstant() const { return factors.empty(); }
  ull degree() const { return maths_detail::degreeOf(factors); }

  // 乘法结果仍是单项式，但同底数幂合并可能溢出，故返回 Result
  Result<Monomial> operator*(const Monomial &rhs) const {
    if (isZero() || rhs.isZero()) {
      return Monomial();
    }
    VarPowers merged = factors;
    merged.insert(merged.end(), rhs.factors.begin(), rhs.factors.end());
    try {
      return Monomial(coeff * rhs.coeff, std::move(merged));
    } catch (const MathsException &error) {
      return std::unexpected(error.code());
    }
  }

  Monomial &operator*=(const Monomial &rhs) {
    *this = (*this * rhs).unwrap();
    return *this;
  }

  Monomial operator-() const { return Monomial(-coeff, factors); }

  // 把 Scope 中已绑定的变量替换为其值，未绑定的变量原样保留。
  // 绑定值本身可以是分式（如 s = v*t、x = a/b），所以结果用 RationalFunction 承载（定义见 scope.hpp）
  RationalFunction substitute(const Scope &scope) const;

  // 完全求值：所有变量都必须已绑定且结果化为常数，否则返回 MathsError::UndefinedVariable
  Result<Fraction> evaluate(const Scope &scope) const;

  // 加减的结果不保证仍是单项式，因此返回 Polynomial（定义见 Polynomial 之后）
  Polynomial operator+(const Monomial &rhs) const;
  Polynomial operator-(const Monomial &rhs) const;

  // 先比变量部分（字典序）再比系数；变量部分的顺序由 normalizeFactors 保证
  std::strong_ordering operator<=>(const Monomial &rhs) const {
    if (auto cmp = factors <=> rhs.factors; cmp != 0) {
      return cmp;
    }
    return coeff <=> rhs.coeff;
  }

  bool operator==(const Monomial &rhs) const { return factors == rhs.factors && coeff == rhs.coeff; }

  std::string str() const {
    if (isZero()) {
      return "0";
    }
    if (factors.empty()) {
      return maths_detail::renderFraction(coeff);
    }
    std::string result;
    if (coeff == -1LL) {
      result += '-';
    } else if (!(coeff == 1LL)) {
      result += maths_detail::renderFraction(coeff);
      result += ' ';
    }
    for (size_t i = 0; i < factors.size(); ++i) {
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
      return maths_detail::renderFractionLatex(coeff);
    }

    std::string result;
    if (coeff == -1LL) {
      result += '-';
    } else if (!(coeff == 1LL)) {
      result += maths_detail::renderFractionLatex(coeff);
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

  Fraction coeff;
  VarPowers factors;
};

class Polynomial {
public:
  using ull = unsigned long long;

  Polynomial() = default; // 零多项式

  // 由单项式隐式提升，使 Monomial 能直接参与多项式的加减乘
  Polynomial(const Monomial &_mono) { addTerm(_mono.getFactors(), _mono.getCoefficient()); }

  const std::map<VarPowers, Fraction> &getTerms() const { return terms; }

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

  // 化简后只剩不超过一项即为单项式（零多项式视为零单项式）
  bool isMonomial() const { return terms.size() <= 1; }

  // 对每一项调用 Monomial::substitute，再统一合并同类项（定义见 scope.hpp）。
  // 结果可能不再是多项式，故返回 RationalFunction
  RationalFunction substitute(const Scope &scope) const;

  // 完全求值：所有变量都必须已绑定且结果化为常数，否则返回 MathsError::UndefinedVariable
  Result<Fraction> evaluate(const Scope &scope) const;

  // 成功化简为单项式，否则返回 MathsError::NotAMonomial
  Result<Monomial> toMonomial() const {
    if (terms.size() > 1) {
      return std::unexpected(MathsError::NotAMonomial);
    }
    if (terms.empty()) {
      return Monomial();
    }
    const auto &[factors, coeff] = *terms.begin();
    return Monomial(coeff, factors);
  }

  ull degree() const {
    ull highest = 0;
    for (const auto &[factors, coeff] : terms) {
      highest = std::max(highest, maths_detail::degreeOf(factors));
    }
    return highest;
  }

  Polynomial operator+(const Polynomial &rhs) const {
    Polynomial result(*this);
    for (const auto &[factors, coeff] : rhs.terms) {
      result.addTerm(factors, coeff);
    }
    return result;
  }

  Polynomial operator-(const Polynomial &rhs) const {
    Polynomial result(*this);
    for (const auto &[factors, coeff] : rhs.terms) {
      result.addTerm(factors, -coeff);
    }
    return result;
  }

  // 展开时可能因指数合并溢出而失败
  Result<Polynomial> operator*(const Polynomial &rhs) const {
    Polynomial result;
    try {
      for (const auto &[lhsFactors, lhsCoeff] : terms) {
        for (const auto &[rhsFactors, rhsCoeff] : rhs.terms) {
          VarPowers merged = lhsFactors;
          merged.insert(merged.end(), rhsFactors.begin(), rhsFactors.end());
          Monomial::normalizeFactors(merged);
          result.addTerm(merged, lhsCoeff * rhsCoeff);
        }
      }
    } catch (const MathsException &error) {
      return std::unexpected(error.code());
    }
    return result;
  }

  Polynomial operator-() const {
    Polynomial result;
    for (const auto &[factors, coeff] : terms) {
      result.addTerm(factors, -coeff);
    }
    return result;
  }

  Polynomial &operator+=(const Polynomial &rhs) {
    *this = *this + rhs;
    return *this;
  }

  Polynomial &operator-=(const Polynomial &rhs) {
    *this = *this - rhs;
    return *this;
  }

  Polynomial &operator*=(const Polynomial &rhs) {
    *this = (*this * rhs).unwrap();
    return *this;
  }

  bool operator==(const Polynomial &rhs) const = default;

  // 展示顺序见 maths_detail::displayOrderLess
  std::string str() const {
    if (terms.empty()) {
      return "0";
    }
    std::vector<std::pair<VarPowers, Fraction>> ordered(terms.begin(), terms.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &lhs, const auto &rhs) { return maths_detail::displayOrderLess(lhs.first, rhs.first); });

    std::string result;
    bool first = true;
    for (const auto &[factors, coeff] : ordered) {
      const bool negative = coeff.isNegative();
      if (first) {
        if (negative) {
          result += '-';
        }
        first = false;
      } else {
        result += negative ? " - " : " + ";
      }
      result += Monomial(negative ? -coeff : coeff, factors).str();
    }
    return result;
  }

  // LaTeX 形式：展示顺序与 str() 相同，但分数系数写成 \frac{}{}
  std::string latex() const {
    if (terms.empty()) {
      return "0";
    }
    std::vector<std::pair<VarPowers, Fraction>> ordered(terms.begin(), terms.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &lhs, const auto &rhs) { return maths_detail::displayOrderLess(lhs.first, rhs.first); });

    std::string result;
    bool first = true;
    for (const auto &[factors, coeff] : ordered) {
      const bool negative = coeff.isNegative();
      if (first) {
        if (negative) {
          result += '-';
        }
        first = false;
      } else {
        result += negative ? " - " : " + ";
      }
      result += Monomial(negative ? -coeff : coeff, factors).latex();
    }
    return result;
  }

  // 加入一项：已存在则合并同类项，系数归零则整项删除。
  // 公开是为了让 RationalFunction 这类需要逐项构建多项式的代码能复用同一套规范化逻辑。
  void addTerm(const VarPowers &key, const Fraction &value) {
    if (value == 0LL) {
      return;
    }
    auto [iter, inserted] = terms.try_emplace(key, value);
    if (!inserted) {
      iter->second += value;
      if (iter->second == 0LL) {
        terms.erase(iter);
      }
    }
  }

private:
  std::map<VarPowers, Fraction> terms;
};

inline Polynomial Monomial::operator+(const Monomial &rhs) const { return Polynomial(*this) + Polynomial(rhs); }

inline Polynomial Monomial::operator-(const Monomial &rhs) const { return Polynomial(*this) - Polynomial(rhs); }

// 左操作数为 Monomial 时成员运算符不适用，补自由函数保证对称性
inline Polynomial operator+(const Monomial &lhs, const Polynomial &rhs) { return Polynomial(lhs) + rhs; }

inline Polynomial operator-(const Monomial &lhs, const Polynomial &rhs) { return Polynomial(lhs) - rhs; }

inline Result<Polynomial> operator*(const Monomial &lhs, const Polynomial &rhs) { return Polynomial(lhs) * rhs; }

inline std::ostream &operator<<(std::ostream &os, const Monomial &value) { return os << value.str(); }

inline std::ostream &operator<<(std::ostream &os, const Polynomial &value) { return os << value.str(); }

// ==================== 多项式带余除法 ====================

// 按字典序取首项；零多项式返回 nullopt
inline std::optional<Monomial> leadingMonomial(const Polynomial &polynomial) {
  const auto &terms = polynomial.getTerms();
  if (terms.empty()) {
    return std::nullopt;
  }
  auto best = terms.begin();
  for (auto it = std::next(terms.begin()); it != terms.end(); ++it) {
    if (maths_detail::compareLex(it->first, best->first) == std::strong_ordering::greater) {
      best = it;
    }
  }
  return Monomial(best->second, best->first);
}

struct PolynomialDivision {
  Polynomial quotient;
  Polynomial remainder;
};

// 带余除法：dividend = quotient * divisor + remainder。
// 多元情况下首项可能无法整除（变量指数不足），此时提前终止；余式非零即表示不能整除。
inline Result<PolynomialDivision> divideWithRemainder(const Polynomial &dividend, const Polynomial &divisor) {
  if (divisor.isZero()) {
    return std::unexpected(MathsError::DivisionByZero);
  }

  Polynomial quotient;
  Polynomial remainder = dividend;

  while (true) {
    const std::optional<Monomial> remainderLead = leadingMonomial(remainder);
    if (!remainderLead) {
      break; // 余式已为零，整除结束
    }
    const std::optional<Monomial> divisorLead = leadingMonomial(divisor);
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
    Monomial::normalizeFactors(reduced);

    // 首项系数必非零，除法不会失败
    const Fraction coefficient = (remainderLead->getCoefficient() / divisorLead->getCoefficient()).unwrap();
    const Monomial term(coefficient, std::move(reduced));

    quotient.addTerm(term.getFactors(), term.getCoefficient());
    remainder = remainder - (term * divisor).unwrap();
  }

  return PolynomialDivision{std::move(quotient), std::move(remainder)};
}

#endif // ALGEBRAIC_EXPRESSION_HPP
