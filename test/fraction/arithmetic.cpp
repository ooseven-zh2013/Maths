#include "numbers.hpp"
#include <iostream>
#define endl '\n'

signed main() {
  std::cout << "=== Fraction 类功能测试 ===" << endl << endl;

  // 1. 基本构造和赋值
  std::cout << "1. 构造和赋值测试:" << endl;
  Fraction a(3LL, 4LL);     // 3/4
  Fraction b(-2LL, 5LL);    // -2/5
  Fraction c(6LL, 1LL);     // 6 (整数形式)
  std::cout << "a = " << a << ", b = " << b << ", c = " << c << endl << endl;

  // 2. 算术运算
  std::cout << "2. 算术运算测试:" << endl;
  std::cout << a << " + " << b << " = " << (a + b) << endl;
  std::cout << a << " - " << b << " = " << (a - b) << endl;
  std::cout << a << " * " << b << " = " << (a * b) << endl;
  std::cout << a << " / " << b << " = " << (a / b) << endl << endl;

  // 3. 复合赋值运算
  std::cout << "3. 复合赋值运算测试:" << endl;
  Fraction d(1LL, 2LL);  // 1/2
  d += Fraction(1LL, 3LL);  // 1/2 + 1/3 = 5/6
  std::cout << "d += 1/3: d = " << d << endl;
  d -= Fraction(1LL, 6LL);  // 5/6 - 1/6 = 4/6 = 2/3
  std::cout << "d -= 1/6: d = " << d << endl;
  d *= Fraction(3LL, 4LL);  // 2/3 * 3/4 = 6/12 = 1/2
  std::cout << "d *= 3/4: d = " << d << endl;
  d /= Fraction(1LL, 4LL);  // 1/2 ÷ 1/4 = 2
  std::cout << "d /= 1/4: d = " << d << endl << endl;

  // 4. 比较运算
  std::cout << "4. 比较运算测试:" << endl;
  Fraction e(1LL, 2LL);  // 1/2
  Fraction f(2LL, 3LL);  // 2/3
  std::cout << e << " < " << f << ": " << (e < f) << " (应该是 1)" << endl;
  std::cout << e << " > " << f << ": " << (e > f) << " (应该是 0)" << endl;
  std::cout << e << " == " << Fraction(2LL, 4LL) << ": " << (e == Fraction(2LL, 4LL)) << " (应该是 1)" << endl;
  std::cout << e << " == 0: " << (e == 0LL) << " (应该是 0)" << endl;
  std::cout << e << " == 1: " << (e == 1LL) << " (应该是 0)" << endl << endl;

  // 5. 负数运算
  std::cout << "5. 负数运算测试:" << endl;
  Fraction g(-3LL, 4LL);   // -3/4
  Fraction h(1LL, 2LL);    // 1/2
  std::cout << g << " + " << h << " = " << (g + h) << " (应该是 -1/4)" << endl;
  std::cout << "-" << g << " = " << (-g) << " (应该是 3/4)" << endl;
  std::cout << g << " * " << h << " = " << (g * h) << " (应该是 -3/8)" << endl << endl;

  // 6. 幂运算（正指数）
  std::cout << "6. 幂运算测试（正指数）:" << endl;
  Fraction base1(2LL, 3LL);   // 2/3
  Integer exp1(3LL);          // 3
  Fraction result1 = base1.pow(exp1);
  std::cout << "(" << base1 << ")^" << exp1 << " = " << result1 << " (应该是 8/27)" << endl;

  Fraction base2(-1LL, 2LL);  // -1/2
  Integer exp2(4LL);          // 4
  Fraction result2 = base2.pow(exp2);
  std::cout << "(" << base2 << ")^" << exp2 << " = " << result2 << " (应该是 1/16)" << endl;

  Fraction base3(-2LL, 3LL);  // -2/3
  Integer exp3(3LL);          // 3
  Fraction result3 = base3.pow(exp3);
  std::cout << "(" << base3 << ")^" << exp3 << " = " << result3 << " (应该是 -8/27)" << endl << endl;

  // 7. 幂运算（负指数）
  std::cout << "7. 幂运算测试（负指数）:" << endl;
  Fraction base4(2LL, 3LL);   // 2/3
  Integer exp4(-2LL);         // -2
  Fraction result4 = base4.pow(exp4);
  std::cout << "(" << base4 << ")^" << exp4 << " = " << result4 << " (应该是 9/4)" << endl;

  Fraction base5(-1LL, 2LL);  // -1/2
  Integer exp5(-3LL);         // -3
  Fraction result5 = base5.pow(exp5);
  std::cout << "(" << base5 << ")^" << exp5 << " = " << result5 << " (应该是 -8)" << endl << endl;

  // 8. 幂运算（零指数）
  std::cout << "8. 幂运算测试（零指数）:" << endl;
  Fraction base6(123LL, 456LL);
  Integer exp6(0LL);
  Fraction result6 = base6.pow(exp6);
  std::cout << "(" << base6 << ")^" << exp6 << " = " << result6 << " (应该是 1)" << endl << endl;

  // 9. 边界情况
  std::cout << "9. 边界情况测试:" << endl;
  Fraction zero(0LL, 1LL);
  Integer exp_pos(5LL);
  Fraction result_zero = zero.pow(exp_pos);
  std::cout << "0^" << exp_pos << " = " << result_zero << " (应该是 0)" << endl;

  try {
    Integer exp_neg(-2LL);
    Fraction result_err = zero.pow(exp_neg);
    std::cout << "错误：应该抛出异常！" << endl;
  } catch (const std::domain_error &e) {
    std::cout << "正确捕获异常: " << e.what() << endl;
  }
  std::cout << endl;

  // 10. 转换为 double
  std::cout << "10. 转换为 double 测试:" << endl;
  Fraction dbl_test(1LL, 3LL);
  double dbl_val = static_cast<double>(dbl_test);
  std::cout << dbl_test << " = " << dbl_val << " (应该是 0.333...)" << endl;
  Fraction neg_dbl(-3LL, 4LL);
  double neg_dbl_val = static_cast<double>(neg_dbl);
  std::cout << neg_dbl << " = " << neg_dbl_val << " (应该是 -0.75)" << endl << endl;

  // 11. 流输入输出
  std::cout << "11. 流输入测试（请输入一个分数，如 3/4 或 5）:" << endl;
  Fraction input;
  std::cin >> input;
  std::cout << "你输入的是: " << input << endl << endl;

  std::cout << "=== 测试完成 ===" << endl;
  return 0;
}
