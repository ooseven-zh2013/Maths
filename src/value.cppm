export module maths.value;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebra;
import maths.real_set;
import maths.algebraic_number;
import maths.parser;

// ==================== Expression：调用方看到的那一个类型 ====================
//
// **它解决什么**：库里每种数学对象是一个独立类型（有理式 / 代数数 / 代数系数式 /
// 根式 / 分段 / 塔 / 多元塔 / 多元分段），而它们**不共享任何操作**。于是调用方
// 要做任何一件事（求值、代入、化简、打印）都**得先判断「这是哪种类型」** ——
// 分派散在调用方各处，判断错一次就是一类 bug（单变量被多元档抢走、同一句话在
// 两条路上含义不同、`\quad` 印到终端…）。
//
// ```cpp
// auto e = Expression::parse("|x^2+y^2-2xy|");   // 一个入口，内部自己决定表示
// e.variables();                                // {x, y}
// e.evaluate(scope);                            // → 精确值
// e.latex();  e.str();
// ```
//
// 它**不替代**底层那些类型 —— 那些仍是精确的表达能力；这里只是给「只想算一下」的
// 调用方一条短路。
//
// ⚠️ 为什么单独成模块、而不是 `maths.parser` 的分区：分区**不能 import 自己的主模块**
// （实测 `maths.parser` 引 `:value`、`:value` 又要用 `maths.parser` 的 parse 函数 ⇒
// 循环依赖），而 `Expression` 正是靠那些 parse 函数建起来的。

export namespace maths {

class Expression {
public:
  // 底层用的哪种表示。
  //
  // ⚠️ 它是**实现细节**：判断「能不能做某件事」请用下面的方法，不要拿它做分支
  // —— 那正是这个类要消灭的东西。公开只为诊断与测试。
  enum class Representation {
    Rational,          // ℚ(x₁,…,xₙ) 上的分式
    AlgebraicNumber,   // 纯数值（含根号），如 \sqrt{2}+\sqrt{3}
    AlgebraicRational, // 系数是实代数数的分式，如 \sqrt{2}*x
    Radical,           // 根号包着变量，如 \sqrt{x}+\sqrt{x+1}
    Tower,             // 套嵌根号（单变量），如 \sqrt{1+\sqrt{x}}
    MultiTower,        // 套嵌根号（多变量），如 \sqrt{x^2+y^2}
    Piecewise,         // 分段函数（含一元绝对值 |x|）
    MultiPiecewise,    // 多元分段（含多元绝对值 |x-y|）
  };

  Expression() = default;

  // ==================== 解析 ====================

  // 唯一的解析入口。内部按下面的顺序试，**调用方不需要知道有哪几档**。
  //
  // 顺序（每档都有存在理由，动它会改变语义）：
  //   1. 有理式        —— 最基础，且它的语法诊断最准
  //   2. 代数数        —— 纯数值，给「精确值」；排在有理式之后是刻意的（1/2 该走上一档）
  //   3. 代数系数的分式 —— \sqrt{2}*x：根号包常数而式子带变量
  //   4. 根式扩张      —— \sqrt{x}、\sqrt{x}+\sqrt{x+1}
  //   5. 多元塔        —— \sqrt{x^2+y^2}；**只在变量 > 1 时**接
  //   6. 多元分段      —— |x-y|；**只在变量 > 1 时**接
  //   7. 一元分段      —— |x| 与 \sqrt{x^2}（同一个东西）
  //   8. 单变量塔      —— \sqrt{1+\sqrt{x}}
  static Result<Expression> parse(std::string_view text);

  // ==================== 基本信息 ====================

  Representation representation() const { return representation_; }
  const std::set<Variable> &variables() const { return variables_; }

  // 输入形如 |…| 或 √(g²)（也就是「本来想写绝对值」）。调用方据此回显成 |…|。
  bool writtenAsAbsoluteValue() const { return writtenAsAbsoluteValue_; }

  std::string latex() const;
  std::string str() const;

