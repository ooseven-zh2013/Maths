# maths.algebra:radical — 根式扩张（√ 里有变量）

对应 `src/algebra/radical.cppm`。

把「根号下是有理函数」表示成精确的代数元素，于是 `√x`、`√(x²+1)`、
甚至 `√(x²+1) + √(x²+2)` 都能参与运算、也能代入求值。

## 表示

多重二次扩张

```
ℚ(x)[y₁,…,y_k] / (y₁² − f₁, …, y_k² − f_k)
```

元素写成 Σ<sub>S</sub> c<sub>S</sub> · ∏<sub>i∈S</sub> y<sub>i</sub>
（c<sub>S</sub> ∈ ℚ(x)，S 取遍子集）。k 个根号 → 维度 2<sup>k</sup>，
上限 `RadicalExtension::kMaxRadicands = 4`（维度 16）。

| k | 基 | 典型元素 |
| --- | --- | --- |
| 1 | `{1, y₁}` | `a + b√f` —— 二次扩张 |
| 2 | `{1, y₁, y₂, y₁y₂}` | `√(x²+1) + √(x²+2)` |

## 基本用法

```cpp
RadicalExtension rootX = RadicalExtension::make(parseExpression("x").unwrap()).unwrap();      // √x
RadicalExtension both  = RadicalExtension::sumOfRadicals({                                  // √(x²+1)+√(x²+2)
    parseExpression("x^2+1").unwrap(), parseExpression("x^2+2").unwrap()}).unwrap();

(rootX * rootX).unwrap().toRationalFunction().unwrap().str();   // "x"    —— √x·√x = x
(both * both).unwrap().latex();                                  // 展开出 y₁y₂ 项
```

## 接口

| 分类 | 成员 |
| --- | --- |
| 构造 | `make(radicands, coefficients)`、`make(radicand)`（√f）、`sumOfRadicals(radicands)`（Σ√fᵢ） |
| 访问 | `radicands()`、`coefficients()`、`coefficient(mask)`、`termCount()`、`isZero()`、`isRadicalFree()` |
| 运算 | `+` `-` `*` `/`（前三个在**同域**时可用）、一元 `-`、`inverse()` |
| 降一阶 | `toRationalFunction()` —— 无根号部分时降回有理函数，否则 `NotARational` |
| 求值 | `evaluate(Scope)` → `RealAlgebraicNumber`（变量全绑定为有理数时） |
| 输出 | `str()`、`latex()` |

乘法的全部规则就是 `yᵢ² = fᵢ`：两个子集**相交**的生成元两两配对、换成对应被开方数之积，
不相交的部分留在结果里。取逆用共轭连乘：2<sup>k</sup> 个共轭之积是 ℚ(x) 中的范数，
`α⁻¹ = (∏_{S≠∅} conj_S α) / N(α)`。

## 自动扩域

两个操作数的生成元不同时，运算会**自动扩域**：把被开方数取并集，把两边都搬进共同的域，
再按同一套规则算。所以「两个不同的根号」可以直接相加相乘：

```cpp
√x · √(x+1)        → \sqrt{x}\sqrt{x + 1}
√x + √(x+1)        → \sqrt{x} + \sqrt{x + 1}
(√x + √(x+1))²     → 2x + 1 + 2\sqrt{x}\sqrt{x + 1}
1/(√x + √(x+1))    → -\sqrt{x} + \sqrt{x + 1}      ← 共轭有理化自动完成
```

扩域时会把每个生成元**按位置重新编号**（`√(x+1)` 在 `[x, x+1]` 里是第 2 个），
掩码的位跟着搬 —— 不能假设「自己的列表是目标列表的前缀」，那只在合并顺序凑巧时成立。

并起来之后仍然要满足独立性：`√x + √(4x)` 会因为 `x·4x = 4x²` 是平方而拒收
（它们其实是同一个根号），`√x + √y` 会因为出现第二个变量而拒收。

## 零生成元的元素（纯有理函数）

`radicands` 允许为空，此时元素就是 `ℚ(x)` 里的一个有理函数：

