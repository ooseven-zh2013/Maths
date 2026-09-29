export module maths.algebra:groebner;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import :expression;

export namespace maths {

// 多元多项式理想上的 Gröbner 基（Buchberger 算法）。
//
// 用途：等式约束组的**一致性判定**（1 是否属于理想）、**消元/参数化**（把 `x − y − z = 0`
// 解成 `x = y + z`）、**投影**（消去若干变量后剩下的条件）。
// 系数全程精确有理运算，不做浮点。
//
// 为什么需要它：多变量点集的**约束式表示**（见 `:constraint_system`）可以判定「某个点是否
// 满足约束」，但要「解出满足约束的点」或「消掉某些变量」，就需要理想上的消元 —— 这正是
// Gröbner 基做的事。柱形代数分解（一般半代数集的显式表示）代价双指数，本库不做；
// 等式组这一层用 Gröbner 就够，而且代价是可以承受的。

// 单项式序
enum class MonomialOrder {
  Lex,   // 字典序：变量名小的先比（名字小的更「重要」）
  GrLex, // 分次字典序：先比总次数，再按字典序
};

namespace groebner_detail {

// 单项式在给定序下的大小
inline std::strong_ordering compareMonomials(const VarPowers &lhs, const VarPowers &rhs, MonomialOrder order) {
  if (order == MonomialOrder::GrLex) {
    const unsigned long long lhsDegree = detail::degreeOf(lhs);
    const unsigned long long rhsDegree = detail::degreeOf(rhs);
    if (lhsDegree != rhsDegree) {
      return lhsDegree > rhsDegree ? std::strong_ordering::greater : std::strong_ordering::less;
    }
  }
  return detail::compareLex(lhs, rhs);
}

// 同变量取指数较大者 —— 单项式的最小公倍
inline VarPowers monomialLcm(const VarPowers &lhs, const VarPowers &rhs) {
  VarPowers result;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < lhs.size() || j < rhs.size()) {
    if (j >= rhs.size() || (i < lhs.size() && lhs[i].first < rhs[j].first)) {
      result.push_back(lhs[i]);
      ++i;
    } else if (i >= lhs.size() || rhs[j].first < lhs[i].first) {
      result.push_back(rhs[j]);
      ++j;
    } else {
      result.emplace_back(lhs[i].first, std::max(lhs[i].second, rhs[j].second));
      ++i;
      ++j;
    }
  }
  return result;
}

// 单项式相除：要求 rhs 的每个指数都不超过 lhs，否则返回 nullopt
inline std::optional<VarPowers> monomialQuotient(const VarPowers &lhs, const VarPowers &rhs) {
  VarPowers result = lhs;
  for (const auto &factor : rhs) {
    bool found = false;
    for (auto &candidate : result) {
      if (candidate.first == factor.first) {
        if (candidate.second < factor.second) {
          return std::nullopt;
        }
        candidate.second -= factor.second;
        found = true;
        break;
      }
    }
    if (!found) {
      return std::nullopt;
    }
  }
  MonomialOf<Fraction>::normalizeFactors(result);
  return result;
}

// 单项式是否互素（没有公共变量）—— Buchberger 第一判别法：互素时 S-多项式必约化为 0
inline bool monomialsAreCoprime(const VarPowers &lhs, const VarPowers &rhs) {
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < lhs.size() && j < rhs.size()) {
    if (lhs[i].first == rhs[j].first) {
      return false;
    }
    if (lhs[i].first < rhs[j].first) {
      ++i;
    } else {
      ++j;
    }
  }
  return true;
}

// 整体乘一个有理数（不用 operator* 是为了避开 Result）
inline Polynomial scaled(const Polynomial &polynomial, const Fraction &factor) {
  Polynomial result;
  for (const auto &entry : polynomial.getTerms()) {
    result.addTerm(entry.first, entry.second * factor);
  }
  return result;
}

} // namespace groebner_detail

// 首项：按给定序取最大项；零多项式返回 nullopt
inline std::optional<std::pair<VarPowers, Fraction>> leadingTerm(const Polynomial &polynomial, MonomialOrder order) {
  const auto &terms = polynomial.getTerms();
  if (terms.empty()) {
    return std::nullopt;
  }
  auto best = terms.begin();
  for (auto it = std::next(terms.begin()); it != terms.end(); ++it) {
    if (groebner_detail::compareMonomials(it->first, best->first, order) == std::strong_ordering::greater) {
      best = it;
    }
  }
  return *best;
}

// 首项系数归一为 1（保持根不变，且让后续除法更省事）
inline Polynomial monic(const Polynomial &polynomial, MonomialOrder order) {
  const std::optional<std::pair<VarPowers, Fraction>> leading = leadingTerm(polynomial, order);
  if (!leading || leading->second == 0LL) {
    return polynomial;
  }
  const Result<Fraction> inverse = Fraction(1, 1) / leading->second;
  if (inverse.isErr()) {
    return polynomial;
  }
  return groebner_detail::scaled(polynomial, inverse.unwrap());
}

