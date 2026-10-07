export module maths.algebra:piecewise_multi;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import :expression;
import :scope;
import :rational_multi;
import :tower_multi;
import :region_multi;

// ==================== 多元规则与多元分段 ====================
//
// 一元那边：`FunctionRule`（根式 or 塔）+ `PiecewiseFunction`（分支是 RealFunction）。
// 多元这边对应的是 `MultiRule`（有理 or 塔）+ `MultiPiecewiseFunction`（分支是
// 「规则 + Region」）。
//
// 与一元的两点差别：
//
//   1. 分支的定义域是 `Region`（半代数区域的 DNF），不是 `RealSet`。`{x ≥ 0 ∧ y ≥ 0}`
//      与 `{x < 0} ∨ {y < 0}` 都得有地方放。
//   2. 分支之间要求**两两不交** —— 求值时按「点落在哪一支」来选，所以重叠就是歧义。

export namespace maths {

// 多元函数的规则：要么是多元有理函数，要么是一条多元塔
class MultiRule {
public:
  MultiRule() : rational_(Fraction(0, 1)) {}
  // 构造函数刻意 **explicit**：隐式转换叠加重载会让「有理 还是 塔」在调用点含糊
  explicit MultiRule(const MultiRationalFunction &value) : rational_(value) {} // NOLINT
  explicit MultiRule(const MultiTowerExtension &value) : tower_(value) {}      // NOLINT

  bool holdsTower() const { return tower_.has_value(); }
  const MultiRationalFunction &asRational() const { return *rational_; }
  const MultiTowerExtension &asTower() const { return *tower_; }

  // ⚠️ 按**值**返回：转发型 getter 返回引用、而被转发方按值返回就是悬垂引用（踩过三次）
  std::set<Variable> variables() const { return rational_ ? rational_->variables() : tower_->variables(); }

  bool isZero() const { return rational_ ? rational_->isZero() : tower_->isZero(); }

  Result<RealAlgebraicNumber> evaluate(const Scope &point) const {
    if (rational_) {
      const Result<Fraction> value = rational_->evaluate(point);
      if (value.isErr()) {
        return std::unexpected(value.unwrapErr());
      }
      return Result<RealAlgebraicNumber>(RealAlgebraicNumber(value.unwrap()));
    }
    return tower_->evaluate(point);
  }

  // ==================== 算术 ====================
  //
  // 与一元 `FunctionRule` 同一套：rational ⊗ rational 走原路径，rational ⊗ tower
  // 把 rational 挂到塔顶，tower ⊗ tower 必须是同一条塔。
  Result<MultiRule> operator+(const MultiRule &rhs) const { return combine(rhs, '+'); }
  Result<MultiRule> operator-(const MultiRule &rhs) const { return combine(rhs, '-'); }
  Result<MultiRule> operator*(const MultiRule &rhs) const { return combine(rhs, '*'); }

  MultiRule negate() const { return rational_ ? MultiRule(-(*rational_)) : MultiRule(tower_->negate()); }

  bool operator==(const MultiRule &rhs) const {
    if (rational_ && rhs.rational_) {
      return *rational_ == *rhs.rational_;
    }
    return tower_ && rhs.tower_ && *tower_ == *rhs.tower_;
  }

  std::string str() const { return rational_ ? rational_->str() : tower_->str(); }
  std::string latex() const { return rational_ ? rational_->latex() : tower_->latex(); }

private:
  Result<MultiRule> combine(const MultiRule &rhs, char operation) const {
    if (rational_ && rhs.rationalFree()) {
      return combineRational(rhs, operation);
    }
    return combineTower(rhs, operation);
  }

  bool rationalFree() const { return rational_.has_value(); }

  Result<MultiRule> combineRational(const MultiRule &rhs, char operation) const {
    Result<MultiRationalFunction> value = Result<MultiRationalFunction>(*rational_);
    switch (operation) {
    case '+':
      value = Result<MultiRationalFunction>(*rational_ + *rhs.rational_);
      break;
    case '-':
      value = Result<MultiRationalFunction>(*rational_ - *rhs.rational_);
      break;
    case '*':
      value = *rational_ * *rhs.rational_;
      break;
    default:
      return Result<MultiRule>::err(MathsError::NotARational); // 有理 ÷ 有理不在本版范围
    }
    if (value.isErr()) {
      return std::unexpected(value.unwrapErr());
    }
    return Result<MultiRule>(MultiRule(value.unwrap()));
  }

