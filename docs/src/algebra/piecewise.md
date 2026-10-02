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

`max(f, g)` = 在 `f ≥ g` 的地方取 `f`，其余取 `g`。所以只要能把 `f − g ≥ 0` 的解集精确解出来就行。
**差的生成元个数 ≤ 1** 时做得到：

| 差 | 怎么解 |
| --- | --- |
| 纯有理函数 | `solveInequality` 直接给 |
| `a + b√f`（一个生成元） | 化归成有理不等式，见下 |
| 两个以上生成元 | `NotARational`（要更深的代数数不等式推理） |

```cpp
maximumOf(x, 1)      // x ≥ 1 处取 x，其余取 1
minimumOf(x², x)     // [0,1] 上取 x²，其余取 x
maximumOf(x, −x)     // 就是 |x| —— 与 absoluteValue() 两条路径给同一个结果
maximumOf(√x, x)     // [0,1] 上 √x ≥ x，之后 x 更大
```

### 一个生成元时怎么化归

`√f ≥ h ⟺ h ≤ 0 ∨ f ≥ h²`，`√f ≤ h ⟺ h ≥ 0 ∧ f ≤ h²`。于是 `a + b√f ≥ 0` 写成

```
b > 0:  √f ≥ −a/b   ⟺   a/b ≥ 0  ∨  f − (a/b)² ≥ 0
b < 0:  √f ≤ −a/b   ⟺   a/b ≤ 0  ∧  f − (a/b)² ≤ 0
b = 0:  a ≥ 0
```

三者取并。**三种情形都得留着** —— `b` 是 `x` 的函数，它的符号随 `x` 变，
不是一个可以事先分好的「大情形」。

### ⚠️ 两支都必须落在「两边都有定义」的地方

`max` 只在两边都定义的点上有意义。只与 `lhs.domain()` / `rhs.domain()` 取交是不够的：
`max(1/x, x)` 的两支分别是 `1/x` 与 `x`，而 `x` 自己的定义域是整条实轴 ——
不拦住的话 `x = 0` 上会冒出一个值来。所以第二支要与 `(lhs − rhs).domain()` 取交。

## 复合

| 形式 | 结果 |
| --- | --- |
| `compose(f, g)`（外层单规则、内层分段） | 逐支复合 |
| `g.compose(f)`（外层分段、内层单规则） | 外层每支分别与 `f` 复合 |
| `g.compose(h)`（两边都是分段） | 外层各支 × 内层各支 |

```cpp
compose(√y, |x|)         // { √x on [0,+∞) ; √(−x) on (−∞,0) } —— 就是 √|x|
x².compose(|x|)          // x²
|x|.compose(x−1)         // |x−1|
|x|.compose(|x|)         // |x|
compose(√(y+1), |x|)     // √(|x|+1)
```

定义域由 `RealFunction::compose` 逐支算好（`g⁻¹(E_j) ∩ dom(g)`），外层各支不交，
复出来的定义域自然也不交，`make` 直接收下。

**内层含根号会失败**（`NotARational`）：那是「根式套根式」，本库不做。
所以 `√|x|` 可以（`|x|` 的两支都是有理函数），`√(√x)` 不行。


## 从文本直接进来

`parsePiecewiseExpression` 把 `\sqrt{x^2}` 这类输入直接接成分段函数
（详见 [expression_parser.md](../parser/expression_parser.md)）：

```cpp
parsePiecewiseExpression("\\sqrt{x^2}");   // 就是上面那个 |x|
```

## 相关

- 单规则函数 → [function.md](function.md)
- 分界的来源（解不等式） → [constraint.md](constraint.md)
- 定义域的表示与运算 → [real_set.md](../numeric/real_set.md)
