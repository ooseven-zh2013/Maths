# maths.algebra:constraint_system — 多维点集（约束式表示）

对应 `src/algebra/constraint_system.cppm`。依赖 `:constraint`、`:scope`、`:algebraic`。

`x − y = z`、`x² + y² \le 1` 这类**多变量**的限定条件，解集是 ℝⁿ 里的半代数集。

## 为什么不展开成区域

n ≥ 2 时把点集写成「胞腔的并」要做**柱形代数分解**（quantifier elimination），
代价对变量数**双指数** —— 本库明确不做。

改法是保留一组**原子约束**（隐式表示），按需判定与化简：

```
x − y = z        → 原子 `x − y − z = 0`
x² + y² ≤ 1      → 原子 `x² + y² − 1 ≤ 0`
```

| 能力 | 做法 | 代价 |
| --- | --- | --- |
| **成员判定** | 代入 + 精确判符号 | 快，且精确 |
| **合取** | 原子列表合并（去重）| 快 |
| **可分离情形** | 每个原子只含一个变量 → 各维一维 `RealSet` 的笛卡尔积 | 快，复用一维那套 |
| 解方程组 / 参数化 / 投影 | 需要 Gröbner 消元或 Fourier–Motzkin | **尚未实现** |

一维才是能写出规范形的特例（解成 `RealSet`，见 [constraint.md](constraint.md)）。

## 接口

```cpp
class AtomConstraint {                       // expression ⋈ 0
  AtomConstraint(Polynomial expression, Relation relation);
  const Polynomial &expression() const;
  Relation relation() const;
  std::set<Variable> variables() const;
  Result<bool> admits(const Scope &point) const;            // 有理点
  Result<bool> admits(const AlgebraicScope &point) const;   // 代数点（x = √2）
  std::string latex() const;                                // "x - y - z = 0"
};

class ConstraintSystem {                     // 若干原子的合取（空合取恒真）
  const std::vector<AtomConstraint> &atoms() const;
  bool isTrivial() const;
  std::set<Variable> variables() const;
  ConstraintSystem andWith(const ConstraintSystem &rhs) const;   // 交
  Result<bool> admits(const Scope &point) const;
  Result<bool> admits(const AlgebraicScope &point) const;
  Result<std::optional<std::map<Variable, RealSet>>> asSeparable() const;
  std::string latex() const;                                   // "x - y - z = 0 \land x \ge 0"
};
```

## 成员判定

代入后做**精确**判号 —— 有理点走 `Fraction`，代数点走 `RealAlgebraicNumber`：

```cpp
AtomConstraint plane(parseExpression("x-y-z").unwrap().getNumerator(), Relation::Equal);

Scope scope;
scope.assign(Variable("x"), Fraction(3, 1));
scope.assign(Variable("y"), Fraction(1, 1));
scope.assign(Variable("z"), Fraction(2, 1));
plane.admits(scope).unwrap();                 // true —— 3−1−2 = 0

AlgebraicScope algebraic;                     // x = √2
algebraic.assign(Variable("x"), RealAlgebraicNumber::parse("\\sqrt{2}").unwrap());
AtomConstraint(root).admits(algebraic);       // x² − 2 = 0 在 √2 处成立
```

变量没绑全 → `UndefinedVariable`（判断不了就说判断不了，不猜）；代入后分母为零 →
`DivisionByZero`（该点不在定义域内）。

## 可分离情形

每个原子只含**一个**变量时，点集就是各维一维实集的笛卡尔积：

```cpp
1 ≤ x ≤ 2 且 y ≥ 0   →  { x: [1, 2], y: [0, +\infty) }
```

返回值是 `Result<std::optional<std::map<Variable, RealSet>>>`：

| 返回 | 含义 |
| --- | --- |
| `optional` 有值 | 各维的一维实集（可能为空 map，表示无限制）|
| `optional` 为空 | **恒假** —— 某个不含变量的原子不成立（如 `1 = 0`）|
| 错误 `InvalidExpression` | **不可分离** —— 某个原子含两个以上变量，那要靠消元 |

## 线性情形的投影：Fourier–Motzkin

全是线性约束时（`Σ aᵢxᵢ ⋈ 0`），「消掉若干变量、看剩下哪些点可达」有精确的有限算法：

```cpp
Result<std::optional<ConstraintSystem>> projectLinear(const ConstraintSystem &system,
                                                      const std::set<Variable> &eliminate);
```

逐个变量消去，每轮把「该变量有上界」与「有下界」的约束两两组合，得到一条不含它的新约束。

```cpp
{x ≥ y, x ≤ z}        消去 x  →  y ≤ z        （(y,z) 能被某个 x 补全的条件）
{x > y, x < z}        消去 x  →  y < z        （严格性被保留：y = z 不行）
{x ≥ 1, x ≤ 0}        消去 x  →  无解
{x ≥ 1, x ≤ 3}        消去 x  →  恒真（对剩余变量没有限制）
{x = y, x = z}        消去 x  →  y = z
{x ≥ 0, y ≥ x, z ≥ y} 消去 x,y →  z ≥ 0
```

三种返回值语义（与 `asSeparable` 一致）：

| 返回 | 含义 |
| --- | --- |
| 空系统（`isTrivial()`） | 投影后**恒真** —— 原约束对剩余变量没有任何限制 |
| 有原子的系统 | 剩余变量上的可达条件 |
| `nullopt` | 原约束组**无解** |
| 错误 `InvalidExpression` | **不适用**：含非线性原子，或含「不等于」（非凸，FM 处理不了）|

实现要点：严格性要逐行跟踪（`x > y ∧ x < z` 的投影是**严格**的 `y < z`，写成 `y ≤ z` 就错了）；
`=` 拆成两条方向相反的不等式；恒真的常数行直接丢弃，矛盾的常数行立刻判无解。

代价随约束条数增长（每轮组合可能平方级），对本库的规模足够。

## 明确不做

- **柱形代数分解**：一般半代数集的显式表示，代价双指数，本库不追求
- **Gröbner 消元**：等式组的消元 / 参数化 / 投影已实现，见 [groebner.md](groebner.md)
- **非线性不等式的投影**：一般半代数集要靠柱形代数分解，本库不做；
  线性情形已由上面的 Fourier–Motzkin 覆盖
- 反三角函数、超越条件：本库排除超越函数
