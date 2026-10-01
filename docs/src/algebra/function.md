# maths.algebra:function — 一元实函数（集合 → 实数）

对应 `src/algebra/function.cppm`。

一个函数 = **规则 + 定义域**。定义域本身是 `RealSet`（区间的有限并，端点是精确的实代数数），
所以 `√(x²−1)` 的定义域 `(−∞,−1] ∪ [1,+∞)` 是算出来的，不是浮点近似画出来的。

## 为什么是「集合 → 实数」

前面几个模块把「式子」做完了，但式子 ≠ 函数：`1/x` 这个式子在有意义的地方是
ℝ\{0}，`√x` 是 [0,+∞)，而 `√x·√x` 作为**式子**仍然要求 x ≥ 0，哪怕乘出来是 x。
函数层把「哪里有意义」这件事显式地挂在类型上，于是

- 求值可以先查定义域，越界报 `OutsideDomain` 而不是算出一个假结果
- 四则与复合的定义域按集合运算自动收窄
- 可以把定义域收窄（`restrict`）成另一个函数，例如「把 f 限制在 [0,1] 上」

## 规则能装什么

规则用 `RadicalExtension` 承载，于是同一个类型覆盖三类函数：

| 函数 | 规则形态 | 定义域 |
| --- | --- | --- |
| `f(x) = 3x² − 1` | 有理函数（零生成元） | ℝ |
| `f(x) = 1/(x−1)` | 有理函数（分母带来约束） | ℝ \ {1} |
| `f(x) = √(x²+1) + √(x²+2)` | 两个生成元的根式扩张 | ℝ（被开方数恒正） |
| `f(x) = √x` | 一个生成元 | [0, +∞) |

## 基本用法

```cpp
// 规则推自变量与定义域
RealFunction f = RealFunction::make(parseExpression("1/(x-1)").unwrap()).unwrap();
f.domain().latex();                 // "(-\infty, 1) \cup (1, +\infty)"
f.at(Fraction(3, 1)).unwrap().str(); // "1/2"
f.at(Fraction(1, 1)).isErr();        // true —— OutsideDomain

// 根式函数：取值结果是实代数数，√2 这种点在定义域内也能算
RealFunction g = RealFunction::make(RadicalExtension::make(parseExpression("x^2+1").unwrap()).unwrap()).unwrap();
RealAlgebraicNumber rootTwo = RealAlgebraicNumber::parse("\\sqrt{2}").unwrap();
g.at(rootTwo).unwrap().latex();      // "\sqrt{3}"

// 收窄定义域（取交，不是覆盖）
RealFunction onUnit = RealFunction::make(parseExpression("x^2").unwrap(),
                                         RealSet::closedInterval(RealAlgebraicNumber(Fraction(1,1)),
                                                                 RealAlgebraicNumber(Fraction(2,1))).unwrap()).unwrap();
onUnit.domain().str();               // "[1, 2]"
```

## 接口

| 分类 | 成员 |
| --- | --- |
| 构造 | `make(rule)`、`make(rule, domain)`（与天然定义域取交）、`constant(value, variable = "x")` |
| 查询 | `variable()`、`rule()`、`domain()`、`variables()`、`isConstant()`、`isRational()` |
| 求值 | `at(RealAlgebraicNumber)`、`at(Fraction)`（定义域外报 `OutsideDomain`） |
| 定义域 | `restrict(RealSet)` |
| 四则 | `operator+ - * /`（返回 `Result`，定义域取交）、`negate()`、`scaledBy(Fraction)` |
| 复合 | `compose(inner)` = `this ∘ inner` |
| 集合 | `preimage(RealSet)` = `g⁻¹(T) ∩ 定义域`；`image(RealSet)` = `f(S)`；`range()` = `f(定义域)` |
| 相关 | 规则装不下拐弯的函数（如 `\|x\|`）→ 用 [piecewise.md](piecewise.md) |
| 输出 | `ruleStr()` / `ruleLatex()`、`domainStr()` / `domainLatex()`、`str()` / `latex()` |

`str()` 与 `latex()` 只在定义域**不是整条实轴**时才把 `x ∈ …` 缀上 ——
那正是这个函数与「同规则、无限制」的那个函数的区别所在。

## 定义域是怎么算出来的

| 来源 | 条件 | 例子 |
| --- | --- | --- |
| 系数的分母 | ≠ 0 | `1/(x−1)` → x ≠ 1 |
| **化简约掉的变量** | ≠ 0 | `x/x` 化简成 `1`，但 `x = 0` 处原本无定义 |
| 每个被开方数 | ≥ 0 | `√(x²−1)` → (−∞,−1] ∪ [1,+∞) |
| 显式给的集合 | 取交 | `make(1/x, [0,2])` → (0,2] |

四者取交。注意显式给的那条是**取交不是覆盖**：规则算不出来的点永远不属于函数。

