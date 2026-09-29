# maths.algebra:groebner — Gröbner 基与消元

对应 `src/algebra/groebner.cppm`。只依赖 `:expression`（多元多项式）。

多变量**等式**组的解法。三件具体的事：

| 要解决的问题 | 用它怎么做 |
| --- | --- |
| 这组方程有没有解？ | 算出基，看里面有没有非零常数（等价于 `1` 属于理想）|
| 解出变量（参数化） | Lex 序下基呈三角形式，首项是「最重要」的那个变量，可直接读成 `x = …` |
| 消掉若干变量（投影） | 取基里**不含**这些变量的元素 —— 它们构成消元理想的基（消元定理）|

为什么需要它：多变量点集的**约束式表示**（[constraint_system.md](constraint_system.md)）能判定
「某个点是否满足约束」，但「解出满足约束的点」需要理想上的消元。而一般半代数集的显式表示
（柱形代数分解）代价双指数，本库不做 —— 等式这一层用 Gröbner 就够，代价可以承受。

## 单项式序

```cpp
enum class MonomialOrder {
  Lex,   // 字典序：变量名小的先比（名字小的更「重要」→ 最先被解出）
  GrLex, // 分次字典序：先比总次数，再按字典序
};
```

`GrevLex`（分次反字典序）尚未实现 —— 它只影响效率，不影响可解性。

## 接口

| 函数 | 作用 |
| --- | --- |
| `leadingTerm(polynomial, order)` | 按给定序取首项（单项式 + 系数）；零多项式返回 `nullopt` |
| `monic(polynomial, order)` | 首项系数归一为 1（根不变，后续除法省事）|
| `multivariateDivide(dividend, divisors, order)` | 多元带余除法：商组 + 余式 |
| `normalForm(polynomial, divisors, order)` | 反复约化后的余式 |
| `sPolynomial(lhs, rhs, order)` | `S(f,g) = (x^γ/LT(f))·f − (x^γ/LT(g))·g`，消掉两个首项 |
| `groebnerBasis(generators, order)` | Buchberger 主循环 |
| `isInconsistent(basis)` | 基里出现非零常数 → 方程组无解 |
| `eliminateVariables(basis, variables)` | 取基里不含这些变量的元素 |

## 实跑的样子

```
{x² + y² − 1, x − y}        →  x − y,  y² − 1/2          （把 x 代掉，2y² = 1）
{x − 2, xy − 1}             →  x − 2,  y − 1/2
{x² + y² + z² − 1, x − y, y − z}  →  x − y, y − z, z² − 1/3
{x + y, x + y + 1}          →  1                          （无解）
```

第三条正好演示三角形式：首项分别是 `x`、`y`、`z`，可以直接读成
`x = y`、`y = z`、`z = ±1/√3`。消去 `x`、`y` 之后只剩 `z² − 1/3`。

## 变量顺序的约定

`Lex` 按**变量名**比较，名字小的变量更「重要」。所以「先解出哪个变量」由名字决定：
`x` 比 `y` 重要、`y` 比 `z` 重要。想换个顺序就换名字（或将来支持指定变量序）。

## 实现与限制

- **朴素 Buchberger** + 第一判别法（首项单项式互素的对直接跳过，其 S-多项式必约化为 0）
- 系数全程精确有理运算（`Fraction`），**不含浮点**
- 有计算预算上限（基 ≤ 64 个元素、处理的 S-多项式 ≤ 512 个）：超出报 `NumericOverflow`。
  这是为了防止「系数爆炸导致算不完」时静默卡死 —— 不是常规错误路径
- 只处理**等式**（理想）：不等式那一层归 `RealSet` / `ConstraintSystem`
- 不做 `GrevLex`、不做 F4/F5 之类的高效变体（当前规模用不上）

> 首一化会带出分数系数，所以**比较基元素时按值比**（转成 `RationalFunction` 再 `==`）：
> `parseExpression("y^2 - 1/2").getNumerator()` 会把分母清成 `2y² − 1`，直接比多项式会不相等。
