# Maths

C++23 header-only 数学库，提供精确的数值计算和代数表达式处理能力。

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

## 构建与安装

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

把编译告警视为错误：

```bash
cmake -S . -B build -G Ninja -DMATHS_WARNINGS_AS_ERRORS=ON
```

作为依赖使用：

```bash
cmake --install build --prefix <prefix>
```

```cmake
find_package(Maths REQUIRED)
target_link_libraries(your_target PRIVATE Maths::Maths)
```

`Maths::Maths` 是 INTERFACE 目标，会自动带上头文件目录与 C++23 要求。

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

详见 [docs/include/maths_error.md](docs/include/maths_error.md) 与
[docs/include/result.md](docs/include/result.md)。

## 模块

| 模块 | 头文件 | 说明 | 文档 |
| --- | --- | --- | --- |
| 精确数值 | `numbers.hpp` | `Integer`、`Fraction` | [文档](docs/include/numbers.md) |
| 代数表达式 | `algebraic_expression.hpp` | `Variable`、`Monomial`、`Polynomial`、带余除法 | [文档](docs/include/algebraic_expression.md) |
| 分式 | `rational_function.hpp` | 有理函数、三层化简、长除法归约 | [文档](docs/include/rational_function.md) |
| 变量绑定 | `scope.hpp` | `Scope` 与 `substitute` / `evaluate` | [文档](docs/include/scope.md) |
| 表达式解析 | `expression_parser.hpp` | 文本与 LaTeX → 式子、代入条件解析 | [文档](docs/include/expression_parser.md) |
| 错误码 | `maths_error.hpp` | `MathsError`、`MathsException` | [文档](docs/include/maths_error.md) |
| 结果类型 | `result.hpp` | `Result<T>` | [文档](docs/include/result.md) |
| 随机数 | `random.hpp` | 区间随机数 | [文档](docs/include/random.md) |
| LaTeX 输出 | — | `str()` 与 `latex()` 对照、排版规则 | [文档](docs/include/latex.md) |

依赖方向单向：`numbers → algebraic_expression → rational_function → scope → expression_parser`。

## 示例程序

| 程序 | 说明 | 文档 |
| --- | --- | --- |
| `maths_simplify` | 交互式表达式化简：输入式子与代入条件，输出化简结果 | [文档](docs/apps/simplify.md) |

```bash
cmake --build build
./build/apps/maths_simplify        # Windows: build\apps\maths_simplify.exe
```

## 文档

完整的模块文档、程序用法、测试与打包说明见 **[docs/README.md](docs/README.md)**。

## 目录结构

```
include/                     头文件（header-only）
  maths_error.hpp              统一错误码 MathsError 与 MathsException
  result.hpp                   统一结果类型 Result<T>
  numbers.hpp                  Integer、Fraction
  algebraic_expression.hpp     Name、Variable、Monomial、Polynomial
  scope.hpp                    变量绑定表 Scope、代入与求值
  rational_function.hpp        分式（有理函数）与有限化简
  expression_parser.hpp        表达式与代入条件的解析
  random.hpp                   区间随机数
apps/                        示例程序
test/                        测试
  check.hpp                    断言宏
docs/                        文档（模块、程序、测试、打包）
scripts/                     构建脚本
  build-releases.sh            一键打包可执行文件到 releases/
cmake/                       CMake 包配置模板
releases/                    打包产物
```

## 许可

MIT
