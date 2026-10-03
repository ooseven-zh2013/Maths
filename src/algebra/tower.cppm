export module maths.algebra:tower;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import maths.real_set;
import :expression;
import :rational;
import :scope;
import :algebraic;
import :constraint;
import :radical;

// ==================== 塔式扩张 ====================
//
//     ℚ(x)(y₁)(y₂) … (y_d)      其中   yᵢ² = fᵢ(y₁, …, y_{i−1})
//
// 与 `RadicalExtension` 的「独立生成元」只差一处：第 i 个生成元的被开方数是
// **上一层里的元素**，而不是 x 的有理函数。`√(1+√x)` 就是 y₁² = x、y₂² = 1 + y₁。
//
// ---- 范式沿用平表 ----
//
// 每一步都是二次扩张，所以 {Π yᵢ^{eᵢ} : eᵢ ∈ {0,1}} 仍是一组基，维数仍是 2^d，
// 元素仍是「有理系数按子集掩码寻址」的平表 —— 与 `RadicalExtension` 同一套。
// 变的只有三件事：
//
//   1. 被开方数的编码：fᵢ 是「上一层里的元素」，用同一个结构存（powers 只用前 i 位）
//   2. 乘法表：yᵢ² 要换成 fᵢ，而 fᵢ 含更低编号的生成元 → 约化从高编号往低编号走
//   3. 合法性：fᵢ 若在上一层里本身是平方，扩张掉维数，平表就不再是基
//
// ---- 计算中间态 ----
//
// 乘法中间态是「若干项之和」，每项是「系数 × 各生成元的幂」，幂允许 ≥ 2；
// 约化时把成对的 yᵢ² 换成 fᵢ，最后幂降到 0/1 再摊回平表。
// 于是乘法就是「幂相加 + 一遍高→低约化」，不需要 Gröbner 约化。

namespace tower_detail {

// 一项：coefficient × Π yᵢ^{powers[i]}
struct Term {
  maths::RationalFunction coefficient;
  std::vector<unsigned> powers;
};

} // namespace tower_detail

export namespace maths {

class TowerExtension {
public:
  using Term = tower_detail::Term;
  using Flat = std::vector<RationalFunction>; // 平表：下标是子集掩码

  // ==================== 构造 ====================

  // `relations[i]` 描述 `y_i² = …`，只能用前 i 个生成元（`powers` 长度必须正好是 i，
  // 取值 0/1）。`base` 是零层那部分。
  //
  // ⚠️ 目前只检测「fᵢ 是 x 的有理函数且是完全平方」这一种退化（报 `RadicandIsSquare`）。
  // 「fᵢ 在上一层里是平方」这种更一般的退化检测不了 —— 那种输入下平表不再是基，
  // 判等会失效。已知的例子是 `√((√x+1)^2)` 这类刻意构造的输入。
  static Result<TowerExtension> make(const RationalFunction &base, const std::vector<Flat> &relations) {
    for (std::size_t index = 0; index < relations.size(); ++index) {
      // 长度必须是 2^i：relation[i] 只能用前 i 个生成元（掩码最大 2^i−1，
      // 正好第 i 位永远是 0，所以「引用自己」这种情形长度约束本身就排除了）
      if (relations[index].size() != (std::size_t(1) << index)) {
        return Result<TowerExtension>::err(MathsError::NestedRadical);
      }
      if (std::all_of(relations[index].begin(), relations[index].end(),
                      [](const RationalFunction &coefficient) { return coefficient.isZero(); })) {
        return Result<TowerExtension>::err(MathsError::InvalidRange); // √0 之后那层是退化的
      }
    }
    // 第一层的被开方数是 x 的有理函数，必须平方自由 —— 否则 y₁² = h² 让扩张掉一维，
    // 平表就不再是基（`√(x^2)` 那种交给分段函数处理）
    if (!relations.empty() && isSquareOfVariable(relations.front().front())) {
      return Result<TowerExtension>::err(MathsError::RadicandIsSquare);
    }
    const std::size_t depth = relations.size();
    Flat flat(std::size_t(1) << depth, RationalFunction(Fraction(0, 1)));
    flat[0] = base;
    return TowerExtension(relations, std::move(flat));
  }

  // 深度 0：只有 ℚ(x) 里的元素
  static Result<TowerExtension> rational(const RationalFunction &value) { return make(value, {}); }

