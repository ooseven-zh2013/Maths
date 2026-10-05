#include "check.hpp"

#include <iostream>
#include <set>
#include <string>

import maths;

using namespace maths;

namespace {

MultiRationalFunction frac(const char *numerator, const char *denominator) {
  return MultiRationalFunction::make(parseExpression(numerator).unwrap().getNumerator(),
                                     parseExpression(denominator).unwrap().getNumerator())
      .unwrap();
}

MultiRationalFunction poly(const char *latex) {
  return MultiRationalFunction(parseExpression(latex).unwrap().getNumerator());
}

MultiFunction function(const char *numerator, const char *denominator) {
  return MultiFunction::make(frac(numerator, denominator)).unwrap();
}

Scope point(const std::string &assignments) {
  // 形如 "x=3,y=4"
  Scope scope;
  std::size_t position = 0;
  while (position < assignments.size()) {
    const std::size_t comma = assignments.find(',', position);
    const std::string piece =
        assignments.substr(position, comma == std::string::npos ? std::string::npos : comma - position);
    const std::size_t equals = piece.find('=');
    const std::string name = piece.substr(0, equals);
    const long long given = std::stoll(piece.substr(equals + 1));
    scope.assign(Variable(name), Fraction(given, 1)).unwrap();
    if (comma == std::string::npos) {
      break;
    }
    position = comma + 1;
  }
  return scope;
}

ConstraintSystem atom(const char *latex, Relation relation) {
  return ConstraintSystem({AtomConstraint(parseExpression(latex).unwrap().getNumerator(), relation)});
}

} // namespace

int main() {
  std::cout << "=== 多元函数 MultiFunction 测试 ===" << '\n';
  std::cout << std::unitbuf;

  // ---------- 构造与定义域 ----------
  {
    const MultiFunction total = MultiFunction::make(poly("x+y")).unwrap();
    CHECK_TRUE(total.isTotal()); // 分母是 1 → 处处有定义
    CHECK_TRUE(!total.isConstant());
    CHECK_TRUE(total.variables() == std::set<Variable>({Variable("x"), Variable("y")}));

    const MultiFunction quotient = function("x", "y");
    CHECK_TRUE(!quotient.isTotal());
    CHECK_TRUE(quotient.domain().atoms().size() == std::size_t(1));
    CHECK_TRUE(quotient.domain().atoms().front().relation() == Relation::NotEqual);

    // 常函数
    CHECK_TRUE(MultiFunction::make(MultiRationalFunction(Fraction(3, 1))).unwrap().isConstant());
    CHECK_TRUE(MultiFunction().isZero());
  }

  // ---------- 点求值 ----------
  {
    // (x+y)/(y−x) 在 (3,4) = 7
    CHECK_TRUE(function("x+y", "y-x").at(point("x=3,y=4")).unwrap() == Fraction(7, 1));
    // x/y 在 (3,4) = 3/4
    CHECK_TRUE(function("x", "y").at(point("x=3,y=4")).unwrap() == Fraction(3, 4));
    // 点在定义域外 → OutsideDomain（不是算错）
    CHECK_ERR(function("x", "y").at(point("x=0,y=0")), MathsError::OutsideDomain);
    // 批量：任一点出错就整批失败
    CHECK_TRUE(function("x", "y").atAll({point("x=1,y=2"), point("x=3,y=4")}).size() == std::size_t(2));
    CHECK_TRUE(function("x", "y").atAll({point("x=1,y=2"), point("x=0,y=0")}).empty());
  }

  // ---------- 收窄定义域 ----------
  {
    const MultiFunction narrowed = MultiFunction::make(poly("x+y"), atom("x", Relation::GreaterEqual)).unwrap();
    CHECK_TRUE(narrowed.domain().atoms().size() == std::size_t(1));
    CHECK_TRUE(narrowed.at(point("x=1,y=1")).unwrap() == Fraction(2, 1));
    CHECK_ERR(narrowed.at(point("x=-1,y=1")), MathsError::OutsideDomain);
    // 再收窄一次 → 合取
    const MultiFunction twice = narrowed.restrict(atom("y", Relation::GreaterEqual)).unwrap();
    CHECK_TRUE(twice.domain().atoms().size() == std::size_t(2));
  }

  // ---------- 四则：定义域取交 ----------
  {
    // x/y 与 x/x 相乘 → x²/y²，定义域要含 y ≠ 0 **和** x ≠ 0
    const MultiFunction product = (function("x", "y") * function("x", "x")).unwrap();
    CHECK_TRUE(product.at(point("x=2,y=4")).unwrap() == Fraction(1, 2)); // (x/y)(x/x) = x/y
    CHECK_TRUE(product.domain().atoms().size() == std::size_t(2));
    CHECK_ERR(product.at(point("x=0,y=1")), MathsError::OutsideDomain); // 规则能算，但不在定义域里

    // 加减与取负
    const MultiFunction sum = (function("x", "y") + function("1", "y")).unwrap();
    CHECK_TRUE(sum.at(point("x=1,y=2")).unwrap() == Fraction(1, 1)); // 1/2 + 1/2
    const MultiFunction difference = (function("x", "y") - function("1", "y")).unwrap();
    CHECK_TRUE(difference.at(point("x=1,y=2")).unwrap() == Fraction(0, 1)); // 化简成 0
    CHECK_TRUE(function("x", "y").negate().at(point("x=3,y=4")).unwrap() == Fraction(-3, 4));
    // 「x/x」化简成 1 但 x ≠ 0 的约束不能丢：下面那条就是在测它
    CHECK_TRUE(function("x", "x").isTotal() == false);
    CHECK_ERR(function("x", "x").at(point("x=0,y=1")), MathsError::OutsideDomain);
    CHECK_TRUE(function("x", "x").at(point("x=3,y=1")).unwrap() == Fraction(1, 1));

    // 缩放
    CHECK_TRUE(function("x", "y").scaledBy(Fraction(2, 1)).unwrap().at(point("x=3,y=4")).unwrap() == Fraction(3, 2));
    // 除以零
    CHECK_ERR(function("x", "y") / MultiFunction::make(MultiRationalFunction(Fraction(0, 1))).unwrap(),
              MathsError::ZeroDenominator);
  }

  // ---------- 判空：多元函数层的定义域 ----------
  {
    // x ≥ 0 与 y ≥ 0 → 非空
    const MultiFunction quadrant = MultiFunction::make(poly("x"), atom("x", Relation::GreaterEqual)).unwrap();
    const MultiFunction also = quadrant.restrict(atom("y", Relation::GreaterEqual)).unwrap();
    CHECK_TRUE(!isEmpty(also.domain()).unwrap());
    // x ≥ 0 与 −1−x ≥ 0 → 空
    const MultiFunction impossible = quadrant.restrict(atom("-1-x", Relation::GreaterEqual)).unwrap();
    CHECK_TRUE(isEmpty(impossible.domain()).unwrap());
  }

  // ---------- 渲染 ----------
  {
    CHECK_TRUE(MultiFunction::make(poly("x+y")).unwrap().latex() == std::string("x + y"));
    CHECK_TRUE(MultiFunction::make(poly("x+y"), atom("x", Relation::GreaterEqual)).unwrap().latex().find('x') !=
               std::string::npos);
  }

  TEST_SUMMARY();
}
