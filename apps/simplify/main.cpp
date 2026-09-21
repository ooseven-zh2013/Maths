// 交互式表达式化简程序。
//
// 用法：输入一个式子（普通写法或 LaTeX 写法均可），
// 然后逐条输入代入条件（变量 = 常数），输入 0=0 结束。

#include "expression_parser.hpp"

#include <cctype>
#include <iostream>
#include <optional>
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

// 读取一行；输入流结束（EOF、或管道里的内容读完）时返回 false
bool readLine(std::string &out) { return static_cast<bool>(std::getline(std::cin, out)); }

// 反复索取式子，直到解析成功；输入流结束则返回 false
bool readExpression(RationalFunction &out) {
  while (true) {
    std::cout << "式子: ";
    std::string line;
    if (!readLine(line)) {
      std::cout << '\n';
      return false;
    }
    if (stripSpaces(line).empty()) {
      continue;
    }

    const Result<RationalFunction> parsed = parseExpression(line);
    if (parsed.isOk()) {
      out = parsed.unwrap();
      std::cout << "解析为: " << out.latex() << '\n';
      return true;
    }

    // 解析失败不退出，让用户有机会改
    std::cout << "  解析失败: " << describe(parsed.unwrapErr()) << '\n';
    std::cout << "  支持的写法示例: (x+1)/(x-1) 、 x^2 + 2*x + 1 、 \\frac{x+1}{x-1}\n";
  }
}

} // namespace

int main() {
  std::cout << "=== 表达式化简 ===\n";
  std::cout << "运算: + - * / ^ 与括号\n";
  std::cout << "变量名: 单个字母可带下标 —— x、a_1、x_{i,j}\n";
  std::cout << "多字母变量名用花括号: {node}、{node}_{car}\n";
  std::cout << "乘法可省略: xy 即 x*y，2x 即 2*x（所以 {node} 不加花括号会变成 n*o*d*e）\n";
  std::cout << "LaTeX 写法同样接受: \\frac{a}{b}、\\cdot、\\times、\\div、x^{2}\n\n";

  RationalFunction expression(Fraction(0, 1));
  if (!readExpression(expression)) {
    return 0;
  }

  Scope scope;
  std::cout << "\n条件（变量 = 表达式，输入 0=0 结束）:\n";
  std::cout << "  右边可用式子里没有的变量，如 s = v*t；但不能含被赋值的变量本身（那是方程）\n";
  while (true) {
    std::cout << "> ";
    std::string line;
    if (!readLine(line)) {
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
      std::cout << "  忽略: " << describe(assignment.unwrapErr()) << '\n';
      continue;
    }
    if (!assignment.unwrap().has_value()) {
      std::cout << "  恒等式，无需记录\n";
      continue;
    }

    // 赋值可能被拒（右边含变量自身时属于方程，不支持）
    const Result<void> assigned = scope.assign(assignment.unwrap()->variable, assignment.unwrap()->value);
    if (assigned.isErr()) {
      std::cout << "  不接受: " << describe(assigned.unwrapErr()) << '\n';
      continue;
    }
    std::cout << "  当前条件: " << scope.str() << '\n';
  }

  std::cout << "\n--- 结果 ---\n";
  std::cout << "原始式子: " << expression.latex() << '\n';

  const Result<RationalFunction> substituted = expression.substitute(scope);
  if (substituted.isErr()) {
    // 代入后分母为零属于数学结论（原式在该点无定义），不是程序错误，
    // 因此正常结束而不是返回非零退出码。
    std::cout << "无法代入: " << describe(substituted.unwrapErr()) << "（原式在这些取值处无定义）\n";
  } else {
    const RationalFunction &result = substituted.unwrap();
    std::cout << "化简结果: " << result.latex() << '\n';

    const Result<Polynomial> polynomial = result.toPolynomial();
    if (polynomial.isOk()) {
      std::cout << "可化为多项式: " << polynomial.unwrap().latex() << '\n';
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
  }

  // 双击运行时窗口不会立刻关闭
  std::cout << "\n按回车键退出...";
  std::string ignored;
  std::getline(std::cin, ignored);

  return 0;
}
