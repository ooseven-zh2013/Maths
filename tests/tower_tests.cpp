#include "check.hpp"

#include <iostream>
#include <string>
#include <vector>

import maths;

using namespace maths;

namespace {

RationalFunction expression(const char *latex) { return parseExpression(latex).unwrap(); }

RationalFunction number(long long numerator, long long denominator = 1) {
  return RationalFunction(Fraction(numerator, denominator));
}

using Flat = TowerExtension::Flat;

// 深度 1：y₁² = x
const std::vector<Flat> kRootX = {Flat{expression("x")}};

// 深度 2：y₁² = x、y₂² = 1 + y₁   （即 √(1+√x)）
// relations[i] 是平表：relations[1] = {1, 1} 表示 y₂² = 1 + y₁
const std::vector<Flat> kNested = {Flat{expression("x")}, Flat{number(1), number(1)}};

TowerExtension element(const std::vector<Flat> &relations, const Flat &flat) {
  return TowerExtension::fromMasks(relations, flat).unwrap();
}

TowerExtension rational(long long numerator, long long denominator = 1) {
  return TowerExtension::rational(number(numerator, denominator)).unwrap();
}

TowerExtension rootOfX() { return element(kRootX, Flat{number(0), number(1)}); }

TowerExtension rootOfNested() { return element(kNested, Flat{number(0), number(0), number(1), number(0)}); }

TowerExtension times(const TowerExtension &lhs, const TowerExtension &rhs) { return (lhs * rhs).unwrap(); }

RealAlgebraicNumber valueAt(const TowerExtension &value, long long x) {
  Scope scope;
  scope.assign(Variable("x"), Fraction(x, 1)).unwrap();
  return value.evaluate(scope).unwrap();
}

} // namespace

