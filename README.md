# Maths

C++23 模块库（`export module` / `import`，不再是 header-only），提供精确的数值计算和代数表达式处理能力。
构建用 [mcpp](https://github.com/mcpp-community/mcpp)——模块优先的 C++ 构建工具。

**全程精确，无浮点。** 所有数值走 `Fraction`（内部是「无符号幅值 + 符号」），
所有代数对象最终都能化到精确形式。

## 主要功能

- **有理数（Fraction）** — 精确的分数运算：四则、比较、幂（含负指数）、流式 I/O
- **整数（Integer）** — 带符号整数运算：四则、取模、自增自减、返回分数的幂
- **代数表达式** — LaTeX 风格变量名（`a_1`、`x_{i,j}`）；`Monomial` 与 `Polynomial` 支持加减乘与自动化简
- **分式（RationalFunction）** — 两个多项式之比，支持四则运算与有限化简
- **变量绑定与代入（Scope）** — 变量可绑定到常数或含其它变量的表达式，代入迭代到不动点
- **表达式解析** — 文本（含 LaTeX 写法）解析成式子
- **随机数** — 区间随机数，支持浮点与整数
- **LaTeX 输出** — 每个代数类型都有 `str()`（终端阅读）与 `latex()`（排版）

## 构建与运行

```bash
mcpp build           # 构建库与示例程序
mcpp test            # 跑 tests/ 下的全部测试（自动发现，一文件一可执行文件）
mcpp run simplify    # 构建并运行示例程序
```

首次构建会自动准备工具链（当前固定 `llvm@20.1.7`，见 `mcpp.toml`）。
编译告警写在 `[build].cxxflags`：`-Wall -Wextra -Wpedantic -Wshadow`。

作为依赖使用——在自己的 `mcpp.toml` 里加一行：

```toml
[dependencies]
maths = "0.1.0"
```

然后按需 `import`：

```cpp
import maths;            // 伞模块：一次引入全部模块
import maths.numbers;    // 或只引入需要的那一个
```

全部类型都在 `namespace maths` 里。注意：**`#include` 必须写在所有 `import` 之前**，
顺序反了 clang 会在标准库头里报错。

作为依赖分发走 `mcpp pack`（打包）与 `mcpp publish`（发布到包索引），
不再提供 CMake 的 `find_package`。

## 错误处理

可能失败的操作返回 `Result<T>`；**构造函数与复合赋值没有返回值位置**，抛 `MathsException`。
两者共用同一套错误码，处理逻辑只需写一套。

```cpp
Result<Fraction> quotient = a / b;                     // 不抛异常，返回错误状态
if (quotient.isErr()) {
  std::cout << describe(quotient.unwrapErr()) << '\n';
}
const Fraction value = (a / Fraction(1, 2)).unwrap();  // 失败则抛 MathsException
```

详见 [docs/src/core/maths_error.md](docs/src/core/maths_error.md) 与
[docs/src/core/result.md](docs/src/core/result.md)。

## 模块

| 模块 | 头文件 | 说明 | 文档 |
| --- | --- | --- | --- |
| 精确数值 | `maths.numbers` | `Integer`、`Fraction` | [文档](docs/src/numeric/numbers.md) |
| 代数表达式 | `maths.algebra:expression` | `Variable`、`Monomial`、`Polynomial`、带余除法 | [文档](docs/src/algebra/algebraic_expression.md) |
| 分式 | `maths.algebra:rational` | 有理函数、三层化简、长除法归约 | [文档](docs/src/algebra/rational_function.md) |
| 变量绑定 | `maths.algebra:scope` | `Scope` 与 `substitute` / `evaluate` | [文档](docs/src/algebra/scope.md) |
| 表达式解析 | `maths.parser` | 文本与 LaTeX → 式子、代入条件解析 | [文档](docs/src/parser/expression_parser.md) |
| 错误码 | `maths.error` | `MathsError`、`MathsException` | [文档](docs/src/core/maths_error.md) |
| 结果类型 | `maths.result` | `Result<T>` | [文档](docs/src/core/result.md) |
| 随机数 | `maths.random` | 区间随机数 | [文档](docs/src/numeric/random.md) |
| LaTeX 输出 | — | `str()` 与 `latex()` 对照、排版规则 | [文档](docs/src/latex.md) |

依赖方向单向：`maths.error → maths.result → maths.numbers → maths.algebra → maths.parser`
（`maths.random` 依赖 `maths.numbers`）。

## 示例程序

| 程序 | 说明 | 文档 |
| --- | --- | --- |
| `simplify` | 交互式表达式化简：输入式子与代入条件，输出化简结果 | [文档](docs/apps/simplify.md) |

```bash
mcpp run simplify
```

## 文档

完整的模块文档、程序用法、测试与打包说明见 **[docs/README.md](docs/README.md)**。

## 目录结构

```
src/                            模块接口单元（.cppm）
  maths.cppm                      伞模块 maths：export import 全部子模块
  core/                           与数学无关的基础设施
    maths_error.cppm                maths.error —— 统一错误码与 MathsException
    result.cppm                     maths.result —— 统一结果类型 Result<T>
  numeric/                        精确数值
    numbers.cppm                    maths.numbers —— Integer、Fraction
    random.cppm                     maths.random —— 区间随机数
  algebra/                        符号代数（三个分区合成 maths.algebra）
    algebra.cppm                    maths.algebra —— 主接口，export import 三个分区
    expression.cppm                 :expression —— Name、Variable、Monomial、Polynomial
    rational.cppm                   :rational —— 分式与有限化简
    scope.cppm                      :scope —— Scope、代入与求值
  parser/parser.cppm               maths.parser —— 表达式与代入条件的解析
tests/                          测试（mcpp test 自动发现）
  check.hpp                       断言宏
apps/                           示例程序
mcpp.toml                       项目清单：包名、目标、编译标志、工具链
docs/                           文档（模块、程序、测试、打包）
```

## 许可

MIT
