// 交互式表达式化简程序。
//
// 用法：输入一个式子（普通写法或 LaTeX 写法均可），
// 然后逐条输入代入条件（变量 = 表达式），输入 0=0 结束。
//
// ===========================================================================
// 输出格式约定 —— 新增提示一律沿用这几种行式，不要另起一套
// ===========================================================================
//
//   字段行   名字: 值            说明与结果。顶格；从属于上一行时缩两格
//   列表行   值                  字段行下面的一组同构项，缩两格
//   反馈行   动作 · 说明         回应一次输入，固定缩两格
//   输入提示 式子>  /  条件>
//
//   反馈行的动作词只有五个，不要自造：
//     已记录   输入被采纳，记下一条约束
//     已删除   输入被采纳，删掉一条已有约束
//     跳过     输入合法，但没有可记录的信息
//     不接受   输入无法采纳，后面接原因
//     提示     补充说明，跟在上面任意一条之后
//
// 为什么要定这个：早先是「加一个功能就加一句提示」，结果措辞、缩进、标点
// 各不相同（"忽略: xxx"、"恒等式，无需记录"、"与式子无关，不记录" 三种说法
// 其实是同一件事）。现在收敛成上面几种行式，并由 printField / printListItem /
// printFeedback 三个函数统一产出格式 —— 新增输出只要挑对行式就不会跑偏。
//
// ===========================================================================

#include "expression_parser.hpp"

#include <cctype>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

// ---------------- 统一的行式产出函数 ----------------

// 字段行：`名字: 值`。indented 为真时整行缩两格，表示从属于上一行。
template <typename Value> void printField(std::string_view name, const Value &value, bool indented = false) {
  std::cout << (indented ? "  " : "") << name << ": " << value << '\n';
}

// 字段行的小节标题：只有名字，值另起若干行（列表行或缩进字段行）
void printSection(std::string_view name) { std::cout << name << ":\n"; }

// 列表行：字段行下面的一组同构项
void printListItem(std::string_view value) { std::cout << "  " << value << '\n'; }

// 反馈行：`  动作 · 说明`。动作词见文件头，只有四个。
void printFeedback(std::string_view action, std::string_view detail) {
  std::cout << "  " << action << " · " << detail << '\n';
}

// ---------------- 输入 ----------------

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
    std::cout << "式子> ";
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
      printField("解析为", out.latex(), true);
      return true;
    }

    // 解析失败不退出，让用户有机会改
    printFeedback("不接受", describe(parsed.unwrapErr()));
    printFeedback("提示", "语法见开头；普通写法与 LaTeX 写法都接受");
  }
}

// 打印语法说明。一次性给全，后面不再零散补充
void printSyntax() {
  printSection("语法");
  printField("运算", "+ - * / ^ 与括号", true);
  printField("变量", "单个字母可带下标 —— x、a_1、x_{i,j}", true);
  printField("长名", "多字母变量加花括号 —— {node}、{node}_{car}", true);
  printField("乘法", "可省略 —— xy 即 x*y，2x 即 2*x（所以 {node} 不写花括号会变成 n*o*d*e）", true);
  printField("写法", "普通写法与 LaTeX 写法都接受 —— \\frac{a}{b}、\\cdot、\\times、\\div、x^{2}", true);
}

// 打印条件输入的说明
void printConstraintHelp() {
  printSection("条件");
  printField("写法", "变量 = 表达式，如 x = 2、s = v*t（右边可含式子里没有的变量）", true);
  printField("删除", "输入 x = x 删掉变量 x 的约束（重复输入同名变量即为覆盖）", true);
  printField("结束", "输入 0=0", true);
  printField("限制", "右边不能含被赋值的变量本身 —— x = 2x 是方程，不支持", true);
}

