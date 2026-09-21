#include "algebraic_expression.hpp"
#include "check.hpp"
#include <iostream>
#include <sstream>
#include <string>

int main() {
  std::cout << "=== Polynomial 测试 ===" << '\n';

  const Variable x("x");
  const Variable y("y");
  const Fraction one(1, 1);

  const Monomial x1(one, {{x, 1ULL}});
  const Monomial y1(one, {{y, 1ULL}});
  const Monomial one1(one);
  const Monomial twoX(Fraction(2, 1), {{x, 1ULL}});
  const Monomial threeX(Fraction(3, 1), {{x, 1ULL}});

  // 1. 构造
  {
    const Polynomial zero;
    CHECK_TRUE(zero.isZero());
    CHECK_TRUE(zero.isMonomial()); // 零多项式视为零单项式
    CHECK_OK(zero.toMonomial());   // 零多项式可化简为零单项式
    CHECK_TRUE(zero.toMonomial().unwrap().isZero());
    CHECK_EQ(zero.str(), std::string("0"));
    CHECK_EQ(zero.getTerms().size(), 0ULL);

    const Polynomial fromMono(x1); // 单项式隐式提升
    CHECK_TRUE(!fromMono.isZero());
    CHECK_TRUE(fromMono.isMonomial());
    CHECK_EQ(fromMono.str(), std::string("x"));

    const Polynomial fromZeroMono{(Monomial())};
    CHECK_TRUE(fromZeroMono.isZero());
    CHECK_EQ(fromZeroMono.getTerms().size(), 0ULL);
  }

  // 2. 加法：同类项合并、异类项保留、相消为零
  {
    const Polynomial merged = Polynomial(twoX) + Polynomial(threeX);
    CHECK_EQ(merged.getTerms().size(), 1ULL);
    CHECK_TRUE(merged.isMonomial());
    CHECK_EQ(merged.toMonomial().unwrap().str(), std::string("5 x"));

    const Polynomial binom = Polynomial(x1) + Polynomial(y1);
    CHECK_EQ(binom.getTerms().size(), 2ULL);
    CHECK_TRUE(!binom.isMonomial());
    CHECK_ERR(binom.toMonomial(), MathsError::NotAMonomial);

    // Monomial + Monomial 直接得到 Polynomial
    CHECK_EQ(x1 + y1, binom);

    const Polynomial cancelled = Polynomial(x1) - Polynomial(x1);
    CHECK_TRUE(cancelled.isZero());
    CHECK_EQ(cancelled.getTerms().size(), 0ULL);
    CHECK_TRUE(cancelled.isMonomial());
    CHECK_TRUE(cancelled.toMonomial().unwrap().isZero());
    CHECK_EQ(cancelled.str(), std::string("0"));

    // 三项相加后塌回单项式
    const Polynomial collapsed = Polynomial(x1) + Polynomial(y1) - Polynomial(y1);
    CHECK_EQ(collapsed.getTerms().size(), 1ULL);
    CHECK_TRUE(collapsed.isMonomial());
    CHECK_EQ(collapsed.toMonomial().unwrap(), x1);
  }

  // 3. 减法与取负
  {
    const Polynomial diff = Polynomial(x1) - Polynomial(y1);
    CHECK_EQ(diff.getTerms().size(), 2ULL);
    CHECK_EQ(diff.str(), std::string("x - y"));

    const Polynomial negated = -diff;
    CHECK_EQ(negated.str(), std::string("-x + y"));
    CHECK_EQ(negated + diff, Polynomial());

    // Monomial 在左侧
    CHECK_EQ(x1 - Polynomial(y1), diff);
  }

  // 4. 乘法与展开
  {
    // (x + 1)(x - 1) = x^2 - 1
    const Polynomial plusOne = Polynomial(x1) + Polynomial(one1);
    const Polynomial minusOne = Polynomial(x1) - Polynomial(one1);
    CHECK_OK(plusOne * minusOne);
    const Polynomial product = (plusOne * minusOne).unwrap();
    CHECK_EQ(product.getTerms().size(), 2ULL);
    CHECK_EQ(product.str(), std::string("x^2 - 1"));
    CHECK_ERR(product.toMonomial(), MathsError::NotAMonomial);

    // (x + y)(x - y) = x^2 - y^2
    const Polynomial difference = ((Polynomial(x1) + Polynomial(y1)) * (Polynomial(x1) - Polynomial(y1))).unwrap();
    CHECK_EQ(difference.getTerms().size(), 2ULL);
    CHECK_EQ(difference.str(), std::string("x^2 - y^2"));

    // (x + y)^2 = x^2 + 2 x y + y^2
    const Polynomial binom = Polynomial(x1) + Polynomial(y1);
    const Polynomial squared = (binom * binom).unwrap();
    CHECK_EQ(squared.getTerms().size(), 3ULL);
    CHECK_EQ(squared.str(), std::string("x^2 + 2 x y + y^2"));
    CHECK_EQ(squared.degree(), 2ULL);

    // Monomial 在左侧参与乘法
    const Polynomial scaled = (x1 * binom).unwrap();
    CHECK_EQ(scaled.getTerms().size(), 2ULL);
    CHECK_EQ(scaled.str(), std::string("x^2 + x y"));
  }

  // 5. 复合赋值
  {
    Polynomial p = Polynomial(x1) + Polynomial(one1);
    p *= p; // (x + 1)^2 = x^2 + 2 x + 1
    CHECK_EQ(p.str(), std::string("x^2 + 2 x + 1"));
    CHECK_EQ(p.degree(), 2ULL);

    Polynomial q = Polynomial(x1) + Polynomial(y1);
    q -= Polynomial(y1);
    CHECK_EQ(q.getTerms().size(), 1ULL);
    CHECK_EQ(q.toMonomial().unwrap().str(), std::string("x"));

    Polynomial r = Polynomial(x1);
    r += Polynomial(y1);
    CHECK_EQ(r.getTerms().size(), 2ULL);
  }

  // 6. isMonomial / toMonomial 边界
  {
    const Polynomial single(Monomial(Fraction(3, 2), {{x, 2ULL}, {y, 1ULL}}));
    CHECK_TRUE(single.isMonomial());
    CHECK_EQ(single.toMonomial().unwrap().str(), std::string("3/2 x^2 y"));

    const Polynomial multiple = single + Polynomial(one1);
    CHECK_TRUE(!multiple.isMonomial());
    CHECK_ERR(multiple.toMonomial(), MathsError::NotAMonomial);
  }

  // 7. 相等：不同构造路径应得到同一个多项式
  {
    const Polynomial expanded = ((Polynomial(x1) + Polynomial(y1)) * (Polynomial(x1) + Polynomial(y1))).unwrap();
    const Polynomial manual = Polynomial(Monomial(one, {{x, 2ULL}})) +
                              Polynomial(Monomial(Fraction(2, 1), {{x, 1ULL}, {y, 1ULL}})) +
                              Polynomial(Monomial(one, {{y, 2ULL}}));
    CHECK_EQ(expanded, manual);
  }

  // 8. 流输出
  {
    std::ostringstream os;
    os << (Polynomial(x1) + Polynomial(one1));
    CHECK_EQ(os.str(), std::string("x + 1"));
  }

  TEST_SUMMARY();
}
