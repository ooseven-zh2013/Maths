#include "check.hpp"

#include <iostream>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

RealAlgebraicNumber number(long long numerator, long long denominator = 1) {
  return RealAlgebraicNumber(Fraction(numerator, denominator));
}

} // namespace

int main() {
  std::cout << "=== 根式 → 分段函数（被开方数是完全平方）测试 ===" << std::endl;
  std::cout << std::unitbuf; // 崩溃时也能看到已输出的断言结果

  // ---------- √(x²) 就是 |x| ----------
  {
    const PiecewiseFunction absolute = parsePiecewiseExpression("\\sqrt{x^2}").unwrap();
    CHECK_TRUE(absolute.branchCount() == std::size_t(2));
    CHECK_TRUE(absolute.domain().isRealLine());
    CHECK_TRUE(absolute.at(number(5)).unwrap() == number(5));
    CHECK_TRUE(absolute.at(number(-5)).unwrap() == number(5));
    CHECK_TRUE(absolute.at(number(0)).unwrap() == number(0));
    CHECK_TRUE(absolute.at(number(-3, 2)).unwrap() == number(3, 2));

    // 与手写的 |x| 结构一致（规则不同没关系，这里比的是两支的定义域划分）
    const PiecewiseFunction handWritten = absoluteValue().unwrap();
    CHECK_TRUE(absolute.branch(0).domain() == handWritten.branch(0).domain());
    CHECK_TRUE(absolute.branch(1).domain() == handWritten.branch(1).domain());

    // 老路径仍然明确拒收 —— 这不是「解析不了」，是「代数函数域装不下」
    CHECK_ERR(parseRadicalExpression("\\sqrt{x^2}"), MathsError::RadicandIsSquare);
  }

  // ---------- 平移、带系数、加减 ----------
  {
    // √((x+1)²) = |x+1|：拐点跟着挪到 −1
    const PiecewiseFunction shifted = parsePiecewiseExpression("\\sqrt{(x+1)^2}").unwrap();
    CHECK_TRUE(shifted.at(number(0)).unwrap() == number(1));
    CHECK_TRUE(shifted.at(number(-3)).unwrap() == number(2));
    CHECK_EQ(shifted.branch(0).domain().latex(), std::string("[-1, +\\infty)"));
    CHECK_EQ(shifted.branch(1).domain().latex(), std::string("(-\\infty, -1)"));

    // √(4x²) = 2|x|
    const PiecewiseFunction scaled = parsePiecewiseExpression("\\sqrt{4x^2}").unwrap();
    CHECK_TRUE(scaled.at(number(-3)).unwrap() == number(6));

    // |x| + 1
    const PiecewiseFunction lifted = parsePiecewiseExpression("\\sqrt{x^2}+1").unwrap();
    CHECK_TRUE(lifted.at(number(-3)).unwrap() == number(4));
    CHECK_TRUE(lifted.at(number(3)).unwrap() == number(4));

    // 2|x| 与 |x|+|x| 是同一个值
    CHECK_TRUE((absoluteValue().unwrap() * RealFunction::constant(Fraction(2, 1))).unwrap().at(number(-4)).unwrap() ==
               number(8));
    CHECK_TRUE(parsePiecewiseExpression("\\sqrt{x^2}+\\sqrt{x^2}").unwrap().at(number(-4)).unwrap() == number(8));
  }

  // ---------- 完全平方的根号与一般的根号混着来 ----------
  {
    // √(x²)·√(x²+1)：两支，规则里还留着那个真正的根号
    const PiecewiseFunction mixed = parsePiecewiseExpression("\\sqrt{x^2}*\\sqrt{x^2+1}").unwrap();
    CHECK_TRUE(mixed.branchCount() == std::size_t(2));
    CHECK_TRUE(mixed.at(number(-2)).unwrap() == number(2) * RealAlgebraicNumber::nthRootOf(Fraction(5, 1), 2).unwrap());
    CHECK_TRUE(mixed.at(number(2)).unwrap() == number(2) * RealAlgebraicNumber::nthRootOf(Fraction(5, 1), 2).unwrap());
  }

  // ---------- 两个完全平方的根号 ----------
  {
    // |x|·|x−1|：三个区间，符号组合互相冲突的那些支定义域为空，被自动丢掉
    const PiecewiseFunction product = parsePiecewiseExpression("\\sqrt{x^2}*\\sqrt{(x-1)^2}").unwrap();
    CHECK_TRUE(product.branchCount() == std::size_t(3));
    CHECK_TRUE(product.at(number(-2)).unwrap() == number(6));      // 2 · 3
    CHECK_TRUE(product.at(number(1, 2)).unwrap() == number(1, 4)); // 0.5 · 0.5
    CHECK_TRUE(product.at(number(3)).unwrap() == number(6));       // 3 · 2
    CHECK_TRUE(product.at(number(1)).unwrap() == number(0));
  }

  // ---------- 没有完全平方的根号：与老入口完全一致 ----------
  {
    const PiecewiseFunction plain = parsePiecewiseExpression("\\sqrt{x^2+1}").unwrap();
    CHECK_TRUE(plain.branchCount() == std::size_t(1));
    CHECK_EQ(plain.branch(0).ruleLatex(), std::string("\\sqrt{x^2 + 1}"));
    CHECK_TRUE(plain.domain().isRealLine());

    const PiecewiseFunction root = parsePiecewiseExpression("\\sqrt{x}").unwrap();
    CHECK_TRUE(root.branchCount() == std::size_t(1));
    CHECK_TRUE(root.at(number(9)).unwrap() == number(3));
    CHECK_EQ(root.domain().latex(), std::string("[0, +\\infty)"));

    // 普通有理函数也照收（单支）
    CHECK_TRUE(parsePiecewiseExpression("x^2-1").unwrap().at(number(3)).unwrap() == number(8));
  }

  // ---------- 直接写 |x|：与 √(x²) 是同一个东西 ----------
  {
    // 内部形式统一是 √(g²)，所以 |x| 与 \sqrt{x^2} 必须得到**完全一样**的分段
    const PiecewiseFunction typed = parsePiecewiseExpression("|x|").unwrap();
    const PiecewiseFunction rooted = parsePiecewiseExpression("\\sqrt{x^2}").unwrap();
    CHECK_TRUE(typed.branch(0).domain() == rooted.branch(0).domain());
    CHECK_TRUE(typed.branch(1).domain() == rooted.branch(1).domain());
    CHECK_TRUE(typed.branch(0).ruleLatex() == rooted.branch(0).ruleLatex());

    // 输出时还原成 |x|：这才是「最终输出把 √(x²) 化简为 |x|」那一步
    CHECK_TRUE(typed.asAbsoluteValue().has_value());
    CHECK_EQ(typed.asAbsoluteValue().value().latex(), std::string("x"));
    CHECK_TRUE(rooted.asAbsoluteValue().has_value());

    // g 可以是任意有理式，不只是变量
    CHECK_EQ(parsePiecewiseExpression("|x+1|").unwrap().asAbsoluteValue().value().latex(), std::string("x + 1"));
    CHECK_EQ(parsePiecewiseExpression("|2x-1|").unwrap().asAbsoluteValue().value().latex(), std::string("2x - 1"));

    // ⚠️ g 自带极点时两支定义域**不是互补**：x = 1 两边都不在（g 那里无定义）。
    //    所以判定必须各自解一次 {g≥0} / {g<0}，不能拿一支的补集去凑 ——
    //    那样 x = 1 会凭空冒进负的那支，绝对值就认不出来了。
    const PiecewiseFunction pole = parsePiecewiseExpression("|\\frac{x}{x-1}|").unwrap();
    CHECK_TRUE(pole.asAbsoluteValue().has_value());
    CHECK_EQ(pole.asAbsoluteValue().value().latex(), std::string("\\frac{x}{x - 1}"));
    CHECK_TRUE(pole.at(number(3)).unwrap() == number(3, 2)); // |3/2|
    CHECK_TRUE(pole.at(number(1, 2)).unwrap() == number(1)); // |(1/2)/(−1/2)| = 1

    // 不是 |g| 形状的就别硬套
    CHECK_TRUE(!parsePiecewiseExpression("\\sqrt{x^2}+1").unwrap().asAbsoluteValue().has_value());
    CHECK_TRUE(!parsePiecewiseExpression("\\sqrt{x^2}*\\sqrt{(x-1)^2}").unwrap().asAbsoluteValue().has_value());
    CHECK_TRUE(!parsePiecewiseExpression("\\sqrt{x^2+1}").unwrap().asAbsoluteValue().has_value());

    // 代入求值：|x| 与手写的绝对值函数给同一个值
    CHECK_TRUE(parsePiecewiseExpression("|x|").unwrap().at(number(-3)).unwrap() == number(3));
    CHECK_TRUE(parsePiecewiseExpression("|x+1|").unwrap().at(number(-3)).unwrap() == number(2));

    // 落单的竖线原样留给解析器报错
    CHECK_ERR(parsePiecewiseExpression("|x"), MathsError::InvalidExpression);
  }

  // ---------- 拒绝的输入 ----------
  {
    // 多元：分段函数是一元的，装不下
    CHECK_ERR(parsePiecewiseExpression("x*y+\\sqrt{x}"), MathsError::NotUnivariate);

    // 高次根仍不支持
    CHECK_ERR(parsePiecewiseExpression("\\sqrt[3]{x^2}"), MathsError::InvalidExpression);

    // 分支出数有上限：k 个完全平方的根号 → 2^k 支，超过就明确拒绝而不是硬撑
    CHECK_ERR(parsePiecewiseExpression("\\sqrt{x^2}*\\sqrt{(x+1)^2}*\\sqrt{(x+2)^2}*\\sqrt{(x+3)^2}*\\sqrt{(x+4)^2}"),
              MathsError::RadicandIsSquare);

    // 四个还在上限内
    CHECK_OK(parsePiecewiseExpression("\\sqrt{x^2}*\\sqrt{(x+1)^2}*\\sqrt{(x+2)^2}*\\sqrt{(x+3)^2}"));

    // 占位变量与原文重名时会自动换一个（{absa}·{absa} 约掉后仍是单变量）
    CHECK_OK(parsePiecewiseExpression("\\sqrt{x^2}*{absa}/{absa}"));
  }

  TEST_SUMMARY();
}
