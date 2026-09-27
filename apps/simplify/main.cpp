// 交互式表达式化简程序。
//
// 用法：输入一个式子（普通写法或 LaTeX 写法均可），
// 然后逐条输入代入条件（变量 = 表达式），输入 0=0 结束。
// 式子不含变量时（纯常数运算）跳过条件输入直接出结果，可以当计算器用。
//
// 关于根号：
//   - **只认 LaTeX 写法** `\sqrt{2}`、`\sqrt[3]{2}`，不认 √ 符号（√ 的结束位置
//     没有公认约定，只有花括号能定死边界）
//   - 纯数值式子里的根号走代数数，直接给精确值
//   - 条件右边同样可以写根号（x = \sqrt{2}）；此时整个代入过程提升到 ℚ(α) 上算，
//     结果依然是精确的
//   - 式子里**含变量又含根号**暂不支持（解析器还没有代数版）
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

#include <cctype>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

import maths;

using namespace maths;

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

// 反馈行：`  动作 · 说明`。动作词见文件头，只有五个。
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

// 只去首尾空白，保留内部的写法（回显原始输入时用）
std::string trim(std::string_view text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

// 读取一行；输入流结束（EOF、或管道里的内容读完）时返回 false
bool readLine(std::string &out) { return static_cast<bool>(std::getline(std::cin, out)); }

// ---------------- 代数数 ----------------

// 把代数数渲染成 LaTeX 根式：
//   有理数                   → 分数
//   最小多项式是 a·x^n + c   → ±\sqrt[n]{-c/a}
//   其余（如 \sqrt{2}+\sqrt{3}）→ 退回 RealAlgebraicNumber 自带的 RootOf 记法
//
// 刻意不输出 √ 符号：√ 后面到哪里结束没有公认约定（√2x 是 √(2x) 还是 (√2)·x？），
// 而 \sqrt{…} 的边界由花括号定死。解析侧也只认这一种写法。
std::string radicalLatex(const RealAlgebraicNumber &value) {
  if (value.isRational()) {
    return value.latex();
  }

  const UnivariatePolynomial &polynomial = value.polynomial();
  const std::size_t degree = polynomial.degree();
  if (degree < 2) {
    return value.latex();
  }

  // 只有「单个根式」还原得出来：中间项必须全为零，即最小多项式是 a·x^n + c。
  // \sqrt{2}+\sqrt{3} 的最小多项式是 x^4 - 10x^2 + 1，中间项非零，没有单一的根式写法。
  for (std::size_t power = 1; power < degree; ++power) {
    if (polynomial.coefficient(power) != Fraction(0, 1)) {
      return value.latex();
    }
  }

  // a·x^n + c = 0  ⟹  x = ±\sqrt[n]{-c/a}
  const Result<Fraction> radicand = (Fraction(0, 1) - polynomial.constantTerm()) / polynomial.leadingCoefficient();
  if (radicand.isErr()) { // 首项系数非零，实际除不出零来；这里只是兜底
    return value.latex();
  }
  const bool negative = value < Fraction(0, 1);

  std::string root = "\\sqrt";
  if (degree != 2) {
    root += "[" + std::to_string(degree) + "]";
  }
  root += "{" + RealAlgebraicNumber(radicand.unwrap()).latex() + "}";
  return negative ? "-" + root : root;
}

// ---------------- 根号的常见误写 ----------------

// 返回提示文本，没错写就返回 nullopt。
//
// 两类：直接敲 √ 符号；写了不带反斜杠的 sqrt(…) / sqrt{…}。
// 后者会被隐含乘法拆成 s·q·r·t·(…)，解析居然是成功的 —— 不报错但显然不是本意，
// 所以成功路径也要提示一句。
std::optional<std::string> radicalHint(std::string_view text) {
  if (text.find("√") != std::string_view::npos) {
    return std::string("根号请写 LaTeX 形式 —— \\sqrt{2}、\\sqrt[3]{2}");
  }

  for (std::size_t position = 0; position + 4 <= text.size(); ++position) {
    if (text.compare(position, 4, "sqrt") != 0) {
      continue;
    }
    if (position > 0 && text[position - 1] == '\\') {
      continue; // \sqrt 是正确写法
    }
    const std::size_t next = position + 4;
    if (next < text.size() && (text[next] == '(' || text[next] == '{')) {
      return std::string("sqrt(...) 会被当成变量相乘；想写根号请用 \\sqrt{2}");
    }
  }
  return std::nullopt;
}

// ---------------- 式子 ----------------

// 式子的两种表示：
//   RationalFunction      系数是有理数，可以含变量，走「代入条件再化简」那条路
//   RealAlgebraicNumber   纯数值且含根号，直接给精确值
using Expression = std::variant<RationalFunction, RealAlgebraicNumber>;

struct InputExpression {
  Expression value;
  std::string text; // 用户的原始输入（去首尾空白），回显用
};

// 反复索取式子，直到解析成功；输入流结束则返回 nullopt
std::optional<InputExpression> readExpression() {
  while (true) {
    std::cout << "式子> ";
    std::string line;
    if (!readLine(line)) {
      std::cout << '\n';
      return std::nullopt;
    }
    if (stripSpaces(line).empty()) {
      continue;
    }

    const Result<RationalFunction> rational = parseExpression(line);
    if (rational.isOk()) {
      printField("解析为", rational.unwrap().latex(), true);
      if (const std::optional<std::string> hint = radicalHint(line)) {
        printFeedback("提示", *hint); // sqrt(2) 这类会被当成变量相乘，解析成功但不是本意
      }
      return InputExpression{rational.unwrap(), trim(line)};
    }

    // RationalFunction 表示不了根号（系数域是有理数），换代数数再试一次。
    // 代数数只接受纯数值，含变量的式子在两条路上都会失败。
    const Result<RealAlgebraicNumber> algebraic = RealAlgebraicNumber::parse(line);
    if (algebraic.isOk()) {
      printField("解析为", radicalLatex(algebraic.unwrap()), true);
      return InputExpression{algebraic.unwrap(), trim(line)};
    }

    // 报有理式那条路的错误：含变量时它的诊断更准
    printFeedback("不接受", describe(rational.unwrapErr()));
    if (const std::optional<std::string> hint = radicalHint(line)) {
      printFeedback("提示", *hint);
    } else if (line.find("\\sqrt") != std::string::npos) {
      printFeedback("提示", "根号只在不含变量的式子里支持；写法是 \\sqrt{2}、\\sqrt[3]{2}");
    } else {
      printFeedback("提示", "语法见开头；普通写法与 LaTeX 写法都接受");
    }
  }
}

// 打印语法说明。一次性给全，后面不再零散补充
void printSyntax() {
  printSection("语法");
  printField("运算", "+ - * / ^ 与括号", true);
  printField("变量", "单个字母可带下标 —— x、a_1、x_{i,j}", true);
  printField("长名", "多字母变量加花括号 —— {node}、{node}_{car}", true);
  printField("乘法", "可省略 —— xy 即 x*y，2x 即 2*x（所以 {node} 不写花括号会变成 n*o*d*e）", true);
  printField("根号", "只认 LaTeX 写法 —— \\sqrt{2}、\\sqrt[3]{2}", true);
  printField("写法", "普通写法与 LaTeX 写法都接受 —— \\frac{a}{b}、\\cdot、\\times、\\div、x^{2}", true);
}

// 打印条件输入的说明
void printConstraintHelp() {
  printSection("条件");
  printField("写法", "变量 = 表达式，如 x = 2、s = v*t（右边可含式子里没有的变量）", true);
  printField("根号", "右边可以直接写根号 —— x = \\sqrt{2}、y = \\sqrt[3]{5}", true);
  printField("删除", "输入 x = x 删掉变量 x 的约束（重复输入同名变量即为覆盖）", true);
  printField("结束", "输入 0=0", true);
  printField("限制", "右边不能含被赋值的变量本身 —— x = 2x 是方程，不支持", true);
}

// ---------------- 条件 ----------------

// 一条条件的值。含根号时 RationalFunction 装不下（它的系数域是有理数），只能用代数数。
using ConstraintValue = std::variant<RationalFunction, RealAlgebraicNumber>;

struct Constraint {
  Variable variable;
  ConstraintValue value;
};

std::string constraintValueLatex(const ConstraintValue &value) {
  if (const RealAlgebraicNumber *number = std::get_if<RealAlgebraicNumber>(&value)) {
    return radicalLatex(*number);
  }
  return std::get<RationalFunction>(value).latex();
}

// 把条件的值提升到代数栈。有理值的提升走 toAlgebraic（单射，不丢信息），
// 根号值本身就是代数数，直接当常数包进去。
AlgebraicRationalFunction toAlgebraicValue(const ConstraintValue &value) {
  if (const RealAlgebraicNumber *number = std::get_if<RealAlgebraicNumber>(&value)) {
    return AlgebraicRationalFunction(*number);
  }
  return toAlgebraic(std::get<RationalFunction>(value));
}

// 记录一条条件；同名变量只保留最新的一条（覆盖）
void recordConstraint(std::vector<Constraint> &constraints, Constraint entry) {
  for (Constraint &existing : constraints) {
    if (existing.variable == entry.variable) {
      existing = std::move(entry);
      return;
    }
  }
  constraints.push_back(std::move(entry));
}

// 从条件表里删掉某个变量，返回是否确实删掉了
std::size_t eraseConstraint(std::vector<Constraint> &constraints, const Variable &variable) {
  std::size_t removed = 0;
  for (std::size_t index = 0; index < constraints.size();) {
    if (constraints[index].variable == variable) {
      constraints.erase(constraints.begin() + static_cast<std::ptrdiff_t>(index));
      ++removed;
      continue;
    }
    ++index;
  }
  return removed;
}

// 解析 "变量 = 含根号的常数"。左边必须是单个变量，右边交给 RealAlgebraicNumber。
// 格式不符就返回 nullopt —— 报错交给有理式那条路，那里诊断更准。
std::optional<Constraint> parseRadicalConstraint(std::string_view text) {
  const std::size_t equals = text.find('=');
  if (equals == std::string_view::npos || text.find('=', equals + 1) != std::string_view::npos) {
    return std::nullopt;
  }

  const Result<RationalFunction> leftHand = parseExpression(text.substr(0, equals));
  if (leftHand.isErr()) {
    return std::nullopt;
  }
  const std::optional<Variable> variable = expression_detail::asSingleVariable(leftHand.unwrap());
  if (!variable) {
    return std::nullopt;
  }

  const Result<RealAlgebraicNumber> value = RealAlgebraicNumber::parse(text.substr(equals + 1));
  if (value.isErr()) {
    return std::nullopt;
  }
  return Constraint{*variable, value.unwrap()};
}

// 根号条件的相关性。它的值是常数、不含变量，所以 isRelevantTo 里「右边引入式子的变量」
// 那几条判据一律不成立，只看左边变量是否与式子或已有条件有关就够了。
bool isRadicalRelevant(const RationalFunction &expression, const std::vector<Constraint> &constraints,
                       const Variable &variable) {
  if (expression.containsVariable(variable)) {
    return true;
  }
  for (const Constraint &entry : constraints) {
    if (entry.variable == variable) {
      return true; // 覆盖一条已有条件
    }
    if (const RationalFunction *value = std::get_if<RationalFunction>(&entry.value)) {
      if (value->containsVariable(variable)) {
        return true; // 已有的值里含这个变量
      }
    }
  }
  return false;
}

// 逐条读取条件：有理的记入 scope，全部（含根号）记入 constraints
void readConstraints(const RationalFunction &expression, Scope &scope, std::vector<Constraint> &constraints) {
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
      const bool wasBound = scope.erase(*erased);
      const std::size_t removed = eraseConstraint(constraints, *erased);
      if (wasBound || removed > 0) {
        printFeedback("已删除", erased->str() + " 的约束");
      } else {
        printFeedback("跳过", erased->str() + " 本来就没有约束");
      }
      continue;
    }

    const Result<std::optional<Assignment>> assignment = parseAssignment(line);

    // 有理式那条路走通（且不是恒等式）时按原样处理，行为与加根号之前完全一致
    if (assignment.isOk() && assignment.unwrap().has_value()) {
      const Assignment &entry = *assignment.unwrap();
      if (!isRelevantTo(expression, scope, entry)) {
        printFeedback("跳过", "与式子和已有条件都无关");
        continue;
      }
      const Result<void> assigned = scope.assign(entry.variable, entry.value);
      if (assigned.isErr()) {
        printFeedback("不接受", describe(assigned.unwrapErr()));
        continue;
      }
      recordConstraint(constraints, Constraint{entry.variable, entry.value});
      printFeedback("已记录", entry.variable.str() + " = " + entry.value.latex());
      continue;
    }

    // 右边含根号：值是常数，RationalFunction 表示不了它
    const std::optional<Constraint> radical = parseRadicalConstraint(line);
    if (radical) {
      if (!isRadicalRelevant(expression, constraints, radical->variable)) {
        printFeedback("跳过", "与式子和已有条件都无关");
        continue;
      }
      // 同名变量之前可能绑的是有理值，覆盖后要从有理作用域里撤掉 ——
      // 否则两条路各记一份，结果取决于走哪条路
      scope.erase(radical->variable);
      recordConstraint(constraints, *radical);
      printFeedback("已记录", radical->variable.str() + " = " + constraintValueLatex(radical->value));
      continue;
    }

    // 两条路都不通。有理式那条的报错更具体，用它
    if (assignment.isErr()) {
      printFeedback("不接受", describe(assignment.unwrapErr()));
      continue;
    }
    printFeedback("跳过", "恒等式，没有可记录的信息");
  }
}

