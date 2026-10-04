export module maths.algebra:tower_multi;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import :expression;
import :scope;
import :rational_multi;
import :domain_multi;

// ==================== 多元塔 ====================
//
//     ℚ(x₁,…,xₙ)(y₁)(y₂) … (y_d)      其中   yᵢ² = fᵢ(y₁, …, y_{i−1})
//
// 与一元的 `TowerExtension`（`src/algebra/tower.cppm`）**同构**，只有两处不同：
//
//   系数      `RationalFunction` → `MultiRationalFunction`
//   求值      单个有理数 → 一个 `Scope`（点）
//
// 平表、约化、乘法表那些逻辑逐字照搬 —— 那些与变量个数无关。
//
// **本版不做**：除法（高斯消元要移植一遍）、定义域、符号判定。
// 三者在一元那边都有现成实现，搬过来是纯体力活，但得逐个验。

export namespace maths {

class MultiTowerExtension {
public:
  using Flat = std::vector<MultiRationalFunction>;

  // `relations[i]` 描述 `y_i² = …`，只能用前 i 个生成元 → 长度必须正好是 2^i
  static Result<MultiTowerExtension> make(MultiRationalFunction base, std::vector<Flat> relations) {
    for (std::size_t index = 0; index < relations.size(); ++index) {
      if (relations[index].size() != (std::size_t(1) << index)) {
        return Result<MultiTowerExtension>::err(MathsError::NestedRadical);
      }
      if (std::all_of(relations[index].begin(), relations[index].end(),
                      [](const MultiRationalFunction &value) { return value.isZero(); })) {
        return Result<MultiTowerExtension>::err(MathsError::InvalidRange);
      }
    }
    const std::size_t depth = relations.size();
    Flat flat(std::size_t(1) << depth, MultiRationalFunction(Fraction(0, 1)));
    flat[0] = std::move(base);
    return Result<MultiTowerExtension>(MultiTowerExtension(std::move(relations), std::move(flat)));
  }

  static Result<MultiTowerExtension> fromMasks(std::vector<Flat> relations, Flat flat) {
    if (flat.size() != (std::size_t(1) << relations.size())) {
      return Result<MultiTowerExtension>::err(MathsError::InvalidExpression);
    }
    const Result<MultiTowerExtension> zero = make(MultiRationalFunction(Fraction(0, 1)), std::move(relations));
    if (zero.isErr()) {
      return std::unexpected(zero.unwrapErr());
    }
    return Result<MultiTowerExtension>(MultiTowerExtension(zero.unwrap().relations_, std::move(flat)));
  }

  // 深度 0：只有 ℚ(x₁,…,xₙ) 里的元素
  static Result<MultiTowerExtension> rational(const MultiRationalFunction &value) { return make(value, {}); }

  // 第 index 个生成元本身
  static Result<MultiTowerExtension> generatorOf(const MultiTowerExtension &tower, std::size_t index) {
    if (index >= tower.depth()) {
      return Result<MultiTowerExtension>::err(MathsError::NestedRadical);
    }
    Flat flat(std::size_t(1) << tower.depth(), MultiRationalFunction(Fraction(0, 1)));
    flat[std::size_t(1) << index] = MultiRationalFunction(Fraction(1, 1));
    return fromMasks(tower.relations(), std::move(flat));
  }

  std::size_t depth() const { return relations_.size(); }
  const std::vector<Flat> &relations() const { return relations_; }
  const Flat &coefficients() const { return coefficients_; }

  bool isZero() const {
    return std::all_of(coefficients_.begin(), coefficients_.end(),
                       [](const MultiRationalFunction &value) { return value.isZero(); });
  }

  // 用到的最高编号生成元（掩码 0 是常数项、不是 y₀，所以降到 1 为止）
  std::size_t highestGeneratorUsed() const {
    for (std::size_t mask = coefficients_.size(); mask-- > 1;) {
      if (!coefficients_[mask].isZero()) {
        return static_cast<std::size_t>(std::countr_zero(mask));
      }
    }
    return static_cast<std::size_t>(-1);
  }

