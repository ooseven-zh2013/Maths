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

// 读取一个花括号分组（允许前面有空白），position 停在 } 之后
inline bool takeBracedGroup(std::string_view source, size_t &position, std::string &out) {
  while (position < source.size() && std::isspace(static_cast<unsigned char>(source[position])) != 0) {
    ++position;
  }
  if (position >= source.size() || source[position] != '{') {
    return false;
  }

  int depth = 0;
  const size_t start = position + 1;
  while (position < source.size()) {
    if (source[position] == '{') {
      ++depth;
    } else if (source[position] == '}') {
      --depth;
      if (depth == 0) {
        out = std::string(source.substr(start, position - start));
        ++position;
        return true;
      }
    }
    ++position;
  }
  return false;
}

// 把 LaTeX 写法规范化成解析器能直接处理的普通写法：
//   \frac{a}{b} → (a)/(b)   （支持嵌套）
//   \cdot、\times → *      \div → /
//   x^{2} → x^2
//   \left、\right → 忽略
// 其余字符原样保留。这样解析器本身只需处理一种语法。
inline std::string normalizeLatex(std::string_view source) {
  std::string result;
  result.reserve(source.size());
  size_t position = 0;

  while (position < source.size()) {
    if (source.compare(position, 5, "\\frac") == 0) {
      position += 5;
      std::string numerator;
      std::string denominator;
      if (!takeBracedGroup(source, position, numerator) || !takeBracedGroup(source, position, denominator)) {
        result += "\\frac"; // 结构不完整，原样保留，交给解析器报错
        continue;
      }
      result += '(';
      result += normalizeLatex(numerator);
      result += ")/(";
      result += normalizeLatex(denominator);
      result += ')';
      continue;
    }
    if (source.compare(position, 5, "\\cdot") == 0) {
      result += '*';
      position += 5;
      continue;
    }
    if (source.compare(position, 6, "\\times") == 0) {
      result += '*';
      position += 6;
      continue;
    }
    if (source.compare(position, 4, "\\div") == 0) {
      result += '/';
      position += 4;
      continue;
    }
    if (source.compare(position, 5, "\\left") == 0) {
      position += 5;
      continue;
    }
    if (source.compare(position, 6, "\\right") == 0) {
      position += 6;
      continue;
    }
    if (source.compare(position, 2, "^{") == 0) { // "^{" 只有 2 个字符
      // 指数只接受整数，因此直接把花括号展开
      const size_t close = source.find('}', position + 2);
      if (close != std::string_view::npos) {
        result += '^';
        result.append(source.substr(position + 2, close - position - 2));
        position = close + 1;
        continue;
      }
    }
    result += source[position];
    ++position;
  }
  return result;
}

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

  // 下一个位置能否开始一个因子 —— 用于识别隐含乘法
  bool startsPrimary() const {
    if (atEnd()) {
      return false;
    }
    const char character = peek();
    return std::isalpha(static_cast<unsigned char>(character)) != 0 ||
           std::isdigit(static_cast<unsigned char>(character)) != 0 || character == '(' || character == '{' ||
           character == '\\';
  }

  Result<RationalFunction> parseMultiplicative() {
    Result<RationalFunction> left = parsePower();
    if (left.isErr()) {
      return left;
    }
    while (true) {
      skipSpaces();
      const char operation = peek();

      // 隐含乘法：数学书写里 xy 就是 x*y，2x 就是 2*x。
      // 没有这一条的话，xy 会被 parseVariable 吞成一个变量名。
      if (operation != '*' && operation != '/' && !startsPrimary()) {
        return left;
      }
      if (operation == '*' || operation == '/') {
        ++position;
      }

      Result<RationalFunction> right = parsePower();
      if (right.isErr()) {
        return right;
      }
      if (operation == '/') {
        Result<RationalFunction> quotient = left.unwrap() / right.unwrap();
        if (quotient.isErr()) {
          return quotient;
        }
        left = quotient;
        continue;
      }
      left = left.unwrap() * right.unwrap();
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
    if (peek() == '{') {
      return parseBracedVariable();
    }
    if (isDigit()) {
      return parseNumber();
    }
    if (isAlpha()) {
      return parseVariable();
    }
    return std::unexpected(MathsError::InvalidExpression);
  }

  // {name} 一次性声明多字母变量名，可跟下标：{node}_{car}。
  // 不用花括号的话，连续的字母按隐含乘法拆开（node 即 n*o*d*e）。
  Result<RationalFunction> parseBracedVariable() {
    std::string name;
    if (!takeBracedGroup(text, position, name) || name.empty()) {
      return std::unexpected(MathsError::InvalidExpression);
    }

    if (!atEnd() && peek() == '_') {
      ++position;
      if (!atEnd() && peek() == '{') {
        std::string index;
        if (!takeBracedGroup(text, position, index)) {
          return std::unexpected(MathsError::InvalidExpression);
        }
        name += "_{" + index + "}";
      } else if (!atEnd()) {
        name += '_';
        name += peek();
        ++position;
      }
    }

    try {
      const Variable variable(name);
      return RationalFunction(Monomial(Fraction(1, 1), {{variable, 1ULL}}));
    } catch (const MathsException &error) {
      return std::unexpected(error.code());
    }
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
    // 变量名 = 单个字母 + 可选下标（x、a_1、x_{i,j}）。
    // 连续字母不合并成一个名字，而是留给 parseMultiplicative 做隐含乘法：xy 即 x*y。
    // 这样「输入 xy」与「输入 x*y」得到同一个式子，也与 latex() 的输出闭环。
    const size_t start = position;
    ++position; // 调用方已确认首字符是字母

    if (!atEnd() && peek() == '_') {
      ++position;
      if (!atEnd() && peek() == '{') {
        int depth = 0;
        while (!atEnd()) {
          if (peek() == '{') {
            ++depth;
          } else if (peek() == '}') {
            --depth;
            if (depth == 0) {
              ++position;
              break;
            }
          }
          ++position;
        }
      } else if (!atEnd()) {
        ++position; // 单字符下标
      }
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

// 解析表达式：先做 LaTeX 规范化，再交给解析器。
// 失败返回 MathsError::InvalidExpression
inline Result<RationalFunction> parseExpression(std::string_view text) {
  // normalized 的生命周期覆盖整个 Parser 调用，Parser 持有的 string_view 不会悬垂
  const std::string normalized = expression_detail::normalizeLatex(text);
  return expression_detail::Parser(normalized).parse();
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
