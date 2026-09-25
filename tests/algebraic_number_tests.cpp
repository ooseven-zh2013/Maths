#include "check.hpp"

#include <compare>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

// x^2 - 2 在 [1, 2] 里的根，即 √2
RealAlgebraicNumber sqrtTwo() {
  const UnivariatePolynomial polynomial(std::vector<Fraction>{Fraction(-2, 1), Fraction(0, 1), Fraction(1, 1)});
  return RealAlgebraicNumber::create(polynomial, Fraction(1, 1), Fraction(2, 1)).unwrap();
}

RealAlgebraicNumber sqrtOf(long long value) { return RealAlgebraicNumber::squareRootOf(Fraction(value, 1)).unwrap(); }

} // namespace

int runTests() {
  std::cout << "=== 实代数数 RealAlgebraicNumber 测试 ===" << std::endl;
  std::cout << std::unitbuf; // 崩溃时也能看到已输出的断言结果

  // ---------- 一元多项式基础 ----------
  {
    const UnivariatePolynomial poly(std::vector<Fraction>{Fraction(-2, 1), Fraction(0, 1), Fraction(1, 1)});
    CHECK_EQ(poly.str(), std::string("x^2 - 2"));
    CHECK_EQ(poly.latex(), std::string("x^{2} - 2"));
    CHECK_TRUE(poly.evaluate(Fraction(2, 1)) == Fraction(2, 1));  // 4 - 2
    CHECK_TRUE(poly.evaluate(Fraction(1, 1)) == Fraction(-1, 1)); // 1 - 2
    CHECK_EQ(poly.derivative().str(), std::string("2x"));
    CHECK_EQ(poly.degree(), std::size_t(2));

    // 平方自由化：(x - 1)^2 (x + 1) = x^3 - x^2 - x + 1 应被约成 x^2 - 1
    const UnivariatePolynomial repeated(
        std::vector<Fraction>{Fraction(1, 1), Fraction(-1, 1), Fraction(-1, 1), Fraction(1, 1)});
    CHECK_EQ(repeated.squareFreePart().str(), std::string("x^2 - 1"));

    // gcd(x^2 - 1, x^2 - 2x + 1) = x - 1
    const UnivariatePolynomial first(std::vector<Fraction>{Fraction(-1, 1), Fraction(0, 1), Fraction(1, 1)});
    const UnivariatePolynomial second(std::vector<Fraction>{Fraction(1, 1), Fraction(-2, 1), Fraction(1, 1)});
    CHECK_EQ(UnivariatePolynomial::gcd(first, second).str(), std::string("x - 1"));

    // Sturm 计数：x^2 - 2 在 [-2, 2] 里有两个实根，在 [1, 2] 里只有一个
    CHECK_EQ(UnivariatePolynomial::countRealRootsIn(poly, Fraction(-2, 1), Fraction(2, 1)), 2);
    CHECK_EQ(UnivariatePolynomial::countRealRootsIn(poly, Fraction(1, 1), Fraction(2, 1)), 1);
    CHECK_EQ(UnivariatePolynomial::countRealRootsIn(poly, Fraction(2, 1), Fraction(3, 1)), 0);
  }

  // ---------- 构造与有理退化 ----------
  {
    const RealAlgebraicNumber root = sqrtTwo();
    CHECK_TRUE(!root.isRational());
    CHECK_EQ(root.degree(), std::size_t(2));
    CHECK_TRUE(root.asRational() == std::nullopt);

    // 完全平方数直接落回有理数
    const RealAlgebraicNumber four = sqrtOf(4);
    CHECK_TRUE(four.isRational());
    CHECK_TRUE(four == Fraction(2, 1));
    CHECK_EQ(four.str(), std::string("2"));

    const RealAlgebraicNumber zero;
    CHECK_TRUE(zero.isZero());
    CHECK_TRUE(zero.isRational());
  }

  // ---------- 乘除：√2 · √2 = 2 ----------
  {
    const RealAlgebraicNumber root = sqrtTwo();
    const RealAlgebraicNumber squared = root * root;
    CHECK_TRUE(squared == Fraction(2, 1));
    CHECK_TRUE(squared.compareToRational(Fraction(2, 1)) == std::strong_ordering::equal);
  }

  // ---------- 加减：抵消与结合 ----------
  {
    const RealAlgebraicNumber rootTwo = sqrtTwo();
    const RealAlgebraicNumber rootThree = sqrtOf(3);

    const RealAlgebraicNumber sum = rootTwo + rootThree;
    CHECK_TRUE((sum - rootThree) == rootTwo); // (√2 + √3) - √3 = √2
    CHECK_TRUE((rootTwo - rootTwo).isZero()); // √2 - √2 = 0
    CHECK_TRUE((-rootTwo + rootTwo).isZero());
  }

  // ---------- 与有理数比较 ----------
  {
    const RealAlgebraicNumber root = sqrtTwo();
    CHECK_TRUE(root > Fraction(7, 5)); // 1.4 < √2
    CHECK_TRUE(root < Fraction(3, 2)); // √2 < 1.5
    CHECK_TRUE(root.compareToRational(Fraction(1, 1)) == std::strong_ordering::greater);
    CHECK_TRUE(!(root == Fraction(1, 1)));

    // 精化后区间应更贴近真值，但比较结论不变
    RealAlgebraicNumber refined = sqrtTwo();
    refined.refine();
    refined.refine();
    CHECK_TRUE(refined > Fraction(7, 5));
    CHECK_TRUE(refined < Fraction(3, 2));
    CHECK_TRUE(refined == root); // √2 与 √2 是同一个数
  }

  // ---------- 倒数 ----------
  {
    const RealAlgebraicNumber root = sqrtTwo();
    const RealAlgebraicNumber reciprocal = root.inverse().unwrap();
    CHECK_TRUE((reciprocal * root) == Fraction(1, 1));

    const RealAlgebraicNumber quotient = (root / root).unwrap();
    CHECK_TRUE(quotient == Fraction(1, 1));

    // 除以零
    const RealAlgebraicNumber zero;
    CHECK_ERR(root / zero, MathsError::DivisionByZero);
    CHECK_ERR(zero.inverse(), MathsError::DivisionByZero);
  }

  // ---------- n 次根 ----------
  {
    const RealAlgebraicNumber cubeRoot = RealAlgebraicNumber::nthRootOf(Fraction(2, 1), 3).unwrap();
    const RealAlgebraicNumber cubed = cubeRoot * cubeRoot * cubeRoot;
    CHECK_TRUE(cubed == Fraction(2, 1));
    CHECK_TRUE(cubeRoot > Fraction(1, 1));
    CHECK_TRUE(cubeRoot < Fraction(3, 2));

    // 奇次根允许负数：∛(-8) = -2，且能落回有理数
    const RealAlgebraicNumber negativeCubeRoot = RealAlgebraicNumber::nthRootOf(Fraction(-8, 1), 3).unwrap();
    CHECK_TRUE(negativeCubeRoot.isRational());
    CHECK_TRUE(negativeCubeRoot == Fraction(-2, 1));

    // 偶数次根下为负没有实根
    CHECK_ERR(RealAlgebraicNumber::squareRootOf(Fraction(-1, 1)), MathsError::InvalidRange);
    CHECK_ERR(RealAlgebraicNumber::nthRootOf(Fraction(-2, 1), 4), MathsError::InvalidRange);

    // 嵌套：√(√2) 的平方应等于 √2
    const RealAlgebraicNumber root = sqrtTwo();
    const RealAlgebraicNumber fourthRoot = root.sqrt().unwrap();
    const RealAlgebraicNumber fourthSquared = fourthRoot * fourthRoot;
    CHECK_TRUE(fourthSquared == root);
  }

  // ---------- 黄金比例：φ^2 = φ + 1 ----------
  {
    // x^2 - x - 1 在 [1, 2] 里的根
    const UnivariatePolynomial polynomial(std::vector<Fraction>{Fraction(-1, 1), Fraction(-1, 1), Fraction(1, 1)});
    const RealAlgebraicNumber phi = RealAlgebraicNumber::create(polynomial, Fraction(1, 1), Fraction(2, 1)).unwrap();

    const RealAlgebraicNumber squared = phi * phi;
    const RealAlgebraicNumber shifted = phi + RealAlgebraicNumber(Fraction(1, 1));
    CHECK_TRUE(squared == shifted);
    CHECK_TRUE(phi > Fraction(8, 5)); // 1.6 < φ
    CHECK_TRUE(phi < Fraction(17, 10));
  }

  // ---------- 大小比较的传递性 ----------
  {
    const RealAlgebraicNumber rootTwo = sqrtTwo();
    const RealAlgebraicNumber rootThree = sqrtOf(3);
    const RealAlgebraicNumber rootFive = sqrtOf(5);

    CHECK_TRUE(rootTwo < rootThree);
    CHECK_TRUE(rootThree < rootFive);
    CHECK_TRUE(rootTwo < rootFive);
    CHECK_TRUE(rootFive > rootTwo);

    // 和与其中一个的大小
    const RealAlgebraicNumber sum = rootTwo + rootThree;
    CHECK_TRUE(sum > rootTwo);
    CHECK_TRUE(sum > rootThree);
  }

  // ---------- 非法构造 ----------
  {
    const UnivariatePolynomial constant(std::vector<Fraction>{Fraction(3, 1)});
    CHECK_ERR(RealAlgebraicNumber::create(constant, Fraction(0, 1), Fraction(1, 1)), MathsError::InvalidExpression);

    // 区间内含两个根，不足以唯一确定
    const UnivariatePolynomial polynomial(std::vector<Fraction>{Fraction(-2, 1), Fraction(0, 1), Fraction(1, 1)});
    CHECK_ERR(RealAlgebraicNumber::create(polynomial, Fraction(-2, 1), Fraction(2, 1)), MathsError::InvalidRange);

    // 区间内没有根
    CHECK_ERR(RealAlgebraicNumber::create(polynomial, Fraction(3, 1), Fraction(5, 1)), MathsError::InvalidRange);
  }

  // ---------- 输出 ----------
  {
    const RealAlgebraicNumber root = sqrtTwo();
    CHECK_EQ(RealAlgebraicNumber(Fraction(-3, 2)).str(), std::string("-3/2"));
    CHECK_EQ(RealAlgebraicNumber(Fraction(-3, 2)).latex(), std::string("-\\frac{3}{2}"));
    CHECK_TRUE(!root.str().empty());
    CHECK_TRUE(!root.latex().empty());
  }

  TEST_SUMMARY();
}

int main() {
  // 包一层，避免未捕获异常把整个进程带走后只剩一个晦涩的退出码，
  // 连是哪一步出的问题都看不到
  try {
    return runTests();
  } catch (const std::exception &error) {
    std::cout << "未捕获异常: " << error.what() << std::endl;
    return 1;
  }
}