  // 由平表直接构造 —— 这是最一般的入口：`flat[mask]` 是「Π yᵢ^{eᵢ} 项」的系数。
  // `make(base, relations)` 就是它的一个特例（只有 mask 0 非零）。
  static Result<TowerExtension> fromMasks(const std::vector<Flat> &relations, const Flat &flat) {
    const std::size_t depth = relations.size();
    if (flat.size() != (std::size_t(1) << depth)) {
      return Result<TowerExtension>::err(MathsError::InvalidExpression);
    }
    const Result<TowerExtension> zero = make(RationalFunction(Fraction(0, 1)), relations);
    if (zero.isErr()) {
      return std::unexpected(zero.unwrapErr());
    }
    return TowerExtension(zero.unwrap().relations_, flat);
  }

  // ==================== 查询 ====================

  std::size_t depth() const { return relations_.size(); }
  const Flat &coefficients() const { return coefficients_; }
  const std::vector<Flat> &relations() const { return relations_; }

  bool isZero() const {
    return std::all_of(coefficients_.begin(), coefficients_.end(),
                       [](const RationalFunction &coefficient) { return coefficient.isZero(); });
  }

  // 塔建在 ℚ(x) 上，唯一的变量就是 x
  std::set<Variable> variables() const {
    if (isConstant()) {
      return {};
    }
    return {Variable("x")};
  }

  // 是不是**同一条塔**（relations 逐条相同）。做算术前必须查：域不同就没法算。
  // operator== 连元素一起比，判「域是否相同」要用这个。
  bool sameTower(const TowerExtension &rhs) const {
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

  // 系数全在 ℚ(x) 里，所以「有没有变量」只看系数
  // 用到的最高编号生成元（一个都没用上时返回 npos）。
  // ⚠️ 掩码 0 是**常数项**、不是 y₀，所以要从高位降到 1 为止。
  std::size_t highestGeneratorUsed() const {
    for (std::size_t mask = coefficients_.size(); mask-- > 1;) {
      if (!coefficients_[mask].isZero()) {
        return static_cast<std::size_t>(std::countr_zero(mask));
      }
    }
    return static_cast<std::size_t>(-1);
  }

  // 常数元素 = **一个生成元都没用上**，且非零的那些系数本身也不含变量。
  // 两个条件都要：y₂ 的系数是常数 1，可「1·y₂」显然不是常数；
  // 反过来深度 0 的元素装的是有理函数 x，它也含变量。
  bool isConstant() const {
    if (highestGeneratorUsed() != static_cast<std::size_t>(-1)) {
      return false;
    }
    return std::all_of(coefficients_.begin(), coefficients_.end(), [](const RationalFunction &coefficient) {
      return coefficient.isZero() || coefficient.variables().empty();
    });
  }

  // ==================== 算术 ====================
  //
  // 两个操作数必须在**同一条塔**上。不同的塔相加没有定义 —— 域都不一样，
  // 要合并得先取复合域，那是另一件事。

  Result<TowerExtension> operator+(const TowerExtension &rhs) const { return combine(rhs, false); }
  Result<TowerExtension> operator-(const TowerExtension &rhs) const { return combine(rhs, true); }

  // 取负不会失败（逐系数变号，不涉及扩域）
  TowerExtension negate() const {
    Flat flat = coefficients_;
    for (RationalFunction &coefficient : flat) {
      coefficient = -coefficient;
    }
    return TowerExtension(relations_, std::move(flat));
  }

  Result<TowerExtension> operator*(const TowerExtension &rhs) const {
    if (!sameTower(rhs)) {
      return Result<TowerExtension>::err(MathsError::InvalidExpression);
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
        terms.push_back(Term{coefficients_[left] * rhs.coefficients_[right], std::move(powers)});
      }
    }
    return TowerExtension(relations_, reduce(terms));
  }

