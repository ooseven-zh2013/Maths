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
//   - 式子里也可以写根号，两种情形都支持：
//       * 根号内是**常数** —— `\sqrt{2}*x`、`x+\sqrt{2}`，系数落在 ℚ(α) 上
//       * 根号包着**变量** —— `\sqrt{x}`、`\sqrt{x^2+1}`、`\sqrt{x}+\sqrt{x+1}`，
//         落在函数域上；代入时要求变量都取到有理数（`√x` 配 `x=4` 给 2）
//   - 含变量根号的限制：只支持二次根（无 `\sqrt[3]{x}`）、不支持嵌套根号、
//     被开方数必须含同一个变量（`\sqrt{x}+\sqrt{y}` 不行，而 `\sqrt{x}*y` 可以）
//   - 输出侧由库的 `RealAlgebraicNumber::latex()` / `RadicalExtension::latex()` 渲染：
//     单根式给 `\sqrt{2}`，还原不成的（如 \sqrt{2}+\sqrt{3}）退回 RootOf 记法
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

// 分段结果的展示形态：是 `|g|` 就给 `|g|`，否则 nullopt（调用方给完整的 cases）。
//
// ⚠️ 判据不能只看「分支数 == 2」—— `|g|` 在 g **恒非负**时（`|(x-1)^2/2|`）内部只剩一支，
// 而「一个本来就单支的分段」跟它结构上完全一样，分辨不出来。所以要靠**输入形态**兜底：
// 用户既然写的是 `|…|`，结果就该还他一个 `|…|`，同一类输入不能给两种形态。
std::optional<std::string> absoluteValueText(const PiecewiseFunction &value, bool writtenAsAbsoluteValue) {
  if (const std::optional<RationalFunction> magnitude = value.asAbsoluteValue()) {
    return "|" + magnitude->latex() + "|";
  }
  // `asAbsoluteValue()` 要规则能降成单一有理函数；塔元素做不到，那条路给不出幅度。
  // 这时直接看**形状**：两支且互为相反数 → 就是 |g|，幅度就是正支的规则。
  if (writtenAsAbsoluteValue && value.branchCount() == 2 && value.branch(1).rule() == -value.branch(0).rule()) {
    return "|" + value.branch(0).ruleLatex() + "|";
  }
  if (writtenAsAbsoluteValue && value.branchCount() == 1) {
    return "|" + value.branch(0).ruleLatex() + "|";
  }
  return std::nullopt;
}

// 变量清单的显示（一元那边只会出现一个，所以以前没写过这个）
//
// ⚠️ `\quad` 是 **LaTeX 排版命令**，印到终端上是噪音（`变量: x,\quad y`）。
// 两份都要：结果区的「变量」这种**给人看的字段**用下面的终端版，
// latex() 渲染里才用带 `\quad` 的那份。
std::string variableListText(const std::set<Variable> &variables) {
  std::string result;
  for (const Variable &variable : variables) {
    if (!result.empty()) {
      result += ", ";
    }
    result += variable.str();
  }
  return result;
}

