#pragma once
#ifndef MATHS_SCOPE_HPP
#define MATHS_SCOPE_HPP

// 变量到值的绑定表：把一个变量映射到它的值（形式上类似命名空间，但绑定的是值本身）。
//
// 值统一以 Fraction 存储。Integer 数学上是 Fraction 的子集（分母为 1 的分数），
// 因此整数与分数共用一个空间既不丢信息，也不会出现「同一变量在两套表里
// 有两个不一致的值」这种状态。
//
// 赋值是覆盖语义（与 map 一致），不存在的变量重复赋值不会报错；
// 只有「读取未绑定的变量」才是错误，由 lookup 返回 MathsError::UndefinedVariable。

#include "algebraic_expression.hpp"
#include "maths_error.hpp"
#include "numbers.hpp"
#include "result.hpp"

#include <cstddef>
#include <map>
#include <ostream>
#include <string>

class Scope {
public:
  using Bindings = std::map<Variable, Fraction>;

  // ==================== 赋值 ====================

  // 已绑定则覆盖
  void assign(const Variable &variable, const Fraction &value) { values[variable] = value; }

  // 整数按等值分数存储（分母为 1）
  void assign(const Variable &variable, const Integer &value) { values[variable] = Fraction::fromInteger(value); }

  // ==================== 查询 ====================

  bool contains(const Variable &variable) const { return values.find(variable) != values.end(); }
  bool empty() const { return values.empty(); }
  std::size_t size() const { return values.size(); }

  // 变量未绑定时返回 MathsError::UndefinedVariable
  Result<Fraction> lookup(const Variable &variable) const {
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

  std::string str() const {
    if (values.empty()) {
      return "{}";
    }
    std::string result = "{";
    bool first = true;
    for (const auto &[variable, value] : values) {
      if (!first) {
        result += ", ";
      }
      first = false;
      result += variable.str();
      result += " = ";
      result += maths_detail::renderFraction(value);
    }
    result += "}";
    return result;
  }

private:
  Bindings values;
};

inline std::ostream &operator<<(std::ostream &os, const Scope &scope) { return os << scope.str(); }

// ==================== 代入替换 ====================
// 实现放在 Scope 定义之后：algebraic_expression.hpp 里只有前向声明，避免循环包含。

inline Monomial Monomial::substitute(const Scope &scope) const {
  if (isZero()) {
    return Monomial();
  }

  Fraction substitutedCoeff = coeff;
  VarPowers remaining;
  remaining.reserve(factors.size());

  for (const auto &[variable, exponent] : factors) {
    const Result<Fraction> value = scope.lookup(variable);
    if (value.isErr()) {
      remaining.push_back({variable, exponent}); // 未绑定：原样保留
      continue;
    }
    // 已绑定：把 value^exponent 并进系数，变量本身消失。
    // 指数恒为非负（零次幂在规范化时已删除），因此 pow 不会失败。
    substitutedCoeff = substitutedCoeff * value.unwrap().pow(Integer(exponent)).unwrap();
  }

  return Monomial(substitutedCoeff, std::move(remaining));
}

inline Polynomial Polynomial::substitute(const Scope &scope) const {
  Polynomial result;
  for (const auto &[factors, coefficient] : terms) {
    const Monomial substituted = Monomial(coefficient, factors).substitute(scope);
    result.addTerm(substituted.getFactors(), substituted.getCoefficient());
  }
  return result;
}

// ==================== 完全求值 ====================
// 先代入，再要求结果化为常数；只要还有变量残留就说明存在未绑定变量。

inline Result<Fraction> Monomial::evaluate(const Scope &scope) const {
  const Monomial substituted = substitute(scope);
  if (!substituted.isConstant()) {
    return std::unexpected(MathsError::UndefinedVariable);
  }
  return substituted.getCoefficient();
}

inline Result<Fraction> Polynomial::evaluate(const Scope &scope) const {
  const Result<Monomial> monomial = substitute(scope).toMonomial();
  // 化简后仍不止一项 → 有变量没绑定；
  // 只剩一项但仍是变量（如 x + y 只绑定了 x 时的 y）→ 同样未绑定
  if (monomial.isErr() || !monomial.unwrap().isConstant()) {
    return std::unexpected(MathsError::UndefinedVariable);
  }
  return monomial.unwrap().getCoefficient();
}

#endif // MATHS_SCOPE_HPP
