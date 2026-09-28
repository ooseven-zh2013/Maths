# maths.algebra:algebraic — 系数取实代数数的代数式

对应 `src/algebra/algebraic.cppm`。依赖 `maths.algebra:expression` / `:rational` / `:scope`
与 `maths.algebraic_number`。

## 代数栈是按系数类型参数化的

整个代数栈（单项式 / 多项式 / 分式 / 作用域）都带一个系数类型参数，**没有第二套实现**：

| 模板 | 别名（系数 = `Fraction`） | 本模块的别名（系数 = `RealAlgebraicNumber`） |
| --- | --- | --- |
| `MonomialOf<C>` | `Monomial` | `AlgebraicMonomial` |
| `PolynomialOf<C>` | `Polynomial` | `AlgebraicPolynomial` |
| `RationalFunctionOf<C>` | `RationalFunction` | `AlgebraicRationalFunction` |
| `ScopeOf<C>` | `Scope` | `AlgebraicScope` |

因为是同一套代码，化简规则、定义域约束、错误码与有理系数版本天然一致。

```cpp
AlgebraicScope scope;
scope.assign(Variable("x"), RealAlgebraicNumber::parse("\\sqrt{2}").unwrap());

AlgebraicPolynomial polynomial;                       // x² + x
polynomial.addTerm({{Variable("x"), 2ULL}}, RealAlgebraicNumber(Fraction(1, 1)));
polynomial.addTerm({{Variable("x"), 1ULL}}, RealAlgebraicNumber(Fraction(1, 1)));
polynomial.evaluate(scope).unwrap().latex();          // "2 + \sqrt{2}"
```

## 与有理系数的唯一差别：L1 约分

分式的三层化简里，**L1「数值内容约分」只对有理系数启用**。
系数取自 ℚ 时「内容」是整数 gcd；换成实代数数后 ℚ(α) 是**域**，
任何非零元都可逆、内容恒为 1，约分没有意义。

```
(6x + 6) / (4x + 4)   有理系数 → (3x + 3) / (2x + 2)    ← L1 约掉了 2
(6x + 6) / (4x + 4)   代数系数 → 保持原样                ← 域里没有「内容」可约
```

L2（单项式公因子）与 L3（分母符号归一）两边都生效。

## 提升：有理系数 → 代数系数

| 入口 | 说明 |
| --- | --- |
| `toAlgebraic(const Monomial &)` | 单项式 |
| `toAlgebraic(const Polynomial &)` | 多项式 |
| `toAlgebraic(const RationalFunction &)` | 分式，**并保留 `discardedConstraints`** |

保留约束这一点很重要：提升只是换了个系数类型，**不是化简**，
静默放宽定义域是这类转换最容易犯的错。

## 系数相关的具体做法

系数类型之间的差异都靠一组 helper + `if constexpr` 抹平，写在 `maths::detail`：

| helper | 作用 |
| --- | --- |
| `coefficientZero` / `coefficientOne` / `coefficientMinusOne<C>()` | 单位元，`C(Fraction(...))` 对两种系数都成立 |
| `isZeroCoefficient` / `isNegativeCoefficient` | 有同名成员就用成员，否则退回 `== 0` / `< 0` |
| `coefficientText` / `coefficientLatex` | 文本与 LaTeX 渲染（系数有 `latex()` 就用它） |
| `coefficientPower` | 系数取幂：系数自带 `pow` 就走它 |
| `valuePower` | 分式取幂：值是纯系数时走 `coefficientPower`，否则二进制取幂 |

**`valuePower` 不能用线性乘代替**：代入时若逐个乘，`α^k` 会退化成 `k−1` 次通用乘积
（环维数 deg²），`x^6` 配 `α = ⁶√2` 就会溢出。

## 一条使用注意

**不同系数类型的 `ScopeOf` 不能互转**（它们是不同的实例化）。
想给代数 Scope 装有理值，直接给一个 `Fraction`：

```cpp
AlgebraicScope scope;
scope.assign(Variable("x"), Fraction(3, 1));    // 隐式转成 RealAlgebraicNumber
```

## 相关

- 文本入口：`parseAlgebraicExpression`（见 [expression_parser.md](../parser/expression_parser.md)），
  `\sqrt{2}*x`、`x + \sqrt{2}` 走的就是这个类型
- 根号里带**变量**（`√x`）是另一件事，见 [radical.md](radical.md)
