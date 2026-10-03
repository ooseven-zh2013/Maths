# maths.algebraic_number — 实代数数

对应 `src/numeric/algebraic_number.cppm`。

一个实数 α 是**代数数**，如果它是某个非零有理系数多项式的根。本模块把它表示成
**最小多项式 + 隔离区间**（区间里恰好只有这一个根），于是四则运算、开方、比较
全都可以精确进行，不引入任何浮点。

| 类型 | 含义 |
| --- | --- |
| `UnivariatePolynomial` | ℚ 上一元多项式：Euclid 带余除法、平方自由化、Sturm 实根计数 |
| `RealAlgebraicNumber` | 实代数数：`poly_` + 隔离区间 `[low_, high_]` |

## 构造

| 入口 | 说明 |
| --- | --- |
| `RealAlgebraicNumber(Fraction)` | 有理数（最小多项式为一次，构造时就退化成一点区间） |
| `create(polynomial, low, high)` | 给定多项式与隔离区间；区间不合法或不是唯一根 → `InvalidRange` |
| `nthRootOf(value, n)` | 有理数的 n 次根，符号问题由实数域定义域决定 |
| `squareRootOf(value)` | 同上，`n = 2` |
| `parse(text)` | 从文本解析（见下） |

## 文本解析

`parse` 只认**数值**，不接受变量，且根号**只认 LaTeX 写法**：

```cpp
RealAlgebraicNumber::parse("\\sqrt{2}");             // √2
RealAlgebraicNumber::parse("\\sqrt[3]{2}");          // ∛2
RealAlgebraicNumber::parse("2\\sqrt{2}");            // 隐含乘法
RealAlgebraicNumber::parse("\\sqrt{2} + \\sqrt{3}"); // 和式
RealAlgebraicNumber::parse("2^{1/2}");               // 有理指数 = √2
RealAlgebraicNumber::parse("2^{-1/2}");              // = 1/√2
RealAlgebraicNumber::parse("2^{\\frac{1}{2}}");      // \frac 写法同样接受

RealAlgebraicNumber::parse("sqrt{2}");               // InvalidExpression：只认 \sqrt{…}
RealAlgebraicNumber::parse("x");                     // InvalidExpression：不接受变量
```

指数接受 `p`、`p/q`、`\frac{p}{q}` 与负号，**分母为 0 → `ZeroDenominator`**，
`0` 的负次幂 → `DivisionByZero`，负数开偶次根 → `NegativeEvenRoot`。
所有整数解析都校验整串消费完 —— `std::stoull` 只解析前缀且不抛异常，
`2^{1/2}` 会被读成指数 `1` 而静默给出 `2`，这条坑必须堵死。

## 运算

加、减、乘、除（`/` 返回 `Result`，除零 → `DivisionByZero`）、取倒数 `inverse()`、
开 n 次方 `nthRoot(n)`、非负整数次幂 `pow(n)`、比较（`<=>`，精确）。

幂与根式都有**廉价特例**，它们不只是优化，更是正确性的护栏 —— 通用路线要在
ℚ[x,y]/(p,q) 里做线性代数，次数一高就顶穿 `Fraction` 的表示范围：

| 特例 | 做法 |
| --- | --- |
| `α + c`（c 有理） | 零化多项式取 `p(x − c)`（平移），区间整体平移 |
| `α · c` | 零化多项式取 `p(x / c)`（缩放），区间按 `c` 缩放（`c < 0` 时端点交换） |
| `α + α` | `= 2α`，走缩放 |
| `α − α` | `= 0`，直接给规范零 |
| `α · α` | 平方专用：`p(x) = e(x²) + x·o(x²)` ⟹ `e(α²)² − α²·o(α²)² = 0` |
| `α^k`（α 是纯根式） | `α = ±ⁿ√r` 时 `α^k` 是 `x^(n/g) − r^(k/g)` 的根（`g = gcd(n,k)`） |

## 输出