// 多元带余除法（对一组除式）：dividend = Σ qᵢ·divisorᵢ + remainder
struct MultivariateDivision {
  std::vector<Polynomial> quotients;
  Polynomial remainder;
};

inline Result<MultivariateDivision> multivariateDivide(const Polynomial &dividend,
                                                       const std::vector<Polynomial> &divisors, MonomialOrder order) {
  MultivariateDivision result;
  result.quotients.assign(divisors.size(), Polynomial());
  Polynomial working = dividend;

  // 每轮取当前首项，能被某个除式的首项整除就减掉，否则收进余式
  while (!working.isZero()) {
    const std::optional<std::pair<VarPowers, Fraction>> term = leadingTerm(working, order);
    if (!term) {
      break;
    }
    bool divided = false;
    for (std::size_t index = 0; index < divisors.size(); ++index) {
      const std::optional<std::pair<VarPowers, Fraction>> divisorTerm = leadingTerm(divisors[index], order);
      if (!divisorTerm || divisorTerm->second == 0LL) {
        continue;
      }
      const std::optional<VarPowers> quotientMonomial =
          groebner_detail::monomialQuotient(term->first, divisorTerm->first);
      if (!quotientMonomial) {
        continue;
      }
      const Result<Fraction> quotientCoefficient = term->second / divisorTerm->second;
      if (quotientCoefficient.isErr()) {
        return std::unexpected(quotientCoefficient.unwrapErr());
      }
      const MonomialOf<Fraction> quotientTerm(quotientCoefficient.unwrap(), *quotientMonomial);
      const Polynomial quotientPolynomial(quotientTerm);
      const Result<Polynomial> product = quotientPolynomial * divisors[index];
      if (product.isErr()) {
        return std::unexpected(product.unwrapErr());
      }
      result.quotients[index] = result.quotients[index] + quotientPolynomial;
      working = working - product.unwrap();
      divided = true;
      break;
    }
    if (!divided) {
      // 首项无法被任何除式整除：这一项进入余式
      Polynomial single;
      single.addTerm(term->first, term->second);
      result.remainder = result.remainder + single;
      working = working - single;
    }
  }
  return result;
}

// 正规形：对一组多项式反复约化后的余式
inline Result<Polynomial> normalForm(const Polynomial &polynomial, const std::vector<Polynomial> &divisors,
                                     MonomialOrder order) {
  const Result<MultivariateDivision> division = multivariateDivide(polynomial, divisors, order);
  if (division.isErr()) {
    return std::unexpected(division.unwrapErr());
  }
  return division.unwrap().remainder;
}

// S-多项式：S(f,g) = (x^γ/LT(f))·f − (x^γ/LT(g))·g，其中 γ 是两首项单项式的最小公倍。
// 它把两个首项消掉，是 Buchberger 算法的核心工具。
inline Result<Polynomial> sPolynomial(const Polynomial &lhs, const Polynomial &rhs, MonomialOrder order) {
  const std::optional<std::pair<VarPowers, Fraction>> lhsTerm = leadingTerm(lhs, order);
  const std::optional<std::pair<VarPowers, Fraction>> rhsTerm = leadingTerm(rhs, order);
  if (!lhsTerm || !rhsTerm) {
    return Polynomial();
  }
  const VarPowers common = groebner_detail::monomialLcm(lhsTerm->first, rhsTerm->first);

  const std::optional<VarPowers> lhsQuotient = groebner_detail::monomialQuotient(common, lhsTerm->first);
  const std::optional<VarPowers> rhsQuotient = groebner_detail::monomialQuotient(common, rhsTerm->first);
  if (!lhsQuotient || !rhsQuotient) {
    return Polynomial();
  }

  const Result<Fraction> lhsInverse = Fraction(1, 1) / lhsTerm->second;
  const Result<Fraction> rhsInverse = Fraction(1, 1) / rhsTerm->second;
  if (lhsInverse.isErr() || rhsInverse.isErr()) {
    return Polynomial();
  }
  // 单项式先包成多项式（系数 1），再整体乘上 1/首项系数
  const Polynomial lhsMonomial(Monomial(Fraction(1, 1), *lhsQuotient));
  const Polynomial rhsMonomial(Monomial(Fraction(1, 1), *rhsQuotient));
  const Polynomial lhsPart = groebner_detail::scaled(lhsMonomial, lhsInverse.unwrap());
  const Polynomial rhsPart = groebner_detail::scaled(rhsMonomial, rhsInverse.unwrap());
  const Result<Polynomial> lhsProduct = lhsPart * lhs; // 多项式乘法可能因指数溢出失败
  const Result<Polynomial> rhsProduct = rhsPart * rhs;
  if (lhsProduct.isErr()) {
    return std::unexpected(lhsProduct.unwrapErr());
  }
  if (rhsProduct.isErr()) {
    return std::unexpected(rhsProduct.unwrapErr());
  }
  return lhsProduct.unwrap() - rhsProduct.unwrap();
}