// ---------------- 结果 ----------------

void printConstraintList(const std::vector<Constraint> &constraints) {
  printSection("条件");
  if (constraints.empty()) {
    printListItem("（无）");
    return;
  }
  for (const Constraint &entry : constraints) {
    printListItem(entry.variable.str() + " = " + constraintValueLatex(entry.value));
  }
}

void printDiscardedNote(const std::set<Variable> &discarded) {
  if (discarded.empty()) {
    return;
  }
  std::string names;
  for (const Variable &variable : discarded) {
    if (!names.empty()) {
      names += ' ';
    }
    names += variable.str();
  }
  printField("注意", "化简中约去了 " + names + "，上述等价关系仅在这些变量非零时成立");
}

// 有理路径：与加根号之前完全一致
void printRationalResult(const RationalFunction &expression, const Scope &scope) {
  const Result<RationalFunction> substituted = expression.substitute(scope);
  if (substituted.isErr()) {
    // 代入后分母为零属于数学结论（原式在该点无定义），不是程序错误，
    // 因此正常结束而不是返回非零退出码。
    printField("无法代入", std::string(describe(substituted.unwrapErr())) + "（原式在这些取值处无定义）");
    return;
  }

  const RationalFunction &result = substituted.unwrap();
  printField("化简结果", result.latex());

  // 长除法能整除时单独给出多项式形式（如 (a^2-1)/(a+1) → a-1），并补上定义域条件：
  // 这种归约会丢掉「原式在分母零点处无定义」这一信息 —— 归约后的式子在那里有定义，
  // 原式没有。
  //
  // 但**只有分母含变量时才值得报**。常数分母恒非零，没有零点可丢，报出来只是把
  // 化简结果再抄一遍（5/6 会被报成「可化为多项式: \frac{5}{6}」，毫无信息量）。
  const Result<Monomial> denominatorMonomial = result.getDenominator().toMonomial();
  const bool denominatorIsConstant = denominatorMonomial.isOk() && denominatorMonomial.unwrap().isConstant();

  const Result<Polynomial> polynomial = result.toPolynomial();
  if (polynomial.isOk() && !denominatorIsConstant) {
    const std::string note = "（原式要求 " + result.getDenominator().latex() + " \\neq 0）";
    printField("可化为多项式", polynomial.unwrap().latex() + note);
  }

  const Result<Fraction> evaluated = result.evaluate(scope);
  if (evaluated.isOk()) {
    printField("常数结果", evaluated.unwrap());
  }

  printDiscardedNote(result.discardedConstraints());
}