  // ==================== 运算 ====================

  // 化简。各表示都**已经是规范形**，所以目前原样返回：
  //   · 多元分段的完全平方已在解析时短路成单支（|x²+y²−2xy| = x²−2xy+y²）
  //   · 塔与根式**不做**最简根式化（√(4x) 保持 √(4x)，刻意为之，见 docs）
  Expression simplify() const { return *this; }

  // 代入。底层支持就换元；不支持（分段 / 塔 / 多元两者）报 InvalidExpression ——
  // 对它们该做的是 `evaluate`（分段要在点上看落在哪支，塔要逐层代生成元）。
  Result<Expression> substitute(const Scope &values) const;

  // 在给定点求值 → 精确值。
  Result<RealAlgebraicNumber> evaluate(const Scope &point) const;

  // ==================== 逃生舱 ====================
  //
  // **多数调用方不需要下面这些。** 它们存在的唯一理由是「代码已经知道自己在处理
  // 哪种表示」（例如渲染要读 PiecewiseFunction 的分支、或要给塔算定义域）。
  //
  // 判据：**如果你先问「它是哪种表示」再决定怎么做，那段代码就是分派** ——
  // 能换成 `variables/evaluate/latex` 就换掉。返回 nullptr 表示「不是这种表示」。

  const RationalFunction *asRational() const { return rational_ ? &*rational_ : nullptr; }
  const RealAlgebraicNumber *asAlgebraicNumber() const { return algebraicNumber_ ? &*algebraicNumber_ : nullptr; }
  const AlgebraicRationalFunction *asAlgebraicRational() const {
    return algebraicRational_ ? &*algebraicRational_ : nullptr;
  }
  const RadicalExtension *asRadical() const { return radical_ ? &*radical_ : nullptr; }
  const PiecewiseFunction *asPiecewise() const { return piecewise_ ? &*piecewise_ : nullptr; }
  const TowerExtension *asTower() const { return tower_ ? &*tower_ : nullptr; }
  const MultiTowerExtension *asMultiTower() const { return multiTower_ ? &*multiTower_ : nullptr; }
  const MultiPiecewiseFunction *asMultiPiecewise() const { return multiPiecewise_ ? &*multiPiecewise_ : nullptr; }

private:
  Representation representation_{Representation::Rational};
  std::set<Variable> variables_;
  bool writtenAsAbsoluteValue_{false};

  // 底层载荷：八种表示里恰好用一种。
  //
  // 用八个 optional 而不是 variant —— 各方法里 `if (rational_)` 之类比访问 variant 的
  // 第 k 个成员直观，也不会在重排变体时静默取错类型。
  std::optional<RationalFunction> rational_;
  std::optional<RealAlgebraicNumber> algebraicNumber_;
  std::optional<AlgebraicRationalFunction> algebraicRational_;
  std::optional<RadicalExtension> radical_;
  std::optional<PiecewiseFunction> piecewise_;
  std::optional<TowerExtension> tower_;
  std::optional<MultiTowerExtension> multiTower_;
  std::optional<MultiPiecewiseFunction> multiPiecewise_;

