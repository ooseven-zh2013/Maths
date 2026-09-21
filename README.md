# Maths

C++23 header-only 数学库，提供精确的数值计算和代数表达式处理能力。

## 主要功能

- **有理数（Fraction）** — 精确的分数运算，支持四则运算、比较、幂运算和流式 I/O。避免浮点精度损失。
- **整数（Integer）** — 带符号整数运算，支持四则运算、取模、自增自减、以及返回分数的幂运算（含负指数）。
- **代数表达式** — 支持 LaTeX 风格变量名（如 `a_1`、`x_{i,j}`）的解析与表示；单项式（`Monomial`）与多项式（`Polynomial`）支持加减乘、自动化简，并可从多项式判定/提取单项式。
- **随机数生成** — 区间随机数工具，同时支持浮点类型和整数类型。

## 构建与测试

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

把编译告警视为错误（CI 在 Linux / macOS 上使用该模式）：

```bash
cmake -S . -B build -G Ninja -DMATHS_WARNINGS_AS_ERRORS=ON
```

## 作为依赖使用

```bash
cmake --install build --prefix <prefix>
```

下游项目：

```cmake
find_package(Maths REQUIRED)
target_link_libraries(your_target PRIVATE Maths::Maths)
```

`Maths::Maths` 是 INTERFACE 目标，会自动带上头文件目录与 C++23 要求。

## 测试

测试位于 `test/`，按模块分子目录，不依赖第三方框架，断言宏定义在 `test/check.hpp`：

```cpp
CHECK_EQ(actual, expected);          // 相等断言，失败时打印实际值与期望值
CHECK_TRUE(condition);               // 条件断言
CHECK_THROWS(expr, exception_type);  // 异常断言
TEST_SUMMARY();                      // 输出汇总并以失败数作为退出码
```

自制断言而非 `assert`，是为了让检查在 Release 构建（`NDEBUG` 已定义）下依然生效。

测试目标名由「子目录名_文件名」组成，例如 `test/integer/arithmetic.cpp` → `integer_arithmetic`。

## 错误处理

全库使用统一的错误分类 `MathsError`（见 `include/maths_error.hpp`）。错误有两条传递路径，
**错误码完全相同**，因此处理逻辑只需写一套：

| 路径 | 适用场景 | 用法 |
| --- | --- | --- |
| `Result<T>` 返回值 | 普通函数与算术运算 | `auto r = a / b; if (r.isErr()) ...` |
| `MathsException` 异常 | 构造函数、复合赋值 | `catch (const MathsException &e) { e.code(); }` |

### Result 的运算语义

`include/result.hpp` 提供全库统一的结果类型：成功携带 `T`，失败携带 `MathsError`。

- **短路传播**：`+ - * /` 与一元 `-` 可直接作用于 `Result`，先左后右，
  某一侧失败即停止求值（运算体不执行），两侧都失败时保留**左侧**错误
- **强制解包**：不提供 `operator T()`，取裸值必须显式调用
  `unwrap()` / `unwrapOr()` / `unwrapOrElse()` / `expect()`，故"忘记检查"无法通过编译
- 不提供 `< > <= >=`：比较需要值语义，失败时无意义，请先 `unwrap()`

```cpp
const Fraction a(1, 2);
const Fraction zero(0, 1);

Result<Fraction> quotient = a / zero;                    // 不抛异常，返回错误状态
if (quotient.isErr()) {
  std::cout << describe(quotient.unwrapErr()) << '\n';   // 除数不能为零
}

const Fraction value = (a / Fraction(1, 2)).unwrap();    // 失败则抛 MathsException
```

### 返回 Result 的入口

| 入口 | 失败原因 |
| --- | --- |
| `Fraction::operator/`、`Integer::operator/` / `%` | 除零 |
| `Fraction::pow`、`Integer::pow`、`operator^` | 0 的负数次幂 |
| `Monomial::operator*`、`Polynomial::operator*` | 同底数幂合并溢出 |
| `Polynomial::toMonomial` | 无法化简为单项式 |
| `Fraction::parse` | 表达式非法、分母为零 |
| `random` | 区间参数非法 |

