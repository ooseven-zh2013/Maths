# maths.random — 区间随机数

对应 `src/numeric/random.cppm`。

## 接口

| 重载 | 区间 | 返回 |
| --- | --- | --- |
| `random(float, float)` | `[l, r)` | `Result<float>` |
| `random(double, double)` | `[l, r)` | `Result<double>` |
| `random(long double, long double)` | `[l, r)` | `Result<long double>` |
| `random(const T &, const T &)` 模板 | `[l, r]` | `Result<T>` |
| `random(const Integer &, const Integer &)` | `[l, r]` | `Result<Integer>` |

浮点是**左闭右开**（`std::uniform_real_distribution` 的语义），整数是**闭区间**。
区间为空时返回 `InvalidRange`。

```cpp
Result<double> value = random(0.0, 1.0);
Result<Integer> dice = random(Integer(1), Integer(6));
```

生成器是共享的：

```cpp
std::mt19937 &get_generator();   // 静态局部，线程不安全
```

多线程场景请各自持有生成器，不要共用这个。

## Integer 版本为什么单写

不能复用模板版本：`std::uniform_int_distribution` 要求模板参数是**内建整型**
（`static_assert` 会直接拦下），而 `Integer` 是用户定义类型，`std::is_integral_v<Integer>` 为 false，
且该 trait **不允许用户特化**。硬塞进去属于 UB —— 实现可以假设它是内建整型并做位级优化。

正解是用 `uniform_int_distribution<long long>` 取分布，再包回 `Integer`。

代价是范围收窄：`Integer` 内部是 `ull + sign`（理论可到 2^64），
而 `long long` 只覆盖 `[-2^63, 2^63-1]`。超出范围返回 `InvalidRange`，**不静默截断**。

转换用 `detail::tryToLongLong()`，**不能用 `getVal()`** ——
`|value| == 2^63` 时 `getVal()` 会触发有符号溢出。

## 没有 Fraction 版本

有理数在实数区间上**稠密**，不存在均匀分布。

真要提供只能另起 `randomFraction`，按分子 / 分母独立采样 ——
但那不是「区间上的均匀随机」，语义完全不同，暂不实现。

## 测试覆盖

闭区间两端可达、单点区间、`long long` 边界（min / max 各取一次）、超出范围返回错误码。
