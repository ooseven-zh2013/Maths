#include "numbers.hpp"
#include <iostream>
#define endl '\n'

signed main() {
  std::cout << "=== Integer 类功能测试 ===" << endl << endl;

  // 1. 基本构造和赋值
  std::cout << "1. 构造和赋值测试:" << endl;
  Integer a(5LL);
  Integer b(-3LL);
  Integer c(0ULL);
  std::cout << "a = " << a << ", b = " << b << ", c = " << c << endl << endl;

  // 2. 算术运算
  std::cout << "2. 算术运算测试:" << endl;
  std::cout << a << " + " << b << " = " << (a + b) << endl;
  std::cout << a << " - " << b << " = " << (a - b) << endl;
  std::cout << a << " * " << b << " = " << (a * b) << endl;
  std::cout << a << " / " << b << " = " << (a / b) << endl;
  std::cout << a << " % " << b << " = " << (a % b) << endl << endl;

  // 3. 复合赋值运算
  std::cout << "3. 复合赋值运算测试:" << endl;
  Integer d(10LL);
  d += Integer(5LL);
  std::cout << "d += 5: d = " << d << endl;
  d -= Integer(3LL);
  std::cout << "d -= 3: d = " << d << endl;
  d *= Integer(2LL);
  std::cout << "d *= 2: d = " << d << endl;
  d /= Integer(4LL);
  std::cout << "d /= 4: d = " << d << endl << endl;

  // 4. 自增自减
  std::cout << "4. 自增自减测试:" << endl;
  Integer e(7LL);
  std::cout << "e = " << e << endl;
  std::cout << "++e = " << (++e) << endl;
  std::cout << "e++ = " << (e++) << ", 之后 e = " << e << endl;
  std::cout << "--e = " << (--e) << endl;
  std::cout << "e-- = " << (e--) << ", 之后 e = " << e << endl << endl;

  // 5. 幂运算（正指数）
  std::cout << "5. 幂运算测试（正指数）:" << endl;
  Integer base1(2LL);
  Integer exp1(10LL);
  Fraction result1 = base1.pow(exp1);
  std::cout << base1 << "^" << exp1 << " = " << result1 << " (应该是 1024)" << endl;

  Integer base2(-3LL);
  Integer exp2(3LL);
  Fraction result2 = base2.pow(exp2);
  std::cout << base2 << "^" << exp2 << " = " << result2 << " (应该是 -27)" << endl;

  Integer base3(-2LL);
  Integer exp3(4LL);
  Fraction result3 = base3.pow(exp3);
  std::cout << base3 << "^" << exp3 << " = " << result3 << " (应该是 16)" << endl << endl;

  // 6. 幂运算（负指数）
  std::cout << "6. 幂运算测试（负指数）:" << endl;
  Integer base4(2LL);
  Integer exp4(-3LL);
  Fraction result4 = base4.pow(exp4);
  std::cout << base4 << "^" << exp4 << " = " << result4 << " (应该是 1/8)" << endl;

  Integer base5(-2LL);
  Integer exp5(-3LL);
  Fraction result5 = base5.pow(exp5);
  std::cout << base5 << "^" << exp5 << " = " << result5 << " (应该是 -1/8)" << endl;

  Integer base6(-3LL);
  Integer exp6(-2LL);
  Fraction result6 = base6.pow(exp6);
  std::cout << base6 << "^" << exp6 << " = " << result6 << " (应该是 1/9)" << endl << endl;

  // 7. 幂运算（零指数）
  std::cout << "7. 幂运算测试（零指数）:" << endl;
  Integer base7(123LL);
  Integer exp7(0LL);
  Fraction result7 = base7.pow(exp7);
  std::cout << base7 << "^" << exp7 << " = " << result7 << " (应该是 1)" << endl << endl;

  // 8. 边界情况
  std::cout << "8. 边界情况测试:" << endl;
  Integer zero(0LL);
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

  // 9. 流输入输出
  std::cout << "9. 流输入测试（请输入一个整数）:" << endl;
  Integer input;
  std::cin >> input;
  std::cout << "你输入的是: " << input << endl << endl;

  std::cout << "=== 测试完成 ===" << endl;
  return 0;
}
