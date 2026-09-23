# maths.numbers — Integer 与 Fraction

对应 `src/numeric/numbers.cppm`。

两者都是**精确**的：内部表示是「无符号幅值 + 符号位」，全程整数运算，不经过浮点。
这是整个库的立身之本 —— 任何"为了输出好看而转浮点"的改动都是错的。

| 类型 | 内部表示 | 可表示范围 |
| --- | --- | --- |
| `Integer` | `unsigned long long val` + `bool sign` | \|value\| ≤ 2^63 |
| `Fraction` | `unsigned long long a, b` + `bool sign`（a/b，恒约分） | 同 `Integer` 的分量范围 |

## Integer

### 接口

| 分类 | 成员 |
| --- | --- |
| 构造 | `Integer()`（0）、`Integer(long long)`、`Integer(unsigned long long)` |
| 转换 | `operator unsigned long long`（→ `getAbs()`）、`operator long long`（→ `getVal()`） |
| 取值 | `getSign()`、`isNegative()`、`getAbs()`、`getVal()` |
| 比较 | `<=>`（对 `Integer` / `ll` / `ull`）、`==` |
| 算术 | `+ - *`（返回 `Integer`）、`/` `%`（返回 `Result<Integer>`，除零 → `DivisionByZero`） |
| 复合赋值 | `+= -= *= /= %= ^=` —— 失败抛 `MathsException` |
| 自增自减 | `++i`、`i++`、`--i`、`i--` |
| 幂 | `pow(Integer) -> Result<Fraction>`；`operator^` 同 |
| I/O | `<<` 输出带符号十进制；`>>` 读一个 `long long` |

### 幂运算返回分数

负指数没有整数结果，所以 `pow` 的返回类型是 `Result<Fraction>` 而不是 `Result<Integer>`：

```cpp
Integer(2).pow(Integer(-3)).unwrap();   // 1/8
Integer(2).pow(Integer(3)).unwrap();    // 8
Integer(0).pow(Integer(-1));            // Err(ZeroToNegativePower)
```

`^=` 要求结果必须是整数，否则抛 `MathsException(NonIntegralPowerResult)`。

### 坑：`getVal()` 在边界上溢出

`Integer` 的幅值是 `unsigned long long`，能表示 |value| = 2^63，但 `long long` 只到 2^63−1。
因此 `getVal()` 在幅值为 2^63 的负数上会触发有符号溢出。

需要 `long long` 时用 `detail::tryToLongLong()`（`maths.random` 里就是这么做的），
它超出范围返回 `false` 而不是静默截断。同理，`Integer` → `Fraction` 要走
`Fraction::fromInteger()`，它直接搬内部表示，不经 `getVal()`。

## Fraction

### 接口

| 分类 | 成员 |
| --- | --- |
| 构造 | `Fraction()`（0/1）、`Fraction(string_view)`（**解析失败抛异常**）、`Fraction(T a, U b = 1)` |
| 静态工厂 | `parse(string_view) -> Result<Fraction>`、`fromInteger(const Integer &)` |
| 转换 | `operator double`（**会丢精度，仅用于对接外部 API**） |
| 取值 | `getNumerator()`、`getDenominator()`、`isNegative()` |
| 比较 | `<=>`（对 `Fraction` / `ll`）、`==` |
| 算术 | `+ - *`（返回 `Fraction`）、`/`（返回 `Result<Fraction>`） |
| 复合赋值 | `+= -= *= /= ^=` —— 失败抛 `MathsException` |
| 幂 | `pow(Integer) -> Result<Fraction>`；`operator^(Fraction, Integer)` |
| I/O | `<<` 输出 `-a` / `a` / `a/b`；`>>` 读 `a/b` 或整数 |

构造后恒约分（内部 `simplify()` 算 gcd），分母为 0 抛 `MathsException(ZeroDenominator)`。

### parse 接受三种写法

```cpp
Fraction::parse("3/4");            // 3/4
Fraction::parse("-12");            // -12
Fraction::parse("\\frac{1}{2}");   // 1/2（支持嵌套、连续除法）
```

失败返回 `InvalidExpression` 或 `DivisionByZero`。
构造函数版本内部调 `parse` 再抛异常 —— **解析外部输入请改用 `parse`**，否则只能 catch。

### 比较用交叉相乘

`a/b <=> c/d` 等价于 `a*d <=> c*b`。中间结果提升到 `wide`
（GCC/Clang 下是 `unsigned __int128`，其他平台退回 `unsigned long long`），避免大数比较时溢出。

### 内部实现的两个细节

- **约分与 gcd 走无符号**：`std::gcd` 内部会对操作数调 `std::abs`，遇到 `LLONG_MIN` 直接断言失败。
  所以取绝对值一律用 `detail::magnitudeOf` 这类无符号回绕实现。
- **`operator double` 是逃生舱**：只在对接需要浮点的外部接口时用，库内部运算不走它。

## 示例

```cpp
const Fraction a(1, 2);
const Fraction b(1, 3);

a + b;                       // 5/6
a * b;                       // 1/6
(a / b).unwrap();            // 3/2
a.pow(Integer(-2)).unwrap(); // 4

std::cout << a << ' ' << b << '\n';   // 1/2 1/3
```