```cpp
RadicalExtension two(Fraction(2, 1));      // 2
RadicalExtension x(parseExpression("x").unwrap());   // x
(two + RadicalExtension::make(parseExpression("x").unwrap()).unwrap()).latex();   // "2 + \sqrt{x}"
```

与根式元素运算时自动扩域，判等也**按值比**：`√x·√x` 与 `x` 相等。

有了它，本类型就能当**系数类型**用（模板里的单位元构造方式正是 `Coefficient(Fraction)`），
这是「让含变量根号进解析器」的前提。

**常数被开方数仍然拒收**：`√2` 属于实代数数（`RealAlgebraicNumber`），
不应该在这里再造一个只有常数的生成元。

## 刻意的限制（都明确报错，绝不猜）

| 限制 | 原因 | 错误码 |
| --- | --- | --- |
| 被开方数不能是**完全平方** | `√(x²) = |x|` 不是单值的代数函数：`ℚ(x)[y]/(y²−x²)` 里 `y² − x²` 可约，环不是域、`y` 是零因子、`±x` 都是根。本库不引入 `\|x\|` 节点 | `RadicandIsSquare` |
| 各根号必须**平方类独立** | `√x + √(4x)` 里两个根号其实是一个（`√(4x) = 2√x`），要用单生成元写；折叠需要最简根式化，本库不做 | `RadicandsNotIndependent` |
| 所有被开方数必须含**同一个变量** | `√x + √y` 是两个变量，自动扩域无法容纳 | `InvalidExpression` |
| 被开方数不能是常数的 | `√2` 属于实代数数，不该在这里造只有常数的生成元 | `InvalidExpression` |
| 被开方数不能是常数 | 那是纯数值，走 `RealAlgebraicNumber` | `InvalidExpression` |
| 不支持**嵌套**根号 | `√(1+√x)` 要扩张的塔，属后续能力 | `InvalidExpression` |

平方性判定用「分子分母各自平方自由」（`squareFreePart` 不降次），
它是平方性的**充分否证**：可能拒掉个别合法但少见的组合，**但不会把不合法的收下**。
独立性判定是 Kummer 意义下的平方类独立 —— 任何非空子集的乘积都不能是平方，
同样是充分否证。

## 代入求值

`evaluate(scope)` 要求所有变量绑定到有理数：每个 `√fᵢ` 变成实代数数，
有理系数组合之后仍是实代数数，返回 `RealAlgebraicNumber`。

```cpp
Scope scope;
scope.assign(Variable("x"), Fraction(3, 1));
both.evaluate(scope).unwrap().latex();     // √10 + √11
```

取值处被开方数为负 → `NegativeEvenRoot`（定义域问题如实上报，不猜）。

## 定义域

```cpp
Result<RealSet> domainOf(const RadicalExtension &expression);
```

**所有被开方数 ≥ 0** 与**所有系数的分母 ≠ 0** 取交：

| 式子 | 定义域 |
| --- | --- |
| `√x` | `[0, +\infty)` |
| `√(x²+1)` | `\mathbb{R}`（被开方数恒正）|
| `√(x²−1)` | `(-\infty, -1] \cup [1, +\infty)` |
| `1/√x` | `(0, +\infty)` —— 被开方数的 `x ≥ 0` 与系数分母的 `x ≠ 0` 取交 |

定义域是**表达式**的性质：`√x·√x` 的值等于 `x`、处处有定义，但作为表达式它要求 `x ≥ 0` ——
所以这里按被开方数逐个收条件，而不是先把元素化简。

只支持**单变量**：多变量时定义域是多维点集（`√x·y` 这类），返回 `InvalidExpression`。

## 与其它类型的分工

| 类型 | 根号包的是 | 例子 |
| --- | --- | --- |
| `AlgebraicRationalFunction` | 数（根号是**系数**） | `√2·x`、`x + √2` |
| `RadicalExtension` | 有理函数（根号是**元素**） | `√x`、`√(x²+1) + √(x²+2)` |

两者不可互换：前者是系数域扩张，后者是函数域扩张。