  // 系数**与 relations** 里的变量都要算 ——  的平表是 {0, 1}，
  // 系数里根本没有 x、y，它们在 relations 的 f₁ = x²+y² 里。
  //
  // ⚠️ 按**值**返回：转发型 getter 返回引用、而被转发方按值返回，就是悬垂引用
  // （这个坑本项目已经踩过两次）
  std::set<Variable> variables() const {
    std::set<Variable> all;
    const auto collect = [&all](const Flat &flat) {
      for (const MultiRationalFunction &coefficient : flat) {
        const std::set<Variable> found = coefficient.variables();
        all.insert(found.begin(), found.end());
      }
    };
    collect(coefficients_);
    for (const Flat &relation : relations_) {
      collect(relation);
    }
    return all;
  }

  // ==================== 算术 ====================

  Result<MultiTowerExtension> operator+(const MultiTowerExtension &rhs) const { return combine(rhs, '+'); }
  Result<MultiTowerExtension> operator-(const MultiTowerExtension &rhs) const { return combine(rhs, '-'); }
  Result<MultiTowerExtension> operator*(const MultiTowerExtension &rhs) const {
    if (!sameTower(rhs)) {
      return Result<MultiTowerExtension>::err(MathsError::InvalidExpression);
    }
    std::vector<Term> terms;
    for (std::size_t left = 0; left < coefficients_.size(); ++left) {
      if (coefficients_[left].isZero()) {
        continue;
      }
      const std::vector<unsigned> leftPowers = powersOfMask(left, depth());
      for (std::size_t right = 0; right < rhs.coefficients_.size(); ++right) {
        if (rhs.coefficients_[right].isZero()) {
          continue;
        }
        std::vector<unsigned> powers = leftPowers;
        const std::vector<unsigned> rightPowers = powersOfMask(right, depth());
        for (std::size_t index = 0; index < powers.size(); ++index) {
          powers[index] += rightPowers[index];
        }
        const Result<MultiRationalFunction> product = coefficients_[left] * rhs.coefficients_[right];
        if (product.isErr()) {
          return std::unexpected(product.unwrapErr());
        }
        terms.push_back(Term{product.unwrap(), std::move(powers)});
      }
    }
    return Result<MultiTowerExtension>(MultiTowerExtension(relations_, reduce(terms)));
  }

  MultiTowerExtension negate() const {
    Flat flat = coefficients_;
    for (MultiRationalFunction &coefficient : flat) {
      coefficient = -coefficient;
    }
    return MultiTowerExtension(relations_, std::move(flat));
  }

  // 往上接一层：y_{d+1}² = radicand（只能用已有的 d 个生成元）
  Result<MultiTowerExtension> adjoining(const Flat &radicand) const {
    if (radicand.size() != coefficients_.size()) {
      return Result<MultiTowerExtension>::err(MathsError::NestedRadical);
    }
    std::vector<Flat> extended = relations_;
    extended.push_back(radicand);
    return make(MultiRationalFunction(Fraction(0, 1)), std::move(extended));
  }

  // ℚ(x₁,…,xₙ) 里的元素放进这条塔
  Result<MultiTowerExtension> lifting(const MultiRationalFunction &value) const {
    Flat flat(coefficients_.size(), MultiRationalFunction(Fraction(0, 1)));
    flat[0] = value;
    return Result<MultiTowerExtension>(MultiTowerExtension(relations_, std::move(flat)));
  }

