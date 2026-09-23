#include "check.hpp"

#include <iostream>
#include <string>

#include <maths/algebra/algebraic_expression.hpp>

int main() {
  std::cout << "=== Variable 名称解析测试 ===" << '\n';

  // 1. 简单变量
  {
    CHECK_EQ(Variable("a").str(), std::string("a"));
    CHECK_EQ(Variable("alpha").str(), std::string("alpha"));
  }

  // 2. 单字符下标（不带大括号）
  {
    CHECK_EQ(Variable("a_b").str(), std::string("a_b"));
    CHECK_EQ(Variable("a_1").str(), std::string("a_1"));
  }

  // 3. 大括号单下标：a_{x} 规范化为 a_x
  {
    CHECK_EQ(Variable("a_{x}").str(), std::string("a_x"));
  }

  // 4. 多重下标
  {
    CHECK_EQ(Variable("a_{x,b_c}").str(), std::string("a_{x,b_c}"));
    CHECK_EQ(Variable("a_{1,2}").str(), std::string("a_{1,2}"));
  }

  // 5. 单下标本身带下标时保留大括号
  {
    CHECK_EQ(Variable("a_{b_c}").str(), std::string("a_{b_c}"));
  }

  // 6. hasIndex
  {
    CHECK_TRUE(!Variable("a").hasIndex());
    CHECK_TRUE(Variable("a_b").hasIndex());
    CHECK_TRUE(Variable("a_{x,b}").hasIndex());
  }

  // 7. 比较
  {
    CHECK_TRUE(Variable("a") < Variable("b"));
    CHECK_TRUE(Variable("a_b") == Variable("a_b"));
    CHECK_TRUE(Variable("a_b") != Variable("a_c"));
    CHECK_TRUE(Variable("a") < Variable("a_b")); // 同名时无下标者更小
    CHECK_TRUE(Variable("a_{b}") == Variable("a_b"));
  }

  // 8. 非法名称
  {
    CHECK_THROWS(Variable("1a"), MathsException);
    CHECK_THROWS(Variable("114_a"), MathsException);
    CHECK_THROWS(Variable(""), MathsException);
    CHECK_THROWS(Variable("."), MathsException);
    CHECK_THROWS(Variable("a_bc"), MathsException); // 无大括号下标必须为单个字符
    CHECK_THROWS(Variable("a_"), MathsException);
    CHECK_THROWS(Variable("abc_"), MathsException);
  }

  // 9. Name 基础行为
  {
    CHECK_EQ(Name("abc").str(), std::string("abc"));
    CHECK_THROWS(Name("a1"), MathsException); // 不能字母数字混合
    CHECK_THROWS(Name(""), MathsException);
    CHECK_TRUE(Name("a") < Name("b"));
  }

  TEST_SUMMARY();
}
