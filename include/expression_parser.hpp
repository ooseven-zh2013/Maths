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
// 另外提供 parseAssignment：解析代入条件 "变量 = 表达式"。
// 支持两侧交换（C = x 等价于 x = C）；常数恒等式（C = C）返回 nullopt 表示无需记录。
// 刻意不支持需要解方程的形式（如 x + 1 = 2），这类输入会明确报错而不是猜。
//
// 另有 parseErase：识别「解除绑定」的输入 x = x —— 意思是删掉该变量此前记录的约束。
// 删除与赋值是两种不同的动作，所以单列一个函数而不是塞进 Assignment；
// 调用方应先试 parseErase，落空再走 parseAssignment。

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
  RationalFunction value; // 右边可以是含其它变量的表达式，如 s = v*t
};

// 解析代入条件 "变量 = 表达式"。
// 返回 nullopt 表示这条输入没有可记录的信息（常数恒等式）。
// 两侧可交换：C = x 会被解释为 x = C。
// 需要解方程的形式（x + 1 = 2）不被支持，返回 InvalidExpression。
// x = x 不是赋值而是「解除该变量的绑定」，见 parseErase。
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

  const RationalFunction &left = leftHand.unwrap();
  const RationalFunction &right = rightHand.unwrap();

  const std::optional<Variable> leftVariable = expression_detail::asSingleVariable(left);
  const std::optional<Variable> rightVariable = expression_detail::asSingleVariable(right);
  const std::optional<Fraction> leftConstant = expression_detail::asConstant(left);
  const std::optional<Fraction> rightConstant = expression_detail::asConstant(right);

  // x = x：同一个变量。解除绑定由 parseErase 负责，这里按「没有可记录的信息」处理
  if (leftVariable && rightVariable && *leftVariable == *rightVariable) {
    return std::optional<Assignment>{};
  }

  // C = C：同一个常数，恒等式
  if (leftConstant && rightConstant && *leftConstant == *rightConstant) {
    return std::optional<Assignment>{};
  }

  // 变量 = 表达式。右边不得含被赋值的变量本身 —— 那是方程而非赋值
  // （x = 2x、x = x + 1 都需要解方程，明确不支持）。
  if (leftVariable) {
    if (right.containsVariable(*leftVariable)) {
      return std::unexpected(MathsError::NotAnAssignment);
    }
    return std::optional<Assignment>{Assignment{*leftVariable, right}};
  }

  // 表达式 = 变量：只要左边不含该变量，就能把变量解到右边（vt = x 即 x = vt，
  // x + 1 = y 即 y = x + 1 —— y 单独在一侧，直接读出，不算解方程）。
  // 左边含它的话（如 x + 1 = x）才是真正的方程，明确不支持。
  if (rightVariable && !left.containsVariable(*rightVariable)) {
    return std::optional<Assignment>{Assignment{*rightVariable, left}};
  }

  return std::unexpected(MathsError::InvalidExpression);
}

// 解析「解除绑定」的输入：x = x 表示删掉变量 x 此前记录的约束（y = y、x_1 = x_1 同理）。
// 返回 nullopt 表示这条输入不是删除指令，调用方应继续按赋值解析。
//
// 与常数恒等式（5 = 5）区分：后者两边是同一个常数，不涉及变量，不是删除指令。
// 解析失败的输入（如 x + 1 = 2）也返回 nullopt —— 报错交给 parseAssignment，
// 那里能给出准确的错误码，这里不该抢先判定。
inline std::optional<Variable> parseErase(std::string_view text) {
  const size_t equals = text.find('=');
  if (equals == std::string_view::npos || text.find('=', equals + 1) != std::string_view::npos) {
    return std::nullopt;
  }

  const Result<RationalFunction> leftHand = parseExpression(text.substr(0, equals));
  const Result<RationalFunction> rightHand = parseExpression(text.substr(equals + 1));
  if (leftHand.isErr() || rightHand.isErr()) {
    return std::nullopt;
  }

  const std::optional<Variable> leftVariable = expression_detail::asSingleVariable(leftHand.unwrap());
  const std::optional<Variable> rightVariable = expression_detail::asSingleVariable(rightHand.unwrap());
  if (leftVariable && rightVariable && *leftVariable == *rightVariable) {
    return *leftVariable;
  }
  return std::nullopt;
}

// 约束是否与式子相关：左边变量出现在式子里，或右边含式子里出现过的变量。
// 两者都不成立时，这条约束对式子毫无影响，记录它没有意义
// （例如式子 2x 配上 s = v*t：s、v、t 都不影响 2x）。
// 约束是否与「式子 + 已有绑定」相关。
// 只看原始式子是不够的：式子 2x 在 x = s 之后，s = v*t 就会影响结果
// —— 因为 x 的有效值已经变成了 s。
inline bool isRelevantTo(const RationalFunction &expression, const Scope &scope, const Assignment &assignment) {
  // 1. 被赋值的变量直接出现在式子里
  if (expression.containsVariable(assignment.variable)) {
    return true;
  }
  // 2. 被赋值的变量出现在某条已有绑定的值里 —— 那条绑定的有效值会变
  for (const auto &entry : scope.bindings()) {
    if (entry.second.containsVariable(assignment.variable)) {
      return true;
    }
  }
  // 3. 右边含式子里出现的变量
  for (const Variable &variable : expression.variables()) {
    if (assignment.value.containsVariable(variable)) {
      return true;
    }
  }
  // 4. 右边含某条已有绑定的变量名 —— 代入链会继续展开
  for (const auto &entry : scope.bindings()) {
    if (assignment.value.containsVariable(entry.first)) {
      return true;
    }
  }
  return false;
}

#endif // MATHS_EXPRESSION_PARSER_HPP