### 仍然抛异常的入口

构造函数与复合赋值运算符**没有可承载 `Result` 的返回值位置**，故继续抛 `MathsException`
（错误码与 `Result` 路径一致）：

```cpp
Fraction(1LL, 0LL);        // MathsException(ZeroDenominator)
Fraction("\\frac{1}{");    // MathsException(InvalidExpression)
Variable("x^");            // MathsException(InvalidName)
a /= zero;                 // 复合赋值内部解包，失败抛 MathsException
```

解析外部输入想显式处理失败时，改用静态工厂：

```cpp
Result<Fraction> parsed = Fraction::parse(input);
if (parsed.isErr()) { /* parsed.unwrapErr() */ }
```

## 变量绑定与代入

`include/scope.hpp` 提供变量到值的绑定表 `Scope`，以及单项式 / 多项式的代入与求值。

```cpp
Scope scope;
scope.assign(Variable("x"), Integer(2));
scope.assign(Variable("y"), Fraction(1, 3));

const Monomial m(Fraction(3, 1), {{Variable("x"), 1ULL}, {Variable("y"), 1ULL}});

m.substitute(scope);   // "6 y"          —— 部分代入：y 未绑定，原样保留
m.evaluate(scope);     // Result = 2     —— 完全求值
```

| 接口 | 语义 |
| --- | --- |
| `Monomial::substitute(scope)` / `Polynomial::substitute(scope)` | **部分代入**：已绑定的变量替换为其值并把幂次并进系数，未绑定的原样保留。返回同类型，不会失败 |
| `Monomial::evaluate(scope)` / `Polynomial::evaluate(scope)` | **完全求值**：要求全部变量都已绑定且结果化为常数，否则返回 `MathsError::UndefinedVariable` |

`Scope` 的值统一以 `Fraction` 存储 —— `Integer` 是分母为 1 的分数，因此整数与分数共用一个空间，
既不会丢失信息，也不会出现同一变量在两处取值不一致的状态。`assign` 是覆盖语义且不会失败，
只有 `lookup` 会失败。

`Polynomial` 的代入按「逐项调用 `Monomial::substitute`，再统一合并同类项」实现。

`Scope::str()` 输出 JSON，键是变量名，值是分数文本：

```cpp
scope.assign(Variable("x"), Fraction(1, 2));
scope.assign(Variable("y"), Integer(-3));
scope.str();   // {"x": "1/2", "y": "-3"}
```

值写成字符串而不是 JSON 数字，是为了不把精确分数浮点化（`0.5` 会丢掉 1/3 这类值），
并且可直接交给 `Fraction::parse` 反向解析，保证输出可往返。

## 代码规范与静态检查

| 工具 | 配置文件 | 用途 |
| --- | --- | --- |
| clang-format | `.clang-format` | 统一格式（LLVM 风格，2 空格缩进，列宽 120） |
| clang-tidy | `.clang-tidy` | 静态检查（bugprone / performance / readability 子集） |
| clangd | `.clangd` | 编辑器内诊断、补全与内联提示 |

```bash
# 格式检查
clang-format --dry-run --Werror include/*.hpp test/check.hpp test/*/*.cpp

# 静态检查（需先生成 compile_commands.json）
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build test/integer/arithmetic.cpp
```

CI（GitHub Actions）在 Linux / macOS / Windows 三个平台构建并运行测试，另外单独执行格式检查与静态检查。

## 目录结构

```
include/                     头文件（header-only）
  maths_error.hpp              统一错误码 MathsError 与 MathsException
  result.hpp                   统一结果类型 Result<T>
  numbers.hpp                  Integer、Fraction
  algebraic_expression.hpp     Name、Variable、Monomial、Polynomial
  scope.hpp                    变量绑定表 Scope、代入与求值
  random.hpp                   区间随机数
test/                        测试
  check.hpp                    断言宏
cmake/                       CMake 包配置模板
```

## 许可

MIT
