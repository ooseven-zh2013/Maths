export module maths.algebra:algebraic;

import std;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import :expression;
import :rational;
import :scope;

export namespace maths {

// 系数为**实代数数**的代数式与作用域。
//
// 代数栈本身已经在 expression / rational / scope 三个分区里按系数参数化了
// （MonomialOf<Coefficient> 之类），这里只是把系数绑定到 RealAlgebraicNumber，
// 并补上「有理系数的式子提升成代数系数」的入口 —— 没有第二套实现，
// 因此化简规则、定义域约束、错误处理三者与有理版本天然一致。
//
// 与有理版本的区别只有一个：L1「数值内容约分」在 ℚ(α) 上无意义
// （域里任何非零元都可逆，内容恒为 1），已由 RationalFunctionOf::simplify 跳过。

using AlgebraicMonomial = MonomialOf<RealAlgebraicNumber>;
using AlgebraicPolynomial = PolynomialOf<RealAlgebraicNumber>;
using AlgebraicRationalFunction = RationalFunctionOf<RealAlgebraicNumber>;
using AlgebraicScope = ScopeOf<RealAlgebraicNumber>;

// ==================== 提升：有理系数 → 代数系数 ====================

inline AlgebraicMonomial toAlgebraic(const Monomial &source) {
  return AlgebraicMonomial(RealAlgebraicNumber(source.getCoefficient()), source.getFactors());
}

inline AlgebraicPolynomial toAlgebraic(const Polynomial &source) {
  AlgebraicPolynomial result;
  for (const auto &entry : source.getTerms()) {
    result.addTerm(entry.first, RealAlgebraicNumber(entry.second));
  }
  return result;
}

// 注意保留 discardedConstraints：提升只是换了个系数类型，化简丢掉的定义域约束不变
inline AlgebraicRationalFunction toAlgebraic(const RationalFunction &source) {
  const Result<AlgebraicRationalFunction> result =
      AlgebraicRationalFunction::make(toAlgebraic(source.getNumerator()), toAlgebraic(source.getDenominator()));
  if (result.isErr()) {
    return AlgebraicRationalFunction(RealAlgebraicNumber(Fraction(0, 1)));
  }
  AlgebraicRationalFunction lifted = result.unwrap();
  for (const Variable &variable : source.discardedConstraints()) {
    lifted.noteDiscardedConstraint(variable);
  }
  return lifted;
}

} // namespace maths
