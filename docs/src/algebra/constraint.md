# maths.algebra:constraint — 取值范围约束

对应 `src/algebra/constraint.cppm`。

把「这个变量能取哪些值」表示成 `variable ∈ RealSet`，于是三件事合流：

| 场景 | 得到的东西 |
| --- | --- |
| 解不等式 `f(x) ⋈ 0` | 解集就是一个 `RealSet` |
| 函数的定义域 | 分母 ≠ 0 → `RealSet` |
| 化简时丢掉的约束（约掉 x ⟹ 默认 x ≠ 0） | 语义就是 `x ∈ ℝ \ {0}`，同一种东西 |

不需要为每种场景另造一个类型 —— 这也是它值得单独一个模块的原因。

## 类型

```cpp
class RangeConstraint {          // variable ∈ allowed
  const Variable &variable() const;
  const RealSet &allowed() const;
  bool admits(const RealAlgebraicNumber &value) const;
  std::string latex() const;     // "x \\in (-\\infty, 0) \\cup (0, +\\infty)"
};
```

> 名字带 `Range` 是有意的：多维点集的原子约束（多项式 ⋈ 0）将来叫 `AtomConstraint`，
> 两者不是一回事；而 `Constraint` 这个名字太通用，容易和调用方自己的同名类型撞。

## 解不等式

```cpp
Result<RealSet> solveInequality(const RationalFunction &function, Relation relation);
```

化到多项式的两步：

1. **不严格不等号**：`sign(p/q) = sign(p·q)`（因为 `q² > 0`），所以对 `p·q` 求解
2. **`=` 与 `≠`**：只看分子 `p`
3. 最后与**定义域取交** —— 分母的零点也是 `p·q` 的根，但那里 `f` 无定义，不属于解集

```cpp
solveInequality(parseExpression("1/x").unwrap(), Relation::Greater);        // (0, +\infty)
solveInequality(parseExpression("1/x").unwrap(), Relation::GreaterEqual);   // 仍是 (0, +\infty)
solveInequality(parseExpression("(x-1)/(x+2)").unwrap(), Relation::GreaterEqual);
                                                                            // (-\infty, -2) \cup [1, +\infty)
```

`1/x ≥ 0` 的答案**不含 0** —— 「取等」不会把定义域外的点收进来。

常数分式（不含变量）处处同号，解集要么整条实轴要么空；`0 ≥ 0` 恒真、`0 > 0` 恒假。

只支持**单变量**：多变量的解集是多维点集，不在本模块范围（返回 `InvalidExpression`）。

## 定义域

```cpp
Result<RealSet> domainOf(const RationalFunction &function);
```

分母的所有实根从 ℝ 里去掉；分母是常数时是整条实轴。
根式表达式另有一个重载（在 [radical.md](radical.md)）：`domainOf(RadicalExtension)`
= 所有被开方数 ≥ 0 与系数分母 ≠ 0 取交。

```cpp
domainOf(parseExpression("1/x").unwrap());         // (-\infty, 0) \cup (0, +\infty)
domainOf(parseExpression("1/(x^2-1)").unwrap());   // 三段：去掉 −1 与 1
domainOf(parseExpression("x/(x^2+1)").unwrap());   // \mathbb{R}
```

## 化简丢掉的约束

```cpp
std::vector<RangeConstraint> constraintsOf(const RationalFunction &function);
```

`discardedConstraints()`（约分约掉的变量）逐条转成 `x ∈ ℝ \ {0}`。
`(x²+x)/(x²−x)` 约掉 `x` 后就带着这样一条约束。

## 接进 Scope

赋值给的是**点**（`x = 2`），约束给的是**范围**（`x ≥ 1`、`x ≠ 0`），两者并存：

```cpp
Scope scope;
scope.restrict(Variable("x"), withoutZero());       // x ∈ ℝ \ {0}
scope.assign(Variable("x"), Fraction(0, 1));

scope.admits(Variable("x"));   // Admission::Violates —— 绑定的值落在约束外
```

| `Admission` | 含义 |
| --- | --- |
| `Admits` | 满足（或该变量没有约束）|
| `Violates` | 违反 |
| `Unknown` | 判断不了：变量未绑定，或绑定值不是常数（含其它变量）|

`Unknown` 是刻意的 —— 判不出来就说判不出来，不猜。

几条语义：

- 同一变量重复 `restrict` 取**交集**（约束只会越来越紧），所以只留一条
- `erase(variable)` 只解除**绑定**，不动约束：约束说的是「这个变量能取什么」，
  与「当前绑没绑」是两件事。要一并清掉用 `clear()`
- `constraints()` 可以取出当前所有约束（含允许集合）

## 明确不做

- 多元约束（需要多维点集的表示与消元，见另一条设计线）
- 带根号的不等式（`√(x²−1) > 2`）：要先对根式做符号结构分析
- 超越不等式：本库排除超越函数