// 代数路径：条件里出现根号时，ℚ 上的代入算不出精确值（√2 装不进 Fraction），
// 整体提升到 ℚ(α) 上算。提升是单射，不丢信息；化简规则与定义域约束跟有理版共用同一套。
void printAlgebraicResult(const RationalFunction &expression, const std::vector<Constraint> &constraints) {
  AlgebraicScope scope;
  for (const Constraint &entry : constraints) {
    const Result<void> assigned = scope.assign(entry.variable, toAlgebraicValue(entry.value));
    if (assigned.isErr()) {
      printField("无法代入", entry.variable.str() + ": " + std::string(describe(assigned.unwrapErr())));
      return;
    }
  }

  const Result<AlgebraicRationalFunction> substituted = toAlgebraic(expression).substitute(scope);
  if (substituted.isErr()) {
    printField("无法代入", std::string(describe(substituted.unwrapErr())) + "（原式在这些取值处无定义）");
    return;
  }

  // 分子分母都化成常数时给精确值（x^2+1 配 x=\sqrt{2} 得 3，这是最漂亮的情形）
  const Result<RealAlgebraicNumber> evaluated = substituted.unwrap().evaluate(scope);
  if (evaluated.isOk()) {
    printField("精确值", radicalLatex(evaluated.unwrap()));
  } else {
    printField("化简结果", substituted.unwrap().latex());
  }

  printDiscardedNote(substituted.unwrap().discardedConstraints());
}

} // namespace