int main() {
  std::cout << "=== 塔式扩张 TowerExtension 测试 ===" << std::endl;
  std::cout << std::unitbuf; // 崩溃时也能看到已输出的断言结果

  // ---------- 深度 0：退化成有理函数 ----------
  {
    const TowerExtension three = rational(3);
    const TowerExtension four = rational(4);
    CHECK_TRUE(three.depth() == std::size_t(0));
    CHECK_TRUE(three.isConstant());
    CHECK_TRUE((three + four).unwrap() == rational(7));
    CHECK_TRUE((four - three).unwrap() == rational(1));
    CHECK_TRUE(times(three, four) == rational(12));
    CHECK_TRUE(three.dividedBy(four).unwrap() == rational(3, 4));
    CHECK_TRUE(!TowerExtension::rational(expression("x")).unwrap().isConstant());
    CHECK_TRUE(rational(0).isZero());
  }

  // ---------- 深度 1：乘法要把 y₁² 换回 f₁ = x ----------
  {
    CHECK_TRUE(times(rootOfX(), rootOfX()) == element(kRootX, Flat{expression("x"), number(0)}));
    // (1+√x)² = 1 + x + 2√x
    const TowerExtension onePlusRoot = element(kRootX, Flat{number(1), number(1)});
    CHECK_TRUE(times(onePlusRoot, onePlusRoot) == element(kRootX, Flat{number(1) + expression("x"), number(2)}));
    CHECK_TRUE((rootOfX() + rootOfX()).unwrap() == element(kRootX, Flat{number(0), number(2)}));
    // (1+√x) − √x = 1。期望值必须建在**同一条塔**上：`rational(1)` 是深度 0 的元素，
    // 与深度 1 的常数 1 分属不同的域，判等为假（这是对的，不是 bug）
    CHECK_TRUE((onePlusRoot - rootOfX()).unwrap() == element(kRootX, Flat{number(1), number(0)}));
  }

  // ---------- 深度 1：求值与除法 ----------
  {
    CHECK_TRUE(valueAt(rootOfX(), 4) == RealAlgebraicNumber(Fraction(2, 1)));
    CHECK_TRUE(valueAt(times(rootOfX(), rootOfX()), 4) == RealAlgebraicNumber(Fraction(4, 1)));
    // 1/√x 在 x = 4 处是 1/2（除法走的是 2^d 维乘法矩阵的高斯消元）。
    // 分子必须建在**同一条塔**上 —— 深度 0 的 1 与深度 1 的 √x 分属不同的域
    const TowerExtension one = element(kRootX, Flat{number(1), number(0)});
    const TowerExtension inverse = one.dividedBy(rootOfX()).unwrap();
    CHECK_TRUE(valueAt(inverse, 4) == RealAlgebraicNumber(Fraction(1, 2)));
    CHECK_ERR(rational(1).dividedBy(rational(0)), MathsError::DivisionByZero);
  }

  // ---------- 深度 2：y₂² = 1 + y₁ 会引出低编号生成元 ----------
  {
    // y₂·y₂ = 1 + y₁ —— 这是塔与「独立生成元」唯一真正的差别
    CHECK_TRUE(times(rootOfNested(), rootOfNested()) ==
               element(kNested, Flat{number(1), number(1), number(0), number(0)}));
    // 求值：x = 3 时 √(1+√3)。1 + √3 不是有理数，只能在代数数上算
    const RealAlgebraicNumber rootThree = RealAlgebraicNumber::nthRootOf(Fraction(3, 1), 2).unwrap();
    const RealAlgebraicNumber expected = (RealAlgebraicNumber(Fraction(1, 1)) + rootThree).nthRoot(2).unwrap();
    CHECK_TRUE(valueAt(rootOfNested(), 3) == expected);
    // y₁·y₂ = √x·√(1+√x)
    const TowerExtension y1 = element(kNested, Flat{number(0), number(1), number(0), number(0)});
    const TowerExtension product = times(y1, rootOfNested());
    CHECK_TRUE(product == element(kNested, Flat{number(0), number(0), number(0), number(1)}));
    CHECK_TRUE(valueAt(product, 3) == rootThree * expected);
  }

  // ---------- 在塔上按占位变量求值（解析器建塔时要用）----------
  {
    // 从深度 0 往上接：y₁² = x
    const TowerExtension base = TowerExtension::rational(number(0)).unwrap();
    const TowerExtension one = base.adjoining(Flat{expression("x")}).unwrap();
    CHECK_TRUE(one.depth() == std::size_t(1));

    // 占位变量 t₁ ↦ y₁：`1 + t₁` 就是 1 + y₁
    const Result<TowerExtension> sum = evaluateOverPlaceholders(expression("1+t"), {Variable("t")}, one);
    CHECK_TRUE(sum.isOk());
    CHECK_TRUE(sum.unwrap() == element(kRootX, Flat{number(1), number(1)}));

    // 再接一层：y₂² = 1 + y₁
    const TowerExtension two = one.adjoining(sum.unwrap().coefficients()).unwrap();
    CHECK_TRUE(two.depth() == std::size_t(2));

    // **一个式子里有多个占位变量**：t₁·t₂ ↦ y₁y₂。
    // 这正是不能「先换 t₁ 再换 t₂」的原因 —— 换出来的元素不是有理函数，没地方放回去；
    // 逐项展开则全程都是「系数 × 生成元的幂」。
    const Result<TowerExtension> product =
        evaluateOverPlaceholders(expression("t*u"), {Variable("t"), Variable("u")}, two);
    CHECK_TRUE(product.isOk());
    CHECK_TRUE(product.unwrap() == element(kNested, Flat{number(0), number(0), number(0), number(1)}));

    // 占位变量比塔还深 → 拒收
    CHECK_ERR(evaluateOverPlaceholders(expression("t"), {Variable("t")}, base), MathsError::NestedRadical);
    // 分母里也有占位变量：1/(1+t) = 1/(1+y₁)
    const Result<TowerExtension> quotient = evaluateOverPlaceholders(expression("1/(1+t)"), {Variable("t")}, one);
    CHECK_TRUE(quotient.isOk());
    CHECK_TRUE((quotient.unwrap() * element(kRootX, Flat{number(1), number(1)})).unwrap() ==
               element(kRootX, Flat{number(1), number(0)}));
  }

  // ---------- 定义域 ----------
  {
    auto tower = [](const std::vector<Flat> &relations) {
      const std::size_t depth = relations.size();
      return TowerExtension::fromMasks(relations, Flat(std::size_t(1) << depth, number(0))).unwrap();
    };

    // 深度 1：被开方数就是 x 的有理函数
    CHECK_EQ(domainOf(tower({Flat{expression("x")}})).unwrap().latex(), std::string("[0, +\\infty)"));
    CHECK_EQ(domainOf(tower({Flat{expression("1/x")}})).unwrap().latex(), std::string("(0, +\\infty)"));
    CHECK_EQ(domainOf(tower({Flat{expression("x-1")}})).unwrap().latex(), std::string("[1, +\\infty)"));

    // 深度 2：f₂ = a + b·y₁，靠 √f ≥ h ⟺ h ≤ 0 ∨ f ≥ h² 化归
    CHECK_EQ(domainOf(tower({Flat{expression("x")}, Flat{number(1), number(1)}})).unwrap().latex(),
             std::string("[0, +\\infty)")); // 1+√x 恒正
    CHECK_EQ(domainOf(tower({Flat{expression("x")}, Flat{number(-1), number(1)}})).unwrap().latex(),
             std::string("[1, +\\infty)")); // √x−1 ≥ 0 ⟺ x ≥ 1
    CHECK_EQ(domainOf(tower({Flat{expression("x")}, Flat{number(1), number(-1)}})).unwrap().latex(),
             std::string("[0, 1]")); // 1−√x ≥ 0 ⟺ x ≤ 1
    CHECK_TRUE(domainOf(tower({Flat{expression("x")}, Flat{number(-1), number(-1)}})).unwrap().isEmpty()); // −1−√x 恒负

    // 判不了的：fᵢ 一次用了两个生成元（y₁y₂）—— 深度 3 才排得下
    CHECK_ERR(domainOf(tower({Flat{expression("x")}, Flat{number(1), number(0)},
                              Flat{number(1), number(0), number(0), number(1)}})),
              MathsError::DomainNotDecidable);
    // relations[i] 的长度必须是 2^i：给短了就是编码不自洽
    CHECK_ERR(TowerExtension::fromMasks({Flat{expression("x")}, Flat{number(1)}},
                                        Flat{number(0), number(0), number(0), number(0)}),
              MathsError::NestedRadical);
  }

  // ---------- 判等 ----------
  {
    CHECK_TRUE(rootOfX() == element(kRootX, Flat{number(0), number(1)}));
    CHECK_TRUE(!(rootOfX() == rootOfNested())); // 深度不同
    CHECK_TRUE(!(rootOfX() == rational(1)));
  }

  // ---------- 拒绝的输入 ----------
  {
    // 第一层被开方数是完全平方 → 扩张掉维数，平表不再是基
    CHECK_ERR(TowerExtension::make(number(0), {Flat{expression("x^2")}}), MathsError::RadicandIsSquare);
    // relations[i] 的长度必须是 2^i —— 引用了不在本层的生成元
    CHECK_ERR(TowerExtension::make(number(0), {Flat{expression("x"), number(1)}}), MathsError::NestedRadical);
    // 被开方数为零：√0 之后那层是退化的
    CHECK_ERR(TowerExtension::make(number(0), {Flat{number(0)}}), MathsError::InvalidRange);
    // 平表长度与深度不符
    CHECK_ERR(TowerExtension::fromMasks(kRootX, Flat{number(1)}), MathsError::InvalidExpression);
    // 不同的塔之间不能运算
    CHECK_ERR(rootOfX() + rootOfNested(), MathsError::InvalidExpression);
    CHECK_ERR(rootOfX() * rootOfNested(), MathsError::InvalidExpression);
  }

  TEST_SUMMARY();
}
