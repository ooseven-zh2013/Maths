#include "../check.hpp"

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
using TowerExtensionValue = TowerExtension;

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
  std::cout << "=== 塔式扩张 TowerExtension 测试 ===" << '\n';
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

  // ---------- 判元素的符号（绝对值按符号分支时要用）----------
  {
    // x - 2√x ≥ 0 ⇔ x = 0 或 x ≥ 4（x≥0）
    const TowerExtension value = parseTowerExpression("x-2\\sqrt{x}").unwrap();
    CHECK_EQ(whereNonNegativeOverTower(value).unwrap().latex(), std::string("\\{0\\} \\cup [4, +\\infty)"));
    // 1 + √x ≥ 0 恒成立 → 整条真线
    const TowerExtension always = parseTowerExpression("1+\\sqrt{x}").unwrap();
    CHECK_TRUE(whereNonNegativeOverTower(always).unwrap().isRealLine());
    // 一次用到两个生成元 → 判不了
    const TowerExtension product = parseTowerExpression("\\sqrt{x}*\\sqrt{1+\\sqrt{x}}").unwrap();
    CHECK_ERR(whereNonNegativeOverTower(product), MathsError::DomainNotDecidable);
  }
  // ---------- 塔分母：除法有个正常的表示 ----------
  //
  // 之前元素只有「平表」，分母被固定成有理函数，于是 1/(1+√x) 曾被表示成
  // c₀ = c₁ = 1/(1-x) —— 在 x=1 处 0/0，而真值是 1/2。有了塔分母，
  // 它就直接是「分子 1、分母 1+√x」，既没有极点，形态也天然正确。
  {
    const TowerExtension inverse = parseTowerExpression("(1)/(1+\\sqrt{x})").unwrap();
    CHECK_EQ(inverse.latex(), std::string("\\frac{1}{1 + \\sqrt{x}}"));
    CHECK_TRUE(inverse.nonzeroCoefficients() == std::size_t(1)); // 分子只有 1
    CHECK_TRUE(valueAt(inverse, 0) == RealAlgebraicNumber(Fraction(1, 1)));
    CHECK_TRUE(valueAt(inverse, 1) == RealAlgebraicNumber(Fraction(1, 2))); // 曾经的 0/0
    CHECK_TRUE(valueAt(inverse, 4) == RealAlgebraicNumber(Fraction(1, 3)));

    // 套嵌的除法
    const TowerExtension nested = parseTowerExpression("1/\\sqrt{1+\\sqrt{x}}").unwrap();
    CHECK_EQ(nested.latex(), std::string("\\frac{1}{\\sqrt{1 + \\sqrt{x}}}"));
    CHECK_TRUE(valueAt(nested, 0) == RealAlgebraicNumber(Fraction(1, 1)));

    // 同一个**函数**、不同的表示：1/√x 与 √x/(√x·√x) 代数上是同一个东西，
    const TowerExtension a = parseTowerExpression("1/\\sqrt{x}").unwrap();
    const TowerExtension b = parseTowerExpression("(\\sqrt{x})/(\\sqrt{x}*\\sqrt{x})").unwrap();
    // 库保证的是「值相等」，不保证「形式相等」—— 平表之比不做规范化约分。
    CHECK_TRUE(valueAt(a, 4) == valueAt(b, 4));
    CHECK_TRUE(valueAt(a, 9) == valueAt(b, 9));
    // 分母为零的除法仍然拒收
    CHECK_ERR(rational(1).dividedBy(rational(0)), MathsError::DivisionByZero);
  }

  // ---------- FunctionRule：规则可以是根式，也可以是一条塔 ----------
  {
    const FunctionRule radical(FunctionRule::rational(expression("x+1")));
    const FunctionRule tower(parseTowerExpression("\\sqrt{1+\\sqrt{x}}").unwrap());
    CHECK_TRUE(!radical.holdsTower() && radical.isRadicalFree());
    CHECK_TRUE(tower.holdsTower() && !tower.isRadicalFree());
    // 塔没有「降回有理函数」这件事
    CHECK_ERR(tower.toRationalFunction(), MathsError::NestedRadical);
    CHECK_TRUE(radical.toRationalFunction().isOk());
    // 塔的变量仍然只有 x
    CHECK_TRUE(tower.variables().size() == std::size_t(1));

    // radical ⊗ radical 走老路径，结果仍是 radical
    const Result<FunctionRule> sum = radical + radical;
    CHECK_TRUE(sum.isOk() && !sum.unwrap().holdsTower());
    CHECK_EQ(sum.unwrap().latex(), std::string("2x + 2"));

    // radical ⊗ tower：把 radical 挂到塔顶再算
    const Result<FunctionRule> mixed = radical + tower;
    CHECK_TRUE(mixed.isOk() && mixed.unwrap().holdsTower());
    // tower ⊗ radical 一样
    const Result<FunctionRule> other = tower + radical;
    CHECK_TRUE(other.isOk() && other.unwrap().holdsTower());
    CHECK_TRUE(mixed.unwrap() == other.unwrap());

    // tower ⊗ tower：两条不同的塔不能相加（域都不一样）
    const FunctionRule other1(parseTowerExpression("\\sqrt{2+\\sqrt{x}}").unwrap());
    CHECK_ERR(tower + other1, MathsError::InvalidExpression);
    // 同一对可以
    CHECK_TRUE((tower + tower).isOk());
    CHECK_TRUE((tower * tower).isOk());
    CHECK_TRUE((tower / tower).isOk());
    CHECK_TRUE((tower - tower).isOk());

    // 取负
    CHECK_TRUE((-tower) == FunctionRule(tower.asTower().negate()));
    CHECK_TRUE((tower + (-tower)).isOk());
  }

  // ---------- 解析器：由内往外建塔 ----------
  {
    // 套嵌根号：y₁² = x、y₂² = 1 + y₁
    const TowerExtension nested = parseTowerExpression("\\sqrt{1+\\sqrt{x}}").unwrap();
    CHECK_TRUE(nested.depth() == std::size_t(2));
    CHECK_TRUE(nested == element(kNested, Flat{number(0), number(0), number(1), number(0)}));
    CHECK_EQ(domainOf(nested).unwrap().latex(), std::string("[0, +\\infty)"));
    // x = 4 时 √(1+2) = √3
    CHECK_TRUE(valueAt(nested, 4) == RealAlgebraicNumber::nthRootOf(Fraction(3, 1), 2).unwrap());

    // 单层也走这条路（深度 1）
    const TowerExtension single = parseTowerExpression("\\sqrt{x}").unwrap();
    CHECK_TRUE(single.depth() == std::size_t(1));
    CHECK_TRUE(single == element(kRootX, Flat{number(0), number(1)}));

    // 整条式子里带系数
    const TowerExtension scaled = parseTowerExpression("2*\\sqrt{1+\\sqrt{x}}").unwrap();
    CHECK_TRUE(scaled.depth() == std::size_t(2));
    CHECK_TRUE(valueAt(scaled, 4) ==
               RealAlgebraicNumber(Fraction(2, 1)) * RealAlgebraicNumber::nthRootOf(Fraction(3, 1), 2).unwrap());

    // 三个根号、两个是套嵌的
    const TowerExtension three = parseTowerExpression("\\sqrt{1+\\sqrt{x}}+\\sqrt{3}").unwrap();
    CHECK_TRUE(three.depth() == std::size_t(3));
    CHECK_TRUE(valueAt(three, 4) == RealAlgebraicNumber::nthRootOf(Fraction(3, 1), 2).unwrap() +
                                        RealAlgebraicNumber::nthRootOf(Fraction(3, 1), 2).unwrap());

    // 除法：1/√(1+√x) 在 x=4 处是 1/√3
    const TowerExtension quotient = parseTowerExpression("1/\\sqrt{1+\\sqrt{x}}").unwrap();
    CHECK_TRUE(quotient.depth() == std::size_t(2));
    CHECK_TRUE(valueAt(quotient, 4) * RealAlgebraicNumber::nthRootOf(Fraction(3, 1), 2).unwrap() ==
               RealAlgebraicNumber(Fraction(1, 1)));

    // 没有根号 → 走别的入口
    CHECK_ERR(parseTowerExpression("x^2+1"), MathsError::InvalidExpression);
    // 高次根不做
    CHECK_ERR(parseTowerExpression("\\sqrt[3]{x}"), MathsError::InvalidExpression);
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
