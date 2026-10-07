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
    if (generator.isZero()) {
      continue;
    }
    const Polynomial normalized = monic(generator, order);
    // 首一化后相同的元素只留一个 —— 主理想（gcd 就是其中之一元）很常见，
    // 留着重复项会让下面「最小化」把它们互相约化掉（见那里的说明）。
    bool duplicate = false;
    for (const Polynomial &existing : basis) {
      if (existing == normalized) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      basis.push_back(normalized);
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
  // ⚠️ 兜底：上面的「最小化」对**主理想**会把每个元素都被其余元素约化成 0 ——
  // gcd(x−y, 2x−2y) 的基是 {x−y, x−y}，首一化后完全相同 ⇒ 互相约化 ⇒ 结果为空。
  // 而主理想的最小基是 {g}，不是 {} —— 空基会让调用方以为「理想是零理想」。
  if (minimal.empty() && !basis.empty()) {
    minimal.push_back(basis.front());
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

// ==================== 完全平方判定 ====================
//
// 「f 是不是常数 × 完全平方」—— 多元绝对值化简要用的就是它：
// `|x²+y²-2xy|` 里 `x²+y²-2xy = (x-y)²`，于是 `|f| = f` 恒成立，不必拆两支。
//
// 判据（Yun 平方自由分解的第一步）：f = c·h² ⟺ w := gcd(f, ∂f/∂x₁, …, ∂f/∂xₙ)
// 满足 `f / w²` 是非零常数。
// 理由：h 整除 f，也整除每个偏导（∂(ch²)/∂xᵢ = 2ch·∂h/∂xᵢ）；
// 而 h 无平方因子时这个 gcd 恰好就是 h。反过来若 f/w² 不是常数，f 就不是平方。
//
// 为什么不用 CAD：判「f ≥ 0 恒成立」是符号判定（研究级）；判「f 是平方」只是
// 代数事实，一个 gcd 就够。**表示与判定是两件事** —— 多元绝对值的分支定义域
// 本来就写着符号，不需要判非负。

// `VarPowers` 是**有序 vector**（不是 map），所以按变量找指数只能顺序扫。
inline unsigned long long exponentOf(const VarPowers &powers, const Variable &target) {
  for (const auto &[variable, exponent] : powers) {
    if (variable == target) {
      return exponent;
    }
  }
  return 0;
}

// 对某个变量求偏导：把每一项的该变量指数减一，指数降到 0 的那一项从 vector 里去掉。
inline Result<Polynomial> partialDerivative(const Polynomial &value, const Variable &target) {
  std::map<VarPowers, Fraction> result;
  for (const auto &[powers, coefficient] : value.getTerms()) {
    const unsigned long long exponent = exponentOf(powers, target);
    if (exponent == 0) {
      continue; // 不含这个变量 ⇒ 偏导为 0
    }
    VarPowers reduced;
    reduced.reserve(powers.size());
    for (const auto &[variable, power] : powers) {
      if (variable == target) {
        if (power > 1) {
          reduced.emplace_back(variable, power - 1); // 指数 1 的项整个消失
        }
        continue;
      }
      reduced.emplace_back(variable, power);
    }
    result[reduced] = result[reduced] + coefficient * Fraction(static_cast<long long>(exponent), 1);
  }
  if (result.empty()) {
    return Result<Polynomial>::err(MathsError::InvalidExpression); // 偏导恒为 0
  }
  Polynomial polynomial;
  for (const auto &[powers, coefficient] : result) {
    polynomial.addTerm(powers, coefficient);
  }
  return Result<Polynomial>(polynomial);
}

// 两个多项式的最大公因式。
//
// 走 Gröbner：理想 `(f,g)` 在 UFD 里是**主理想**，生成元就是 gcd，
// 而它就是基里那个首项整除其余全部首项的元素。
inline Result<Polynomial> polynomialGcd(const Polynomial &lhs, const Polynomial &rhs) {
  if (lhs.isZero()) {
    return Result<Polynomial>(rhs);
  }
  if (rhs.isZero()) {
    return Result<Polynomial>(lhs);
  }
  constexpr MonomialOrder order = MonomialOrder::Lex;
  const Result<std::vector<Polynomial>> basis = groebnerBasis({lhs, rhs}, order);
  if (basis.isErr()) {
    return std::unexpected(basis.unwrapErr());
  }
  const std::vector<Polynomial> &generators = basis.unwrap();
  if (generators.empty()) {
    return Result<Polynomial>::err(MathsError::InvalidExpression);
  }
  // 唯一的生成元：首项整除所有其它首项
  std::optional<Polynomial> found;
  for (const Polynomial &candidate : generators) {
    const std::optional<std::pair<VarPowers, Fraction>> leading = leadingTerm(candidate, order);
    if (!leading.has_value()) {
      continue;
    }
    bool dividesAll = true;
    for (const Polynomial &other : generators) {
      const std::optional<std::pair<VarPowers, Fraction>> otherLeading = leadingTerm(other, order);
      if (!otherLeading.has_value()) {
        continue;
      }
      if (!groebner_detail::monomialQuotient(otherLeading->first, leading->first).has_value()) {
        dividesAll = false;
        break;
      }
    }
    if (dividesAll) {
      found = monic(candidate, order);
      break;
    }
  }
  if (!found.has_value()) {
    return Result<Polynomial>::err(MathsError::InvalidExpression);
  }
  return Result<Polynomial>(found.value());
}

// 常数多项式是不是 1。
//
// ⚠️ 别用 `evaluate(Scope())` —— `Scope` 在 `:scope` 模块，本模块没导入它。
// 直接看项：常数多项式恰好只有一项且该项的 VarPowers 为空。
inline bool isConstantOne(const Polynomial &value) {
  if (!value.variables().empty()) {
    return false;
  }
  const std::map<VarPowers, Fraction> &terms = value.getTerms();
  if (terms.size() != 1) {
    return false;
  }
  return terms.begin()->first.empty() && terms.begin()->second == Fraction(1, 1);
}

// f = c·h² 吗？是则返回 h（首一化），否则 nullopt。
//
// ⚠️ 返回的是 h 而不是 √c —— 常数因子是否平方（2·x² = (√2x)² 里的 √2）不在这里判断，
// 因为那要开方；调用方（`|f|` 化简）只需要知道「f 是平方」这件事本身。
inline std::optional<Polynomial> squareRootIfSquare(const Polynomial &value) {
  if (value.isZero()) {
    return Polynomial(); // 0 = 0²
  }
  if (value.variables().empty()) {
    return std::nullopt; // 非零常数：是不是有理数的平方要开方，不在这里判
  }

  // Yun 的分解，反复做：
  //   a := gcd(r, r 的全部偏导)      —— 剥掉每个不可约因子的一层
  //   b := r / a
  //   g := gcd(a, b)                 —— 这一层里「还带着偶数次幂」的部分
  //   r := r / g² ,  h := h · g
  // 结束时若 r 是常数，f = c·h²  ⇒ f 是平方，平方根就是 h。
  //
  // ⚠️ **为什么不能只看 a**：`x⁴y²` 时 a = x³y（每个因子剥一层），
  // 而真正的平方根是 `x²y` —— 它既不等于 a 也不在「a 的幂」上。
  // `g = gcd(a, b)` 才把这一层能剥的偶次幂取出来：a = x³y、b = xy ⇒ g = xy ✓
  // 只用 a 的话 x⁴y² / a² = 1/y² 除不尽，于是被判成「不是平方」✗
  //
  // ⚠️ 也**不能只对单个变量取偏导**：那样 x²y² 会走成 x²y² → xy² → y² → y → 1，
  // 平方根 `xy` 根本不在链上。全部偏导一起取才一步到位。
  Polynomial rest = value;
  Polynomial root;
  for (std::size_t round = 0; round < 64; ++round) {
    if (rest.variables().empty()) {
      return root; // rest 是常数 ⇒ 成功
    }
    Result<Polynomial> running = Result<Polynomial>(rest);
    for (const Variable &variable : rest.variables()) {
      const Result<Polynomial> derivative = partialDerivative(rest, variable);
      if (derivative.isErr()) {
        continue; // 该偏导为 0
      }
      const Result<Polynomial> next = polynomialGcd(running.unwrap(), derivative.unwrap());
      if (next.isErr()) {
        return std::nullopt; // gcd 算不出来 ⇒ 只说「判不出」
      }
      running = next;
    }
    const Polynomial a = running.unwrap();
    if (isConstantOne(a)) {
      return std::nullopt; // gcd 已是 1：r 无平方因子，不是平方
    }
    const Result<MultivariateDivision> first = multivariateDivide(rest, {a}, MonomialOrder::Lex);
    if (first.isErr() || !first.unwrap().remainder.isZero()) {
      return std::nullopt;
    }
    const Result<Polynomial> layer = polynomialGcd(a, first.unwrap().quotients.front());
    if (layer.isErr() || isConstantOne(layer.unwrap())) {
      return std::nullopt; // gcd(a,b) = 1 ⇒ 这一层没有偶次幂
    }
    const Result<Polynomial> squared = layer.unwrap() * layer.unwrap();
    if (squared.isErr()) {
      return std::nullopt;
    }
    const Result<MultivariateDivision> peeled = multivariateDivide(rest, {squared.unwrap()}, MonomialOrder::Lex);
    if (peeled.isErr() || !peeled.unwrap().remainder.isZero()) {
      return std::nullopt; // g² 除不尽
    }
    const Result<Polynomial> product = root * layer.unwrap();
    if (product.isErr()) {
      return std::nullopt;
    }
    root = product.unwrap();
    rest = peeled.unwrap().quotients.front();
  }
  return std::nullopt; // 64 轮还没剥完 ⇒ 判不出
}

} // namespace maths
