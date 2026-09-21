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
    CHECK_ERR(parseExpression("1 2"), MathsError::InvalidExpression);
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
      CHECK_EQ(simple->value, Fraction(2, 1));
    }

    // 右边允许是常数表达式
    const Result<std::optional<Assignment>> evaluated = parseAssignment("x = 1/2 + 1/3");
    CHECK_OK(evaluated);
    const std::optional<Assignment> &sum = evaluated.unwrap();
    CHECK_TRUE(sum.has_value());
    if (sum) {
      CHECK_EQ(sum->value, Fraction(5, 6));
    }

    // 负值与分数
    const Result<std::optional<Assignment>> negative = parseAssignment("y = -3/4");
    CHECK_OK(negative);
    const std::optional<Assignment> &bound = negative.unwrap();
    CHECK_TRUE(bound.has_value());
    if (bound) {
      CHECK_EQ(bound->value, Fraction(-3, 4));
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
      CHECK_EQ(swapped->value, Fraction(3, 1));
    }

    const Result<std::optional<Assignment>> fractional = parseAssignment("1/2 = a_b");
    CHECK_OK(fractional);
    const std::optional<Assignment> &bound = fractional.unwrap();
    CHECK_TRUE(bound.has_value());
    if (bound) {
      CHECK_EQ(bound->value, Fraction(1, 2));
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

  // 11. 不支持的形式：明确报错，不猜测
  {
    CHECK_ERR(parseAssignment("x"), MathsError::InvalidExpression);         // 缺少等号
    CHECK_ERR(parseAssignment("x = y"), MathsError::InvalidExpression);     // 右边是变量而非常数
    CHECK_ERR(parseAssignment("x + 1 = 2"), MathsError::InvalidExpression); // 需要解方程
    CHECK_ERR(parseAssignment("f(x) = 2"), MathsError::InvalidExpression);  // 不是单纯变量
    CHECK_ERR(parseAssignment("x = 2 = 3"), MathsError::InvalidExpression); // 多个等号
    CHECK_ERR(parseAssignment("x = "), MathsError::InvalidExpression);
    CHECK_ERR(parseAssignment("= 2"), MathsError::InvalidExpression);
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

  TEST_SUMMARY();
}
