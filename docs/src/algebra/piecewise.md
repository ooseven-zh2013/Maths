# maths.algebra:piecewise — 分段函数（绝对值等）

对应 `src/algebra/piecewise.cppm`。

**有限个分支**的并，每个分支就是一个 [RealFunction](function.md)。表示成「先写的分支优先」，
构造时会把后面分支与前面对不交的部分减掉，所以内部存的一定是**两两不交**的一组分支。

## 为什么必须有这个类型

`RealFunction` 的规则只能是 `RadicalExtension`，而那种元素的每个成员都是
**在定义域上有理（因而连续）**的函数。`|x|` 不是 —— 它在 0 处拐弯，表示不成任何一条规则。

```
      ⎧  x , x ≥ 0
|x| = ⎨
      ⎩ −x , x < 0
```

### 这**不违反**「本库不引入 |x|」那条红线

红线的原话是：`√(x²)` 在代数函数域 ℚ(x)[y]/(y²−x²) 里绕不过去（环可约、y 是零因子、
±x 都是根，没有单值元素），所以 `RadicalExtension` **拒收**它。

那条限制管的是**规则之内**。`|x|` 作为 `ℝ → ℝ` 的函数本身定义良好，只是表示不成单条规则
而已 —— 换个容器装就行。两件事不要混。

```cpp
PiecewiseFunction absolute = absoluteValue().unwrap();      // |x|
absolute.branchCount();                                      // 2
absolute.at(Fraction(-5, 1)).unwrap().str();                 // "5"
absolute.range().unwrap().latex();                           // "[0, +\infty)"
absolute.image(closedInterval(-2, 1)).unwrap().latex();      // "[0, 2]"
absolute.latex();                                            // \begin{cases} ...
```

## 接口

| 分类 | 成员 |
| --- | --- |
| 构造 | `make(cases)`（归一化：丢空分支、按顺序去重）、`absoluteValue(variable)` |
| 查询 | `cases()`、`branchCount()`、`branch(i)`、`variable()`、`domain()`、`isSingleBranch()` |
| 降阶 | `toRealFunction()` —— 只有一个分支时降回 `RealFunction` |
| 求值 | `at(点)` —— 命中哪个分支就交给它；一个都不命中是 `OutsideDomain` |
| 定义域 | `restrict(RealSet)` |
| 像集 | `image(S)` / `range()` —— **各分支像之并** |
| 四则 | `+ - * /`（分段之间、分段与 `RealFunction` 之间）、`negate()`、`scaledBy(Fraction)` |
| 输出 | `str()`（一行，分支用 `;` 隔开）、`latex()`（`cases` 环境） |

## 三条约定

**1. 先写的分支优先。** 后写的分支会被前面已经覆盖的部分减掉。

```cpp
make({ x on ℝ , x² on [0,1] })          // → 只剩一支 x（第二支被完全盖住）
make({ 1 on [0,2] , 2 on [1,3] })       // → { 1 on [0,2] ; 2 on (2,3] }
```

**2. 自变量必须一致。** 常函数分支不参与约束（它没有自变量），其余分支的自变量名必须相同，
否则报 `NotUnivariate`。

**3. 像集是各分支像之并。** 分支不交 + 每支的像是一个区间，所以并起来就是真像 ——
这是分段表示最划算的一条：直接复用 `RealFunction::image`，不需要另写一套单调性分析。

## 四则：分支两两配对

点落在第 i 支且第 j 支上的区域就是 `D_i ∩ E_j`。`D_i`、`E_j` 各自两两不交，
所以配出来的块也两两不交 —— 于是合并规则就是逐块套用单规则的四则，
不需要额外的划分逻辑。`n` 支乘 `m` 支最多产生 `n·m` 块，空块由 `make` 丢掉。

```cpp
auto squared = (absolute * absolute).unwrap();   // |x|·|x|：两支的规则都化成 x²
squared.at(Fraction(-3, 1)).unwrap().str();      // "9"
```

## 逐点取大 / 取小

`max(f, g)` = 在 `f ≥ g` 的地方取 `f`，其余取 `g`。分界点就是 `f − g = 0` 的根，
交给 `solveInequality` 精确解出 —— 所以**要求两条规则都是有理函数**：含根号时要解
含根号的不等式，本库不做（报 `NotARational`）。

```cpp
maximumOf(x, 1)      // x ≥ 1 处取 x，其余取 1
minimumOf(x², x)     // [0,1] 上取 x²，其余取 x
maximumOf(x, −x)     // 就是 |x| —— 与 absoluteValue() 两条路径给同一个结果
```

## 相关

- 单规则函数 → [function.md](function.md)
- 分界的来源（解不等式） → [constraint.md](constraint.md)
- 定义域的表示与运算 → [real_set.md](../numeric/real_set.md)
