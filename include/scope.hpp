#pragma once
#ifndef MATHS_SCOPE_HPP
#define MATHS_SCOPE_HPP

// 变量到值的绑定表（形式上类似命名空间，但绑定的是值本身）。
//
// 值统一以 RationalFunction 存储 —— 常数是分式的特例（分母为 1），
// 于是整数、分数、多项式、分式共用一个空间，既不丢信息也不会出现两套表不一致。
//
// 因此支持把变量绑定到**含其它变量的表达式**，例如 s = v*t、x = 1/(a+b)。
// 右边可以引用式子里没出现过的变量，它们会在代入时继续被替换（见 substitute 的不动点迭代）。
//
// 赋值**不允许自引用**：x = 2x、x = x + 1 这类是方程而不是赋值，需要解方程，因此明确报错。

#include "algebraic_expression.hpp"
#include "maths_error.hpp"
#include "numbers.hpp"
#include "rational_function.hpp"
#include "result.hpp"

#include <cstddef>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <string_view>

namespace maths_detail {

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

} // namespace maths_detail

class Scope {
public:
  using Bindings = std::map<Variable, RationalFunction>;

  // ==================== 赋值 ====================

  // 右边不得含被赋值的变量本身 —— 那是方程而非赋值（如 x = 2x），需要解方程，明确不支持。
  // 同时做环检测：x = s、s = t、t = x 这类绕开形式同样拒绝。
  Result<void> assign(const Variable &variable, const RationalFunction &value) {
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
  Result<void> assign(const Variable &variable, const Fraction &value) {
    return assign(variable, RationalFunction(value));
  }

  Result<void> assign(const Variable &variable, const Integer &value) {
    return assign(variable, RationalFunction(Fraction::fromInteger(value)));
  }

  // ==================== 查询 ====================

  bool contains(const Variable &variable) const { return values.find(variable) != values.end(); }
  bool empty() const { return values.empty(); }
  std::size_t size() const { return values.size(); }

  // 变量未绑定时返回 MathsError::UndefinedVariable
  Result<RationalFunction> lookup(const Variable &variable) const {
    const auto found = values.find(variable);
    if (found == values.end()) {
      return std::unexpected(MathsError::UndefinedVariable);
    }
    return found->second;
  }

  // ==================== 修改 ====================

  // 解除绑定；返回是否确实删除了某个绑定
  bool erase(const Variable &variable) { return values.erase(variable) != 0; }

  void clear() { values.clear(); }

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
      result += maths_detail::jsonEscape(variable.str());
      result += "\": \"";
      result += maths_detail::jsonEscape(value.str());
      result += '"';
    }
    result += "}";
    return result;
  }

  // value 里出现的变量，沿现有绑定链是否最终指向 target。
  // 现有绑定保证无环（每次 assign 都过了检测），visited 只是防御性兜底。
  bool createsCycle(const Variable &variable, const RationalFunction &value) const {
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
};

// ==================== 代入的实现 ====================
// 放在 Scope 定义之后：algebraic_expression.hpp 与 rational_function.hpp 里只有声明。

// 一次替换：已绑定的变量换成它的值，未绑定的原样保留。
// 结果用 RationalFunction 承载，因为绑定值本身可能是分式。
inline RationalFunction Monomial::substitute(const Scope &scope) const {
  if (isZero()) {
    return RationalFunction(Fraction(0, 1));
  }

  RationalFunction result(coeff);
  VarPowers remaining;

  for (const auto &[variable, exponent] : factors) {
    const Result<RationalFunction> value = scope.lookup(variable);
    if (value.isErr()) {
      remaining.push_back({variable, exponent}); // 未绑定：原样保留
      continue;
    }

    // value^exponent 并进结果（指数非负，零次幂在规范化时已删除）
    RationalFunction power(Fraction(1, 1));
    for (unsigned long long i = 0; i < exponent; ++i) {
      power = power * value.unwrap();
    }
    result = result * power;
  }

  if (!remaining.empty()) {
    result = result * RationalFunction(Monomial(Fraction(1, 1), std::move(remaining)));
  }
  return result;
}

inline RationalFunction Polynomial::substitute(const Scope &scope) const {
  RationalFunction result(Fraction(0, 1));
  for (const auto &entry : terms) {
    result = result + Monomial(entry.second, entry.first).substitute(scope);
  }
  return result;
}

// 完全求值统一委托给 RationalFunction：代入后要求分子分母都化为常数。
// 绑定值可能是分式，所以不能像原来那样直接看单项式的系数。
inline Result<Fraction> Monomial::evaluate(const Scope &scope) const { return RationalFunction(*this).evaluate(scope); }

inline Result<Fraction> Polynomial::evaluate(const Scope &scope) const {
  return RationalFunction(*this).evaluate(scope);
}

// 反复替换直到不再变化，处理 s = v*t、v = a*b 这类链式绑定。
// 迭代上限取 size() + 1，足以展开任意无环依赖链，同时避免循环绑定导致的不终止
// （循环绑定虽然被 assign 拦住了自引用，但 a = b、b = a 仍是环，这里保底）。
inline Result<RationalFunction> RationalFunction::substitute(const Scope &scope) const {
  RationalFunction current = *this;
  const std::size_t limit = scope.size() + 1;

  for (std::size_t iteration = 0; iteration < limit; ++iteration) {
    // 必须用 current 的分子分母。若用 *this 的，每一轮都在替换原始式子，
    // 链式绑定（s = v*t 且 v = a*b）就只能展开一层。
    const RationalFunction replacedNumerator = current.getNumerator().substitute(scope);
    const RationalFunction replacedDenominator = current.getDenominator().substitute(scope);

    if (replacedDenominator.isZero()) {
      return std::unexpected(MathsError::ZeroDenominator);
    }

    const Result<RationalFunction> quotient = replacedNumerator / replacedDenominator;
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
}

// 完全求值：要求代入后分子分母都化为常数
inline Result<Fraction> RationalFunction::evaluate(const Scope &scope) const {
  const Result<RationalFunction> expanded = substitute(scope);
  if (expanded.isErr()) {
    return std::unexpected(expanded.unwrapErr());
  }

  const Result<Monomial> numeratorValue = expanded.unwrap().getNumerator().toMonomial();
  const Result<Monomial> denominatorValue = expanded.unwrap().getDenominator().toMonomial();
  if (numeratorValue.isErr() || !numeratorValue.unwrap().isConstant() || denominatorValue.isErr() ||
      !denominatorValue.unwrap().isConstant()) {
    return std::unexpected(MathsError::UndefinedVariable);
  }

  // 分母非零由 substitute 保证，除法不会失败
  return numeratorValue.unwrap().getCoefficient() / denominatorValue.unwrap().getCoefficient();
}

inline std::ostream &operator<<(std::ostream &os, const Scope &scope) { return os << scope.str(); }

#endif // MATHS_SCOPE_HPP