// 逐条读取条件，成功则记入 scope
void readConstraints(const RationalFunction &expression, Scope &scope) {
  while (true) {
    std::cout << "条件> ";
    std::string line;
    if (!readLine(line)) {
      std::cout << '\n';
      return;
    }

    const std::string trimmed = stripSpaces(line);
    if (trimmed == "0=0") {
      return;
    }
    if (trimmed.empty()) {
      continue;
    }

    // x = x 是删除指令，优先于赋值解析 —— 否则它会被当成恒等式丢掉
    const std::optional<Variable> erased = parseErase(line);
    if (erased) {
      if (scope.erase(*erased)) {
        printFeedback("已删除", erased->str() + " 的约束");
      } else {
        printFeedback("跳过", erased->str() + " 本来就没有约束");
      }
      continue;
    }

    const Result<std::optional<Assignment>> assignment = parseAssignment(line);
    if (assignment.isErr()) {
      printFeedback("不接受", describe(assignment.unwrapErr()));
      continue;
    }
    if (!assignment.unwrap().has_value()) {
      printFeedback("跳过", "恒等式，没有可记录的信息");
      continue;
    }

    const Assignment &entry = *assignment.unwrap();

    // 左右两边都与「式子和已有绑定」无关时记录它没有意义
    if (!isRelevantTo(expression, scope, entry)) {
      printFeedback("跳过", "与式子和已有条件都无关");
      continue;
    }

    // 赋值可能被拒（右边含变量自身时属于方程，不支持）
    const Result<void> assigned = scope.assign(entry.variable, entry.value);
    if (assigned.isErr()) {
      printFeedback("不接受", describe(assigned.unwrapErr()));
      continue;
    }
    printFeedback("已记录", entry.variable.str() + " = " + entry.value.latex());
  }
}

// 打印代入化简的结果
void printResult(const RationalFunction &expression, const Scope &scope) {
  printField("式子", expression.latex());

  printSection("条件");
  if (scope.empty()) {
    printListItem("（无）");
  } else {
    for (const auto &[variable, value] : scope.bindings()) {
      printListItem(variable.str() + " = " + value.latex());
    }
  }

  const Result<RationalFunction> substituted = expression.substitute(scope);
  if (substituted.isErr()) {
    // 代入后分母为零属于数学结论（原式在该点无定义），不是程序错误，
    // 因此正常结束而不是返回非零退出码。
    printField("无法代入", std::string(describe(substituted.unwrapErr())) + "（原式在这些取值处无定义）");
    return;
  }

  const RationalFunction &result = substituted.unwrap();
  printField("化简结果", result.latex());

  // 化简结果本身已是多项式形式（分母为 1）时不重复报告，
  // 只有分母非 1 但能被长除法整除时（如 (a^2-1)/(a+1) → a-1）才单独给出。
  // 这种归约会丢掉「原式在分母零点处无定义」这一信息 —— 归约后的式子在那里有定义，
  // 原式没有 —— 所以必须把定义域条件一并报出。
  const Result<Monomial> denominatorMonomial = result.getDenominator().toMonomial();
  const bool denominatorIsConstant = denominatorMonomial.isOk() && denominatorMonomial.unwrap().isConstant();
  const bool denominatorIsOne = denominatorIsConstant && denominatorMonomial.unwrap().getCoefficient() == 1LL;

  const Result<Polynomial> polynomial = result.toPolynomial();
  if (polynomial.isOk() && !denominatorIsOne) {
    std::string text = polynomial.unwrap().latex();
    if (!denominatorIsConstant) { // 常数分母恒非零，无需附加条件
      text += "（原式要求 " + result.getDenominator().latex() + " \\neq 0）";
    }
    printField("可化为多项式", text);
  }

  const Result<Fraction> evaluated = result.evaluate(scope);
  if (evaluated.isOk()) {
    printField("常数结果", evaluated.unwrap());
  }

  if (!result.discardedConstraints().empty()) {
    std::string names;
    for (const Variable &variable : result.discardedConstraints()) {
      if (!names.empty()) {
        names += ' ';
      }
      names += variable.str();
    }
    printField("注意", "化简中约去了 " + names + "，上述等价关系仅在这些变量非零时成立");
  }
}

} // namespace

int main() {
  std::cout << "=== 表达式化简 ===\n\n";

  printSyntax();
  std::cout << '\n';

  RationalFunction expression(Fraction(0, 1));
  if (!readExpression(expression)) {
    return 0;
  }

  Scope scope;
  std::cout << '\n';
  printConstraintHelp();
  readConstraints(expression, scope);

  std::cout << "\n--- 结果 ---\n";
  printResult(expression, scope);

  // 双击运行时窗口不会立刻关闭
  std::cout << "\n按回车键退出...";
  std::string ignored;
  std::getline(std::cin, ignored);

  return 0;
}