  // 1 / rhs：解 `lhs · x = 1`。
  //
  // 乘以非零元素在域上是双射，所以 lhs 在 2^d 维基下的乘法矩阵可逆，在 ℚ(x) 上
  // 高斯消元解出来就是 x。比「逐层取共轭」省事，也不会在中间层掉维度时失效。
  Result<TowerExtension> dividedBy(const TowerExtension &rhs) const {
    if (!sameTower(rhs)) {
      return Result<TowerExtension>::err(MathsError::InvalidExpression);
    }
    if (rhs.isZero()) {
      return Result<TowerExtension>::err(MathsError::DivisionByZero);
    }
    const std::size_t size = coefficients_.size();
    // 解的是 `rhs · x = lhs`，所以**矩阵取自 rhs、右端取自 lhs**。
    // 矩阵按 `[掩码][基向量]` 存 —— 与高斯消元里「行=掩码、列=未知量」一致。
    std::vector<Flat> matrix(size, Flat(size, RationalFunction(Fraction(0, 1))));
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
    // 对增广矩阵做高斯消元
    Flat right = coefficients_;
    for (std::size_t step = 0; step < size; ++step) {
      std::optional<std::size_t> pivot;
      for (std::size_t row = step; row < size && !pivot.has_value(); ++row) {
        if (!matrix[row][step].isZero()) {
          pivot = row;
        }
      }
      if (!pivot.has_value()) {
        return Result<TowerExtension>::err(MathsError::DivisionByZero); // 矩阵不满秩：a 是零因子
      }
      if (*pivot != step) {
        std::swap(matrix[step], matrix[*pivot]);
        std::swap(right[step], right[*pivot]);
      }
      for (std::size_t row = 0; row < size; ++row) {
        if (row == step || matrix[row][step].isZero()) {
          continue;
        }
        const Result<RationalFunction> factor = matrix[row][step] / matrix[step][step];
        if (factor.isErr()) {
          return Result<TowerExtension>::err(factor.unwrapErr());
        }
        for (std::size_t column = 0; column < size; ++column) {
          const Result<RationalFunction> updated = matrix[row][column] - factor.unwrap() * matrix[step][column];
          if (updated.isErr()) {
            return Result<TowerExtension>::err(updated.unwrapErr());
          }
          matrix[row][column] = updated.unwrap();
        }
        const Result<RationalFunction> updated = right[row] - factor.unwrap() * right[step];
        if (updated.isErr()) {
          return Result<TowerExtension>::err(updated.unwrapErr());
        }
        right[row] = updated.unwrap();
      }
    }
    Flat solution(size, RationalFunction(Fraction(0, 1)));
    for (std::size_t index = 0; index < size; ++index) {
      const Result<RationalFunction> value = right[index] / matrix[index][index];
      if (value.isErr()) {
        return Result<TowerExtension>::err(value.unwrapErr());
      }
      solution[index] = value.unwrap();
    }
    return TowerExtension(relations_, std::move(solution));
  }

  // ==================== 求值 ====================
  //
  // 逐层算：yᵢ 的值 = √(fᵢ 在前面各层上的值)。fᵢ 的值是**代数数**，
  // 所以这里就是嵌套调 `nthRoot` —— 求值对塔不增加任何难度。
  // 本版只支持**有理取值**的点：有理函数只能在有理赋值下求值，
  // 得到的分数再包成实代数数。要在代数点上求值得先把整条塔搬到代数栈上。
  Result<RealAlgebraicNumber> evaluate(const Scope &scope) const {
    std::vector<RealAlgebraicNumber> generators;
    generators.reserve(depth());
    for (std::size_t index = 0; index < depth(); ++index) {
      const Result<RealAlgebraicNumber> radicand = evaluateFlat(relations_[index], scope, generators);
      if (radicand.isErr()) {
        return std::unexpected(radicand.unwrapErr());
      }
      if (radicand.unwrap().compareToRational(Fraction(0, 1)) == std::strong_ordering::less) {
        return std::unexpected(MathsError::NegativeEvenRoot); // 被开方数为负，这一层没有实值
      }
      const Result<RealAlgebraicNumber> root = radicand.unwrap().nthRoot(2);
      if (root.isErr()) {
        return std::unexpected(root.unwrapErr());
      }
      generators.push_back(root.unwrap());
    }
    return evaluateFlat(coefficients_, scope, generators);
  }

  // ==================== 往上接一层 ====================

  // ℚ(x) 里的元素放进这条塔（塔里的常数项）
  Result<TowerExtension> lifting(const RationalFunction &value) const {
    Flat flat(coefficients_.size(), RationalFunction(Fraction(0, 1)));
    flat[0] = value;
    return Result<TowerExtension>(TowerExtension(relations_, std::move(flat)));
  }

