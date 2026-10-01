# maths.error — 统一错误码

对应 `src/core/maths_error.cppm`。

全库只有**一套**错误码 `MathsError`。异常路径（`MathsException`）与返回值路径（`Result<T>`）
共用它，因此调用方的错误处理逻辑只需写一次。

```cpp
catch (const MathsException &e) { /* e.code() */ }   // 异常路径
if (result.isErr()) { /* result.unwrapErr() */ }     // Result 路径
```

两种写法拿到的 `MathsError` 完全相同。

## 错误码

`describe(MathsError)` 返回中文说明，`operator<<` 输出同一段文本。

| 错误码 | `describe()` | 典型触发 |
| --- | --- | --- |
| `DivisionByZero` | 除数不能为零 | `Integer` / `Fraction` 除以零；长除法的除式为零多项式 |
| `ZeroDenominator` | 分母不能为零 | `Fraction(1, 0)`、`RationalFunction::make` 分母为零多项式 |
| `ZeroToNegativePower` | 0 的负数次幂无定义 | `pow` 底数为 0、指数为负 |
| `NonIntegralPowerResult` | 指数运算结果不是整数 | `Integer ^= 负指数`（结果是分数，塞不回 `Integer`） |
| `ExponentOverflow` | 变量指数超出可表示范围 | 同底数幂合并时指数相加溢出 |
| `NumericOverflow` | 数值超出可精确表示的范围 | 代数数运算的中间量过大，`Fraction` 容纳不下 |
| `NegativeEvenRoot` | 负数不能开偶次根 | `\sqrt{-4}`、`(-4)^{1/2}`、`RealAlgebraicNumber::nthRootOf(-2, 4)` |
| `RadicandIsSquare` | 被开方数是完全平方，开方结果带绝对值（如 √(x²) = \|x\|），本库不引入 \|x\| | `RadicalExtension` 的 `√(x²)`、`√((x+1)²)` |
| `RadicandsNotIndependent` | 多个根号落在同一平方类（如 √x 与 √(4x)），请先化成最简根式再写 | `RadicalExtension::sumOfRadicals({x, 4x})` |
| `NotAMonomial` | 多项式无法化简为单项式 | `Polynomial::toMonomial` 遇到多于一项 |
| `NotAPolynomial` | 分式无法化简为多项式 | `RationalFunction::toPolynomial` 余式非零 |
| `NotAnInteger` | 分数不是整数 | `Fraction::toInteger` 分母不为 1 |
| `NotARational` | 实代数数不是有理数 | `RealAlgebraicNumber::toFraction` 值无理；`RadicalExtension::toRationalFunction` 仍有根号部分 |
| `InvalidExpression` | 不支持的表达式 | 解析失败、残留字符、`x + 1 = 2` 这类需要解方程的条件 |
| `InvalidName` | 非法的名字 | `Variable("x^")`、`Name` 含非字母数字字符 |
| `InvalidRange` | 区间参数非法 | `random` 的区间为空或超出 `long long`；代数数的隔离区间不合法、精化超限。**只管真正的区间问题** —— 负数开偶次根另有错误码 |
| `UndefinedVariable` | 变量未定义 | `Scope::lookup` 未绑定；`evaluate` 代入后仍含未绑定变量 |
| `NotAnAssignment` | 右边含被赋值的变量本身，那是方程不是赋值 | `x = 2x`、`x = x + 1` |
| `CircularReference` | 该赋值会形成循环引用 | `x = s`、`s = t`、`t = x` |
| `OutsideDomain` | 该点不在函数定义域内 | `RealFunction::at` 传入定义域外的点（如 `√x` 在 x = −1）。**不是「变量未定义」** —— 那是没给值，这是给了值但该点不属于函数 |
| `NotUnivariate` | 不是一元函数（含多个自变量） | `RealFunction::make` 的规则里出现两个以上自变量；一元函数四则时两侧自变量名不同；`PiecewiseFunction` 的分支之间自变量不同名 |
| `EmptyCollection` | 样本为空或观测数不够，没有该统计量 | `maximumOf` / `meanOf` / `varianceOf` 收到空序列或空集；`sampleVarianceOf` 少于 2 个观测 |
| `NotFiniteSet` | 不是有限点集（含区间块）—— 连续集合上的求和是积分，不是求和 | `sumOf(RealSet)` / `meanOf(RealSet)` 等收到含区间块的集合 |

## 两条传递路径

| 路径 | 适用场景 | 原因 |
| --- | --- | --- |
| `Result<T>` 返回值 | 普通函数与算术运算 | 有返回值位置 |
| `MathsException` 异常 | **构造函数、复合赋值运算符** | 语言层面没有可承载 `Result` 的返回值位置 |

这不是设计取舍，是语言限制：`Fraction(1LL, 0LL)` 没法"返回一个失败"，
`a /= b` 也必须返回自身的引用。

```cpp
Fraction(1LL, 0LL);        // MathsException(ZeroDenominator)
Fraction("\\frac{1}{");    // MathsException(InvalidExpression)
Variable("x^");            // MathsException(InvalidName)
a /= zero;                 // 复合赋值内部 unwrap()，失败抛 MathsException
```

### 想显式处理失败：用静态工厂

构造函数会抛，改用同名的静态工厂就能拿到 `Result`：

```cpp
Result<Fraction> parsed = Fraction::parse(input);
if (parsed.isErr()) {
  std::cout << describe(parsed.unwrapErr()) << '\n';
}
```

同一套路子的还有 `RationalFunction::make(numerator, denominator)` 与
`parseExpression(text)`。

## 复合赋值为什么抛

`+= -= *= /= %= ^=` 语义上必须就地修改并返回自身引用，无法承载 `Result`。
实现上是内部 `unwrap()`：失败即抛 `MathsException`，错误码与 `Result` 路径一致。

需要不抛的版本，就写成显式运算再自己处理 `Result`：

```cpp
Result<Fraction> quotient = a / b;   // 不抛
if (quotient.isOk()) {
  a = quotient.unwrap();
}
```