  // ==================== 除法 ====================
  //
  // 解 `rhs · x = lhs`。乘以非零元素在域上是双射，所以 rhs 在 2^d 维基下的乘法矩阵
  // 可逆，在 ℚ(x₁..xₙ) 上高斯消元即可 —— 与一元那边同一个办法，只是系数换了类型。
  Result<MultiTowerExtension> dividedBy(const MultiTowerExtension &rhs) const {
    if (!sameTower(rhs)) {
      return Result<MultiTowerExtension>::err(MathsError::InvalidExpression);
    }
    if (rhs.isZero()) {
      return Result<MultiTowerExtension>::err(MathsError::DivisionByZero);
    }
    const std::size_t size = coefficients_.size();
    // matrix[掩码][基向量]，行=掩码、列=未知量（与高斯消元里的行列一致）
    std::vector<Flat> matrix(size, Flat(size, MultiRationalFunction(Fraction(0, 1))));
    for (std::size_t column = 0; column < size; ++column) {
      std::vector<Term> product;
      for (std::size_t mask = 0; mask < size; ++mask) {
        if (rhs.coefficients_[mask].isZero()) {
          continue;
        }
        std::vector<unsigned> powers = powersOfMask(mask, depth());
        const std::vector<unsigned> extra = powersOfMask(column, depth());
        for (std::size_t index = 0; index < powers.size(); ++index) {
          powers[index] += extra[index];
        }
        product.push_back(Term{rhs.coefficients_[mask], std::move(powers)});
      }
      const Flat image = reduce(product);
      for (std::size_t mask = 0; mask < size; ++mask) {
        matrix[mask][column] = image[mask];
      }
    }
    Flat right = coefficients_;
    for (std::size_t step = 0; step < size; ++step) {
      std::optional<std::size_t> pivot;
      for (std::size_t row = step; row < size && !pivot.has_value(); ++row) {
        if (!matrix[row][step].isZero()) {
          pivot = row;
        }
      }
      if (!pivot.has_value()) {
        return Result<MultiTowerExtension>::err(MathsError::DivisionByZero); // 矩阵不满秩 → 零因子
      }
      if (*pivot != step) {
        std::swap(matrix[step], matrix[*pivot]);
        std::swap(right[step], right[*pivot]);
      }
      for (std::size_t row = 0; row < size; ++row) {
        if (row == step || matrix[row][step].isZero()) {
          continue;
        }
        const Result<MultiRationalFunction> factor = matrix[row][step] / matrix[step][step];
        if (factor.isErr()) {
          return std::unexpected(factor.unwrapErr());
        }
        for (std::size_t column = 0; column < size; ++column) {
          const Result<MultiRationalFunction> updated = matrix[row][column] - factor.unwrap() * matrix[step][column];
          if (updated.isErr()) {
            return std::unexpected(updated.unwrapErr());
          }
          matrix[row][column] = updated.unwrap();
        }
        const Result<MultiRationalFunction> updatedRight = right[row] - factor.unwrap() * right[step];
        if (updatedRight.isErr()) {
          return std::unexpected(updatedRight.unwrapErr());
        }
        right[row] = updatedRight.unwrap();
      }
    }
    Flat solution(size, MultiRationalFunction(Fraction(0, 1)));
    for (std::size_t index = 0; index < size; ++index) {
      const Result<MultiRationalFunction> value = right[index] / matrix[index][index];
      if (value.isErr()) {
        return std::unexpected(value.unwrapErr());
      }
      solution[index] = value.unwrap();
    }
    return Result<MultiTowerExtension>(MultiTowerExtension(relations_, std::move(solution)));
  }

  // ==================== 求值 ====================

  // 逐层嵌套调 nthRoot。只支持**有理取值**的点 —— 与一元塔同源限制
  // （`MultiRationalFunction::evaluate` 收 `Scope`）。
  Result<RealAlgebraicNumber> evaluate(const Scope &point) const {
    std::vector<RealAlgebraicNumber> generators;
    generators.reserve(depth());
    for (std::size_t index = 0; index < depth(); ++index) {
      const Result<RealAlgebraicNumber> radicand = evaluateFlat(relations_[index], point, generators);
      if (radicand.isErr()) {
        return std::unexpected(radicand.unwrapErr());
      }
      if (radicand.unwrap().compareToRational(Fraction(0, 1)) == std::strong_ordering::less) {
        return std::unexpected(MathsError::NegativeEvenRoot);
      }
      const Result<RealAlgebraicNumber> root = radicand.unwrap().nthRoot(2);
      if (root.isErr()) {
        return std::unexpected(root.unwrapErr());
      }
      generators.push_back(root.unwrap());
    }
    return evaluateFlat(coefficients_, point, generators);
  }

