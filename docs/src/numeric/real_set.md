# maths.real_set — 一维实点集与不等式

对应 `src/numeric/real_set.cppm`。

`RealSet` 是一个**实数集合**，用来表示「`x` 能取哪些值」：解不等式的解集、
函数的定义域、条件的取值范围，都是它。

## 一种表示就够

一维实点集 = **有限个区间的并集**。孤立点写成退化区间 `[a, a]`；空集 = 没有区间；
全集 = `(-∞, +∞)`。

这不是简化：一元多项式（不）等式的**任何布尔组合**，解集一定能写成有限个区间 / 点 ——
这就是一维半代数集的规范形。所以不存在「另一种形式的集合」需要来回转换。

**端点是 `RealAlgebraicNumber`**，不是浮点：`√2` 作为端点也是精确的，
区间的相交、合并、判等全部走精确比较，不采样、不近似。


## 接口

| 分类 | 成员 |
| --- | --- |
| 构造 | `empty()`、`realLine()`、`point(a)`、`closedInterval(a, b)`、`make(intervals)`（排序 + 合并 + 校验）|

`make` 的合并判据只看**接触点有没有被覆盖**：

| 相接的两段 | 合并成 | 为什么 |
| --- | --- | --- |
| `[0,1]` 与 `[1,2]` | `[0,2]` | 1 被两侧取到 |
| `(0,1]` 与 `(1,2)` | `(0,2)` | 1 被前侧取到 |
| `(−∞,0)` 与 `[0,+∞)` | `ℝ` | 0 被后侧取到，中间没有洞 |
| `(0,1)` 与 `(1,2)` | **不并** | 1 两侧都不取，并了会把 1 也算进去 |

「两侧都取到」是过强的条件：它会把 `(−∞,0) ∪ [0,+∞)` 这种本来就是整条实轴的集合
留成两段，于是 `isRealLine()` 为假、与 `ℝ` 判等也对不上。
| 查询 | `intervals()`、`isEmpty()`、`isRealLine()`、`contains(point)` |
| 运算 | `unite`、`intersect`、`complement()`（相对 ℝ）|
| 求不等式 | `solve(polynomial, relation)` |
| 输出 | `str()`、`latex()` |

`Bound` 表示端点（有限值 + 是否取到，或 ±∞），`Interval` 是一段，
`Relation` 是 `= ≠ < ≤ > ≥` 六个关系。

## 解不等式

`RealSet::solve(p(x), ⋈)` 精确求解，分三步：

1. **求临界点**：`p` 的全部实根，用 Sturm 实根计数 + 二分隔离，升序、互不相同
   （重根由平方自由化先去掉）
2. **逐段判号**：每两个相邻临界点之间取一个**有理样本点**，代入 `p` 做精确有理运算 ——
   段内符号恒定，所以一个样本点就定了整段
3. **拼结果**：符号符合条件的开区间；`≤ / ≥` 时端点（也就是零点本身）标成取到，
   于是 `[-1, 0]` 这类闭区间会自动合出来

`polynomial` 是一元多项式 `UnivariatePolynomial`（下例的 `polynomialOf({−1, 0, 1})` 表示 `x² − 1`，
系数从常数项开始）。

```cpp
RealSet::solve(polynomial, Relation::Greater).unwrap().latex();     // x²−1 > 0  → (-\infty, -1) \cup (1, +\infty)
RealSet::solve(polynomial, Relation::LessEqual).unwrap().latex();   // x²−1 ≤ 0  → [-1, 1]
RealSet::solve(polynomial, Relation::Equal).unwrap().latex();       // x²−1 = 0  → \{-1\} \cup \{1\}
RealSet::solve(polynomial, Relation::NotEqual).unwrap().intervals().size();   // 3 段：ℝ 去掉两个点
```

> **两条红线**：临界点必须是**代数数**（不是浮点根），样本点必须是**有理数**。
> 破任一条，解集就可能悄悄错一个端点 —— 而这种错用户看不出来。

退化情形也按数学处理：零多项式下 `0 ≥ 0` 是全集、`0 < 0` 是空集；
常数多项式没有临界点，整条实轴符号恒定。

## 根式的端点

`x² − 2 ≥ 0` 的解是 `(-∞, -√2] ∪ [√2, +∞)`，端点就是精确的代数数：

```cpp
RealSet set = RealSet::solve(polynomialOf({-2, 0, 1}), Relation::GreaterEqual).unwrap();
set.contains(RealAlgebraicNumber::parse("\\sqrt{2}").unwrap());   // true —— 精确比较
```

## 配套：多项式的全部实根

解不等式要用「列出全部实根」，这个入口在 `RealAlgebraicNumber::realRoots(polynomial)`：

```cpp
RealAlgebraicNumber::realRoots(polynomialOf({-2, 0, 1}));   // [−√2, √2]，升序
RealAlgebraicNumber::realRoots(polynomialOf({1, 0, 1}));    // 空 —— x²+1 没有实根
```

做法是先取 Cauchy 界 `1 + max|aᵢ/aₙ|`（所有根严格落在 `±界` 内，因此端点不会是根），
再对半分裂 + Sturm 计数递归；分裂点恰好是根（有理根）时先把它除掉再递归，
避免端点落在根上导致计数含糊。

## 明确不做

- **多元集合**：二维以上的一般半代数集需要柱形代数分解（CAD），量级完全不同。
  多变量的表示与限定条件另见设计备忘（约束式表示 + 消元），不在本模块。
- **超越不等式**：本库排除超越函数。
- **最简区间化之外的化简**：集合只保证「排序 + 合并 + 端点取到性」这一层规范形。
