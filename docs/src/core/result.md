# maths.result — Result&lt;T&gt;

对应 `src/core/result.cppm`。

全库统一的结果类型：成功携带 `T`，**失败类型固定为 `MathsError`**（不再有第二个模板参数），
因此所有类的运算结果在类型名称与语义上保持一致。

两条硬规则：

1. **运算短路传播** —— 先左后右，某一侧失败即停止求值，两侧都失败时保留**左侧**错误
2. **不提供 `operator T()`** —— 取裸值必须显式 `unwrap()`，"忘记检查"编译期就过不去

## 构造

```cpp
Result<Fraction> a = Fraction(1, 2);              // 隐式转换（构造函数非 explicit）
auto b = Result<Fraction>::ok(Fraction(1, 2));
auto c = Result<Fraction>::err(MathsError::DivisionByZero);

// 函数内部最常用：直接返回 unexpected
return std::unexpected(MathsError::InvalidExpression);
```

类上有 `[[nodiscard]]`，静默丢弃结果会告警。

## 状态与取法

| 方法 | 说明 |
| --- | --- |
| `isOk()` / `isErr()` | 状态查询 |
| `explicit operator bool` | `isOk()` 的简写 |
| `unwrap()` | 取裸值；失败抛 `MathsException`。有左值 / 常量 / 右值三个重载 |
| `unwrapOr(fallback)` | 失败时给默认值 |
| `unwrapOrElse(f)` | 失败时按错误计算默认值 |
| `expect(message)` | 同 `unwrap()`，语义上是"这里失败就是 bug" |
| `unwrapErr()` | 取错误码；**在 Ok 上调用抛 `std::runtime_error`** |
| `error()` | 取错误码，不检查状态（调用方需先确认 `isErr()`） |

刻意**不提供** `< > <= >=`：比较需要"值"的语义，失败时无意义。请先 `unwrap()`。

## 运算：短路传播

`+ - * /` 与一元 `-` 可直接作用于 `Result`，支持三种组合：

```cpp
Result<Fraction> x = ...;
Result<Fraction> y = ...;

x + y;                 // Result op Result
x + Fraction(1, 2);    // Result op T
Fraction(1, 2) + x;    // T op Result（自由函数）
```

- 任一操作数为 Err → 立即返回 Err，**运算体不执行**
- 两侧都 Err → 保留**左侧**的
- `operator/` 的运算体本身可能失败（`T::operator/` 返回 `Result<T>`）

## 组合

| 方法 | 说明 |
| --- | --- |
| `map<U>(f)` | 成功时变换值，失败原样传递 |
| `andThen<U>(f)` | 成功时交给返回 `Result<U>` 的函数（单子链） |
| `orElse(f)` | 失败时按错误码恢复 |
| `mapErr(f)` | 失败时变换错误码 |
| `ok()` / `err()` | 转成 `std::optional<T>` / `std::optional<MathsError>` |
| `operator==` | 两个 `Result` 直接比较（值相等且状态相同才相等） |

## 流输出

`operator<<` 输出的是**结果本身**，不涉及解包：

```cpp
std::cout << Result<Fraction>::err(MathsError::DivisionByZero);
// Err(除数不能为零)
```

值类型不支持 `<<` 时输出 `Ok(...)`。

## Result&lt;void&gt;

只表示成功或失败，不携带值（对应 Rust 的 `Result<(), E>`），内部是 `std::optional<MathsError>`。

```cpp
Result<void> assigned = scope.assign(Variable("x"), Integer(2));
if (assigned.isErr()) {
  std::cout << describe(assigned.unwrapErr()) << '\n';
}
assigned.unwrap();   // 失败则抛 MathsException
```

需要它的典型场景：`Scope::assign` 要能报「自引用」和「循环引用」两类错误，但成功时没有值可返回。

## 完整示例

```cpp
const Fraction a(1, 2);
const Fraction zero(0, 1);

Result<Fraction> quotient = a / zero;                    // 不抛异常，返回错误状态
if (quotient.isErr()) {
  std::cout << describe(quotient.unwrapErr()) << '\n';   // 除数不能为零
}

const Fraction value = (a / Fraction(1, 2)).unwrap();    // 失败则抛 MathsException
```
