#include "check.hpp"
#include "expression_parser.hpp"
#include <iostream>
#include <string>

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
    CHECK_ERR(parseAssignment("x + 1 = y"), MathsError::InvalidExpression); // 同上，不能靠移项猜
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
    CHECK_EQ(LATEX_OF("x^{3}"), std::string("x^{3}"));
    CHECK_EQ(LATEX_OF("(x + 1)^{2}"), std::string("x^{2} + 2x + 1"));

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

  // 18. 约束与式子的相关性：两边都无关时不该记录
  {
    const Assignment sEqualsVt{Variable("s"), parseExpression("v*t").unwrap()};
    const Assignment xEquals3{Variable("x"), parseExpression("3").unwrap()};

    // 2x 配上 s = v*t：s 不在式子里，v*t 也不含 x → 无关
    CHECK_TRUE(!isRelevantTo(parseExpression("2x").unwrap(), sEqualsVt));

    // 2s 配上 s = v*t：左边 s 就在式子里 → 相关
    CHECK_TRUE(isRelevantTo(parseExpression("2s").unwrap(), sEqualsVt));

    // 2v 配上 s = v*t：右边含 v，代入后 v 会被继续展开 → 相关
    CHECK_TRUE(isRelevantTo(parseExpression("2v").unwrap(), sEqualsVt));

    CHECK_TRUE(isRelevantTo(parseExpression("2x").unwrap(), xEquals3));
  }

  TEST_SUMMARY();
}
