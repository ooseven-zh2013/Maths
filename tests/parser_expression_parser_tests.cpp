#include "check.hpp"

#include <iostream>
#include <optional>
#include <string>

import maths;

using namespace maths;

namespace {

// 解析并返回 LaTeX 形式。失败时记为断言失败，而不是让 unwrap 抛异常中断整个测试 ——
// 崩溃只会给出一句 terminate，而断言失败会指出是哪一行、哪个表达式。
std::string latexOf(std::string_view expression, const char *label, int line) {
  const Result<RationalFunction> parsed = parseExpression(expression);
  if (parsed.isErr()) {
    maths_test::report(false, label, __FILE__, line, "解析失败: " + std::string(describe(parsed.unwrapErr())));
    return "<解析失败>";
  }
  return parsed.unwrap().latex();
}

} // namespace

// 借宏带上调用点行号
#define LATEX_OF(expression) latexOf(expression, #expression, __LINE__)

int main() {
  std::cout << "=== 表达式解析测试 ===" << '\n';

  // 1. 基本元素
  {
    CHECK_EQ(parseExpression("42").unwrap().str(), std::string("42"));
    CHECK_EQ(parseExpression("x").unwrap().str(), std::string("x"));
    CHECK_EQ(parseExpression("x_1").unwrap().str(), std::string("x_1"));
    CHECK_EQ(parseExpression("  x  ").unwrap().str(), std::string("x")); // 忽略空白
  }

  // 2. 四则运算
  {
    CHECK_EQ(parseExpression("1 + 2").unwrap().str(), std::string("3"));
    CHECK_EQ(parseExpression("1 - 2").unwrap().str(), std::string("-1"));
    CHECK_EQ(parseExpression("2 * 3").unwrap().str(), std::string("6"));
    CHECK_EQ(parseExpression("1 / 2").unwrap().str(), std::string("1/2"));
    CHECK_EQ(parseExpression("x + x").unwrap().str(), std::string("2 x"));
    CHECK_EQ(parseExpression("x * x").unwrap().str(), std::string("x^2"));
  }

  // 3. 优先级、结合性与括号
  {
    CHECK_EQ(parseExpression("1 + 2 * 3").unwrap().str(), std::string("7"));
    CHECK_EQ(parseExpression("(1 + 2) * 3").unwrap().str(), std::string("9"));
    CHECK_EQ(parseExpression("2 - 3 - 4").unwrap().str(), std::string("-5")); // 左结合
    CHECK_EQ(parseExpression("2 / 3 / 4").unwrap().str(), std::string("1/6"));
  }

  // 4. 幂
  {
    CHECK_EQ(parseExpression("2^10").unwrap().str(), std::string("1024"));
    CHECK_EQ(parseExpression("x^3").unwrap().str(), std::string("x^3"));
    CHECK_EQ(parseExpression("(x + 1)^2").unwrap().str(), std::string("x^2 + 2 x + 1"));
    CHECK_EQ(parseExpression("x^0").unwrap().str(), std::string("1"));
  }

  // 5. 一元负号
  {
    CHECK_EQ(parseExpression("-5").unwrap().str(), std::string("-5"));
    CHECK_EQ(parseExpression("-x").unwrap().str(), std::string("-x"));
    CHECK_EQ(parseExpression("2 * -3").unwrap().str(), std::string("-6"));
    CHECK_EQ(parseExpression("+7").unwrap().str(), std::string("7"));
  }

  // 6. 分式
  {
    CHECK_EQ(parseExpression("1 / x").unwrap().str(), std::string("(1) / (x)"));
    CHECK_EQ(parseExpression("(x^2 - 1) / (x - 1)").unwrap().str(), std::string("(x^2 - 1) / (x - 1)"));
    CHECK_EQ(parseExpression("6 * x^2 * y / (4 * x * y^2)").unwrap().str(), std::string("(3 x) / (2 y)"));
  }

  // 7. 解析失败
  {
    CHECK_ERR(parseExpression(""), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("   "), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("1 +"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("(1"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("1)"), MathsError::InvalidExpression); // 残留字符
    CHECK_ERR(parseExpression("()"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("@"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^y"), MathsError::InvalidExpression); // 指数必须是整数
    CHECK_ERR(parseExpression("1 / 0"), MathsError::ZeroDenominator);
  }

  // ==================== 代入条件 ====================

  // 8. 变量 = 常数
  {
    const Result<std::optional<Assignment>> parsed = parseAssignment("x = 2");
    CHECK_OK(parsed);
    const std::optional<Assignment> &simple = parsed.unwrap();
    CHECK_TRUE(simple.has_value());
    if (simple) {
      CHECK_EQ(simple->variable.str(), std::string("x"));
      CHECK_EQ(simple->value.latex(), std::string("2"));
    }

    // 右边是常数表达式（会被求值成一个分数）
    const Result<std::optional<Assignment>> evaluated = parseAssignment("x = 1/2 + 1/3");
    CHECK_OK(evaluated);
    const std::optional<Assignment> &sum = evaluated.unwrap();
    CHECK_TRUE(sum.has_value());
    if (sum) {
      CHECK_EQ(sum->value.latex(), std::string("\\frac{5}{6}"));
    }

    // 负值与分数
    const Result<std::optional<Assignment>> negative = parseAssignment("y = -3/4");
    CHECK_OK(negative);
    const std::optional<Assignment> &bound = negative.unwrap();
    CHECK_TRUE(bound.has_value());
    if (bound) {
      CHECK_EQ(bound->value.latex(), std::string("-\\frac{3}{4}"));
    }
  }

  // 9. 两侧交换：C = x 解释为 x = C
  {
    const Result<std::optional<Assignment>> parsed = parseAssignment("3 = x");
    CHECK_OK(parsed);
    const std::optional<Assignment> &swapped = parsed.unwrap();
    CHECK_TRUE(swapped.has_value());
    if (swapped) {
      CHECK_EQ(swapped->variable.str(), std::string("x"));
      CHECK_EQ(swapped->value.latex(), std::string("3"));
    }

    const Result<std::optional<Assignment>> fractional = parseAssignment("1/2 = a_b");
    CHECK_OK(fractional);
    const std::optional<Assignment> &bound = fractional.unwrap();
    CHECK_TRUE(bound.has_value());
    if (bound) {
      CHECK_EQ(bound->value.latex(), std::string("\\frac{1}{2}"));
    }
  }

  // 10. 恒等式：返回 nullopt，表示无需记录
  {
    CHECK_OK(parseAssignment("x = x"));
    CHECK_TRUE(!parseAssignment("x = x").unwrap().has_value());
    CHECK_TRUE(!parseAssignment("2 = 2").unwrap().has_value());
    CHECK_TRUE(!parseAssignment("0 = 0").unwrap().has_value());
    CHECK_TRUE(!parseAssignment("1/2 = 2/4").unwrap().has_value()); // 约分后相等
  }

  // 11. 右边可以是含其它变量的表达式
  {
    // x = y：y 不是 x 自己，允许（相当于给 x 起别名）
    const Result<std::optional<Assignment>> alias = parseAssignment("x = y");
    CHECK_OK(alias);
    CHECK_TRUE(alias.unwrap().has_value());
    if (alias.unwrap()) {
      CHECK_EQ(alias.unwrap()->variable.str(), std::string("x"));
      CHECK_EQ(alias.unwrap()->value.latex(), std::string("y"));
    }

    const Result<std::optional<Assignment>> product = parseAssignment("s = v*t");
    CHECK_OK(product);
    CHECK_TRUE(product.unwrap().has_value());
    if (product.unwrap()) {
      CHECK_EQ(product.unwrap()->variable.str(), std::string("s"));
      CHECK_EQ(product.unwrap()->value.latex(), std::string("tv"));
    }
  }

  // 12. 不支持的形式：明确报错，不猜测
  {
    CHECK_ERR(parseAssignment("x"), MathsError::InvalidExpression);         // 缺少等号
    CHECK_ERR(parseAssignment("x + 1 = 2"), MathsError::InvalidExpression); // 需要解方程（左边不是变量）
    // x + 1 = y 交换成 y = x + 1：y 单独在右边，直接读出，不需要解方程
    const Result<std::optional<Assignment>> flipped = parseAssignment("x + 1 = y");
    CHECK_OK(flipped);
    CHECK_TRUE(flipped.unwrap().has_value());
    if (flipped.unwrap()) {
      CHECK_EQ(flipped.unwrap()->variable.str(), std::string("y"));
      CHECK_EQ(flipped.unwrap()->value.latex(), std::string("x + 1"));
    }
    CHECK_ERR(parseAssignment("f(x) = 2"), MathsError::InvalidExpression);  // 不是单纯变量
    CHECK_ERR(parseAssignment("x = 2 = 3"), MathsError::InvalidExpression); // 多个等号
    CHECK_ERR(parseAssignment("x = "), MathsError::InvalidExpression);
    CHECK_ERR(parseAssignment("= 2"), MathsError::InvalidExpression);

    // 自引用属于方程而非赋值
    CHECK_ERR(parseAssignment("x = 2x"), MathsError::NotAnAssignment);
    CHECK_ERR(parseAssignment("x = x + 1"), MathsError::NotAnAssignment);
    CHECK_ERR(parseAssignment("x = 1/x"), MathsError::NotAnAssignment);
  }

  // ==================== LaTeX 写法 ====================

  // 12. \frac 与其它 LaTeX 记号
  {
    CHECK_EQ(LATEX_OF("\\frac{1}{2}"), std::string("\\frac{1}{2}"));
    CHECK_EQ(LATEX_OF("\\frac{x + 1}{x - 1}"), std::string("\\frac{x + 1}{x - 1}"));
    // 嵌套：\frac{\frac{1}{2}}{3} = 1/6
    CHECK_EQ(LATEX_OF("\\frac{\\frac{1}{2}}{3}"), std::string("\\frac{1}{6}"));

    CHECK_EQ(LATEX_OF("2 \\cdot x"), std::string("2x"));
    CHECK_EQ(LATEX_OF("2 \\times x"), std::string("2x"));
    CHECK_EQ(LATEX_OF("6 \\div 3"), std::string("2"));

    // 指数花括号
    CHECK_EQ(LATEX_OF("x^3"), std::string("x^3"));
    CHECK_EQ(LATEX_OF("(x + 1)^2"), std::string("x^2 + 2x + 1"));

    // \left \right 被忽略
    CHECK_EQ(LATEX_OF("\\left( x + 1 \\right)"), std::string("x + 1"));
  }

  // 13. \frac{a}{b} + c 合并为统一的分式 \frac{a + b*c}{b}
  {
    CHECK_EQ(LATEX_OF("\\frac{x}{y} + 1"), std::string("\\frac{x + y}{y}"));
    // 通分得到 y + x，但多项式会按展示顺序重排成 x + y
    CHECK_EQ(LATEX_OF("1 + \\frac{x}{y}"), std::string("\\frac{x + y}{y}"));
    CHECK_EQ(LATEX_OF("\\frac{1}{2} + \\frac{1}{3}"), std::string("\\frac{5}{6}"));
    CHECK_EQ(LATEX_OF("\\frac{x}{y} \\cdot 2"), std::string("\\frac{2x}{y}"));
  }

  // 14. 结构不完整的 LaTeX 仍然报错
  {
    CHECK_ERR(parseExpression("\\frac{1}{"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("\\frac{1}"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("\\unknown{x}"), MathsError::InvalidExpression);
  }

  // ==================== 隐含乘法 ====================

  // 15. xy 即 x*y：数学书写习惯，也是与 latex() 输出闭环的前提
  {
    CHECK_EQ(parseExpression("xy").unwrap().str(), std::string("x y"));
    CHECK_EQ(parseExpression("x*y").unwrap().str(), std::string("x y"));
    CHECK_EQ(parseExpression("x y").unwrap().str(), std::string("x y"));

    // 输入两种写法必须得到完全相同的式子
    CHECK_EQ(parseExpression("xy").unwrap(), parseExpression("x*y").unwrap());

    CHECK_EQ(parseExpression("2x").unwrap().str(), std::string("2 x"));
    CHECK_EQ(parseExpression("2(x + 1)").unwrap().str(), std::string("2 x + 2"));
    CHECK_EQ(parseExpression("x(x + 1)").unwrap().str(), std::string("x^2 + x"));
  }

  // 16. 变量名 = 单个字母 + 可选下标；连续字母不再合并成名字
  {
    CHECK_EQ(parseExpression("x").unwrap().str(), std::string("x"));
    CHECK_EQ(parseExpression("x_1").unwrap().str(), std::string("x_1"));
    CHECK_EQ(parseExpression("x_{i,j}").unwrap().str(), std::string("x_{i,j}"));
    CHECK_EQ(parseExpression("a_1b").unwrap().str(), std::string("a_1 b")); // a_1 乘 b
    CHECK_EQ(parseExpression("x^2y").unwrap().str(), std::string("x^2 y"));
  }

  // 17. 多字母变量名用花括号声明
  {
    CHECK_EQ(parseExpression("{node}").unwrap().str(), std::string("node"));
    // 单字符组成的索引不补花括号，与 Variable 的既有输出规则一致
    CHECK_EQ(parseExpression("{node}_{car}").unwrap().str(), std::string("node_car"));
    CHECK_EQ(parseExpression("{node}_{i,j}").unwrap().str(), std::string("node_{i,j}"));
    CHECK_EQ(parseExpression("{x}").unwrap().str(), std::string("x")); // 与裸写 x 等价
    CHECK_EQ(parseExpression("{x}").unwrap(), parseExpression("x").unwrap());

    // 不加花括号就退回隐含乘法（四个变量按名字排序输出）
    CHECK_EQ(parseExpression("node").unwrap().str(), std::string("d e n o"));

    // 与系数、指数组合
    CHECK_EQ(parseExpression("2{node}").unwrap().str(), std::string("2 node"));
    CHECK_EQ(parseExpression("{node}^2").unwrap().str(), std::string("node^2"));
    CHECK_EQ(parseExpression("{node}{car}").unwrap().str(), std::string("car node"));

    // 空花括号不是合法变量
    CHECK_ERR(parseExpression("{}"), MathsError::InvalidExpression);
  }

  const Variable x("x");
  const Variable s("s");
  const Variable t("t");
  const Variable v("v");

  // 18. 约束与式子的相关性：两边都无关时不该记录
  {
    const Assignment sEqualsVt{Variable("s"), parseExpression("v*t").unwrap()};
    const Assignment xEquals3{Variable("x"), parseExpression("3").unwrap()};

    // 2x 配上 s = v*t：s 不在式子里，v*t 也不含 x → 无关
    CHECK_TRUE(!isRelevantTo(parseExpression("2x").unwrap(), Scope(), sEqualsVt));

    // 2s 配上 s = v*t：左边 s 就在式子里 → 相关
    CHECK_TRUE(isRelevantTo(parseExpression("2s").unwrap(), Scope(), sEqualsVt));

    // 2v 配上 s = v*t：右边含 v，代入后 v 会被继续展开 → 相关
    CHECK_TRUE(isRelevantTo(parseExpression("2v").unwrap(), Scope(), sEqualsVt));

    CHECK_TRUE(isRelevantTo(parseExpression("2x").unwrap(), Scope(), xEquals3));

    // 19. 相关性要考虑已有绑定：x = s 之后，s = v*t 就与式子 2x 相关了
    Scope bound;
    CHECK_OK(bound.assign(x, parseExpression("s").unwrap()));
    CHECK_TRUE(isRelevantTo(parseExpression("2x").unwrap(), bound, sEqualsVt));
  }

  // 20. 环检测：直接与间接的自引用都要拒绝
  {
    Scope scope;
    CHECK_OK(scope.assign(s, parseExpression("v*t").unwrap()));
    CHECK_ERR(scope.assign(s, parseExpression("2s").unwrap()), MathsError::NotAnAssignment); // 直接自引用
    CHECK_OK(scope.assign(x, parseExpression("s").unwrap()));
    CHECK_ERR(scope.assign(s, parseExpression("x").unwrap()), MathsError::CircularReference); // x → s → x
    CHECK_ERR(scope.assign(t, parseExpression("t").unwrap()), MathsError::NotAnAssignment);
  }

  // 21. 表达式 = 变量：左边不含该变量即可交换
  {
    const Result<std::optional<Assignment>> swapped = parseAssignment("v*t = x");
    CHECK_OK(swapped);
    CHECK_TRUE(swapped.unwrap().has_value());
    if (swapped.unwrap()) {
      CHECK_EQ(swapped.unwrap()->variable.str(), std::string("x"));
      CHECK_EQ(swapped.unwrap()->value.latex(), std::string("tv"));
    }

    // 左边含该变量时仍然是方程
    CHECK_ERR(parseAssignment("x + 1 = x"), MathsError::InvalidExpression);
  }

  // 22. 多重替换的方向：t = v 表示「遇到 t 换成 v」，不会反向污染已展开的部分
  {
    Scope scope;
    CHECK_OK(scope.assign(x, parseExpression("s*v").unwrap()));
    CHECK_OK(scope.assign(s, parseExpression("2*v").unwrap()));
    CHECK_OK(scope.assign(t, parseExpression("v").unwrap())); // t 不出现在 2x 的展开里

    // 2x → 2*s*v → 2*(2v)*v = 4v^2，不会被 t = v 绕回去
    CHECK_EQ(parseExpression("2x").unwrap().substitute(scope).unwrap().latex(), std::string("4v^2"));
  }

  // 23. 解除绑定：x = x 是删除指令，不是恒等式
  {
    const std::optional<Variable> erased = parseErase("x = x");
    CHECK_TRUE(erased.has_value());
    if (erased) {
      CHECK_EQ(erased->str(), std::string("x"));
    }

    // 带下标的变量同理
    const std::optional<Variable> subscripted = parseErase("a_1 = a_1");
    CHECK_TRUE(subscripted.has_value());
    if (subscripted) {
      CHECK_EQ(subscripted->str(), std::string("a_1"));
    }

    // 常数恒等式不涉及变量，不是删除指令
    CHECK_TRUE(!parseErase("2 = 2").has_value());
    CHECK_TRUE(!parseErase("0 = 0").has_value());
    CHECK_TRUE(!parseErase("1/2 = 2/4").has_value());

    // 赋值也不是删除指令
    CHECK_TRUE(!parseErase("x = 2").has_value());
    CHECK_TRUE(!parseErase("x = y").has_value());

    // 解析失败的输入交回 parseAssignment 报错，这里不抢先判定
    CHECK_TRUE(!parseErase("x + 1 = 2").has_value());
    CHECK_ERR(parseAssignment("x + 1 = 2"), MathsError::InvalidExpression);

    // 实际效果：删掉之后变量恢复自由，重复删除返回 false
    Scope scope;
    CHECK_OK(scope.assign(x, parseExpression("3").unwrap()));
    CHECK_EQ(parseExpression("2x").unwrap().substitute(scope).unwrap().latex(), std::string("6"));
    CHECK_TRUE(scope.erase(x));
    CHECK_TRUE(!scope.contains(x));
    CHECK_EQ(parseExpression("2x").unwrap().substitute(scope).unwrap().latex(), std::string("2x"));
    CHECK_TRUE(!scope.erase(x)); // 已经删过了
  }

  // 24. 常数式子不含变量 —— app 据此跳过条件输入、直接出结果
  {
    CHECK_TRUE(parseExpression("2 + 3*4").unwrap().variables().empty());
    CHECK_TRUE(parseExpression("1/2 + 1/3").unwrap().variables().empty());
    CHECK_TRUE(parseExpression("\\frac{7}{2}").unwrap().variables().empty());

    // 含变量的式子不能跳过
    CHECK_TRUE(!parseExpression("2x").unwrap().variables().empty());
    CHECK_TRUE(!parseExpression("2 + 3*4 + y").unwrap().variables().empty());
  }

  // ---------- 指数：^{1/2} 这类必须报错，不能静默当成除法 ----------
  //
  // 回归用：曾经 normalizeLatex 无条件剥掉 ^{...} 的花括号，x^{1/2} 被改写成 x^1/2，
  // 于是解析成 (x^1)/2 = x/2 并当作合法结果返回 —— 不报错、给错答案。
  {
    // 合法的整数指数
    CHECK_EQ(parseExpression("x^{2}").unwrap().str(), std::string("x^2"));
    CHECK_EQ(parseExpression("x^{12}").unwrap().str(), std::string("x^12"));
    CHECK_EQ(parseExpression("2^{3}").unwrap().str(), std::string("8"));
    CHECK_EQ(parseExpression("x^2").unwrap().str(), std::string("x^2"));

    // 非整数指数：明确报错
    CHECK_ERR(parseExpression("x^{1/2}"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^{3/4}"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^{2/3}"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^{1/2}+1"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^{-1}"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^{}"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^{a}"), MathsError::InvalidExpression);
    CHECK_ERR(parseExpression("x^{1/2"), MathsError::InvalidExpression); // 括号不配平

    // 不能过度拒绝：花括号之外出现除号是正常的除法（x^2 除以 3）
    CHECK_EQ(parseExpression("x^{2}/3").unwrap().str(), std::string("(x^2) / (3)"));
    CHECK_EQ(parseExpression("x^2/3").unwrap().str(), std::string("(x^2) / (3)"));
    CHECK_EQ(parseExpression("2^3/4").unwrap().str(), std::string("2"));
  }

  // ---------- 代数版解析：根号出现在系数位置 ----------
  //
  // 之前 `\sqrt{2}*x`、`x+\sqrt{2}` 被拒收（有理系数域装不下根式），
  // 只好绕道「式子 x*z + 条件 x=\sqrt{2}」。现在同一个 ParserOf 换个系数类型即可。
  {
    const Result<AlgebraicRationalFunction> parsed = parseAlgebraicExpression("\\sqrt{2}*x");
    CHECK_OK(parsed);
    CHECK_EQ(parsed.unwrap().latex(), std::string("\\sqrt{2}x"));
    CHECK_TRUE(parsed.unwrap().containsVariable(Variable("x")));

    CHECK_EQ(parseAlgebraicExpression("x + \\sqrt{2}").unwrap().latex(), std::string("x + \\sqrt{2}"));
    CHECK_EQ(parseAlgebraicExpression("\\sqrt[3]{2}*y").unwrap().latex(), std::string("\\sqrt[3]{2}y"));
    CHECK_EQ(parseAlgebraicExpression("\\sqrt{2}*x*y").unwrap().latex(), std::string("\\sqrt{2}xy"));
    CHECK_EQ(parseAlgebraicExpression("\\sqrt{2}/2").unwrap().latex(), std::string("\\frac{\\sqrt{2}}{2}"));

    // 与「式子 + 条件」那条老路必须等价 —— 这正是新增入口的意义
    AlgebraicPolynomial xTimesZ;
    xTimesZ.addTerm({{Variable("x"), 1ULL}, {Variable("z"), 1ULL}}, RealAlgebraicNumber(Fraction(1, 1)));
    AlgebraicScope scope;
    CHECK_OK(scope.assign(Variable("x"), RealAlgebraicNumber::parse("\\sqrt{2}").unwrap()));
    CHECK_TRUE(AlgebraicRationalFunction(xTimesZ).substitute(scope).unwrap() ==
               parseAlgebraicExpression("\\sqrt{2}*z").unwrap());

    // 纯有理式子走这条入口也应与有理解析等价
    CHECK_TRUE(parseAlgebraicExpression("2x + 1").unwrap() == toAlgebraic(parseExpression("2x + 1").unwrap()));

    // 根号包**变量**仍不支持：那是代数函数域 ℚ(x)[y]/(y²−x)，与「系数取代数数」是两件事
    CHECK_ERR(parseAlgebraicExpression("\\sqrt{x}"), MathsError::InvalidExpression);
    CHECK_ERR(parseAlgebraicExpression("\\sqrt{x^2}"), MathsError::InvalidExpression);
    CHECK_ERR(parseAlgebraicExpression("\\sqrt{2x}"), MathsError::InvalidExpression);

    // 有理路径不受影响：ℚ 里没有根式，`\sqrt{2}*x` 仍应拒收
    CHECK_ERR(parseExpression("\\sqrt{2}*x"), MathsError::InvalidExpression);
    CHECK_EQ(parseExpression("2x + 1").unwrap().str(), std::string("2 x + 1"));
  }

  // ---------- 含变量根号的表达式（parseRadicalExpression） ----------
  //
  // 根号包变量需要函数域：解析出来的是 RadicalExtension 的元素，
  // 域按式子里出现的根号自动扩张。
  {
    CHECK_EQ(parseRadicalExpression("\\sqrt{x}").unwrap().latex(), std::string("\\sqrt{x}"));
    CHECK_EQ(parseRadicalExpression("2\\sqrt{x}").unwrap().latex(), std::string("2\\sqrt{x}"));
    CHECK_EQ(parseRadicalExpression("\\sqrt{x} + 1").unwrap().latex(), std::string("1 + \\sqrt{x}"));
    CHECK_EQ(parseRadicalExpression("\\sqrt{x^2 + 1}").unwrap().latex(), std::string("\\sqrt{x^2 + 1}"));

    // 两个不同的根号：域自动扩张
    const RadicalExtension widened = parseRadicalExpression("\\sqrt{x} \\cdot \\sqrt{x+1}").unwrap();
    CHECK_TRUE(widened.radicands().size() == std::size_t(2));
    CHECK_EQ(widened.latex(), std::string("\\sqrt{x}\\sqrt{x + 1}"));

    // 多根号相加（用户要的那个形状）
    CHECK_EQ(parseRadicalExpression("\\sqrt{x^2+1} + \\sqrt{x^2+2}").unwrap().latex(),
             std::string("\\sqrt{x^2 + 1} + \\sqrt{x^2 + 2}"));

    // 乘方与除法（分母有理化自动完成）
    CHECK_EQ(parseRadicalExpression("(\\sqrt{x}+1)^2").unwrap().latex(), std::string("x + 1 + 2\\sqrt{x}"));
    CHECK_EQ(parseRadicalExpression("\\frac{1}{\\sqrt{x}}").unwrap().latex(), std::string("\\frac{1}{x}\\sqrt{x}"));

    // 根号乘上一个别的字母：被开方数仍然只有一个变量，合法
    CHECK_EQ(parseRadicalExpression("\\sqrt{x}*y").unwrap().latex(), std::string("y\\sqrt{x}"));

    // 纯有理式子也能走这条入口
    CHECK_EQ(parseRadicalExpression("2x + 1").unwrap().latex(), std::string("2x + 1"));

    // 代入求值
    Scope scope;
    CHECK_OK(scope.assign(Variable("x"), Fraction(4, 1)));
    CHECK_TRUE(parseRadicalExpression("\\sqrt{x}").unwrap().evaluate(scope).unwrap() == Fraction(2, 1));
    CHECK_OK(scope.assign(Variable("x"), Fraction(-1, 1)));
    CHECK_ERR(parseRadicalExpression("\\sqrt{x}").unwrap().evaluate(scope), MathsError::NegativeEvenRoot);

    // 拒收的情形
    CHECK_ERR(parseRadicalExpression("\\sqrt{x} + \\sqrt{y}"), MathsError::MultiVariableRadical); // 两个变量
    CHECK_ERR(parseRadicalExpression("\\sqrt{x} + \\sqrt{4x}"), MathsError::RadicandsNotIndependent);
    CHECK_ERR(parseRadicalExpression("\\sqrt[3]{x}"), MathsError::InvalidExpression);  // 高次根
    CHECK_ERR(parseRadicalExpression("\\sqrt{\\sqrt{x}}"), MathsError::NestedRadical); // 嵌套
    CHECK_ERR(parseRadicalExpression("\\sqrt{2}"), MathsError::InvalidExpression);     // 常数根号
    CHECK_ERR(parseRadicalExpression("\\sqrt{x"), MathsError::InvalidExpression);      // 括号不配平
  }

  // ---------- 一元负号 vs 幂的优先级 ----------
  {
    // `-x^2` 是 -(x^2)，不是 (-x)^2。原来 power 先调 unary，负号被 ^ 吃掉了。
    CHECK_EQ(parseExpression("-x^2").unwrap().str(), std::string("-x^2"));
    CHECK_EQ(parseExpression("-x^2").unwrap().latex(), std::string("-x^2"));
    CHECK_EQ(parseExpression("(-x)^2").unwrap().str(), std::string("x^2"));
    CHECK_EQ(parseExpression("-x^2+2x").unwrap().str(), std::string("-x^2 + 2 x"));

    // 数值那一路：-4^{1/2} 之前会落到 (-4)^{1/2} 上（报「负数不能开偶次根」），
    // 正确答案是 -2。
    CHECK_TRUE(RealAlgebraicNumber::parse("-4^{1/2}").unwrap() == RealAlgebraicNumber(Fraction(-2, 1)));
    CHECK_TRUE(RealAlgebraicNumber::parse("(-4)^{1/2}").isErr()); // 加了括号才是负数开偶次根
    CHECK_TRUE(RealAlgebraicNumber::parse("-2^{1/2}").unwrap() ==
               -RealAlgebraicNumber::nthRootOf(Fraction(2, 1), 2).unwrap());

    // 根式那一路同样：-x^2 的被开方数是 -x^2，负数开偶次根
    CHECK_ERR(parseRadicalExpression("-\\sqrt{x^2}"), MathsError::RadicandIsSquare);
    CHECK_OK(parseRadicalExpression("-\\sqrt{x+1}"));
  }

  TEST_SUMMARY();
}