  // 往上接一层：y_{d+1}² = radicand。`radicand` 只能用**已有**的 d 个生成元
  // （长度必须正好是 2^d）。
  Result<TowerExtension> adjoining(const Flat &radicand) const {
    if (radicand.size() != coefficients_.size()) {
      return Result<TowerExtension>::err(MathsError::NestedRadical);
    }
    std::vector<Flat> extended = relations_;
    extended.push_back(radicand);
    return Result<TowerExtension>(make(RationalFunction(Fraction(0, 1)), extended));
  }

  // 把一个独立根式扩张「挂」到这条塔的顶上：它的每个生成元各占一层。
  //
  // 这是「塔是更大的代数」的具体用法 —— 所有关系都在第 0 层的塔正好就是多生成元的
  // 独立根式扩张，所以 radical ⊗ tower 只要把 radical 挂上来再算，不用另设一套运算。
  Result<TowerExtension> liftedWith(const RadicalExtension &value) const {
    if (value.isRadicalFree()) {
      const Result<RationalFunction> plain = value.toRationalFunction();
      if (plain.isErr()) {
        return Result<TowerExtension>::err(plain.unwrapErr());
      }
      return Result<TowerExtension>(lifting(plain.unwrap()));
    }
    Result<TowerExtension> extended = Result<TowerExtension>(*this);
    for (const RationalFunction &radicand : value.radicands()) {
      const Result<TowerExtension> appended =
          extended.unwrap().adjoining(flatOfConstant(coefficients_.size(), radicand));
      if (appended.isErr()) {
        return appended;
      }
      extended = appended;
    }
    // 元素本身：原来的掩码整体右移「挂上去的层数」位
    const std::size_t shift = extended.unwrap().depth() - depth();
    Flat flat(std::size_t(1) << extended.unwrap().depth(), RationalFunction(Fraction(0, 1)));
    for (std::size_t mask = 0; mask < value.coefficients().size(); ++mask) {
      if (!value.coefficients()[mask].isZero()) {
        flat[mask << shift] = value.coefficients()[mask];
      }
    }
    return fromMasks(extended.unwrap().relations(), flat);
  }

  // 长度 size、只有常数项是 value 的平表（relations[i] 的标准形状）
  static Flat flatOfConstant(std::size_t size, const RationalFunction &value) {
    Flat flat(size, RationalFunction(Fraction(0, 1)));
    flat[0] = value;
    return flat;
  }

  // 第 index 个生成元本身（平表里只有那一位是 1）
  static Result<TowerExtension> generatorOf(const TowerExtension &tower, std::size_t index) {
    if (index >= tower.depth()) {
      return Result<TowerExtension>::err(MathsError::NestedRadical);
    }
    Flat flat(std::size_t(1) << tower.depth(), RationalFunction(Fraction(0, 1)));
    flat[std::size_t(1) << index] = RationalFunction(Fraction(1, 1));
    return fromMasks(tower.relations(), flat);
  }

  // ==================== 判等 ====================
  //
  // 平表就是基（`make` 已排除可检测的退化），所以逐项比系数即可。
  bool operator==(const TowerExtension &rhs) const {
    // ⚠️ relations 必须**逐条**比：只比条数的话，y₂² = 1 + y₁ 与 y₂² = 2 + y₁
    // 这两条不同的塔会被判成相等（长度一样）
    return relations_ == rhs.relations_ && coefficients_ == rhs.coefficients_;
  }

  // ==================== 输出 ====================

  std::string str() const { return render(false); }
  std::string latex() const { return render(true); }

private:
  TowerExtension(std::vector<Flat> relations, Flat coefficients)
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

  // 系数是不是 1（用来在渲染时省掉 `1√x` 的那个 1）
  static bool isUnit(const RationalFunction &value) { return value == RationalFunction(Fraction(1, 1)); }

  // 负号看**分子首项**的系数（分母已归一化成正的），与 RadicalExtension 同一判据
  static bool isNegative(const RationalFunction &value) {
    const std::optional<Monomial> leading = leadingMonomial(value.getNumerator());
    return leading.has_value() && leading->getCoefficient().isNegative();
  }