std::string variableListLatex(const std::set<Variable> &variables) {
  std::string result;
  for (const Variable &variable : variables) {
    if (!result.empty()) {
      result += ",\\quad ";
    }
    result += variable.str();
  }
  return result;
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

// 式子的五种表示：
//   RationalFunction          系数是有理数，可以含变量 —— 走「代入条件再化简」那条路
//   RealAlgebraicNumber       纯数值且含根号 —— 直接给精确值
//   AlgebraicRationalFunction 系数含根号、且带变量（`\sqrt{2}*x`、`x+\sqrt{2}`）——
//                             一直在 ℚ(α) 上算，效果等同「式子 + 条件」那条路
//   RadicalExtension          根号包着变量（`\sqrt{x}`、`\sqrt{x^2+1}`、`\sqrt{x}+\sqrt{x+1}`）——
//                             落在函数域上，代入有理数后给精确值
//   PiecewiseFunction         被开方数是**完全平方**的根号（`\sqrt{x^2}` = |x|）——
//                             那不是单个式子、分段才装得下，所以单独一档
// 最后一档 `MultiTowerExtension` 是**多元**的（变量不止一个）。它只在「代入求值」上
// 与前几档不同：条件要给**一个点**（x=3, y=4），而一元那边给 x = 值就够了。
// 第六个变体是多元分段（`|x-y|`）：单变量的一元分段装不下多元绝对值，
// 而多元有理函数 / 多元塔都装不下「按符号分区域」这件事。
using Expression = std::variant<RationalFunction, RealAlgebraicNumber, AlgebraicRationalFunction, RadicalExtension,
                                PiecewiseFunction, TowerExtension, MultiTowerExtension, MultiPiecewiseFunction>;

struct InputExpression {
  Expression value;
  std::string text;                   // 用户的原始输入（去首尾空白），回显用
  bool writtenAsAbsoluteValue{false}; // 输入形如 |…| 或 √(g²)
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

    // RationalFunction 的系数域是 ℚ，装不下根号，换代数数再试。
    // 纯数值的代数数优先于下面那条：它给「精确值」，那条走的是化简那套。
    const Result<RealAlgebraicNumber> algebraic = RealAlgebraicNumber::parse(line);
    if (algebraic.isOk()) {
      // 渲染全交给库：RealAlgebraicNumber::latex() 会把单根式还原成 \sqrt 写法，
      // 还原不成的（如 \sqrt{2}+\sqrt{3}）退回 RootOf
      printField("解析为", algebraic.unwrap().latex(), true);
      return InputExpression{algebraic.unwrap(), trim(line)};
    }

    // 第三档：系数取实代数数的代数式 —— `\sqrt{2}*x`、`x+\sqrt{2}` 这种
    // 「根号包着常数、式子又带变量」的写法走这里。以前只能绕道「式子 + 条件」。
    const Result<AlgebraicRationalFunction> algebraicExpression = parseAlgebraicExpression(line);
    if (algebraicExpression.isOk()) {
      printField("解析为", algebraicExpression.unwrap().latex(), true);
      return InputExpression{algebraicExpression.unwrap(), trim(line)};
    }

    // 第四档：根号包着**变量**的式子 —— `\sqrt{x}`、`\sqrt{x^2+1}`、`\sqrt{x}+\sqrt{x+1}`。
    // 它落在函数域上（域按式子里出现的根号自动扩张），代入有理数后仍是精确值。
    const Result<RadicalExtension> radicalExpression = parseRadicalExpression(line);
    if (radicalExpression.isOk()) {
      printField("解析为", radicalExpression.unwrap().latex(), true);
      return InputExpression{radicalExpression.unwrap(), trim(line)};
    }

    // 第五档：被开方数是**完全平方**的根号（`\sqrt{x^2}`）与直接写的绝对值（`|x|`）——
    // 两者是同一个东西，`|g|` 先被改写成 `\sqrt{{g}^2}` 再走同一条路。
    //
    // 它装不进上面任何一档：代数函数域里 `√(g²)` 不是单值元素（`ℚ(x)[y]/(y²−g²)` 可约、
    // y 是零因子），但作为 **ℝ → ℝ 的函数**完全合法，只需要分段才表示得出来。
    // 分段不是「化简得不好」，而是这类函数的本来面目 —— 所以单独走一条路、单独报结果。
    // 多元的套嵌根号（`\sqrt{x^2+y^2}`、`\sqrt{1+\sqrt{x^2+y^2}}`）（`\sqrt{x^2+y^2}`、`\sqrt{1+\sqrt{x^2+y^2}}`）。
    // 变量不止一个才走这一档 —— 单变量的输入在前面几档就成功了。
    const Result<MultiTowerExtension> multi = parseMultiTowerExpression(line);
    if (multi.isOk() && multi.unwrap().variables().size() > std::size_t(1)) {
      printField("解析为", multi.unwrap().latex(), true);
      printField("变量", variableListLatex(multi.unwrap().variables()), true);
      return InputExpression{multi.unwrap(), trim(line)};
    }

    // 多元的**绝对值**（`|x-y|`、`|x*y-1|`）：拆成 {f≥0} 与 {f<0} 两支，各带一个区域。
    //
    // 位置要紧：必须在多元塔**之后**（`|x-y|` 里没有根号，塔那一档本来就拒），
    // 也必须在下面那个一元分段**之前** —— 一元分段见多元会报 `NotUnivariate`，
    // 那个诊断只对「一元函数」的输入才有意义。
    //
    // ⚠️ `variables().size() > 1` 这个守卫**不能省** —— 少了它 `|x|` 会被这一档抢走：
    // `parseMultiPiecewiseExpression("|x|")` 是**成功**的（它构造得出两支，只是只有 1 个变量），
    // 于是单变量绝对值被要求给「一次给全的点」，提示变成「还差 x」/「要一次给全」，
    // 而它本来该走一元分段那档、接受 `x = 5`。判据与上面多元塔那档一致。
    const Result<MultiPiecewiseFunction> multiAbsolute = parseMultiPiecewiseExpression(line);
    if (multiAbsolute.isOk() && multiAbsolute.unwrap().variables().size() > std::size_t(1)) {
      printField("解析为", trim(line), true);
      printField("变量", variableListLatex(multiAbsolute.unwrap().variables()), true);
      return InputExpression{multiAbsolute.unwrap(), trim(line), true};
    }

    const Result<PiecewiseParseResult> piecewise = parsePiecewiseExpressionDetailed(line);
    if (piecewise.isOk()) {
      const std::optional<std::string> shown = absoluteValueText(piecewise.unwrap().value, true);
      printField("解析为", shown.has_value() ? *shown : piecewise.unwrap().value.latex(), true);
      return InputExpression{piecewise.unwrap().value, trim(line), piecewise.unwrap().writtenAsAbsoluteValue};
    }

    // 第六档：**套嵌**根号（`\sqrt{1+\sqrt{x}}`）。代数函数域装不下，需要塔。
    // 放在分段那一档之后：没有套嵌的输入在前面几档就成功了。
    const Result<TowerExtension> tower = parseTowerExpression(line);
    if (tower.isOk()) {
      printField("解析为", tower.unwrap().latex(), true);
      const Result<RealSet> domain = domainOf(tower.unwrap());
      if (domain.isOk() && !domain.unwrap().isRealLine()) {
        printField("定义域", domain.unwrap().latex(), true);
      }
      return InputExpression{tower.unwrap(), trim(line)};
    }

    // 都失败了，报谁的错误？看谁更具体：
    //   含变量的输入 —— 有理解析器的诊断更准（它认得变量、能说清语法错在哪）
    //   纯数值输入   —— 只有代数数解析器给得出 ZeroDenominator / DivisionByZero /
    //                   NumericOverflow 这类具体原因（2^{1/0}、0^{-1}、(-4)^{1/2}）
    // InvalidExpression 是各条路共有的兜底错误码，它不算「更具体」。
    //
    // 根式这一档还要特殊一点：`RadicandIsSquare` / `RadicandsNotIndependent` 都是在
    // **整条输入按根号语法解析成功之后**、最后一步合法性校验才抛出来的 ——
    // 也就是说语法没问题，卡住的是「√(x²) 是 |x|」这种数学上的限制。
    // 那种诊断比「不支持的表达式」有用得多，必须报出来。
    // （这里原来把 `RadicandIsSquare` 排除在外，于是 `\sqrt{x^2}` 只显示
    //   「不支持的表达式」，用户看不到真正的原因。）
    const MathsError algebraicError = algebraic.unwrapErr();
    const MathsError radicalError = radicalExpression.unwrapErr();
    const MathsError piecewiseError = piecewise.unwrapErr();
    MathsError reported = rational.unwrapErr();
    if (algebraicError != MathsError::InvalidExpression) {
      reported = algebraicError;
    }
    if (radicalError != MathsError::InvalidExpression) {
      reported = radicalError;
    }
    // 第五档（分段）排在最后压轴：它认得「绝对值内部是 √(g²)���所以 `|x-2√x|` 是套嵌、
    // `|a+b|` 是多元」这些**只有它看得出**的原因。只看第四档的话这两种都只剩
    // 「不支持的表达式」。
    if (piecewiseError != MathsError::InvalidExpression) {
      reported = piecewiseError;
    }
    // 塔那一档对「套嵌」的诊断最准，放最后压轴
    if (tower.unwrapErr() != MathsError::InvalidExpression) {
      reported = tower.unwrapErr();
    }
    // ⚠️ 「这一档表示不了」的错误码同样要透出，不能让它们退回「不支持的表达式」。
    //
    // `NotUnivariate` 就是这样漏掉的：`|x-y|` 在四档里都因为「含多个自变量」被拒，
    // 而四档**本来就不该收多元** —— 于是这个「装不下」的信息被兜底错误码吃掉了，
    // 用户只看到「不支持的表达式」，完全不知道是哪个表示装不下。
    //
    // 这里能加是安全的：这段代码只在**所有档都失败**之后才跑，
    // 所以「五档说多元」不会盖掉「三档收下了」——那种情况根本不会走到这里。
    if (reported == MathsError::InvalidExpression) {
      if (piecewiseError == MathsError::NotUnivariate) {
        reported = piecewiseError;
      } else if (algebraicError == MathsError::DomainNotDecidable) {
        reported = algebraicError;
      }
    }
    // 只报**原因**，不报「该怎么办」—— 原因本身已经够具体（每个限制都有专属错误码），
    // 补救办法写在 docs/apps/simplify.md 里，不必每次敲一遍。
    // 「根号里不能再套根号」是**内部形式**的说法 —— 用户写的是 `|\sqrt{x}|`，只写了一个根号。
    // 内部要把 `|g|` 变成 `\sqrt{g^2}`，g 自带根号才套上；拿内部形态去报错，
    // 等于让用户怀疑自己写错了。所以这里翻译成他看得懂的那句话。
    // （真写成 `\sqrt{\sqrt{x}}` 的，报原错误码就是对的，不用翻。）
    std::string_view reason = describe(reported);
    if (reported == MathsError::NestedRadical && line.find('|') != std::string::npos) {
      reason = "绝对值里面不能再带根号";
    }
    printFeedback("不接受", reason);
    // 唯一保留的提示：输入**解析成功了**但很可能不是本意（`sqrt(2)` 被当成 s·q·r·t·(2)）——
    // 那不是限制的说明，是「你可能写错了」的提醒。
    if (const std::optional<std::string> hint = radicalHint(line)) {
      printFeedback("提示", *hint);
    }
  }
}

