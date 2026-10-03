export module maths.algebra:rule;

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
import :tower;

// 函数的「规则」：要么是代数函数域里的元素（`RadicalExtension`），要么是**一条塔**
// （`TowerExtension`，被开方数里可以有根号，如 `√(1+√x)`）。
//
// 为什么不用 `std::variant` 直接换掉：调用方（`RealFunction`、`PiecewiseFunction`、app）
// 已经在用 `RadicalExtension` 的方法名（`variables` / `isRadicalFree` / `toRationalFunction`
// / `evaluate` / `substitute` / `latex` / 四则），这里**沿用同一套方法名**，
// 换字段类型时调用点几乎不用改。
//
// ---- 塔是更大的代数，独立根式扩张是它的特例 ----
//
// 所有关系都在第 0 层的塔，正好就是多生成元的 `RadicalExtension`。所以
//
//   radical ⊗ radical  →  走原有路径，一行代码不变（绝大多数输入都走这里）
//   radical ⊗ tower    →  把 radical 挂到塔顶再算（`TowerExtension::liftedWith`）
//   tower    ⊗ tower   →  塔自己的算术；**两条塔必须相同**（域都不一样）
//
// 塔没有 counterparts 的方法（`isRadicalFree` 恒 false、`toRationalFunction` /
// `substitute` 视情况）一律**明确报错**，不静默降级。
export namespace maths {

class FunctionRule {
public:
  using Flat = TowerExtension::Flat;

  FunctionRule() = default; // 零
  // 构造函数刻意 **explicit**：隐式转换叠加重载会让 `make(有理函数, 定义域)` 这类调用
  // 在「RationalFunction 版」与「RadicalExtension 版」之间含糊，也拼不出
  // `RationalFunction → RadicalExtension → FunctionRule` 的两次转换。要用就调工厂。
  explicit FunctionRule(const RadicalExtension &value) : radical_(value) {}
  explicit FunctionRule(const TowerExtension &value) : tower_(value) {}

  static FunctionRule rational(const RationalFunction &value) { return FunctionRule(RadicalExtension(value)); }
  static FunctionRule radicalOf(const RadicalExtension &value) { return FunctionRule(value); }
  static FunctionRule towerOf(const TowerExtension &value) { return FunctionRule(value); }

  bool holdsTower() const { return tower_.has_value(); }
  const RadicalExtension &asRadical() const { return *radical_; }
  const TowerExtension &asTower() const { return *tower_; }

  // ==================== 观察 ====================

  // ⚠️ 必须**按值返回**： 是按值的，返回引用会绑到临时对象上，
  // 读它就是访问已释放的内存 —— 表现为「莫名其妙多了几个变量」，进而报 NotUnivariate。
  std::set<Variable> variables() const { return radical_ ? radical_->variables() : tower_->variables(); }

  bool isZero() const { return radical_ ? radical_->isZero() : tower_->isZero(); }

  // 塔里必然含生成元，所以「无根号」这件事对塔恒为假
  bool isRadicalFree() const { return radical_ && radical_->isRadicalFree(); }

  // 降回有理函数：塔没有对应的单一有理函数
  Result<RationalFunction> toRationalFunction() const {
    if (!radical_) {
      return Result<RationalFunction>::err(MathsError::NestedRadical);
    }
    return radical_->toRationalFunction();
  }

  Result<RealSet> domain() const {
    if (radical_) {
      return domainOf(*radical_);
    }
    return domainOf(*tower_);
  }

  // ==================== 求值 ====================

  Result<RealAlgebraicNumber> evaluate(const AlgebraicScope &scope) const {
    if (radical_) {
      return radical_->evaluate(scope);
    }
    const Result<Scope> rational = algebraicToRational(scope);
    if (rational.isErr()) {
      return std::unexpected(rational.unwrapErr());
    }
    return tower_->evaluate(rational.unwrap());
  }

  // ==================== 算术 ====================

  FunctionRule operator-() const { return radical_ ? FunctionRule(-*radical_) : FunctionRule(tower_->negate()); }

  Result<FunctionRule> operator+(const FunctionRule &rhs) const { return combine(rhs, '+'); }
  Result<FunctionRule> operator-(const FunctionRule &rhs) const { return combine(rhs, '-'); }
  Result<FunctionRule> operator*(const FunctionRule &rhs) const { return combine(rhs, '*'); }
  Result<FunctionRule> operator/(const FunctionRule &rhs) const { return combine(rhs, '/'); }