  // 有理函数是不是某个有理函数的平方（只看 ℚ(x) 里的那一类退化）
  static bool isSquareOfVariable(const RationalFunction &value) {
    const std::optional<UnivariatePolynomial> univariate = toUnivariatePolynomial(value.getNumerator(), Variable("x"));
    if (!univariate.has_value()) {
      return false; // 多变量：不是这一类退化
    }
    return univariate->squareFreePart().degree() != univariate->degree();
  }

  // 约化：把幂 ≥ 2 的部分按 yᵢ² = fᵢ 换掉，最后摊回平表。
  //
  // fᵢ 本身可能是一**和**（例如 y₂² = 1 + y₁），所以替换时它的每个单项式要**各自成项** ——
  // 累加进同一项会把常数 1 并进 y₁ 项里去。
  //
  // 从高编号往低编号走一遍就够：处理 i 时引入的生成元编号都 < i，后面那轮会管到。
  Flat reduce(const std::vector<Term> &input) const {
    std::vector<Term> pending = input;
    for (std::size_t index = depth(); index-- > 0;) {
      std::vector<Term> next;
      for (const Term &term : pending) {
        if (term.powers.size() <= index || term.powers[index] < 2) {
          next.push_back(term);
          continue;
        }
        // 指数 e 拆成「e mod 2 个 yᵢ 留下」+ 「⌊e/2⌋ 份 fᵢ」。
        // 被约掉的那部分**不再单独保留** —— 保留会把它重复算一遍。
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
            next.push_back(Term{term.coefficient * relations_[index][mask], std::move(spawned)});
          }
        }
        if (leftover != 0) {
          next.push_back(Term{term.coefficient, std::move(base)});
        }
      }
      pending = std::move(next);
    }
    Flat flat(coefficients_.size(), RationalFunction(Fraction(0, 1)));
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