// 打印语法说明。一次性给全，后面不再零散补充
// 语法说明**只在文档里**，程序不再重复印 —— 用法类内容属于 docs/apps/simplify.md。
// 这里只留一行指针。
void printSyntax() { std::cout << "（语法与用法见 docs/apps/simplify.md）\n\n"; }

// 打印条件输入的说明
// 同上：条件怎么写进文档，这里只留最小提示（`0=0` 结束这件事不写出来会让人卡住）
void printConstraintHelp() { std::cout << "（变量 = 表达式；0=0 结束 · 详见 docs/apps/simplify.md）\n"; }

// ---------------- 条件 ----------------

// 一条条件的值。含根号时 RationalFunction 装不下（它的系数域是有理数），只能用代数数。
using ConstraintValue = std::variant<RationalFunction, RealAlgebraicNumber>;

struct Constraint {
  Variable variable;
  ConstraintValue value;
};

// 两种取值都自带 latex()，直接取
std::string constraintValueLatex(const ConstraintValue &value) {
  return std::visit([](const auto &entry) { return entry.latex(); }, value);
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

// 一条条件是否与式子有关。库里的 isRelevantTo 只认 RationalFunction；代数式那侧
// 按同一套判据补一份 —— 相关性只看「哪些变量出现在哪里」，系数是有理数还是代数数
// 与它无关，所以两种写法共用同一套逻辑。
//
// 用 requires 探一下库里有没有对应重载：将来库里加了代数版，这里会自动走库里的那份。
template <typename ExpressionType>
bool constraintIsRelevant(const ExpressionType &expression, const Scope &scope, const Assignment &assignment) {
  if constexpr (requires { isRelevantTo(expression, scope, assignment); }) {
    return isRelevantTo(expression, scope, assignment);
  } else {
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
}

// 根号条件的相关性。它的值是常数、不含变量，所以 isRelevantTo 里「右边引入式子的变量」
// 那几条判据一律不成立，只看左边变量是否与式子或已有条件有关就够了。
template <typename ExpressionType>
bool isRadicalRelevant(const ExpressionType &expression, const std::vector<Constraint> &constraints,
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

// 逐条读取条件：有理的记入 scope，全部（含根号）记入 constraints。
//
// 模板参数是式子的表示类型（有理式 / 代数式）—— 相关性判断要问「式子里有哪些变量」，
// 这件事两种表示都答得上来，其余逻辑完全一样。
template <typename ExpressionType>
void readConstraints(const ExpressionType &expression, Scope &scope, std::vector<Constraint> &constraints) {
  // 逗号后面还没处理的片段。一行写多条（`x=3, y=4`）时用它排到下一轮。
  std::string queued;
  while (true) {
    std::string line;
    if (queued.empty()) {
      std::cout << "条件> ";
      if (!readLine(line)) {
        std::cout << '\n';
        return;
      }
    } else {
      line = queued;
      queued.clear();
    }

    // ⚠️ 一行里可以写多条：`x=3, y=4` 与分两行等价。
    //
    // 塔那一档（多元取点）本来就收这个写法，只有 Expression 这条路把整行当**一条**
    // 解析，于是 `x=3, y=4` 在 `\sqrt{x+y}` 下能用、在 `x+y` 下报「不支持的表达式」。
    // 同一句话在两条路上含义不同是最糟的，所以这里也拆。
    // 逗号右边先存进 `queued`，下一轮先处理它 —— 免得拆完还得再读一行。
    {
      const std::size_t at = line.find(',');
      if (at != std::string::npos) {
        const std::string head = stripSpaces(line.substr(0, at));
        const std::string tail = stripSpaces(line.substr(at + 1));
        if (!head.empty() && !tail.empty()) {
          queued = tail;
          line = head;
        }
      }
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
      if (!constraintIsRelevant(expression, scope, entry)) {
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

// ---------------- 精确值的渲染 ----------------

// 精确值优先给「有理数」写法。
//
// 起因：`x^3` 配条件 `x=\sqrt[3]{2}` 的结果其实是 2，但它的表示仍是 3 次多项式
// `x^3 - 8`、隔离区间没有退化成一点，于是 `latex()` 走单根式分支渲染成 `\sqrt[3]{8}`。
// 数值精确、但人不这么写。
//
// 这里先拿 `toFraction()`（有理根定理）问一句「你其实是有理数吧」，答是就按分数渲染。
// **别用 `isRational()` 代替它** —— 那个是「表示」属性（端点是否重合），不是数学判断。
// 代入分段函数要的是一个**实数**：条件右边是纯数值（整数/分数/根号）时给得出，
// 右边还含变量时无从谈起 —— 分段函数是一元的，没有「再代一层」这回事。
Result<RealAlgebraicNumber> constraintPointValue(const Constraint &entry) {
  if (const RealAlgebraicNumber *number = std::get_if<RealAlgebraicNumber>(&entry.value)) {
    return *number;
  }
  const RationalFunction &rational = std::get<RationalFunction>(entry.value);
  if (!rational.variables().empty()) {
    return Result<RealAlgebraicNumber>::err(MathsError::UndefinedVariable);
  }
  const Result<Fraction> value = rational.evaluate(Scope());
  if (value.isErr()) {
    return Result<RealAlgebraicNumber>::err(value.unwrapErr());
  }
  return RealAlgebraicNumber(value.unwrap());
}

// 多元的条件输入：一行可以给多个（`x=3, y=4`），读满所有变量才算一个点。
//
// 与一元那边三处不同：
//   - 一行多个赋值（一元是一行一个）
//   - 不支持 `x = x` 那种删除（多元这边没这个需求）
//   - 取值只接受**纯有理数** —— 多元塔只在有理取值的点上有值
// `std::nullopt` = **用户没要取值点**（直接 `0=0` 或 Ctrl-D）。
//
// ⚠️ 这个区分是必要的：**化简不需要代入**。多元两条档（塔 / 分段）以前一律强制
// 取点，于是 `0=0` 之后报「还差 x」/「要一次给全」—— 那是在要求用户做他没要求的事。
// 一元那条路本来就没这个问题（无条件就只印化简结果），多元这边要一样。
//
// 「给了一半」仍然报错：那是在说「我要取值点」，只是没写完。
std::optional<Scope> readPoint(const std::set<Variable> &variables) {
  Scope scope;
  printConstraintHelp();
  while (true) {
    std::cout << "条件> ";
    std::string line;
    if (!readLine(line)) {
      return std::nullopt;
    }
    const std::string trimmed = stripSpaces(line);
    if (trimmed == "0=0") {
      if (scope.empty()) {   // 一个都没给 —— 用户就是不想代入
        return std::nullopt; // 一个都没给 —— 用户就是不想代入
      }
      // 给了一半：报错，而不是拿部分变量去算 —— 那会算出一个没有意义的值
      for (const Variable &variable : variables) {
        if (scope.lookup(variable).isErr()) {
          printFeedback("不接受", "还差 " + variableListText({variable}));
          return std::nullopt;
        }
      }
      return std::optional<Scope>(scope);
    }
    if (trimmed.empty()) {
      continue;
    }
    std::size_t position = 0;
    while (position < trimmed.size()) {
      const std::size_t comma = trimmed.find(',', position);
      const std::string piece =
          trimmed.substr(position, comma == std::string::npos ? std::string::npos : comma - position);
      const std::size_t equals = piece.find('=');
      if (equals == std::string::npos) {
        printFeedback("不接受", "多元的条件写成 x=3, y=4 这样");
        return std::nullopt;
      }
      const std::string name = stripSpaces(piece.substr(0, equals));
      const std::string value = stripSpaces(piece.substr(equals + 1));
      const Result<RationalFunction> parsed = parseExpression(value);
      if (parsed.isErr()) {
        printFeedback("不接受", std::string(describe(parsed.unwrapErr())));
        return std::nullopt;
      }
      if (!parsed.unwrap().variables().empty()) {
        printFeedback("不接受", "取值点只接受有理数");
        return std::nullopt;
      }
      const Result<void> recorded = scope.assign(Variable(name), parsed.unwrap());
      if (recorded.isErr()) {
        printFeedback("不接受", std::string(describe(recorded.unwrapErr())));
        return std::nullopt;
      }
      printFeedback("已记录", name + " = " + parsed.unwrap().str());
      if (comma == std::string::npos) {
        break;
      }
      position = comma + 1;
    }
  }
}

// 塔只在**有理取值**的点上有值（库那边 evaluate 收的是有理赋值），所以条件右边的
// 根号值必须能化成有理数；含变量的更不行 —— 那不是「代不代得进去」，是没有值。
Result<Fraction> rationalValueOf(const ConstraintValue &value) {
  if (const RealAlgebraicNumber *number = std::get_if<RealAlgebraicNumber>(&value)) {
    return number->toFraction();
  }
  const RationalFunction &rational = std::get<RationalFunction>(value);
  if (!rational.variables().empty()) {
    return Result<Fraction>::err(MathsError::UndefinedVariable);
  }
  return rational.evaluate(Scope());
}

std::string exactValueLatex(const RealAlgebraicNumber &value) {
  const Result<Fraction> rational = value.toFraction();
  if (rational.isErr()) {
    return value.latex();
  }
  // 再包回 RealAlgebraicNumber 只为复用库里的分数 LaTeX（库没导出 fractionLatex）
  return RealAlgebraicNumber(rational.unwrap()).latex();
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

// 读条件、打式子与条件区。三种式子里只有「纯数值的代数数」不读条件，
// 有理式与代数式这一段完全一样，抽出来共用。
template <typename ExpressionType>
void printExpressionAndCollectConstraints(const ExpressionType &expression, Scope &scope,
                                          std::vector<Constraint> &constraints) {
  // 式子不含变量时它就是纯常数运算，没有可代入的东西 —— 直接出结果，当计算器用。
  // 这时连条件说明都不必打印，否则用户会对着一段用不上的提示发愣。
  if (expression.variables().empty()) {
    // 不给理由：没有条件可读是自明的
  } else {
    std::cout << '\n';
    printConstraintHelp();
    readConstraints(expression, scope, constraints);
  }

  std::cout << "\n--- 结果 ---\n";
  printField("式子", expression.latex());
  printConstraintList(constraints);
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

// 代数路径：式子的系数含根号、或某条条件的值是根号时，ℚ 上的代入算不出精确值
// （√2 装不进 Fraction），整体在 ℚ(α) 上算。有理式由调用方用 toAlgebraic 提升过来，
// 提升是单射、不丢信息；化简规则与定义域约束跟有理版共用同一套。
void printAlgebraicResult(const AlgebraicRationalFunction &expression, const std::vector<Constraint> &constraints) {
  AlgebraicScope scope;
  for (const Constraint &entry : constraints) {
    const Result<void> assigned = scope.assign(entry.variable, toAlgebraicValue(entry.value));
    if (assigned.isErr()) {
      printField("无法代入", entry.variable.str() + ": " + std::string(describe(assigned.unwrapErr())));
      return;
    }
  }

  const Result<AlgebraicRationalFunction> substituted = expression.substitute(scope);
  if (substituted.isErr()) {
    printField("无法代入", std::string(describe(substituted.unwrapErr())) + "（原式在这些取值处无定义）");
    return;
  }

  // 分子分母都化成常数时给精确值（x^2+1 配 x=\sqrt{2} 得 3，这是最漂亮的情形）
  const Result<RealAlgebraicNumber> evaluated = substituted.unwrap().evaluate(scope);
  if (evaluated.isOk()) {
    printField("精确值", exactValueLatex(evaluated.unwrap()));
  } else {
    printField("化简结果", substituted.unwrap().latex());
  }

  printDiscardedNote(substituted.unwrap().discardedConstraints());
}

// 根式路径：根号包里是变量，值落在函数域上。求值要把每个被开方数都开出来，
// 所以条件是「所有变量都取到有理数」—— 做不到只代一部分。
void printRadicalResult(const RadicalExtension &expression, const std::vector<Constraint> &constraints) {
  // 先报定义域：被开方数 ≥ 0 与系数分母 ≠ 0 取交。整条实轴时不报（没有信息量）
  if (const Result<RealSet> domain = domainOf(expression); domain.isOk() && !domain.unwrap().isRealLine()) {
    printField("定义域", domain.unwrap().latex());
  }

  Scope scope;
  for (const Constraint &entry : constraints) {
    const RationalFunction *value = std::get_if<RationalFunction>(&entry.value);
    if (value == nullptr) {
      printField("无法代入", entry.variable.str() + ": 含变量根号的式子暂时只能用有理数取值（该条件给的是根号）");
      return;
    }
    const Result<void> assigned = scope.assign(entry.variable, *value);
    if (assigned.isErr()) {
      printField("无法代入", entry.variable.str() + ": " + std::string(describe(assigned.unwrapErr())));
      return;
    }
  }

  const Result<RealAlgebraicNumber> evaluated = expression.evaluate(scope);
  if (evaluated.isOk()) {
    printField("精确值", exactValueLatex(evaluated.unwrap()));
    return;
  }
  if (evaluated.unwrapErr() == MathsError::UndefinedVariable) {
    printField("无法代入", "还有变量没有取值 —— 含变量根号的式子要把变量都代成有理数");
    return;
  }
  printField("无法代入", std::string(describe(evaluated.unwrapErr())) + "（该处没有实数值）");
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
    printField("精确值", exactValueLatex(*algebraic));
  } else if (const AlgebraicRationalFunction *algebraicExpression =
                 std::get_if<AlgebraicRationalFunction>(&input->value)) {
    // 系数里含根号的式子（\sqrt{2}*x、x+\sqrt{2}）：条件照读，一直在 ℚ(α) 上算
    printFeedback("提示", "式子里含根号系数，按代数数精确计算");
    Scope scope;
    std::vector<Constraint> constraints;
    printExpressionAndCollectConstraints(*algebraicExpression, scope, constraints);
    printAlgebraicResult(*algebraicExpression, constraints);
  } else if (const RadicalExtension *radical = std::get_if<RadicalExtension>(&input->value)) {
    // 根号包着变量的式子（\sqrt{x}、\sqrt{x^2+1}）：条件照读，但取值要有理数
    printFeedback("提示", "式子里含带变量的根号，按函数域精确计算；条件请给有理数取值");
    Scope scope;
    std::vector<Constraint> constraints;
    printExpressionAndCollectConstraints(*radical, scope, constraints);
    printRadicalResult(*radical, constraints);
  } else if (const PiecewiseFunction *piecewise = std::get_if<PiecewiseFunction>(&input->value)) {
    // √(x²) 与 |x| 是同一个东西：内部按符号分段算，输出还原成 |x|。
    // 条件照读 —— 分段函数一样能代入求值，只是取的是「命中哪一支」。
    Scope scope;
    std::vector<Constraint> constraints;
    // 条件读取借「只含自变量的那个有理函数」当壳 —— 相关性判定要靠它，
    // 别的变量的条件会被判成「与式子无关」而跳过（分段函数本来就只认一个变量）。
    // 打印式子那几行不能走公共函数：它打的是壳（只有 x），这里要打用户写的原文。
    printConstraintHelp();
    readConstraints(RationalFunction(variablePolynomial(piecewise->variable())), scope, constraints);
    std::cout << "\n--- 结果 ---\n";
    printField("式子", input->text);
    printConstraintList(constraints);

    // 与「解析为」同一套判定：同一类输入不能给两种形态
    const std::optional<std::string> shown = absoluteValueText(*piecewise, input->writtenAsAbsoluteValue);
    if (shown.has_value()) {
      printField("分段结果", *shown);
    } else {
      printSection("分段结果");
      printListItem(piecewise->latex());
    }
    // 自变量与定义域都要写出来 —— 各支的区间是「在哪个变量上」的范围，
    // 光给一串区间读者得自己猜（`|2x-1|` 猜得到，`|2t-1|` 也猜得到，
    // 但规则里没出现变量时就完全没辙了），所以这里明确写死。
    printField("自变量", piecewise->variable().str());
    printField("定义域", piecewise->domain().latex());
    printSection("各支");
    for (std::size_t index = 0; index < piecewise->branchCount(); ++index) {
      const RealFunction &branch = piecewise->branch(index);
      printListItem(branch.ruleLatex() + "   当 " + piecewise->variable().str() + " \\in " + branch.domainLatex());
    }

    // 代入求值：分段函数是一元的，所以只认它自己那个变量的条件
    if (!constraints.empty()) {
      const Constraint &entry = constraints.front();
      if (!(entry.variable == piecewise->variable())) {
        printField("无法代入", "分段函数只认 " + piecewise->variable().str() + "，其余条件与它无关");
      } else if (const Result<RealAlgebraicNumber> point = constraintPointValue(entry); point.isErr()) {
        printField("无法代入", std::string(describe(point.unwrapErr())) + "（该处没有实数值）");
      } else if (!piecewise->domain().contains(point.unwrap())) {
        printField("无法代入", piecewise->variable().str() + " = " + point.unwrap().str() + " 不在定义域 " +
                                   piecewise->domain().str() + " 内");
      } else if (const Result<RealAlgebraicNumber> value = piecewise->at(point.unwrap()); value.isErr()) {
        printField("无法代入", std::string(describe(value.unwrapErr())));
      } else {
        printField("精确值", exactValueLatex(value.unwrap()));
      }
    }
  } else if (const MultiTowerExtension *multi = std::get_if<MultiTowerExtension>(&input->value)) {
    // 多元：条件要给一个**点**（x=3, y=4），不是一条一元约束
    const std::set<Variable> variables = multi->variables();
    const std::optional<Scope> point = readPoint(variables);

    std::cout << "\n--- 结果 ---\n";
    printField("式子", input->text);
    printField("变量", variableListText(variables));
    if (!point.has_value()) {
      // 用户没要取值点（`0=0`）—— **化简已经完成了**，不必报「无法代入」。
      // 多元塔的化简结果就是它自身：`2*\sqrt{x^2+y^2}` 没什么可再化的。
      return 0;
    }
    const Result<RealAlgebraicNumber> value = multi->evaluate(point.value());
    if (value.isErr()) {
      printField("无法代入", std::string(describe(value.unwrapErr())));
      return 0;
    }
    printField("精确值", exactValueLatex(value.unwrap()));
  } else if (const MultiPiecewiseFunction *multiAbsolute = std::get_if<MultiPiecewiseFunction>(&input->value)) {
    // 多元绝对值：和多元塔一样，条件要给一个**点**（x=3, y=4）。
    // 点落在哪一支就取那一支 —— 分支的定义域就是符号本身，所以不需要「判符号」。
    const std::set<Variable> variables = multiAbsolute->variables();
    const std::optional<Scope> point = readPoint(variables);

    std::cout << "\n--- 结果 ---\n";
    printField("式子", input->text);
    printField("变量", variableListText(variables));

    // ⚠️ **化简结果无条件也要给** —— 它就是那两支（`{f≥0}` 与 `{f<0}`），
    // 多元绝对值的「化简」到此为止，后面代入只是**求值**。一元分段那边就是这个顺序
    // （先印分段结果，条件非空才代入），多元这边以前反过来了。
    printField("分段结果", multiAbsolute->latex());
    printSection("各支");
    for (const MultiPiecewiseFunction::Branch &branch : multiAbsolute->branches()) {
      printListItem(branch.rule.latex() + "   当 " + branch.domain.latex());
    }

    if (!point.has_value()) {
      return 0; // 用户没要取值点 —— 化简已经给出了
    }
    if (!multiAbsolute->admits(point.value()).unwrap()) {
      printField("无法代入", "这个点不在定义域内");
      return 0;
    }
    const Result<RealAlgebraicNumber> value = multiAbsolute->at(point.value());
    if (value.isErr()) {
      printField("无法代入", std::string(describe(value.unwrapErr())));
      return 0;
    }
    printField("精确值", exactValueLatex(value.unwrap()));
  } else if (const TowerExtension *tower = std::get_if<TowerExtension>(&input->value)) {
    // 套嵌根号（`\sqrt{1+\sqrt{x}}`）：代数函数域装不下，走塔。定义域在解析时给过了。
    Scope scope;
    std::vector<Constraint> constraints;
    printConstraintHelp();
    readConstraints(RationalFunction(variablePolynomial(Variable("x"))), scope, constraints);

    std::cout << "\n--- 结果 ---\n";
    printField("式子", input->text);
    printConstraintList(constraints);
    const Result<RealSet> domain = domainOf(*tower);
    if (domain.isOk() && !domain.unwrap().isRealLine()) {
      printField("定义域", domain.unwrap().latex());
    }

    if (constraints.empty()) {
      return 0;
    }
    const Constraint &entry = constraints.front();
    if (entry.variable != Variable("x")) {
      printField("无法代入", "这条式子只有变量 x");
      return 0;
    }
    const Result<Fraction> point = rationalValueOf(entry.value);
    if (point.isErr()) {
      printField("无法代入", std::string(describe(point.unwrapErr())) + "（套嵌根号只在有理取值处求值）");
      return 0;
    }
    Scope values;
    values.assign(Variable("x"), point.unwrap()).unwrap();
    const Result<RealAlgebraicNumber> value = tower->evaluate(values);
    if (value.isErr()) {
      printField("无法代入", std::string(describe(value.unwrapErr())));
      return 0;
    }
    printField("精确值", exactValueLatex(value.unwrap()));
  } else {
    const RationalFunction &expression = std::get<RationalFunction>(input->value);
    Scope scope;
    std::vector<Constraint> constraints;

    printExpressionAndCollectConstraints(expression, scope, constraints);

    // 只要有一条条件的值是根号，ℚ 上的精确计算就做不下去了，整体改走代数栈
    bool hasRadical = false;
    for (const Constraint &entry : constraints) {
      if (std::holds_alternative<RealAlgebraicNumber>(entry.value)) {
        hasRadical = true;
        break;
      }
    }

    if (hasRadical) {
      printAlgebraicResult(toAlgebraic(expression), constraints);
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
