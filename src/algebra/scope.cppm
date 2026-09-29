export module maths.algebra:scope;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import :expression;
import :rational;
import :constraint;

export namespace maths {

// 变量到值的绑定表（形式上类似命名空间，但绑定的是值本身）。系数类型由模板参数决定。
//
// 值统一以 RationalFunctionOf 存储 —— 常数是分式的特例（分母为 1），
// 于是整数、分数、多项式、分式共用一个空间，既不丢信息也不会出现两套表不一致。
//
// 因此支持把变量绑定到**含其它变量的表达式**，例如 s = v*t、x = 1/(a+b)。
// 右边可以引用式子里没出现过的变量，它们会在代入时继续被替换（见 substitute 的不动点迭代）。
//
// 赋值**不允许自引用**：x = 2x、x = x + 1 这类是方程而不是赋值，需要解方程，因此明确报错。

namespace detail {

// JSON 字符串转义。按当前变量名与表达式的字符集其实无需转义，
// 做完整处理是为了将来放宽字符集时不产出非法 JSON。
inline std::string jsonEscape(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (char c : text) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
      break;
    }
  }
  return result;
}

} // namespace detail

// 绑定的值是否满足该变量的取值范围约束
enum class Admission {
  Admits,   // 满足（或该变量没有约束）
  Violates, // 违反
  Unknown,  // 判断不了：变量未绑定，或绑定值不是常数
};

template <class Coefficient> class ScopeOf {
public:
  using ValueType = RationalFunctionOf<Coefficient>;
  using Bindings = std::map<Variable, ValueType>;

  // ==================== 赋值 ====================

  // 右边不得含被赋值的变量本身 —— 那是方程而非赋值（如 x = 2x），需要解方程，明确不支持。
  // 同时做环检测：x = s、s = t、t = x 这类绕开形式同样拒绝。
  Result<void> assign(const Variable &variable, const ValueType &value) {
    if (value.containsVariable(variable)) {
      return Result<void>::err(MathsError::NotAnAssignment);
    }
    if (createsCycle(variable, value)) {
      return Result<void>::err(MathsError::CircularReference);
    }
    values[variable] = value;
    return Result<void>();
  }

  // 常数与整数的便捷入口
  Result<void> assign(const Variable &variable, const Coefficient &value) { return assign(variable, ValueType(value)); }

  Result<void> assign(const Variable &variable, const Integer &value) {
    return assign(variable, ValueType(Coefficient(Fraction::fromInteger(value))));
  }

  // ==================== 查询 ====================

  bool contains(const Variable &variable) const { return values.find(variable) != values.end(); }
  bool empty() const { return values.empty(); }
  std::size_t size() const { return values.size(); }

  // 变量未绑定时返回 MathsError::UndefinedVariable
  Result<ValueType> lookup(const Variable &variable) const {
    const auto found = values.find(variable);
    if (found == values.end()) {
      return std::unexpected(MathsError::UndefinedVariable);
    }
    return found->second;
  }

  // ==================== 取值范围约束 ====================
  //
  // 赋值给的是「点」（x = 2），约束给的是「范围」（x ≥ 1、x ≠ 0），两者并存。
  // 同一个变量重复给约束时取交集，于是约束只会越来越紧。

  Result<void> restrict(const Variable &variable, const RealSet &allowed) {
    for (RangeConstraint &constraint : constraints_) {
      if (constraint.variable() == variable) {
        const Result<RealSet> narrowed = constraint.allowed().intersect(allowed);
        if (narrowed.isErr()) {
          return Result<void>::err(narrowed.unwrapErr());
        }
        constraint = RangeConstraint(variable, narrowed.unwrap());
        return Result<void>();
      }
    }
    constraints_.emplace_back(variable, allowed);
    return Result<void>();
  }

  const std::vector<RangeConstraint> &constraints() const { return constraints_; }

  // 该变量当前绑定的值是否满足约束。
  // 绑定值不是常数（含其它变量）时无从判定 —— 返回 Unknown 而不是猜。
  Admission admits(const Variable &variable) const {
    for (const RangeConstraint &constraint : constraints_) {
      if (!(constraint.variable() == variable)) {
        continue;
      }
      const auto found = values.find(variable);
      if (found == values.end()) {
        return Admission::Unknown; // 还没绑定，无从谈起
      }
      const Result<MonomialOf<Coefficient>> numerator = found->second.getNumerator().toMonomial();
      const Result<MonomialOf<Coefficient>> denominator = found->second.getDenominator().toMonomial();
      if (numerator.isErr() || !numerator.unwrap().isConstant() || denominator.isErr() ||
          !denominator.unwrap().isConstant()) {
        return Admission::Unknown; // 绑定值是含变量的式子
      }
      const Result<Coefficient> value = numerator.unwrap().getCoefficient() / denominator.unwrap().getCoefficient();
      if (value.isErr()) {
        return Admission::Unknown;
      }
      if constexpr (std::is_same_v<Coefficient, Fraction>) {
        return constraint.admits(RealAlgebraicNumber(value.unwrap())) ? Admission::Admits : Admission::Violates;
      } else if constexpr (std::is_same_v<Coefficient, RealAlgebraicNumber>) {
        return constraint.admits(value.unwrap()) ? Admission::Admits : Admission::Violates;
      } else {
        return Admission::Unknown;
      }
    }
    return Admission::Admits; // 该变量没有约束
  }

  // ==================== 修改 ====================

  // 解除绑定；返回是否确实删除了某个绑定
  bool erase(const Variable &variable) { return values.erase(variable) != 0; }

  // 清空整张表：绑定与取值范围约束一起清
  void clear() {
    values.clear();
    constraints_.clear();
  }

  const Bindings &bindings() const { return values; }

  // JSON 对象：键是变量名，值是表达式文本
  std::string str() const {
    std::string result = "{";
    bool first = true;
    for (const auto &[variable, value] : values) {
      if (!first) {
        result += ", ";
      }
      first = false;
      result += '"';
      result += detail::jsonEscape(variable.str());
      result += "\": \"";
      result += detail::jsonEscape(value.str());
      result += '"';
    }
    result += "}";
    return result;
  }

  // value 里出现的变量，沿现有绑定链是否最终指向 target。
  // 现有绑定保证无环（每次 assign 都过了检测），visited 只是防御性兜底。
  bool createsCycle(const Variable &variable, const ValueType &value) const {
    for (const Variable &start : value.variables()) {
      std::set<Variable> visited;
      if (reachesVariable(start, variable, visited)) {
        return true;
      }
    }
    return false;
  }

  bool reachesVariable(const Variable &from, const Variable &target, std::set<Variable> &visited) const {
    if (from == target) {
      return true;
    }
    if (!visited.insert(from).second) {
      return false;
    }
    const auto found = values.find(from);
    if (found == values.end()) {
      return false;
    }
    for (const Variable &next : found->second.variables()) {
      if (reachesVariable(next, target, visited)) {
        return true;
      }
    }
    return false;
  }

private:
  Bindings values;
  // 取值范围约束。`erase` 只解除绑定，不动约束（约束说的是这个变量能取什么，
  // 与「当前绑了没」是两件事）；要一并清掉用 `clear`。
  std::vector<RangeConstraint> constraints_;
};