int main() {
  std::cout << "=== 表达式化简 ===\n\n";

  printSyntax();
  std::cout << '\n';

  const std::optional<InputExpression> input = readExpression();
  if (!input) {
    return 0;
  }

  if (const RealAlgebraicNumber *algebraic = std::get_if<RealAlgebraicNumber>(&input->value)) {
    // 含根号的纯数值式子不是有理函数，没有「代入条件」可言，直接给精确值
    printFeedback("提示", "根号按精确代数数计算，不需要代入条件");
    std::cout << "\n--- 结果 ---\n";
    printField("式子", input->text);
    printField("精确值", radicalLatex(*algebraic));
  } else {
    const RationalFunction &expression = std::get<RationalFunction>(input->value);
    Scope scope;
    std::vector<Constraint> constraints;

    // 式子不含变量时它就是纯常数运算，没有可代入的东西 —— 直接出结果，当计算器用。
    // 这时连条件说明都不必打印，否则用户会对着一段用不上的提示发愣。
    if (expression.variables().empty()) {
      printFeedback("提示", "式子不含变量，跳过条件输入");
    } else {
      std::cout << '\n';
      printConstraintHelp();
      readConstraints(expression, scope, constraints);
    }

    std::cout << "\n--- 结果 ---\n";
    printField("式子", expression.latex());
    printConstraintList(constraints);

    // 只要有一条条件的值是根号，ℚ 上的精确计算就做不下去了，整体改走代数栈
    bool hasRadical = false;
    for (const Constraint &entry : constraints) {
      if (std::holds_alternative<RealAlgebraicNumber>(entry.value)) {
        hasRadical = true;
        break;
      }
    }

    if (hasRadical) {
      printAlgebraicResult(expression, constraints);
    } else {
      printRationalResult(expression, scope);
    }
  }

  // 双击运行时窗口不会立刻关闭
  std::cout << "\n按回车键退出...";
  std::string ignored;
  std::getline(std::cin, ignored);

  return 0;
}