第三条容易漏：`RationalFunction` 化简时会把分子分母的公因式约掉（`x/x → 1`），
约掉 x 等于默认 x ≠ 0 —— 约束记在 `discardedConstraints` 里。函数层必须把它算进定义域，
否则 `x/x` 会在 x = 0 上给出 1，那是**静默给错**。只收与自变量同名的约束
（不同名说明那个变量已经被约得不再是自变量）。

## 复合的定义域

`(f ∘ g)(x)` 的定义域是 `{x ∈ dom(g) : g(x) ∈ dom(f)}`。这里有个便宜的等价路径：

复合后规则的**天然定义域**恰好等于 `g⁻¹(dom f)`（定义域怎么定义的，代入后就是怎么定义的），
所以 f 没被 `restrict` 过时直接交给 `make` 即可。被收窄过才需要真的算 `preimage`
—— 而 `preimage` 要求端点是有理数，算不出来时明确报错，**不拿天然定义域蒙混**
（那会把定义域悄悄放大，正是本库最在意的一类错误）。

## 集合视角：拉回与像集

两个方向都做了，都只支持**有理规则**（含根号的要逐根号做单调性推理，见下面的边界）。

| 操作 | 含义 | 例子 |
| --- | --- | --- |
| `preimage(T)` | `g⁻¹(T) ∩ 定义域`，把集合沿函数拉回去 | `x²` 对 `[1,4]` 的拉回 = `[−2,−1] ∪ [1,2]` |
| `image(S)` | `f(S) = {f(x) : x ∈ S}`，S 先与定义域取交 | `x²` 在 `(1,2)` 上的像 = `(1,4)` |
| `range()` | `f(定义域)` | `x/(x²+1)` 的值域 = `[−1/2, 1/2]` |

**拉回**复用 `solveInequality`：`g(x) ∈ [a,b]` 就是两条不等式 `g − a ≥ 0` 与 `g − b ≤ 0`。
目标集合是若干**互不相交**的区间，所以逐区间拉回再并起来就是完整的拉回。

**像集**用临界点把函数切成单调段：有理函数处处可导，`g′ = (p′q − pq′)/q²`，
分母是平方恒不为负，所以临界点就是 `p′q − pq′` 的实根（Sturm 精确隔离）。
于是每个连通块的像**都是一个区间**，两端取遍

```
{临界点上的值} ∪ {闭端点的值} ∪ {开端点的单侧极限}
```

的 min / max。全程精确：临界点是实代数数，极限都有精确表达式 ——
次数比（`deg p` 与 `deg q` 的大小关系）给出无穷远的极限，开端点上有定义就是该点的值
（但不取到），极点上则是 ±∞（方向由相邻「符号格」里的有理样本判号）。

**端点取到与否**是这套算法唯一容易写错的地方，所以三类值分开收集、不合并：

- 临界点与闭端点上的值 → 取到
- 开端点上的单侧极限 → 不取到（`x²` 在 `(1,2)` 上的像是 `(1,4)` 而不是 `[1,4]`）
- `min == max` 时函数在整块上恒等于该值（像是单点集），那时取到 ——
  但**只有两头都没跑向无穷**时才能这么推：`−1/x` 在 `(0,∞)` 上候选值只有 0，
  像却是 `(−∞,0)`，端点不能因为「候选值相等」就标成取到

```cpp
functionOf("x/(x^2+1)").range().unwrap().latex();   // "[-\\frac{1}{2}, \\frac{1}{2}]"
functionOf("x^2").range().unwrap().latex();          // "[0, +\\infty)"
functionOf("x/(x^2-1)").range().unwrap().latex();    // "\\mathbb{R}"  ← 中间那块从 +∞ 掉到 −∞
```

## 四条刻意的边界

1. **一元**。规则里出现两个以上自变量报 `NotUnivariate`。多元函数的定义域是多维点集
   （`AtomConstraint` / `ConstraintSystem`）；要「多元」就先用 `Scope` 部分代入把自变量消掉。
2. **系数是有理函数**。`√2·x` 这类「代数数当系数」的函数不在本类型里，那是
   `AlgebraicRationalFunction` 的领域；本类型管的是「根号里带变量」。
   反过来取值结果用 `RealAlgebraicNumber`，√2 完全装得下。
3. **复合的内层不能含根号**（外层含根号没问题）。内层含根号就是「根式套根式」，
   需要多重二次扩张的张量积，本库不做。
4. **`preimage` 与 `image` 只支持有理规则，且 `preimage` 的目标集合端点必须是有理数**。
   前者因为「√f(x) ∈ [a,b]」要逐根号做单调性推理；后者因为拉回时端点要进多项式系数
   （`Polynomial` 的系数是 ℚ）。像集这一侧没有端点限制：它只**产出**代数端点
   （例如临界点 `√2` 对应的函数值），不需要把代数端点喂进多项式系数。

## 相关

- 定义域的表示与运算 → [real_set.md](../numeric/real_set.md)
- 规则的来源 → [radical.md](radical.md)、[rational_function.md](rational_function.md)
- 不等式的解 → [constraint.md](constraint.md)
