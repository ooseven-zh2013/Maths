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
  numbers.hpp                  Integer、Fraction
  algebraic_expression.hpp     Name、Variable、Monomial、Polynomial
  random.hpp                   区间随机数
test/                        测试
  check.hpp                    断言宏
cmake/                       CMake 包配置模板
```

## 许可

MIT
