# maths.algebra:expression — 变量、单项式、多项式

对应 `src/algebra/expression.cppm`。

| 类型 | 含义 |
| --- | --- |
| `Name` | 变量名的基名，纯字母或纯数字 |
| `Variable` | 变量名 = 基名 + 可选下标（`x`、`a_1`、`x_{i,j}`） |
| `VarPowers` | 单项式的变量因子表 `vector<pair<Variable, unsigned long long>>` |
| `Monomial` | 单项式 = 系数 × 变量因子 |
| `Polynomial` | 多项式 = `map<VarPowers, Fraction>`（变量因子 → 系数） |

## Name

`Name::check` 严格要求**全字母**或**全数字**，其它字符（`^`、`{}`）一律非法，
非法时 `operator=` 抛 `MathsException(InvalidName)`。

## Variable

LaTeX 风格的下标，基名必须是字母：

| 写法 | 含义 | 合法 |
| --- | --- | --- |
| `x` | 变量 x | ✓ |
| `a_1`、`a_x` | 下标为单字符 | ✓ |
| `x_{i,j}` | 多下标 | ✓ |
| `x^2` | —— | ✗（`^` 不在字符集里） |
| `x^` | —— | ✗ |
| `12` | 纯数字 | ✗（除非 `allowNumeric` 为 true，那用于下标） |

下标**不等于指数**：`x_2` 是「x 的第 2 个」，不是 x 的平方。指数只能是字面常数。

`str()` 还原成 LaTeX 形式（单下标不加花括号，多下标加）。排序按基名再逐个比下标。

**索引位置的类型是 `unsigned long long`**，`Variable` 没有到整型的转换，
所以想写 `{{x, x}}`（x 的 x 次方）编译不过 —— 这在类型层面杜绝了 `x^x`。

## Monomial

系数 + 变量因子。规范化后：不含零次幂、按变量升序、同底数已合并，
因此**同一个单项式只有一种表示**，可以直接比较。

| 分类 | 成员 |
| --- | --- |
| 构造 | `Monomial()`（零单项式）、`Monomial(Fraction)`、`Monomial(Fraction, VarPowers)` |
| 查询 | `getCoefficient()`、`getFactors()`、`isZero()`、`isConstant()`、`degree()` |
| 乘法 | `operator* -> Result<Monomial>`（同底数幂合并可能溢出）、`operator*=`（失败抛异常） |
| 加减 | `operator+` / `operator-` → **`Polynomial`**（结果不保证还是单项式） |
| 代入 | `substitute(scope) -> RationalFunction`、`evaluate(scope) -> Result<Fraction>` |
| 输出 | `str()`、`latex()` |
| 规范化 | 静态 `normalizeFactors(VarPowers &)` |

零单项式统一表示为 `0`，不携带变量。

### 指数溢出是显式错误

`normalizeFactors` 合并同底数幂时先查溢出：

```cpp
if (merged.back().second > std::numeric_limits<ull>::max() - factor.second) {
  throw MathsException(MathsError::ExponentOverflow);
}
```

不查的话 `2^64 · 2^1` 会回绕成指数 0，而零次幂又被规范化删掉 —— 结果**静默变成一个常数**，
毫无报错，这是最坏的一类 bug。

## Polynomial

`std::map<VarPowers, Fraction>`：键是变量因子，值是系数。零系数项会自动删除。

| 分类 | 成员 |
| --- | --- |
| 构造 | `Polynomial()`（零多项式）、`Polynomial(const Monomial &)`（隐式提升） |
| 查询 | `getTerms()`、`containsVariable()`、`isZero()`、`isMonomial()`、`degree()` |
| 转换 | `toMonomial() -> Result<Monomial>`（多于一项则 `NotAMonomial`） |
| 加减乘 | `+ -` 返回 `Polynomial`；`*` 返回 `Result<Polynomial>`（指数溢出） |
| 复合赋值 | `+= -= *=` —— 失败抛 `MathsException` |
| 代入 | `substitute(scope) -> RationalFunction`、`evaluate(scope) -> Result<Fraction>` |
| 构造辅助 | `addTerm(VarPowers, Fraction)` —— **public**，合并同类项 + 删零项 |
| 输出 | `str()`、`latex()` |

`Monomial` 能隐式提升为 `Polynomial`，所以 `Monomial + Polynomial` 这类混合运算直接可用；
左操作数为 `Monomial` 的情形另有自由函数保证对称。

`addTerm` 之所以是 public，是为了让 `RationalFunction` 这类需要逐项构建多项式的代码
复用同一套规范化逻辑，而不是再实现一遍。

## 坑：存储序 ≠ 数学项序

**这个坑咬过两次**（长除法不终止、符号归一取反），务必记住：

- `Polynomial` 的 `terms` 是 `std::map`，按 `VarPowers` 的默认比较排序 —— 那是**存储顺序**
- 需要「首项」时必须用 `detail::compareLex`（字典序），不能用 `terms.begin()`

`VarPowers` 的默认比较甚至**不是合法的单项式序**：它不满足乘法相容性。
默认比较下 `x < y`，但两边同乘 `x` 后得到 `x^2 > xy`，与 `x < y` 矛盾。
项序不相容会让多项式除法**不终止**。

两个序各有用途：

| 序 | 函数 | 用途 |
| --- | --- | --- |
| 字典序（lex） | `detail::compareLex` | 数学运算：取首项、长除法 —— **必须用它** |
| 展示序 | `detail::displayOrderLess` | 仅用于 `str()` / `latex()` 的打印顺序 |

展示序是「次数降序 → 变量名升序 → 同变量指数降序」，目的是让
`x^2 + 2xy + y^2` 输出成手写习惯的样子，而不是存储序的 `y^2 + x^2 + 2xy`。

**数学代码一律不要用展示序。**

## 多项式带余除法

```cpp
std::optional<Monomial> leadingMonomial(const Polynomial &);   // 按 lex 取首项，零多项式返回 nullopt

struct PolynomialDivision { Polynomial quotient; Polynomial remainder; };

Result<PolynomialDivision> divideWithRemainder(const Polynomial &dividend,
                                               const Polynomial &divisor);
```

- 除式为零多项式 → `DivisionByZero`
- 多元时首项可能无法整除（变量指数不足），此时**提前终止**，余式非零即代表不能整除
- 循环结束时余式为零 ⟺ 整除

长除法是 `RationalFunction::toPolynomial()` 的基础，见
[rational_function.md](rational_function.md#化为多项式长除法)。

## 示例

```cpp
const Variable x("x");
const Variable y("y");

const Monomial m(Fraction(3, 1), {{x, 2ULL}, {y, 1ULL}});
m.str();      // "3 x^2 y"
m.latex();    // "3x^{2}y"

Polynomial p = m + Monomial(Fraction(1, 1), {{x, 1ULL}});
p.str();      // "3 x^2 y + x"

(p * p).unwrap().degree();   // 6
p.toMonomial();              // Err(NotAMonomial)
```
