# maths/algebra/rational_function.hpp — 分式

对应 `include/maths/algebra/rational_function.hpp`。

有理函数 `RationalFunction`：两个多项式之比 P/Q，Q 恒不为零多项式。

类型关系上它是最宽的一层：`Integer` ⊂ `Fraction` ⊂ `Polynomial` ⊂ `RationalFunction`。
`Scope` 的值也统一用它存储。

## 接口

| 分类 | 成员 |
| --- | --- |
| 构造 | 默认（0/1）、由 `Polynomial` / `Monomial` / `Fraction` **隐式提升**（分母为 1） |
| 完整构造 | `static make(Polynomial, Polynomial) -> Result<RationalFunction>` |
| 访问 | `getNumerator()`、`getDenominator()`、`containsVariable()`、`variables()`、`isZero()` |
| 约束 | `discardedConstraints() -> const std::set<Variable> &` |
| 化简 | `simplify()`（幂等） |
| 运算 | `+ - *`（返回 `RationalFunction`）、`/`（返回 `Result`）、一元 `-`、`+= -= *=` |
| 判等 | `operator==` —— **交叉相乘**，不依赖化简 |
| 代入 | `substitute(scope) -> Result<RationalFunction>`、`evaluate(scope) -> Result<Fraction>` |
| 归约 | `toPolynomial() -> Result<Polynomial>` |
| 输出 | `str()`、`latex()` |

`make` 在分母为零多项式时返回 `ZeroDenominator`。

只提供 `Fraction` 的常量构造，**没有 `Integer` 版本** —— 否则 `RationalFunction(5)` 会有歧义
（`int`→`Fraction` 与 `int`→`Integer` 都是一次用户定义转换）。需要时显式写
`RationalFunction(Fraction::fromInteger(value))`。

```cpp
Result<RationalFunction> r = RationalFunction::make(numerator, denominator);

r.unwrap().getNumerator();             // 分子
r.unwrap().getDenominator();           // 分母
r.unwrap().discardedConstraints();     // 化简中被丢掉的「变量非零」约束
```

## 化简做到什么程度

| 层级 | 内容 | 是否产生约束 |
| --- | --- | --- |
| L1 | **数值内容约分** —— 分子分母所有系数的 gcd，按有理数算。`6x^2y / 4xy^2 → 3x^2y / 2xy^2` | 否 —— 常数非零恒成立 |
| L2 | **单项式公因子约分** —— 变量指数逐项取 min 后同除。`→ 3x / 2y` | **是** —— 每约一个变量记一条 |
| L3 | **分母符号归一** —— 分母首项系数取正，消除 `1/(-x-1)` 与 `-1/(x+1)` 两种写法 | 否 |

`simplify()` **幂等**：已化简的分式再次调用不产生变化，也不重复记录约束。
`+ - * /` 内部都会调用它，约束跨运算传播。

L3 取首项必须用 `leadingMonomial`（字典序），**不能用 `getTerms().begin()`** ——
存储序下常数项的键最小，会把常数项的符号当成首项符号，
`(x^2 - 1)/(x - 1)` 会被整体取反成 `(-x^2 + 1)/(-x + 1)`。

### 明确不做

| 不做 | 原因 |
| --- | --- |
| 多项式因式分解 | 多元复杂、系数爆炸 |
| 多项式 GCD | 同上；判等已用交叉相乘绕开 |

**不做因式分解，所以 `(x^2-1)/(x-1)` 靠 L1/L2 约不掉。** 分子 `x^2 - 1` 的公共变量因子为空
（常数项不含 x），分母也是，L2 无从下手。

## 约分的代价：定义域

L2 会改变定义域：约掉变量 `x` 等价于默认 `x ≠ 0`，也就是丢掉了「原式在 `x = 0` 处无定义」。

```cpp
RationalFunction r = ...;      // (x^2 + x) / (x^2 - x)
r.simplify();                  // (x + 1) / (x - 1)
r.discardedConstraints();      // {x}  —— 结果仅在 x ≠ 0 时与原式等价
```

`discardedConstraints()` 的语义是：**以上化简在这些变量非零的前提下成立**。
关心定义域就检查它；忽略则相当于接受「化简后定义域更宽」。

L1 和 L3 不产生任何约束。

## 相等判断不依赖化简

`operator==` 内部用**交叉相乘**：`a/b == c/d ⟺ a·d == c·b`。

即使两个分式写法不同、靠 L1/L2 约不到一起，判等依然正确 ——
这正是「不做多项式 GCD 却语义仍然可靠」的原因。

## 化为多项式（长除法）

```cpp
Result<Polynomial> toPolynomial() const;
```

当且仅当**分母整除分子**时成功，返回商；否则 `NotAPolynomial`。

```cpp
// (x^2 - 1) / (x - 1)  →  x + 1
// 1 / x                →  Err(分式无法化简为多项式)
```

内部是多项式带余除法（见 [algebraic_expression.md](algebraic_expression.md#多项式带余除法)），
按**字典序**确定首项。

### 这与「不做因式分解」不矛盾

长除法不需要通用的因式分解算法：

- 因式分解是「给你 `x^2-1`，找出它等于 `(x-1)(x+1)`」—— 得自己找出因子
- 长除法是「**已知**分母 `x-1`，判断它是否整除 `x^2-1`」—— 因子是现成的，只需做一次带余除法

判断整除比找因子容易得多，风险也可控（余式非零就直接失败）。
换成 `(x^2-1)/(x-2)` 余式非零，`toPolynomial()` 立刻失败 —— 那才是因式分解真正会派上用场的地方。

### 定义域

这种归约会丢掉「原式在分母零点处无定义」这一信息：
`(a^2-1)/(a+1)` 化为 `a-1` 的前提是 `a ≠ -1`，归约后的式子在那里**有定义**，原式没有。

分母零点一般求不出（= 解方程），所以不像 L2 那样能给出精确的 `discardedConstraints`。
务实做法是调用方自行补上「原式要求 ⟨分母⟩ ≠ 0」——
`apps/simplify` 就是这么做的（分母是常数时不补，因为常数恒非零）。

## 代入与求值

```cpp
Scope scope;
scope.assign(Variable("x"), Integer(2));

Result<RationalFunction> reduced = r.substitute(scope);   // 代入后仍是分式
Result<Fraction> value = r.evaluate(scope);               // 完全求值
```

- `substitute` **迭代到不动点**，处理链式绑定，详见 [scope.md](scope.md#迭代到不动点)
- 代入后分母可能退化成零多项式（`1/(x-1)` 代入 `x = 1`）→ `ZeroDenominator`。
  **那不是程序错误，而是原式的极点**
- `evaluate` 要求代入后分子分母都化为常数，否则 `UndefinedVariable`
- 代入**不改变**此前化简已丢掉的约束，`discarded` 照旧保留

## 输出

`str()` 面向终端阅读，`latex()` 面向排版，两者并存：

| 情形 | `str()` | `latex()` |
| --- | --- | --- |
| 一般分式 | `(x + 1) / (x - 1)` | `\frac{x + 1}{x - 1}` |
| 常数分式 | `5/6` | `\frac{5}{6}` |
| 分母为 1 | `x` | `x`（不写成 `\frac{x}{1}`） |

格式细节见 [latex.md](../latex.md)。

`str()` 判断「分母为 1」时**必须同时要求 `isConstant()`** ——
只检查系数等于 1 的话，分母是 `x` 这种系数为 1 的单项式会被误判成 1，整个分式被输出成多项式。