// Buchberger：算出给定生成元的 Gröbner 基
//
// 朴素版 + 第一判别法（首项互素的对跳过，它们的 S-多项式必约化为 0）。
// 上限是为了兜住「系数爆炸导致算不完」的极端输入 —— 那不是常规错误路径。
inline Result<std::vector<Polynomial>> groebnerBasis(const std::vector<Polynomial> &generators, MonomialOrder order) {
  constexpr std::size_t kMaxBasisSize = 64;
  constexpr std::size_t kMaxPairs = 512;

  std::vector<Polynomial> basis;
  for (const Polynomial &generator : generators) {
    if (!generator.isZero()) {
      basis.push_back(monic(generator, order));
    }
  }
  if (basis.empty()) {
    return basis;
  }

  std::vector<std::pair<std::size_t, std::size_t>> pairs;
  for (std::size_t i = 0; i < basis.size(); ++i) {
    for (std::size_t j = i + 1; j < basis.size(); ++j) {
      pairs.emplace_back(i, j);
    }
  }

  std::size_t processed = 0;
  while (!pairs.empty()) {
    const auto [i, j] = pairs.back();
    pairs.pop_back();
    if (++processed > kMaxPairs) {
      return std::unexpected(MathsError::NumericOverflow); // 算不动了，明确报错而不是硬跑
    }
    if (i >= basis.size() || j >= basis.size()) {
      continue;
    }

    const std::optional<std::pair<VarPowers, Fraction>> lhsTerm = leadingTerm(basis[i], order);
    const std::optional<std::pair<VarPowers, Fraction>> rhsTerm = leadingTerm(basis[j], order);
    if (!lhsTerm || !rhsTerm) {
      continue;
    }
    // 第一判别法：首项互素时 S-多项式必能约化为 0，不必算
    if (groebner_detail::monomialsAreCoprime(lhsTerm->first, rhsTerm->first)) {
      continue;
    }

    const Result<Polynomial> s = sPolynomial(basis[i], basis[j], order);
    if (s.isErr()) {
      return std::unexpected(s.unwrapErr());
    }
    const Result<Polynomial> reduced = normalForm(s.unwrap(), basis, order);
    if (reduced.isErr()) {
      return std::unexpected(reduced.unwrapErr());
    }
    if (reduced.unwrap().isZero()) {
      continue;
    }

    const Polynomial added = monic(reduced.unwrap(), order);
    basis.push_back(added);
    if (basis.size() > kMaxBasisSize) {
      return std::unexpected(MathsError::NumericOverflow);
    }
    for (std::size_t index = 0; index + 1 < basis.size(); ++index) {
      pairs.emplace_back(index, basis.size() - 1);
    }
  }

  // 去掉能被其余元素约化掉的多余元素（既有的对已全部处理完）
  std::vector<Polynomial> minimal;
  for (std::size_t index = 0; index < basis.size(); ++index) {
    std::vector<Polynomial> others;
    for (std::size_t other = 0; other < basis.size(); ++other) {
      if (other != index) {
        others.push_back(basis[other]);
      }
    }
    const Result<Polynomial> reduced = normalForm(basis[index], others, order);
    if (reduced.isErr()) {
      return std::unexpected(reduced.unwrapErr());
    }
    if (!reduced.unwrap().isZero()) {
      minimal.push_back(basis[index]);
    }
  }
  return minimal;
}

// 等式组是否无解：Gröbner 基里出现非零常数（等价于 1 属于理想）
inline bool isInconsistent(const std::vector<Polynomial> &basis) {
  for (const Polynomial &polynomial : basis) {
    if (!polynomial.isZero() && polynomial.degree() == 0) { // 非零常数：等价于 1 属于理想
      return true;
    }
  }
  return false;
}

// 消元：取基里**不含**这些变量的元素 —— 它们组成消元理想的 Gröbner 基。
//
// 配合 Lex 序使用：名字越小的变量越「重要」，所以列在前面的是被解出的变量。
// 例如 {x − y = 0, z − 2 = 0} 用 Lex 消去 x 后剩下 z − 2 那条。
inline std::vector<Polynomial> eliminateVariables(const std::vector<Polynomial> &basis,
                                                  const std::set<Variable> &variables) {
  std::vector<Polynomial> result;
  for (const Polynomial &polynomial : basis) {
    bool contains = false;
    for (const Variable &variable : variables) {
      if (polynomial.containsVariable(variable)) {
        contains = true;
        break;
      }
    }
    if (!contains) {
      result.push_back(polynomial);
    }
  }
  return result;
}

} // namespace maths