  // 把一个多元有理函数放进这条塔 —— 就是 `lifting`。
  //
  // ⚠️ 有理函数**本来就在第 0 层**（ℚ(x₁..xₙ) 是塔的基域），所以**不需要新生成元**。
  // 我第一版在这里多加了一层，两侧深度就差了 1、乘法直接被 `sameTower` 拒掉 ——
  // 一元那边的 `liftedWith` 之所以有「加生成元」的分支，是因为那里的入参
  // 是 `RadicalExtension`（可能自带根号）；多元这边入参就是有理函数，不需要。
  // 保留这个名字是为了与一元对齐。
  Result<MultiTowerExtension> liftedWith(const MultiRationalFunction &value) const {
    return Result<MultiTowerExtension>(lifting(value));
  }

  // ==================== 判等 ====================

  // 平表就是基（make 已排除可检测的退化），逐项比即可。
  // ⚠️ relations 必须**逐条**比：只比条数的话 y₂²=1+y₁ 与 y₂²=2+y₁ 会被判成相等。
  bool operator==(const MultiTowerExtension &rhs) const {
    return relations_ == rhs.relations_ && coefficients_ == rhs.coefficients_;
  }

  // 判「域是否相同」用这个（`operator==` 连元素一起比）
  bool sameTower(const MultiTowerExtension &rhs) const {
    if (relations_.size() != rhs.relations_.size()) {
      return false;
    }
    for (std::size_t index = 0; index < relations_.size(); ++index) {
      if (relations_[index] != rhs.relations_[index]) {
        return false;
      }
    }
    return true;
  }

  // ==================== 输出 ====================

  std::string str() const { return render(false); }
  std::string latex() const { return render(true); }

private:
  // 计算中间态：系数 × 各生成元的幂（幂允许 ≥ 2）
  struct Term {
    MultiRationalFunction coefficient;
    std::vector<unsigned> powers;
  };

  MultiTowerExtension(std::vector<Flat> relations, Flat coefficients)
      : relations_(std::move(relations)), coefficients_(std::move(coefficients)) {}

  static std::vector<unsigned> powersOfMask(std::size_t mask, std::size_t depth) {
    std::vector<unsigned> powers(depth, 0);
    for (std::size_t index = 0; index < depth; ++index) {
      if ((mask & (std::size_t(1) << index)) != 0) {
        powers[index] = 1;
      }
    }
    return powers;
  }

  // 约化：指数 e 拆成「e mod 2 个 yᵢ 留下」+「⌊e/2⌋ 份 fᵢ」，
  // 从高编号往低编号走一遍就够
  Flat reduce(const std::vector<Term> &input) const {
    std::vector<Term> pending = input;
    for (std::size_t index = depth(); index-- > 0;) {
      std::vector<Term> next;
      for (const Term &term : pending) {
        if (term.powers.size() <= index || term.powers[index] < 2) {
          next.push_back(term);
          continue;
        }
        const unsigned exponent = term.powers[index];
        const unsigned pairs = exponent / 2;
        const unsigned leftover = exponent % 2;
        std::vector<unsigned> base = term.powers;
        base[index] = leftover;
        base.resize(depth(), 0);
        for (unsigned count = 0; count < pairs; ++count) {
          for (std::size_t mask = 0; mask < relations_[index].size(); ++mask) {
            if (relations_[index][mask].isZero()) {
              continue;
            }
            std::vector<unsigned> spawned = base;
            const std::vector<unsigned> lower = powersOfMask(mask, index);
            for (std::size_t position = 0; position < lower.size(); ++position) {
              spawned[position] += lower[position];
            }
            const Result<MultiRationalFunction> product = term.coefficient * relations_[index][mask];
            if (product.isErr()) {
              continue; // 乘法只在极端边界失败，跳过这项
            }
            next.push_back(Term{product.unwrap(), std::move(spawned)});
          }
        }
        if (leftover != 0) {
          next.push_back(Term{term.coefficient, std::move(base)});
        }
      }
      pending = std::move(next);
    }
    Flat flat(coefficients_.size(), MultiRationalFunction(Fraction(0, 1)));
    for (const Term &term : pending) {
      std::size_t mask = 0;
      for (std::size_t index = 0; index < term.powers.size(); ++index) {
        if (term.powers[index] != 0) {
          mask |= std::size_t(1) << index;
        }
      }
      flat[mask] = flat[mask] + term.coefficient;
    }
    return flat;
  }