  // ==================== 代入 ====================

  Result<FunctionRule> substitute(const Scope &scope) const {
    if (radical_) {
      const Result<RadicalExtension> value = radical_->substitute(scope);
      if (value.isErr()) {
        return std::unexpected(value.unwrapErr());
      }
      return Result<FunctionRule>(FunctionRule(value.unwrap()));
    }
    return Result<FunctionRule>::err(MathsError::NestedRadical);
  }

  // ==================== 输出 ====================

  std::string latex() const { return radical_ ? radical_->latex() : tower_->latex(); }
  std::string str() const { return radical_ ? radical_->str() : tower_->str(); }

  bool operator==(const FunctionRule &rhs) const {
    if (radical_ && rhs.radical_) {
      return *radical_ == *rhs.radical_;
    }
    return tower_ && rhs.tower_ && *tower_ == *rhs.tower_;
  }

private:
  Result<FunctionRule> combine(const FunctionRule &rhs, char operation) const {
    if (radical_ && rhs.radical_) {
      Result<RadicalExtension> value = *radical_;
      switch (operation) {
      case '+':
        value = *radical_ + *rhs.radical_;
        break;
      case '-':
        value = *radical_ - *rhs.radical_;
        break;
      case '*':
        value = *radical_ * *rhs.radical_;
        break;
      default:
        value = *radical_ / *rhs.radical_;
        break;
      }
      if (value.isErr()) {
        return std::unexpected(value.unwrapErr());
      }
      return Result<FunctionRule>(FunctionRule(value.unwrap()));
    }
    // 两边都是塔但**不是同一条** → 域都不一样，没有定义。
    // 不查这一条的话下面会拿左边的塔当公用地，悄悄把右边的生成元关系丢掉。
    if (tower_ && rhs.tower_ && !tower_->sameTower(*rhs.tower_)) {
      return Result<FunctionRule>::err(MathsError::InvalidExpression);
    }
    // 只要有一边是塔，就把另一边挂上去，之后全走塔的算术
    const TowerExtension &base = tower_ ? *tower_ : rhs.asTower();
    const Result<TowerExtension> left = tower_ ? Result<TowerExtension>(*tower_) : base.liftedWith(*radical_);
    if (left.isErr()) {
      return std::unexpected(left.unwrapErr());
    }
    // 条件看的是**右操作数**有没有根式：两边都是塔时不能去解引用 rhs.radical_（空的）
    const Result<TowerExtension> right = rhs.radical_ ? base.liftedWith(*rhs.radical_) : Result<TowerExtension>(base);
    if (right.isErr()) {
      return std::unexpected(right.unwrapErr());
    }
    Result<TowerExtension> value = Result<TowerExtension>(left.unwrap());
    switch (operation) {
    case '+':
      value = left.unwrap() + right.unwrap();
      break;
    case '-':
      value = left.unwrap() - right.unwrap();
      break;
    case '*':
      value = left.unwrap() * right.unwrap();
      break;
    default:
      value = left.unwrap().dividedBy(right.unwrap());
      break;
    }
    if (value.isErr()) {
      return std::unexpected(value.unwrapErr());
    }
    return Result<FunctionRule>(FunctionRule(value.unwrap()));
  }

  // 塔的 evaluate 收 Scope（有理赋值）；代数作用域里的值要先落回有理数
  static Result<Scope> algebraicToRational(const AlgebraicScope &scope) {
    Scope narrowed;
    for (const auto &[variable, value] : scope.bindings()) {
      if (!value.variables().empty()) {
        return Result<Scope>::err(MathsError::UndefinedVariable);
      }
      const Result<RealAlgebraicNumber> evaluated = value.evaluate(scope);
      if (evaluated.isErr()) {
        return Result<Scope>::err(evaluated.unwrapErr());
      }
      const Result<Fraction> rational = evaluated.unwrap().toFraction();
      if (rational.isErr()) {
        return Result<Scope>::err(rational.unwrapErr());
      }
      const Result<void> assigned = narrowed.assign(variable, rational.unwrap());
      if (assigned.isErr()) {
        return Result<Scope>::err(assigned.unwrapErr());
      }
    }
    return Result<Scope>(narrowed);
  }

  std::optional<RadicalExtension> radical_;
  std::optional<TowerExtension> tower_;
};

} // namespace maths