  Result<TowerExtension> combine(const TowerExtension &rhs, bool subtract) const {
    if (!sameTower(rhs)) {
      return Result<TowerExtension>::err(MathsError::InvalidExpression);
    }
    std::vector<Term> terms;
    // 左操作数原样，右操作数在「减」时取负 —— 两边都取负那是加法的负号
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
      terms.push_back(Term{subtract ? -rhs.coefficients_[mask] : rhs.coefficients_[mask], powersOfMask(mask, depth())});
    }
    return TowerExtension(relations_, reduce(terms));
  }

  static Result<RealAlgebraicNumber> evaluateFlat(const Flat &flat, const Scope &scope,
                                                  const std::vector<RealAlgebraicNumber> &generators) {
    RealAlgebraicNumber total(Fraction(0, 1));
    for (std::size_t mask = 0; mask < flat.size(); ++mask) {
      if (flat[mask].isZero()) {
        continue;
      }
      const Result<Fraction> coefficient = flat[mask].evaluate(scope);
      if (coefficient.isErr()) {
        return std::unexpected(coefficient.unwrapErr());
      }
      RealAlgebraicNumber term(coefficient.unwrap());
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
      const bool negative = isNegative(flat[mask]);
      const RationalFunction magnitude = negative ? -flat[mask] : flat[mask];
      std::string piece;
      for (std::size_t index = 0; index < depth(); ++index) {
        if ((mask & (std::size_t(1) << index)) == 0) {
          continue;
        }
        // 单位系数不印：`(1,1)·y₁` 应该是 `√x` 而不是 `1 √x`
        if (!(mask == (std::size_t(1) << index) && isUnit(magnitude))) {
          piece += useLatex ? magnitude.latex() : magnitude.str();
          piece += useLatex ? " " : "*";
        }
        piece += generatorText(index, useLatex);
      }
      if (piece.empty()) { // 纯常数项
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

  // 第 index 个生成元印成 √(fᵢ)，fᵢ 递归渲染
  std::string generatorText(std::size_t index, bool useLatex) const {
    const std::string body = renderFlat(relations_[index], useLatex);
    return useLatex ? "\\sqrt{" + body + "}" : "sqrt(" + body + ")";
  }

  std::vector<Flat> relations_;
  Flat coefficients_;
};

// 一层的条件：{x : fᵢ 在 x 处 ≥ 0}。见 domainOf 的说明。
inline Result<RealSet> layerCondition(std::size_t index, const TowerExtension::Flat &relation,
                                      const std::vector<TowerExtension::Flat> &relations) {
  const std::size_t count = relation.size();
  std::optional<std::size_t> generator; // 非零项用到的生成元
  for (std::size_t mask = 1; mask < count; ++mask) {
    if (relation[mask].isZero()) {
      continue;
    }
    if (std::popcount(mask) != 1) {
      return Result<RealSet>::err(MathsError::DomainNotDecidable); // 一次出现多个生成元
    }
    if (generator.has_value()) {
      return Result<RealSet>::err(MathsError::DomainNotDecidable); // 不止一个生成元
    }
    generator = mask;
  }

  // 只有常数项：fᵢ 是 x 的有理函数
  if (!generator.has_value()) {
    return solveInequality(relation[0], Relation::GreaterEqual);
  }
  // fᵢ = a + b·y_{i−1}：b 必须是常数项之外的**唯一**一项，且 y_{i−1} 本身要有理被开方数
  const std::size_t previousBit = std::size_t(1) << (index - 1);
  if (*generator != previousBit || index == 0) {
    return Result<RealSet>::err(MathsError::DomainNotDecidable);
  }
  if (relations[index - 1].size() != 1 || relations[index - 1][0].isZero()) {
    return Result<RealSet>::err(MathsError::DomainNotDecidable); // y_{i−1} 的被开方数不是有理函数
  }

  const RationalFunction &a = relation[0];
  const RationalFunction &b = relation[previousBit];
  // 记 r = a/b（**不是 b/a**！），于是 h = −a/b = −r，三条判据都写成关于 r 的：
  //   b>0 → √f ≥ −r ⟺ (r ≥ 0) ∨ (f ≥ r²)
  //   b<0 → √f ≤ −r ⟺ (r ≤ 0) ∧ (f ≤ r²)
  const Result<RationalFunction> ratio = a / b;
  if (ratio.isErr()) {
    return Result<RealSet>::err(ratio.unwrapErr());
  }
  const Result<RationalFunction> residual = relations[index - 1][0] - ratio.unwrap() * ratio.unwrap();
  if (residual.isErr()) {
    return Result<RealSet>::err(residual.unwrapErr());
  }

  // b > 0 → √f ≥ −a/b；b < 0 → √f ≤ −a/b；b = 0 → a ≥ 0
  // 而 √f ≥ h ⟺ h ≤ 0 ∨ f ≥ h²，√f ≤ h ⟺ h ≥ 0 ∧ f ≤ h²，h = −a/b
  const Result<RealSet> positive = solveInequality(b, Relation::Greater);
  const Result<RealSet> negative = solveInequality(b, Relation::Less);
  const Result<RealSet> zero = solveInequality(b, Relation::Equal);
  const Result<RealSet> ratioNonNegative = solveInequality(ratio.unwrap(), Relation::GreaterEqual);
  const Result<RealSet> residualNonNegative = solveInequality(residual.unwrap(), Relation::GreaterEqual);
  const Result<RealSet> ratioNonPositive = solveInequality(ratio.unwrap(), Relation::LessEqual);
  const Result<RealSet> residualNonPositive = solveInequality(residual.unwrap(), Relation::LessEqual);
  const Result<RealSet> aNonNegative = solveInequality(a, Relation::GreaterEqual);
  for (const Result<RealSet> *step : {&positive, &negative, &zero, &ratioNonNegative, &residualNonNegative,
                                      &ratioNonPositive, &residualNonPositive, &aNonNegative}) {
    if (step->isErr()) {
      return *step;
    }
  }
  // √f ≥ −a/b  ⟺  (−a/b ≤ 0) ∨ (f ≥ (a/b)²)，而 (−a/b ≤ 0) ⟺ a/b ≥ 0
  const Result<RealSet> atLeast = RealSet(ratioNonNegative.unwrap().unite(residualNonNegative.unwrap()));
  // √f ≤ −a/b  ⟺  (−a/b ≥ 0) ∧ (f ≤ (a/b)²)，而 (−a/b ≥ 0) ⟺ a/b ≤ 0
  const Result<RealSet> atMost = ratioNonPositive.unwrap().intersect(residualNonPositive.unwrap());
  if (atLeast.isErr()) {
    return atLeast;
  }
  if (atMost.isErr()) {
    return atMost;
  }
  const Result<RealSet> upperBranch = positive.unwrap().intersect(atLeast.unwrap());
  if (upperBranch.isErr()) {
    return upperBranch;
  }
  const Result<RealSet> lowerBranch = negative.unwrap().intersect(atMost.unwrap());
  if (lowerBranch.isErr()) {
    return lowerBranch;
  }
  const Result<RealSet> flatBranch = zero.unwrap().intersect(aNonNegative.unwrap());
  if (flatBranch.isErr()) {
    return flatBranch;
  }
  return Result<RealSet>(upperBranch.unwrap().unite(lowerBranch.unwrap()).unite(flatBranch.unwrap()));
}

// ==================== 在塔上求值 ====================

// 把有理函数 `value` 里的变量 `name` 换成塔里的元素 `replacement`。
//
// 有理函数在 name 上是 P(name)/Q(name)（系数是有理数），所以：
//   分子 = Σ aₖ·replacementᵏ（Horner），分母同理，再相除
// 分母可能变成零元素，那时报 DivisionByZero。
// 把「含占位变量的有理函数」变成塔里的元素。
//
// 解析套嵌根号时，被开方数是**占位变量**上的有理函数（例如 t₂² = 1 + t₁ 里的 `1 + t₁`），
// 而占位变量要换成塔里的生成元。逐项展开即可：
//
//   P(t₁,…,t_k) 的每一项 = 有理系数 × t₁^{e₁} × … × t_k^{e_k} × （不含占位变量的那部分）
//                          → 有理系数那部分放进塔，再乘 eⱼ 次第 j 个生成元
//
// 分母同理，最后分子 ÷ 分母。**不能**先把 t₁ 换成元素再换 t₂ —— 换出来的元素不是有理函数，
// 没有地方放回去；逐项展开则全程都是「系数 × 生成元的幂」，可以累加。
inline Result<TowerExtension> evaluateOverPlaceholders(const RationalFunction &value,
                                                       const std::vector<Variable> &placeholders,
                                                       const TowerExtension &tower) {
  const auto expand = [&placeholders, &tower](const Polynomial &polynomial) -> Result<TowerExtension> {
    std::optional<TowerExtension> total;
    for (const auto &[factors, coefficient] : polynomial.getTerms()) {
      std::vector<unsigned> powers(tower.depth(), 0);
      VarPowers rest;
      for (const auto &[variable, power] : factors) {
        bool isPlaceholder = false;
        for (std::size_t index = 0; index < placeholders.size(); ++index) {
          if (placeholders[index] == variable) {
            if (index >= tower.depth()) {
              return Result<TowerExtension>::err(MathsError::NestedRadical);
            }
            powers[index] += power;
            isPlaceholder = true;
            break;
          }
        }
        if (!isPlaceholder) {
          rest.push_back({variable, power});
        }
      }
      Result<TowerExtension> term = tower.lifting(RationalFunction(Monomial(coefficient, rest)));
      for (std::size_t index = 0; index < powers.size(); ++index) {
        for (unsigned step = 0; step < powers[index]; ++step) {
          if (term.isErr()) {
            return term;
          }
          const Result<TowerExtension> generator = TowerExtension::generatorOf(tower, index);
          if (generator.isErr()) {
            return generator;
          }
          const Result<TowerExtension> product = term.unwrap() * generator.unwrap();
          if (product.isErr()) {
            return product;
          }
          term = product;
        }
      }
      if (term.isErr()) {
        return term;
      }
      if (!total.has_value()) {
        total = term.unwrap();
        continue;
      }
      const Result<TowerExtension> sum = total.value() + term.unwrap();
      if (sum.isErr()) {
        return sum;
      }
      total = sum.unwrap();
    }
    if (!total.has_value()) {
      return tower.lifting(RationalFunction(Fraction(0, 1)));
    }
    return Result<TowerExtension>(total.value());
  };

  const Result<TowerExtension> numerator = expand(value.getNumerator());
  if (numerator.isErr()) {
    return numerator;
  }
  const Result<TowerExtension> denominator = expand(value.getDenominator());
  if (denominator.isErr()) {
    return denominator;
  }
  if (denominator.unwrap().isZero()) {
    return Result<TowerExtension>::err(MathsError::ZeroDenominator);
  }
  return numerator.unwrap().dividedBy(denominator.unwrap());
}

inline Result<TowerExtension> substituteVariable(const RationalFunction &value, const Variable &name,
                                                 const TowerExtension &replacement) {
  const auto overTower = [&name, &replacement](const Polynomial &polynomial) -> Result<TowerExtension> {
    std::optional<TowerExtension> total;
    for (const auto &[factors, coefficient] : polynomial.getTerms()) {
      unsigned exponent = 0;
      VarPowers rest;
      for (const auto &[variable, power] : factors) {
        if (variable == name) {
          exponent = power;
        } else {
          rest.push_back({variable, power});
        }
      }
      Result<TowerExtension> term = replacement.lifting(RationalFunction(Monomial(coefficient, rest)));
      for (unsigned step = 0; step < exponent; ++step) {
        if (term.isErr()) {
          return term;
        }
        const Result<TowerExtension> product = term.unwrap() * replacement;
        if (product.isErr()) {
          return product;
        }
        term = product;
      }
      if (term.isErr()) {
        return term;
      }
      if (!total.has_value()) {
        total = term.unwrap();
        continue;
      }
      const Result<TowerExtension> sum = total.value() + term.unwrap();
      if (sum.isErr()) {
        return sum;
      }
      total = sum.unwrap();
    }
    if (!total.has_value()) {
      return replacement.lifting(RationalFunction(Fraction(0, 1)));
    }
    return Result<TowerExtension>(total.value());
  };

  const Result<TowerExtension> numerator = overTower(value.getNumerator());
  if (numerator.isErr()) {
    return numerator;
  }
  const Result<TowerExtension> denominator = overTower(value.getDenominator());
  if (denominator.isErr()) {
    return denominator;
  }
  return numerator.unwrap().dividedBy(denominator.unwrap());
}

// ==================== 判元素的符号 ====================

// `{x : g(x) ≥ 0}`，g 是塔里的元素。绝对值按符号分支时要用。
//
// 只支持“ g 对某个生成元线性、且那个生成元的被开方数是有理函数”这一类 ——
// 那正好是 `layerCondition` 的适用形状（它给的是“每层被开方数 ≥ 0”，而这里需要的是“任意元素”）。
// 判不了的报 DomainNotDecidable —— 不猜。
inline Result<RealSet> whereNonNegativeOverTower(const TowerExtension &value) {
  const TowerExtension::Flat &flat = value.coefficients();
  std::optional<std::size_t> generator;
  for (std::size_t mask = 1; mask < flat.size(); ++mask) {
    if (flat[mask].isZero()) {
      continue;
    }
    if (generator.has_value() || std::popcount(mask) != 1) {
      return Result<RealSet>::err(MathsError::DomainNotDecidable); // 一次用到多个生成元
    }
    generator = mask;
  }
  if (!generator.has_value()) {
    return solveInequality(flat[0], Relation::GreaterEqual); // 纯有理函数
  }
  // 把「对 y_k 线性」形成 「第 k+1 层的被开方数」的形状，直接复用 layerCondition
  return layerCondition(static_cast<std::size_t>(std::countr_zero(*generator)) + 1, flat, value.relations());
}

// ==================== 定义域 ====================
//
// {x : 每一层的被开方数在 x 处都 ≥ 0}。逐层判、必要时与已算出的定义域取交 ——
// 后面的层要用到前面各层的生成元，那些层先得是实数。
//
// 判定范围刻意保守，算不出来就明确报错，不猜：
//
//   fᵢ 是 x 的有理函数            → solveInequality(fᵢ, ≥)
//   fᵢ = a + b·y_{i−1}（a、b 与 f_{i−1} 都是 x 的有理函数）
//                                   → √f ≥ h ⟺ h ≤ 0 ∨ f ≥ h²，三情形化归
//   其余（fᵢ 含更早的生成元）        → DomainNotDecidable
inline Result<RealSet> domainOf(const TowerExtension &value) {
  Result<RealSet> domain = Result<RealSet>(RealSet::realLine());
  for (std::size_t index = 0; index < value.depth(); ++index) {
    const TowerExtension::Flat &relation = value.relations()[index];
    Result<RealSet> layer = layerCondition(index, relation, value.relations());
    if (layer.isErr()) {
      return layer;
    }
    const Result<RealSet> narrowed = domain.unwrap().intersect(layer.unwrap());
    if (narrowed.isErr()) {
      return narrowed;
    }
    domain = narrowed;
  }
  return domain;
}

} // namespace maths
