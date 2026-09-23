# maths/algebra/scope.hpp — 变量绑定与代入

对应 `include/maths/algebra/scope.hpp`。

`Scope` 是变量到值的绑定表。除了绑定表本身，**所有** `substitute` / `evaluate` 的实现也都在这个文件里
（因为 `maths/algebra/rational_function.hpp` 对 `Scope` 只有前向声明，依赖必须单向）。

## 值统一存 RationalFunction

`Scope` 的值一律是 `RationalFunction`，不按类型分表。

常数是分式的特例（分母为 1），所以整数、分数、多项式、分式共用一个空间，
既不丢信息，也不会出现「同一变量在两套表里值不一致」这种怪状态。

因此**值可以是含其它变量的表达式**：

```cpp
Scope scope;
scope.assign(Variable("s"), parseExpression("v*t").unwrap());   // s = v*t
scope.assign(Variable("v"), Integer(3));
scope.assign(Variable("t"), Integer(4));

parseExpression("2s").unwrap().substitute(scope).unwrap().latex();   // "24"
```

## 接口

| 分类 | 成员 |
| --- | --- |
| 赋值 | `assign(Variable, RationalFunction)` / `(Variable, Fraction)` / `(Variable, Integer)` → `Result<void>` |
| 查询 | `lookup(Variable) -> Result<RationalFunction>`、`contains`、`empty`、`size`、`bindings()` |
| 修改 | `erase(Variable) -> bool`、`clear()` |
| 输出 | `str()` —— JSON |

`assign` 是**覆盖语义**。`lookup` 在变量未绑定时返回 `UndefinedVariable`。

## 赋值的两条禁令

| 输入 | 结果 | 错误码 |
| --- | --- | --- |
| `x = 2x`、`x = x + 1` | 拒绝 —— 那是方程不是赋值 | `NotAnAssignment` |
| `x = s`、`s = t`、`t = x` | 拒绝 —— 间接环也拦 | `CircularReference` |

自引用要解方程才能解出 `x`，本库明确不做（见
[rational_function.md](rational_function.md) 的能力边界）。
这就是 `assign` 返回 `Result<void>` 而不是 `void` 的原因。

环检测（`createsCycle`）从 value 里出现的变量出发，沿现有绑定链递归追，看是否回到被赋值变量。
因为每次 `assign` 都过了检测，现有绑定保证无环，递归必然终止（`visited` 只是防御性兜底）。

## 代入 vs 求值

| 接口 | 语义 | 返回 |
| --- | --- | --- |
| `substitute(scope)` | **部分代入**：已绑定的换成值，未绑定的原样保留 | `RationalFunction` |
| `evaluate(scope)` | **完全求值**：要求代入后化为常数 | `Result<Fraction>`，否则 `UndefinedVariable` |

三个类型都有这两个方法：`Monomial`、`Polynomial`、`RationalFunction`。

**注意 `substitute` 的返回类型是 `RationalFunction` 而不是同类** ——
绑定值本身可能是分式（`x = a/b`），代入后结果就不再是多项式了。这个类型连锁当初改了很久。

`Polynomial::substitute` 的实现是「逐项调用 `Monomial::substitute`，再统一合并同类项」。

### 迭代到不动点

`RationalFunction::substitute` 会反复替换直到不再变化，以处理链式绑定
（`s = v*t`、`v = a*b`）：

```cpp
RationalFunction current = *this;
const std::size_t limit = scope.size() + 1;
for (std::size_t iteration = 0; iteration < limit; ++iteration) {
  const RationalFunction replacedNumerator = current.getNumerator().substitute(scope);
  const RationalFunction replacedDenominator = current.getDenominator().substitute(scope);
  ...
  if (quotient.unwrap() == current) break;   // 不动点
  current = quotient.unwrap();
}
```

**必须用 `current` 的分子分母迭代，不能用 `*this` 的** —— 否则每一轮都在替换原始式子，
链式绑定只能展开一层。这个 bug 曾经让 `x = 1/y, y = 3/4` 只展开到 `1/y`，
`evaluate` 于是报 `UndefinedVariable`。

迭代上限取 `size() + 1`：足以展开任意无环依赖链，同时避免环导致的不终止。

### 代入后分母为零是极点

`1/(x-1)` 代入 `x = 1` → `ZeroDenominator`。

**那不是程序错误，而是数学结论** —— 分式在那里本来就没有定义。

## str() 输出 JSON

```cpp
scope.assign(Variable("x"), Fraction(1, 2));
scope.assign(Variable("y"), Integer(-3));
scope.str();   // {"x": "1/2", "y": "-3"}
```

值写成**字符串**而不是 JSON 数字，是为了不把精确分数浮点化（`0.5` 会丢掉 1/3 这类值），
并且可直接交给 `Fraction::parse` 反向解析 —— **输出可往返**。

键按变量名排序（`std::map<Variable, ...>` 的自然结果）。
空表输出 `{}`。

## 示例

```cpp
const Monomial m(Fraction(3, 1), {{Variable("x"), 1ULL}, {Variable("y"), 1ULL}});   // 3xy

Scope scope;
scope.assign(Variable("x"), Integer(2));           // 只绑定 x

m.substitute(scope).str();   // "6 y"  —— y 未绑定，原样保留
m.evaluate(scope);           // Err(UndefinedVariable)

scope.assign(Variable("y"), Fraction(1, 3));       // 再绑定 y = 1/3
m.evaluate(scope).unwrap();  // 2  —— 3·2·(1/3)，完全求值
```

`Monomial::evaluate` 与 `Polynomial::evaluate` 都统一委托给 `RationalFunction::evaluate`：
代入后要求分子分母都化为常数。
