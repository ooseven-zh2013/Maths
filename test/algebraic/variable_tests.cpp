#include "algebraic_expression.hpp"
#include <cassert>
#include <iostream>

int main() {
  using namespace std;

  // 简单变量
  Variable v1("a");
  assert(v1.str() == "a");

  // 带下标的变量（单个索引，无大括号）
  Variable v2("a_b");
  assert(v2.str() == "a_b");

  // 带数字索引：a_1
  Variable v2b("a_1");
  assert(v2b.str() == "a_1");

  // 带大括号单索引：输入 a_{x}，输出为 a_x（实现逻辑会在单索引且索引无自身下标时省略大括号）
  Variable v3("a_{x}");
  assert(v3.str() == "a_x");

  // 多重索引：a_{x,b_c}
  Variable v4("a_{x,b_c}");
  assert(v4.str() == string("a_{x,b_c}"));

  // 单个索引但索引本身带下标：a_{b_c} -> 应当保持大括号
  Variable v5("a_{b_c}");
  assert(v5.str() == string("a_{b_c}"));

  // 非法名字（字母或纯数字以外）应该抛出异常

  bool threw = false;
  try {
    Variable v6("1a");
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  try {
    Variable v7("114_a");
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  try {
    Variable v8("");
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  try {
    Variable v9(".");
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  assert(threw);

  cout << "variable_tests: PASS\n";
  return 0;
}