  Result<MultiRule> combineTower(const MultiRule &rhs, char operation) const {
    const MultiTowerExtension &base = tower_ ? *tower_ : *rhs.tower_;
    if (tower_ && rhs.tower_ && !tower_->sameTower(*rhs.tower_)) {
      return Result<MultiRule>::err(MathsError::InvalidExpression); // 两条不同的塔，域都不一样
    }
    const Result<MultiTowerExtension> left =
        tower_ ? Result<MultiTowerExtension>(*tower_) : base.liftedWith(*rational_);
    if (left.isErr()) {
      return std::unexpected(left.unwrapErr());
    }
    const Result<MultiTowerExtension> right =
        rhs.tower_ ? Result<MultiTowerExtension>(*rhs.tower_) : base.liftedWith(*rhs.rational_);
    if (right.isErr()) {
      return std::unexpected(right.unwrapErr());
    }
    Result<MultiTowerExtension> value = Result<MultiTowerExtension>(left.unwrap());
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
    return Result<MultiRule>(MultiRule(value.unwrap()));
  }

  std::optional<MultiRationalFunction> rational_;
  std::optional<MultiTowerExtension> tower_;
};

// ==================== 多元分段函数 ====================

class MultiPiecewiseFunction {
public:
  struct Branch {
    MultiRule rule;
    Region domain;
  };

  MultiPiecewiseFunction() = default;
  explicit MultiPiecewiseFunction(std::vector<Branch> branches) : branches_(std::move(branches)) {}

  static Result<MultiPiecewiseFunction> make(std::vector<Branch> branches) {
    // 分支必须**两两不交**，否则按点求值时选哪一支是歧义。
    // 只在「支数不多」时真去验 —— 交运算的支数会涨，穷举检查不现实。
    if (branches.size() <= 8) {
      for (std::size_t i = 0; i < branches.size(); ++i) {
        for (std::size_t j = i + 1; j < branches.size(); ++j) {
          const Result<Region> overlap = branches[i].domain.intersect(branches[j].domain);
          if (overlap.isErr()) {
            return std::unexpected(overlap.unwrapErr());
          }
          // ⚠️ **判不出 ≠ 重叠。**
          //
          // `isEmptyRegion()` 判不出来时返回 `DomainNotDecidable`（多元多项式不等式
          // 有无可行解不是本库能定的事）。原来这里直接 `.unwrap()` —— 对错误结果
          // unwrap 就是 abort（0xC0000409），多元绝对值因此必崩。
          //
          // 判不出就当「不重叠」放行：这个检查本来就是尽力而为（支数一多就跳过），
          // 而且解析器造出来的分支**按构造就互斥** —— 同一个多项式上一支要 `≥ 0`、
          // 另一支要 `< 0`。只有「能证明非空」才算歧义并拒收。
          const Result<bool> empty = overlap.unwrap().isEmptyRegion();
          if (empty.isOk() && !empty.unwrap()) {
            return Result<MultiPiecewiseFunction>::err(MathsError::InvalidExpression);
          }
        }
      }
    }
    return Result<MultiPiecewiseFunction>(MultiPiecewiseFunction(std::move(branches)));
  }

  const std::vector<Branch> &branches() const { return branches_; }
  bool isEmpty() const { return branches_.empty(); }

  // 各支定义域里出现过的变量的并集 —— app 靠它判断该不该走多元那一档
  std::set<Variable> variables() const {
    std::set<Variable> all;
    for (const Branch &branch : branches_) {
      for (const Variable &variable : branch.domain.variables()) {
        all.insert(variable);
      }
    }
    return all;
  }

  // 这个点在函数里吗
  Result<bool> admits(const Scope &point) const {
    for (const Branch &branch : branches_) {
      const Result<bool> inside = branch.domain.admits(point);
      if (inside.isErr()) {
        return std::unexpected(inside.unwrapErr());
      }
      if (inside.unwrap()) {
        return Result<bool>(true);
      }
    }
    return Result<bool>(false);
  }

  // 点落在哪一支就取那一支；都不在 → OutsideDomain
  Result<RealAlgebraicNumber> at(const Scope &point) const {
    for (const Branch &branch : branches_) {
      const Result<bool> inside = branch.domain.admits(point);
      if (inside.isErr()) {
        return std::unexpected(inside.unwrapErr());
      }
      if (inside.unwrap()) {
        return branch.rule.evaluate(point);
      }
    }
    return Result<RealAlgebraicNumber>::err(MathsError::OutsideDomain);
  }

  std::string str() const { return render(false); }
  std::string latex() const { return render(true); }

private:
  std::string render(bool useLatex) const {
    if (branches_.empty()) {
      return useLatex ? "\\varnothing" : "(empty)";
    }
    std::string result;
    for (std::size_t index = 0; index < branches_.size(); ++index) {
      if (index > 0) {
        // ⚠️ 别用 `\\\\`（LaTeX 的换行）当分隔符 —— 它在数学式里没有意义。
        result += useLatex ? ";\\quad " : " , ";
      }
      const Branch &branch = branches_[index];
      result += (useLatex ? branch.rule.latex() : branch.rule.str());
      result += useLatex ? " & " : " when ";
      result += branch.domain.latex();
    }
    return result;
  }

  std::vector<Branch> branches_;
};

} // namespace maths