  void assign(const RationalFunction &);
  void assign(const RealAlgebraicNumber &);
  void assign(const AlgebraicRationalFunction &);
  void assign(const RadicalExtension &);
  void assign(const PiecewiseFunction &);
  void assign(const TowerExtension &);
  void assign(const MultiTowerExtension &);
  void assign(const MultiPiecewiseFunction &);
};

// ==================== 实现 ====================

namespace expression_detail {

// 多元那两档的守卫：**只在变量 > 1 时接**。
//
// ⚠️ 少了它 `|x|` 会被多元分段抢走 —— `parseMultiPiecewiseExpression("|x|")` 是成功的
// （它构造得出两支，只是只有 1 个变量），于是单变量绝对值被要求给「一次给全的点」。
// 判据与多元塔那一档一致。
template <class Value> bool takesMultipleVariables(const Result<Value> &parsed) {
  return parsed.isOk() && parsed.unwrap().variables().size() > std::size_t(1);
}

// 取「点」上某个变量的有理值。
//
// ⚠️ `Scope::lookup` 返回的是 `RationalFunction`（带变量的分式），**不是系数** ——
// 一个 Scope 存的是「变量 → 有理函数」，所以要再求一次值才得到数。
inline Result<Fraction> rationalPointValue(const Scope &point, const Variable &variable) {
  const Result<RationalFunction> bound = point.lookup(variable);
  if (bound.isErr()) {
    return std::unexpected(bound.unwrapErr());
  }
  if (!bound.unwrap().variables().empty()) {
    return Result<Fraction>::err(MathsError::UndefinedVariable); // 点里给了个还带变量的式子
  }
  return bound.unwrap().evaluate(Scope()); // 无变量的有理函数求值 = 它本身那个有理数
}

// 多档都失败时报哪个错？看谁**更具体**：
//   · InvalidExpression 是各档共有的兜底码，不算「更具体」
//   · 纯数值输入只有代数数那档给得出 ZeroDenominator / DivisionByZero /
//     NumericOverflow（2^{1/0}、0^{-1}、(-4)^{1/2}）
//   · 根号那档的 RadicandIsSquare / RadicalsNotIndependent 是在**整条输入按根号语法
//     解析成功之后**才抛的（语法没问题，卡住的是「√(x²) 是 |x|」这种数学限制）
template <class... Values> MathsError mostSpecific(const Values &...attempts) {
  MathsError reported = MathsError::InvalidExpression;
  auto consider = [&reported](const auto &attempt) {
    if (attempt.isErr() && attempt.unwrapErr() != MathsError::InvalidExpression) {
      reported = attempt.unwrapErr();
    }
  };
  (consider(attempts), ...);
  return reported;
}

} // namespace expression_detail

inline Result<Expression> Expression::parse(std::string_view text) {
  // ⚠️ `value` 收 const 引用：按值收会**每个参数都复制一份**却只当 const 引用用
  // （clang-tidy 的 performance-unnecessary-value-param 会报，CI 按 error 处理）
  auto make = [](const auto &value, Expression::Representation representation, std::set<Variable> names,
                 bool asAbsoluteValue) {
    Expression expression;
    expression.representation_ = representation;
    expression.variables_ = std::move(names);
    expression.writtenAsAbsoluteValue_ = asAbsoluteValue;
    expression.assign(value);
    return Result<Expression>(std::move(expression));
  };

  // 1. 有理式。最基础的一档，它的语法诊断也最准（认得变量、说得出错在哪）。
  const Result<RationalFunction> rational = parseExpression(text);
  if (rational.isOk()) {
    return make(rational.unwrap(), Representation::Rational, rational.unwrap().variables(), false);
  }

  // 2. 代数数：纯数值（含根号），给的是「精确值」而不是「化简结果」。
  const Result<RealAlgebraicNumber> number = RealAlgebraicNumber::parse(text);
  if (number.isOk()) {
    return make(number.unwrap(), Representation::AlgebraicNumber, {}, false); // 纯数值没有变量
  }

  // 3. 系数取实代数数的分式：\sqrt{2}*x、x+\sqrt{2}。
  const Result<AlgebraicRationalFunction> algebraic = parseAlgebraicExpression(text);
  if (algebraic.isOk()) {
    return make(algebraic.unwrap(), Representation::AlgebraicRational, algebraic.unwrap().variables(), false);
  }

  // 4. 根号包着**变量**：\sqrt{x}、\sqrt{x^2+1}、\sqrt{x}+\sqrt{x+1}。
  const Result<RadicalExtension> radical = parseRadicalExpression(text);
  if (radical.isOk()) {
    return make(radical.unwrap(), Representation::Radical, radical.unwrap().variables(), false);
  }

  // 5. 多元塔：\sqrt{x^2+y^2}。变量不止一个才接。
  const Result<MultiTowerExtension> multiTower = parseMultiTowerExpression(text);
  if (expression_detail::takesMultipleVariables(multiTower)) {
    return make(multiTower.unwrap(), Representation::MultiTower, multiTower.unwrap().variables(), false);
  }

  // 6. 多元分段：|x-y|、|x*y-1|、|x^2+y^2-2xy|（完全平方会短路成单支）。
  //
  //    位置要紧：必须在多元塔**之后**（|x-y| 里没有根号，塔那档本来就拒），
  //    也必须在下面那个一元分段**之前** —— 一元分段见多元会报 NotUnivariate，
  //    那个诊断只对「输入本该是一元」才有意义。
  const Result<MultiPiecewiseFunction> multiPiecewise = parseMultiPiecewiseExpression(text);
  if (expression_detail::takesMultipleVariables(multiPiecewise)) {
    return make(multiPiecewise.unwrap(), Representation::MultiPiecewise, multiPiecewise.unwrap().variables(), true);
  }

  // 7. 一元分段：|x| 与 √(x²) 是同一个东西（`|g|` 先被改写成 `√(g²)` 再走同一条路）。
  //    它装不进前面任何一档：代数函数域里 √(g²) 不是单值元素，但作为 ℝ → ℝ 的函数
  //    完全合法 —— 分段不是「化简得不好」，是这类函数的本来面目。
  const Result<PiecewiseParseResult> piecewise = parsePiecewiseExpressionDetailed(text);
  if (piecewise.isOk()) {
    Expression expression;
    expression.representation_ = Representation::Piecewise;
    expression.piecewise_ = piecewise.unwrap().value;
    expression.variables_ = {expression.piecewise_->variable()}; // 一元分段只有一个自变量
    expression.writtenAsAbsoluteValue_ = piecewise.unwrap().writtenAsAbsoluteValue;
    return Result<Expression>(std::move(expression));
  }

  // 8. 单变量塔：\sqrt{1+\sqrt{x}}。放最后：没有套嵌的输入在前面几档就成功了。
  const Result<TowerExtension> tower = parseTowerExpression(text);
  if (tower.isOk()) {
    return make(tower.unwrap(), Representation::Tower, tower.unwrap().variables(), false);
  }

  // 全败 ⇒ 报最具体的那条原因（InvalidExpression 是兜底码，不算数）
  return std::unexpected(expression_detail::mostSpecific(rational, number, algebraic, radical, multiTower,
                                                         multiPiecewise, piecewise, tower));
}

inline std::string Expression::latex() const {
  if (rational_) {
    return rational_->latex();
  }
  if (algebraicNumber_) {
    return algebraicNumber_->latex();
  }
  if (algebraicRational_) {
    return algebraicRational_->latex();
  }
  if (radical_) {
    return radical_->latex();
  }
  if (piecewise_) {
    return piecewise_->latex();
  }
  if (tower_) {
    return tower_->latex();
  }
  if (multiTower_) {
    return multiTower_->latex();
  }
  if (multiPiecewise_) {
    return multiPiecewise_->latex();
  }
  return {};
}

inline std::string Expression::str() const {
  if (rational_) {
    return rational_->str();
  }
  if (algebraicNumber_) {
    return algebraicNumber_->str();
  }
  if (algebraicRational_) {
    return algebraicRational_->str();
  }
  if (radical_) {
    return radical_->str();
  }
  if (piecewise_) {
    return piecewise_->str();
  }
  if (tower_) {
    return tower_->str();
  }
  if (multiTower_) {
    return multiTower_->str();
  }
  if (multiPiecewise_) {
    return multiPiecewise_->str();
  }
  return {};
}

inline Result<Expression> Expression::substitute(const Scope &values) const {
  if (rational_) {
    const Result<RationalFunction> substituted = rational_->substitute(values);
    if (substituted.isErr()) {
      return std::unexpected(substituted.unwrapErr());
    }
    Expression result = *this;
    result.rational_ = substituted.unwrap();
    result.variables_ = result.rational_->variables();
    return Result<Expression>(std::move(result));
  }
  if (radical_) {
    const Result<RadicalExtension> substituted = radical_->substitute(values);
    if (substituted.isErr()) {
      return std::unexpected(substituted.unwrapErr());
    }
    Expression result = *this;
    result.radical_ = substituted.unwrap();
    result.variables_ = result.radical_->variables();
    return Result<Expression>(std::move(result));
  }
  // 分段 / 塔 / 多元两者不提供 substitute：对它们该做的是**求值**（见 evaluate）。
  // 这里明确报错，别静默走成别的路。
  return Result<Expression>::err(MathsError::InvalidExpression);
}

inline Result<RealAlgebraicNumber> Expression::evaluate(const Scope &point) const {
  if (rational_) {
    // 有理式在**有理点**上求值 ⇒ 得到 Fraction，包成 RealAlgebraicNumber 与其它档对齐
    const Result<Fraction> value = rational_->evaluate(point);
    if (value.isErr()) {
      return std::unexpected(value.unwrapErr());
    }
    return Result<RealAlgebraicNumber>(RealAlgebraicNumber(value.unwrap()));
  }
  if (algebraicNumber_) {
    return Result<RealAlgebraicNumber>(*algebraicNumber_);
  }
  if (algebraicRational_) {
    // ⚠️ 它收的是 **AlgebraicScope**（ScopeOf<RealAlgebraicNumber>）而不是 Scope ——
    // 有理点要逐变量转成代数数。
    ScopeOf<RealAlgebraicNumber> algebraicScope;
    for (const Variable &variable : algebraicRational_->variables()) {
      const Result<Fraction> position = expression_detail::rationalPointValue(point, variable);
      if (position.isErr()) {
        return std::unexpected(position.unwrapErr());
      }
      const Result<void> assigned = algebraicScope.assign(variable, RealAlgebraicNumber(position.unwrap()));
      if (assigned.isErr()) {
        return std::unexpected(assigned.unwrapErr());
      }
    }
    const Result<RealAlgebraicNumber> value = algebraicRational_->evaluate(algebraicScope);
    return value.isErr() ? std::unexpected(value.unwrapErr()) : value;
  }
  if (radical_) {
    const Result<RealAlgebraicNumber> value = radical_->evaluate(point);
    return value.isErr() ? std::unexpected(value.unwrapErr()) : value;
  }
  if (piecewise_) {
    // 分段是一元的：只看它自己那个变量在点上的值
    const Result<Fraction> position = expression_detail::rationalPointValue(point, piecewise_->variable());
    if (position.isErr()) {
      return std::unexpected(position.unwrapErr());
    }
    return piecewise_->at(position.unwrap());
  }
  if (tower_) {
    const Result<RealAlgebraicNumber> value = tower_->evaluate(point);
    return value.isErr() ? std::unexpected(value.unwrapErr()) : value;
  }
  if (multiTower_) {
    const Result<RealAlgebraicNumber> value = multiTower_->evaluate(point);
    return value.isErr() ? std::unexpected(value.unwrapErr()) : value;
  }
  if (multiPiecewise_) {
    return multiPiecewise_->at(point);
  }
  return Result<RealAlgebraicNumber>::err(MathsError::InvalidExpression);
}

inline void Expression::assign(const RationalFunction &value) { rational_ = value; }
inline void Expression::assign(const RealAlgebraicNumber &value) { algebraicNumber_ = value; }
inline void Expression::assign(const AlgebraicRationalFunction &value) { algebraicRational_ = value; }
inline void Expression::assign(const RadicalExtension &value) { radical_ = value; }
inline void Expression::assign(const PiecewiseFunction &value) { piecewise_ = value; }
inline void Expression::assign(const TowerExtension &value) { tower_ = value; }
inline void Expression::assign(const MultiTowerExtension &value) { multiTower_ = value; }
inline void Expression::assign(const MultiPiecewiseFunction &value) { multiPiecewise_ = value; }

} // namespace maths