  Result<MultiTowerExtension> combine(const MultiTowerExtension &rhs, char operation) const {
    if (!sameTower(rhs)) {
      return Result<MultiTowerExtension>::err(MathsError::InvalidExpression);
    }
    std::vector<Term> terms;
    for (std::size_t mask = 0; mask < coefficients_.size(); ++mask) {
      if (coefficients_[mask].isZero()) {
        continue;
      }
      terms.push_back(Term{coefficients_[mask], powersOfMask(mask, depth())});
    }
    for (std::size_t mask = 0; mask < rhs.coefficients_.size(); ++mask) {
      if (rhs.coefficients_[mask].isZero()) {
        continue;
      }
      const MultiRationalFunction coefficient = operation == '-' ? -rhs.coefficients_[mask] : rhs.coefficients_[mask];
      terms.push_back(Term{coefficient, powersOfMask(mask, depth())});
    }
    return Result<MultiTowerExtension>(MultiTowerExtension(relations_, reduce(terms)));
  }

  static Result<RealAlgebraicNumber> evaluateFlat(const Flat &flat, const Scope &point,
                                                  const std::vector<RealAlgebraicNumber> &generators) {
    RealAlgebraicNumber total(Fraction(0, 1));
    for (std::size_t mask = 0; mask < flat.size(); ++mask) {
      if (flat[mask].isZero()) {
        continue;
      }
      const Result<Fraction> rational = flat[mask].evaluate(point);
      if (rational.isErr()) {
        return std::unexpected(rational.unwrapErr());
      }
      RealAlgebraicNumber term(rational.unwrap());
      for (std::size_t index = 0; index < generators.size(); ++index) {
        if ((mask & (std::size_t(1) << index)) == 0) {
          continue;
        }
        const Result<RealAlgebraicNumber> product = term * generators[index];
        if (product.isErr()) {
          return std::unexpected(product.unwrapErr());
        }
        term = product.unwrap();
      }
      const Result<RealAlgebraicNumber> sum = total + term;
      if (sum.isErr()) {
        return std::unexpected(sum.unwrapErr());
      }
      total = sum.unwrap();
    }
    return total;
  }

  std::string render(bool useLatex) const { return renderFlat(coefficients_, useLatex); }

  std::string renderFlat(const Flat &flat, bool useLatex) const {
    std::string result;
    bool first = true;
    for (std::size_t mask = 0; mask < flat.size(); ++mask) {
      if (flat[mask].isZero()) {
        continue;
      }
      // 负号看**分子首项**的系数（分母已归一化成正的）
      bool negative = false;
      for (const auto &[factors, coefficient] : flat[mask].numerator().getTerms()) {
        negative = coefficient.isNegative();
        break;
      }
      const MultiRationalFunction magnitude = negative ? -flat[mask] : flat[mask];
      std::string piece;
      for (std::size_t index = 0; index < depth(); ++index) {
        if ((mask & (std::size_t(1) << index)) == 0) {
          continue;
        }
        if (!(mask == (std::size_t(1) << index) && magnitude == MultiRationalFunction(Fraction(1, 1)))) {
          piece += useLatex ? magnitude.latex() : magnitude.str();
          piece += useLatex ? " " : "*";
        }
        piece += useLatex ? "\\sqrt{" + renderFlat(relations_[index], useLatex) + "}"
                          : "sqrt(" + renderFlat(relations_[index], useLatex) + ")";
      }
      if (piece.empty()) {
        piece = useLatex ? magnitude.latex() : magnitude.str();
      }
      if (first) {
        if (negative) {
          result += "-";
        }
      } else {
        result += negative ? " - " : " + ";
      }
      result += piece;
      first = false;
    }
    return first ? "0" : result;
  }

  std::vector<Flat> relations_;
  Flat coefficients_;
};

} // namespace maths