// ==================== 代入的实现 ====================
// 放在 ScopeOf 定义之后：expression.cppm 与 rational.cppm 里只有声明。

namespace detail {

// 值取幂。值是纯系数（分母为 1、分子为常数）时直接走系数的 pow，那里有纯根式的
// O(deg) 特例；其余情形用二进制取幂。
//
// **别改回线性乘**：`α^k` 会被退化成 k−1 次通用乘积（环维数 deg²），
// x^3 配 α = ⁶√2 在 α²·α 那一步就 NumericOverflow（实测过）。
template <class Coefficient>
inline Result<RationalFunctionOf<Coefficient>> valuePower(const RationalFunctionOf<Coefficient> &base,
                                                          unsigned long long exponent) {
  if (exponent == 0ULL) {
    return RationalFunctionOf<Coefficient>(detail::coefficientOne<Coefficient>());
  }

  const Result<MonomialOf<Coefficient>> numerator = base.getNumerator().toMonomial();
  const Result<MonomialOf<Coefficient>> denominator = base.getDenominator().toMonomial();
  if (numerator.isOk() && numerator.unwrap().isConstant() && denominator.isOk() && denominator.unwrap().isConstant() &&
      denominator.unwrap().getCoefficient() == detail::coefficientOne<Coefficient>()) {
    const Result<Coefficient> powered = detail::coefficientPower(numerator.unwrap().getCoefficient(), exponent);
    if (powered.isErr()) {
      return std::unexpected(powered.unwrapErr());
    }
    return RationalFunctionOf<Coefficient>(powered.unwrap());
  }

  RationalFunctionOf<Coefficient> result(detail::coefficientOne<Coefficient>());
  RationalFunctionOf<Coefficient> current = base;
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

} // namespace detail

// 一次替换：已绑定的变量换成它的值，未绑定的原样保留。
// 结果用 RationalFunctionOf 承载，因为绑定值本身可能是分式。
template <class Coefficient>
inline RationalFunctionOf<Coefficient> MonomialOf<Coefficient>::substitute(const ScopeOf<Coefficient> &scope) const {
  using Value = RationalFunctionOf<Coefficient>;
  using MonomialType = MonomialOf<Coefficient>;

  if (isZero()) {
    return Value(detail::coefficientZero<Coefficient>());
  }

  Value result(coeff);
  VarPowers remaining;

  for (const auto &[variable, exponent] : factors) {
    const Result<Value> value = scope.lookup(variable);
    if (value.isErr()) {
      remaining.push_back({variable, exponent}); // 未绑定：原样保留
      continue;
    }

    // value^exponent 并进结果（指数非负，零次幂在规范化时已删除）。
    // 走 valuePower 而不是线性乘：高次幂必须借到系数的纯根式特例。
    const Result<Value> power = detail::valuePower(value.unwrap(), exponent);
    if (power.isErr()) {
      // 本函数没有返回值位置（签名就是 RationalFunctionOf），
      // 由 RationalFunctionOf::substitute 在边界把异常转回 Result。
      throw MathsException(power.unwrapErr());
    }
    result = result * power.unwrap();
  }

  if (!remaining.empty()) {
    result = result * Value(MonomialType(detail::coefficientOne<Coefficient>(), std::move(remaining)));
  }
  return result;
}

template <class Coefficient>
inline RationalFunctionOf<Coefficient> PolynomialOf<Coefficient>::substitute(const ScopeOf<Coefficient> &scope) const {
  using Value = RationalFunctionOf<Coefficient>;

  Value result(detail::coefficientZero<Coefficient>());
  for (const auto &entry : terms) {
    result = result + MonomialOf<Coefficient>(entry.second, entry.first).substitute(scope);
  }
  return result;
}

// 完全求值统一委托给 RationalFunctionOf：代入后要求分子分母都化为常数。
// 绑定值可能是分式，所以不能像原来那样直接看单项式的系数。
template <class Coefficient>
inline Result<Coefficient> MonomialOf<Coefficient>::evaluate(const ScopeOf<Coefficient> &scope) const {
  return RationalFunctionOf<Coefficient>(*this).evaluate(scope);
}

template <class Coefficient>
inline Result<Coefficient> PolynomialOf<Coefficient>::evaluate(const ScopeOf<Coefficient> &scope) const {
  return RationalFunctionOf<Coefficient>(*this).evaluate(scope);
}

// 反复替换直到不再变化，处理 s = v*t、v = a*b 这类链式绑定。
// 迭代上限取 size() + 1，足以展开任意无环依赖链，同时避免循环绑定导致的不终止
// （循环绑定虽然被 assign 拦住了自引用，但 a = b、b = a 仍是环，这里保底）。
template <class Coefficient>
inline Result<RationalFunctionOf<Coefficient>>
RationalFunctionOf<Coefficient>::substitute(const ScopeOf<Coefficient> &scope) const {
  // 代入过程中内部的多项式运算在极端情况下仍会抛 MathsException（内部有 unwrap）。
  // 但 substitute 有返回值位置，就该把失败以 Result 交出去 ——
  // 让异常跑出去，宿主程序只会得到一个没有错误信息的硬崩溃。
  try {
    const auto compute = [this, &scope]() -> Result<RationalFunctionOf> {
      RationalFunctionOf current = *this;
      const std::size_t limit = scope.size() + 1;

      for (std::size_t iteration = 0; iteration < limit; ++iteration) {
        // 必须用 current 的分子分母。若用 *this 的，每一轮都在替换原始式子，
        // 链式绑定（s = v*t 且 v = a*b）就只能展开一层。
        const RationalFunctionOf replacedNumerator = current.getNumerator().substitute(scope);
        const RationalFunctionOf replacedDenominator = current.getDenominator().substitute(scope);

        if (replacedDenominator.isZero()) {
          return std::unexpected(MathsError::ZeroDenominator);
        }

        const Result<RationalFunctionOf> quotient = replacedNumerator / replacedDenominator;
        if (quotient.isErr()) {
          return quotient;
        }
        if (quotient.unwrap() == current) {
          break; // 到达不动点
        }
        current = quotient.unwrap();
      }

      // 代入不改变此前化简已经丢掉的定义域约束，照旧保留
      current.discarded = discarded;
      return current;
    };
    return compute();
  } catch (const MathsException &error) {
    return std::unexpected(error.code());
  }
}

// 完全求值：要求代入后分子分母都化为常数
template <class Coefficient>
inline Result<Coefficient> RationalFunctionOf<Coefficient>::evaluate(const ScopeOf<Coefficient> &scope) const {
  // 同 substitute：有返回值位置就交 Result，不让内部异常跑出去
  try {
    const Result<RationalFunctionOf> expanded = substitute(scope);
    if (expanded.isErr()) {
      return std::unexpected(expanded.unwrapErr());
    }

    const Result<MonomialOf<Coefficient>> numeratorValue = expanded.unwrap().getNumerator().toMonomial();
    const Result<MonomialOf<Coefficient>> denominatorValue = expanded.unwrap().getDenominator().toMonomial();
    if (numeratorValue.isErr() || !numeratorValue.unwrap().isConstant() || denominatorValue.isErr() ||
        !denominatorValue.unwrap().isConstant()) {
      return std::unexpected(MathsError::UndefinedVariable);
    }

    // 分母非零由 substitute 保证，除法不会失败
    return numeratorValue.unwrap().getCoefficient() / denominatorValue.unwrap().getCoefficient();
  } catch (const MathsException &error) {
    return std::unexpected(error.code());
  }
}

template <class Coefficient> inline std::ostream &operator<<(std::ostream &os, const ScopeOf<Coefficient> &scope) {
  return os << scope.str();
}

// ==================== 既有名字（系数为有理数） ====================

using Scope = ScopeOf<Fraction>;

} // namespace maths
