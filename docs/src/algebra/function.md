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
| 集合 | `preimage(RealSet)` = `g⁻¹(T) ∩ 定义域` |
| 输出 | `ruleStr()` / `ruleLatex()`、`domainStr()` / `domainLatex()`、`str()` / `latex()` |

`str()` 与 `latex()` 只在定义域**不是整条实轴**时才把 `x ∈ …` 缀上 ——
那正是这个函数与「同规则、无限制」的那个函数的区别所在。

## 定义域是怎么算出来的

| 来源 | 条件 | 例子 |
| --- | --- | --- |
| 系数的分母 | ≠ 0 | `1/(x−1)` → x ≠ 1 |
| 每个被开方数 | ≥ 0 | `√(x²−1)` → (−∞,−1] ∪ [1,+∞) |
| 显式给的集合 | 取交 | `make(1/x, [0,2])` → (0,2] |

三者取交。注意最后一条是**取交不是覆盖**：规则算不出来的点永远不属于函数。

## 复合的定义域

`(f ∘ g)(x)` 的定义域是 `{x ∈ dom(g) : g(x) ∈ dom(f)}`。这里有个便宜的等价路径：

复合后规则的**天然定义域**恰好等于 `g⁻¹(dom f)`（定义域怎么定义的，代入后就是怎么定义的），
所以 f 没被 `restrict` 过时直接交给 `make` 即可。被收窄过才需要真的算 `preimage`
—— 而 `preimage` 要求端点是有理数，算不出来时明确报错，**不拿天然定义域蒙混**
（那会把定义域悄悄放大，正是本库最在意的一类错误）。

## 四条刻意的边界

1. **一元**。规则里出现两个以上自变量报 `NotUnivariate`。多元函数的定义域是多维点集
   （`AtomConstraint` / `ConstraintSystem`）；要「多元」就先用 `Scope` 部分代入把自变量消掉。
2. **系数是有理函数**。`√2·x` 这类「代数数当系数」的函数不在本类型里，那是
   `AlgebraicRationalFunction` 的领域；本类型管的是「根号里带变量」。
   反过来取值结果用 `RealAlgebraicNumber`，√2 完全装得下。
3. **复合的内层不能含根号**（外层含根号没问题）。内层含根号就是「根式套根式」，
   需要多重二次扩张的张量积，本库不做。
4. **`preimage` 只支持有理规则、且目标集合的端点是有理数**。前者因为「√f(x) ∈ [a,b]」
   要逐根号做单调性推理；后者因为解不等式时端点要进多项式系数（`Polynomial` 的系数是 ℚ）。

## 相关

- 定义域的表示与运算 → [real_set.md](../numeric/real_set.md)
- 规则的来源 → [radical.md](radical.md)、[rational_function.md](rational_function.md)
- 不等式的解 → [constraint.md](constraint.md)