| 方法 | 用途 |
| --- | --- |
| `str()` | 终端阅读：有理数、或 `RootOf(poly, [lo, hi])` 记法（带诊断信息） |
| `latex()` | 排版：能还原成单个根式就写 `\sqrt{…}`，否则退回 `RootOf` |

`latex()` 的判定：最小多项式只有首项与常数项非零（`a·x^n + c`）时，该数就是 `±ⁿ√(-c/a)`。
符号要分开处理 —— **奇次根**符号已由被开方数承载（`-∛2` 写成 `-\sqrt[3]{2}` 而不是
`-\sqrt[3]{-2}`，后者是正数）；**偶次根**被开方数恒正、± 分不出来，必须靠隔离区间实测。

## 尝试降一阶

与 `Polynomial::toMonomial`、`RationalFunction::toPolynomial` 同一套命名：

| 方法 | 成功条件 | 失败错误码 |
| --- | --- | --- |
| `toFraction()` | 值是有理数（含 `√2·√2` 这种表示上还挂着多项式、值却有理的情形） | `NotARational` |

判据是有理根定理：候选 `p/q`（`p` 整除常数项、`q` 整除首项）中落在隔离区间里的那个。
系数的试除分解有上限，**只会漏判、不会误判**。

## 明确不做

- **多项式因式分解**：因此加法/乘法得到的零化多项式可能不是最小的
  （`√2 + √3` 的最小多项式是 `x⁴ − 10x² + 1`，而 `√2 + √2` 这类同数相加已用特例绕开）
- **最简根式化**：`2√2` 输出成 `\sqrt{8}`，两者数值相等且都精确

## 错误码

| 情形 | 错误码 |
| --- | --- |
| 语法错误、不接受变量、系数域里没有根式 | `InvalidExpression` |
| 负数开偶次根 | `NegativeEvenRoot` |
| 分母为 0 | `ZeroDenominator` |
| 除数 / 倒数为 0 | `DivisionByZero` |
| 隔离区间不合法、精化超限 | `InvalidRange` |
| 精确表示范围溢出 | `NumericOverflow` |

## 根式的渲染

`latex()` 能还原出根式就写根式，还原不成才退到 `\operatorname{RootOf}`。两类形状：

| 形状 | 判据 | 例子 |
| --- | --- | --- |
| 纯根式 | 最小多项式只有首项与常数项（`a·x^n + c`） | `" + F + "sqrt{2}" + "`、`" + F + "sqrt[3]{5}" + "` |
| 带平移量的二次根式 | 判别式 `b²−4ac` 除以 `4a²` 是非负有理数 | `2 - ` + F + "sqrt{3}" + "`、`49 + 20` + F + "sqrt{6}" + "`、`" + F + "frac{1 + " + F + "sqrt{5}}{2}" + "` |

带平移量的那一类以前一律退化成 `RootOf(x^2-4x+1, [-5/8, 25/8])`，可读性差一大截 ——
而它恰好是学生答案里最常见的形状。写法按可读性分三种：平移量是**整数**用加法式
（`2 - ` + F + "sqrt{3}" + "`），**带分数**用教科书的单分数式（`" + F + "frac{1 + " + F + "sqrt{5}}{2}" + "`，
而不是 `1/2 + ` + F + "sqrt{5/4}" + "`），为 0 就只写根式。

**整数**被开方数还会提出平方因子：`" + F + "sqrt{2400}" + "` 写成 `20" + F + "sqrt{6}" + "`（反复平方很容易攒出这种大整数）。
分数不动 —— `" + F + "sqrt{5/4}" + "` 提成 `(1/2)" + F + "sqrt{5}` 更难看。

> `str()` **故意保持 `RootOf` 形态**：它是终端诊断用的输出，多项式与隔离区间比 `" + F + "sqrt{2}" + "` 有用。
> 只有 `latex()`（排版/展示形态）做上面这些化简。两者并存是有意的分工。
