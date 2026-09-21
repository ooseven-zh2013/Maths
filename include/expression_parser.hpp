#pragma once
#ifndef MATHS_EXPRESSION_PARSER_HPP
#define MATHS_EXPRESSION_PARSER_HPP

// 简易表达式解析器：把文本解析成 RationalFunction。
//
// 支持的语法：
//   expr    := term (('+' | '-') term)*
//   term    := power (('*' | '/') power)*
//   power   := unary ('^' 非负整数)?
//   unary   := ('-' | '+')? primary
//   primary := 整数 | 变量名 | '(' expr ')'
//
// 结果统一用 RationalFunction 承载：除法必然引入分式，用统一类型可以省掉
// 「多项式还是分式」的分支判断。
//
// 另外提供 parseAssignment：解析代入条件 "变量 = 常数"。
// 支持两侧交换（C = x 等价于 x = C），恒等式（x = x、C = C）返回 nullopt 表示无需记录。
// 刻意不支持需要解方程的形式（如 x + 1 = 2），这类输入会明确报错而不是猜。

#include "algebraic_expression.hpp"
#include "maths_error.hpp"
#include "numbers.hpp"
#include "rational_function.hpp"
#include "result.hpp"
#include "scope.hpp"

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace expression_detail {

class Parser {
public:
  explicit Parser(std::string_view source) : text(source) {}

  Result<RationalFunction> parse() {
    skipSpaces();
    if (atEnd()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    Result<RationalFunction> value = parseAdditive();
    if (value.isErr()) {
      return value;
    }
    skipSpaces();
    if (!atEnd()) {
      return std::unexpected(MathsError::InvalidExpression); // 存在无法消费的残留字符
    }
    return value;
  }

private:
  bool atEnd() const { return position >= text.size(); }
  char peek() const { return atEnd() ? '\0' : text[position]; }
  bool isDigit() const { return !atEnd() && std::isdigit(static_cast<unsigned char>(peek())) != 0; }
  bool isAlpha() const { return !atEnd() && std::isalpha(static_cast<unsigned char>(peek())) != 0; }

  void skipSpaces() {
    while (!atEnd() && std::isspace(static_cast<unsigned char>(text[position])) != 0) {
      ++position;
    }
  }

  Result<RationalFunction> parseAdditive() {
    Result<RationalFunction> left = parseMultiplicative();
    if (left.isErr()) {
      return left;
    }
    while (true) {
      skipSpaces();
      const char operation = peek();
      if (operation != '+' && operation != '-') {
        return left;
      }
      ++position;
      Result<RationalFunction> right = parseMultiplicative();
      if (right.isErr()) {
        return right;
      }
      left = (operation == '+') ? left.unwrap() + right.unwrap() : left.unwrap() - right.unwrap();
    }
  }

  Result<RationalFunction> parseMultiplicative() {
    Result<RationalFunction> left = parsePower();
    if (left.isErr()) {
      return left;
    }
    while (true) {
      skipSpaces();
      const char operation = peek();
      if (operation != '*' && operation != '/') {
        return left;
      }
      ++position;
      Result<RationalFunction> right = parsePower();
      if (right.isErr()) {
        return right;
      }
      if (operation == '*') {
        left = left.unwrap() * right.unwrap();
        continue;
      }
      Result<RationalFunction> quotient = left.unwrap() / right.unwrap();
      if (quotient.isErr()) {
        return quotient;
      }
      left = quotient;
    }
  }

  Result<RationalFunction> parsePower() {
    Result<RationalFunction> base = parseUnary();
    if (base.isErr()) {
      return base;
    }
    skipSpaces();
    if (peek() != '^') {
      return base;
    }
    ++position;
    Result<unsigned long long> exponent = parseUnsignedInteger();
    if (exponent.isErr()) {
      return std::unexpected(exponent.unwrapErr());
    }
    return powerOf(base.unwrap(), exponent.unwrap());
  }

  Result<RationalFunction> parseUnary() {
    skipSpaces();
    if (peek() == '-') {
      ++position;
      Result<RationalFunction> operand = parseUnary();
      if (operand.isErr()) {
        return operand;
      }
      return -operand.unwrap();
    }
    if (peek() == '+') {
      ++position;
      return parseUnary();
    }
    return parsePrimary();
  }

  Result<RationalFunction> parsePrimary() {
    skipSpaces();
    if (atEnd()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    if (peek() == '(') {
      ++position;
      Result<RationalFunction> inner = parseAdditive();
      if (inner.isErr()) {
        return inner;
      }
      skipSpaces();
      if (peek() != ')') {
        return std::unexpected(MathsError::InvalidExpression);
      }
      ++position;
      return inner;
    }
    if (isDigit()) {
      return parseNumber();
    }
    if (isAlpha()) {
      return parseVariable();
    }
    return std::unexpected(MathsError::InvalidExpression);
  }

  Result<RationalFunction> parseNumber() {
    const size_t start = position;
    while (isDigit()) {
      ++position;
    }
    try {
      const long long value = std::stoll(std::string(text.substr(start, position - start)));
      return RationalFunction(Monomial(Fraction(value, 1LL)));
    } catch (const std::exception &) {
      return std::unexpected(MathsError::InvalidExpression);
    }
  }

  Result<RationalFunction> parseVariable() {
    const size_t start = position;
    while (!atEnd() && (std::isalnum(static_cast<unsigned char>(peek())) != 0 || peek() == '_')) {
      ++position;
    }
    const std::string name(text.substr(start, position - start));
    try {
      const Variable variable(name);
      return RationalFunction(Monomial(Fraction(1, 1), {{variable, 1ULL}}));
    } catch (const MathsException &error) {
      return std::unexpected(error.code());
    }
  }

  Result<unsigned long long> parseUnsignedInteger() {
    skipSpaces();
    if (!isDigit()) {
      return std::unexpected(MathsError::InvalidExpression);
    }
    const size_t start = position;
    while (isDigit()) {
      ++position;
    }
    try {
      return static_cast<unsigned long long>(std::stoull(std::string(text.substr(start, position - start))));
    } catch (const std::exception &) {
      return std::unexpected(MathsError::InvalidExpression);
    }
  }

  // 幂用重复乘法实现：RationalFunction 未提供 pow
  static Result<RationalFunction> powerOf(const RationalFunction &base, unsigned long long exponent) {
    RationalFunction result(Fraction(1, 1));
    for (unsigned long long i = 0; i < exponent; ++i) {
      result = result * base;
    }
    return result;
  }

  std::string_view text;
  size_t position{0};
};

// 是否为「恰好等于某个变量」的表达式，要求系数为 1、指数为 1、且分母为 1
inline std::optional<Variable> asSingleVariable(const RationalFunction &value) {
  const Result<Monomial> denominator = value.getDenominator().toMonomial();
  if (denominator.isErr() || !denominator.unwrap().isConstant() || denominator.unwrap().getCoefficient() != 1LL) {
    return std::nullopt;
  }

  const Result<Monomial> numerator = value.getNumerator().toMonomial();
  if (numerator.isErr() || numerator.unwrap().getCoefficient() != 1LL) {
    return std::nullopt;
  }

  const VarPowers &factors = numerator.unwrap().getFactors();
  if (factors.size() != 1 || factors[0].second != 1) {
    return std::nullopt;
  }
  return factors[0].first;
}

// 是否为常数（空 Scope 下可求值即说明不含变量）
inline std::optional<Fraction> asConstant(const RationalFunction &value) {
  const Result<Fraction> evaluated = value.evaluate(Scope());
  if (evaluated.isErr()) {
    return std::nullopt;
  }
  return evaluated.unwrap();
}

} // namespace expression_detail

// 解析表达式，失败返回 MathsError::InvalidExpression
inline Result<RationalFunction> parseExpression(std::string_view text) {
  return expression_detail::Parser(text).parse();
}

struct Assignment {
  Variable variable;
  Fraction value;
};

// 解析代入条件 "变量 = 常数"。
// 返回 nullopt 表示这是恒等式（x = x、C = C），没有信息可记录。
// 两侧可交换：C = x 会被解释为 x = C。
// 需要解方程的形式（x + 1 = 2）不被支持，返回 InvalidExpression。
inline Result<std::optional<Assignment>> parseAssignment(std::string_view text) {
  const size_t equals = text.find('=');
  if (equals == std::string_view::npos) {
    return std::unexpected(MathsError::InvalidExpression);
  }
  if (text.find('=', equals + 1) != std::string_view::npos) {
    return std::unexpected(MathsError::InvalidExpression); // 不支持 == 这类写法
  }

  Result<RationalFunction> leftHand = parseExpression(text.substr(0, equals));
  if (leftHand.isErr()) {
    return std::unexpected(leftHand.unwrapErr());
  }
  Result<RationalFunction> rightHand = parseExpression(text.substr(equals + 1));
  if (rightHand.isErr()) {
    return std::unexpected(rightHand.unwrapErr());
  }

  const std::optional<Variable> leftVariable = expression_detail::asSingleVariable(leftHand.unwrap());
  const std::optional<Variable> rightVariable = expression_detail::asSingleVariable(rightHand.unwrap());
  const std::optional<Fraction> leftConstant = expression_detail::asConstant(leftHand.unwrap());
  const std::optional<Fraction> rightConstant = expression_detail::asConstant(rightHand.unwrap());

  // x = x：同一个变量，恒等式
  if (leftVariable && rightVariable && *leftVariable == *rightVariable) {
    return std::optional<Assignment>{};
  }

  // C = C：同一个常数，恒等式
  if (leftConstant && rightConstant && *leftConstant == *rightConstant) {
    return std::optional<Assignment>{};
  }

  if (leftVariable && rightConstant) {
    return std::optional<Assignment>{Assignment{*leftVariable, *rightConstant}};
  }

  // C = x 交换成 x = C
  if (rightVariable && leftConstant) {
    return std::optional<Assignment>{Assignment{*rightVariable, *leftConstant}};
  }

  return std::unexpected(MathsError::InvalidExpression);
}

#endif // MATHS_EXPRESSION_PARSER_HPP
