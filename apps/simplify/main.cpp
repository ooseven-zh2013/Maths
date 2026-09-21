// 交互式表达式化简程序。
//
// 用法：输入一个式子，然后逐条输入代入条件（变量 = 常数），
// 输入 0=0 结束，程序给出代入化简后的结果。

#include "expression_parser.hpp"

#include <cctype>
#include <iostream>
#include <string>
#include <string_view>

namespace {

// 去掉全部空白，用于识别终止哨兵 0=0
std::string stripSpaces(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (char character : text) {
    if (std::isspace(static_cast<unsigned char>(character)) == 0) {
      result += character;
    }
  }
  return result;
}

} // namespace

int main() {
  std::cout << "=== 表达式化简 ===\n";
  std::cout << "支持的运算: + - * / ^ 与括号；变量名可含字母、数字、下划线\n\n";

  std::cout << "式子: ";
  std::string expression;
  if (!std::getline(std::cin, expression)) {
    return 0;
  }

  const Result<RationalFunction> parsed = parseExpression(expression);
  if (parsed.isErr()) {
    std::cout << "解析失败: " << describe(parsed.unwrapErr()) << '\n';
    return 1;
  }
  std::cout << "解析为: " << parsed.unwrap().str() << '\n';

  Scope scope;
  std::cout << "\n条件（变量 = 常数，输入 0=0 结束）:\n";
  while (true) {
    std::cout << "> ";
    std::string line;
    if (!std::getline(std::cin, line)) {
      std::cout << '\n';
      break;
    }

    const std::string trimmed = stripSpaces(line);
    if (trimmed == "0=0") {
      break;
    }
    if (trimmed.empty()) {
      continue;
    }

    const Result<std::optional<Assignment>> assignment = parseAssignment(line);
    if (assignment.isErr()) {
      std::cout << "  忽略: 只接受「变量 = 常数」形式（" << describe(assignment.unwrapErr()) << "）\n";
      continue;
    }
    if (!assignment.unwrap().has_value()) {
      std::cout << "  恒等式，无需记录\n";
      continue;
    }

    scope.assign(assignment.unwrap()->variable, assignment.unwrap()->value);
    std::cout << "  当前条件: " << scope.str() << '\n';
  }

  std::cout << "\n--- 结果 ---\n";

  const Result<RationalFunction> substituted = parsed.unwrap().substitute(scope);
  if (substituted.isErr()) {
    std::cout << "代入失败: " << describe(substituted.unwrapErr()) << '\n';
    return 1;
  }

  const RationalFunction &result = substituted.unwrap();
  std::cout << "化简结果: " << result.str() << '\n';

  const Result<Polynomial> polynomial = result.toPolynomial();
  if (polynomial.isOk()) {
    std::cout << "可化为多项式: " << polynomial.unwrap().str() << '\n';
  }

  const Result<Fraction> evaluated = result.evaluate(scope);
  if (evaluated.isOk()) {
    std::cout << "常数结果: " << evaluated.unwrap() << '\n';
  }

  if (!result.discardedConstraints().empty()) {
    std::cout << "注意: 化简中约去了";
    for (const Variable &variable : result.discardedConstraints()) {
      std::cout << ' ' << variable.str();
    }
    std::cout << "，上述等价关系仅在这些变量非零时成立\n";
  }

  return 0;
}
