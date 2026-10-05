export module maths.algebra:function_multi;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.real_set;
import :expression;
import :scope;
import :constraint_system;
import :rational_multi;
import :domain_multi;

// ==================== 多元函数 ====================
//
// 一元那边是 `RealFunction`：`(Variable, FunctionRule, RealSet)`，规则是根式扩张或塔。
// 多元版把三处换掉：
//
//   变量      `Variable` → `std::set<Variable>`
//   定义域    `RealSet`（ℝ 的区间）→ `ConstraintSystem`（ℝⁿ 的半代数合取）
//   求值      `at(Fraction)` → `at(Scope)`（接**一个点**，不是单个有理数）
//
// **本版规则只到 `MultiRationalFunction`** —— 多元的根式扩张与塔还没做，
// 它们要把 `RadicalExtension` / `TowerExtension` 的系数换成多元版本（平表与约化逻辑
// 可以照搬）。所以现在能表达的是 `x/y`、`(x+y)/(x−y)`、`1/(x²+y²)` 这类。
//
// 语义与一元那边一致：定义域**取交**而不是覆盖 —— 规则算不出来的点永远不属于函数。

export namespace maths {

class MultiFunction {
public:
  MultiFunction() : rule_(Fraction(0, 1)), domain_(ConstraintSystem()) {}

  // 规则自己的天然定义域（分母 ≠ 0 处）
  static Result<MultiFunction> make(MultiRationalFunction rule) {
    const Result<ConstraintSystem> natural = domainOf(rule);
    if (natural.isErr()) {
      return std::unexpected(natural.unwrapErr());
    }
    return Result<MultiFunction>(MultiFunction(std::move(rule), natural.unwrap()));
  }

  // 显式给定义域：与天然定义域**取交**
  static Result<MultiFunction> make(const MultiRationalFunction &rule, const ConstraintSystem &domain) {
    const Result<MultiFunction> base = make(std::move(rule));
    if (base.isErr()) {
      return std::unexpected(base.unwrapErr());
    }
    return Result<MultiFunction>(base.unwrap().restrict(domain));
  }

  const MultiRationalFunction &rule() const { return rule_; }
  const ConstraintSystem &domain() const { return domain_; }
  // ⚠️ 按**值**返回：`MultiRationalFunction::variables()` 是按值的，返回引用会绑到临时对象
  std::set<Variable> variables() const { return rule_.variables(); }

  bool isZero() const { return rule_.isZero(); }
  // 没有变量 → 常函数
  bool isConstant() const { return rule_.variables().empty(); }
  // 规则里分母是 1 → 处处有定义
  bool isTotal() const { return domain_.isTrivial(); }

  // ==================== 求值 ====================

  // 点必须落在定义域内，否则 OutsideDomain —— 那是「函数在那里没定义」，
  // 不是算错，与一元那边同一个错误码。
  Result<Fraction> at(const Scope &point) const {
    const Result<bool> inside = domain_.admits(point);
    if (inside.isErr()) {
      return std::unexpected(inside.unwrapErr());
    }
    if (!inside.unwrap()) {
      return Result<Fraction>::err(MathsError::OutsideDomain);
    }
    return rule_.evaluate(point);
  }

  // 批量：逐点求值，任一点出错就整批报错（不给「一半成功」的结果）
  std::vector<Fraction> atAll(const std::vector<Scope> &points) const {
    std::vector<Fraction> values;
    values.reserve(points.size());
    for (const Scope &point : points) {
      const Result<Fraction> value = at(point);
      if (value.isErr()) {
        return {};
      }
      values.push_back(value.unwrap());
    }
    return values;
  }

  // ==================== 收窄定义域 ====================

  Result<MultiFunction> restrict(const ConstraintSystem &subset) const {
    const Result<ConstraintSystem> narrowed = domain_.andWith(subset);
    if (narrowed.isErr()) {
      return std::unexpected(narrowed.unwrapErr());
    }
    return Result<MultiFunction>(MultiFunction(rule_, narrowed.unwrap()));
  }

  // ==================== 四则 ====================

  // 定义域取交：`x/y · 1/x` 只在 x ≠ 0 上有定义，即使乘积规则恰好在 x=0 也能算
  Result<MultiFunction> operator+(const MultiFunction &rhs) const { return combine(rhs, '+'); }
  Result<MultiFunction> operator-(const MultiFunction &rhs) const { return combine(rhs, '-'); }
  Result<MultiFunction> operator*(const MultiFunction &rhs) const { return combine(rhs, '*'); }
  Result<MultiFunction> operator/(const MultiFunction &rhs) const { return combine(rhs, '/'); }

  MultiFunction negate() const { return MultiFunction(-rule_, domain_); }

  Result<MultiFunction> scaledBy(const Fraction &factor) const {
    return operator*(MultiFunction(MultiRationalFunction(factor), ConstraintSystem()));
  }

  // ==================== 输出 ====================

  std::string str() const { return render(false); }
  std::string latex() const { return render(true); }

private:
  MultiFunction(MultiRationalFunction rule, ConstraintSystem domain)
      : rule_(std::move(rule)), domain_(std::move(domain)) {}

  Result<MultiFunction> combine(const MultiFunction &rhs, char operation) const {
    Result<MultiRationalFunction> merged = Result<MultiRationalFunction>(rule_);
    switch (operation) {
    case '+':
      merged = Result<MultiRationalFunction>(rule_ + rhs.rule_);
      break;
    case '-':
      merged = Result<MultiRationalFunction>(rule_ - rhs.rule_);
      break;
    case '*':
      merged = rule_ * rhs.rule_;
      break;
    default:
      merged = rule_ / rhs.rule_;
      break;
    }
    if (merged.isErr()) {
      return std::unexpected(merged.unwrapErr());
    }
    const Result<ConstraintSystem> domain = domain_.andWith(rhs.domain_);
    if (domain.isErr()) {
      return std::unexpected(domain.unwrapErr());
    }
    // 合出来的规则会带自己的天然定义域，再取一次交（与一元同一策略）
    return make(merged.unwrap(), domain.unwrap());
  }

  std::string render(bool useLatex) const {
    std::string body = useLatex ? rule_.latex() : rule_.str();
    if (domain_.isTrivial()) {
      return body;
    }
    return body + (useLatex ? ",\\quad " : " , ") + domain_.latex();
  }

  MultiRationalFunction rule_;
  ConstraintSystem domain_;
};

} // namespace maths
