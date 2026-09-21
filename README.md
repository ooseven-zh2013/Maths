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
| `Monomial::substitute(scope)` / `Polynomial::substitute(scope)` | **部分代入**：已绑定的变量换成其值，未绑定的原样保留。因为绑定值可能是分式，结果为 `RationalFunction` |
| `Monomial::evaluate(scope)` / `Polynomial::evaluate(scope)` | **完全求值**：要求代入后化为常数，否则返回 `MathsError::UndefinedVariable` |

`Scope` 的值统一以 `RationalFunction` 存储 —— 常数是分式的特例（分母为 1），
于是整数、分数、多项式、分式共用一个空间，既不丢信息也不会出现两套表不一致。

因此**值可以是含其它变量的表达式**，例如 `s = v*t`。右边引用的变量不必出现在原式里，
它们会在代入时继续被替换（`substitute` 会迭代到不动点）：

```cpp
Scope scope;
scope.assign(Variable("s"), parseExpression("v*t").unwrap());   // s = v*t
scope.assign(Variable("v"), Integer(3));
scope.assign(Variable("t"), Integer(4));

parseExpression("2s").unwrap().substitute(scope).unwrap().latex();   // "24"
```

**赋值不允许自引用**：`x = 2x`、`x = x + 1` 这类是方程而不是赋值（需要解方程），
因此返回 `MathsError::NotAnAssignment`。这也是 `assign` 返回 `Result<void>` 的原因。

`assign` 是覆盖语义；`lookup` 在变量未绑定时返回 `UndefinedVariable`。

`Polynomial` 的代入按「逐项调用 `Monomial::substitute`，再统一合并同类项」实现。

`Scope::str()` 输出 JSON，键是变量名，值是分数文本：

```cpp
scope.assign(Variable("x"), Fraction(1, 2));
scope.assign(Variable("y"), Integer(-3));
scope.str();   // {"x": "1/2", "y": "-3"}
```

值写成字符串而不是 JSON 数字，是为了不把精确分数浮点化（`0.5` 会丢掉 1/3 这类值），
并且可直接交给 `Fraction::parse` 反向解析，保证输出可往返。

## 分式

`include/rational_function.hpp` 提供有理函数 `RationalFunction`（两个多项式之比），
支持四则运算与**有限化简**。

```cpp
Result<RationalFunction> r = RationalFunction::make(numerator, denominator);

r.unwrap().getNumerator();             // 分子
r.unwrap().getDenominator();           // 分母
r.unwrap().discardedConstraints();     // 化简中被丢掉的「变量非零」约束
```

### 化简做到什么程度

| 层级 | 内容 |
| --- | --- |
| L1 | **数值内容约分** —— `6x^2y / 4xy^2 → 3x^2y / 2xy^2`。按有理数算 gcd，`x/2 + 1/3` 这类系数也能约 |
| L2 | **单项式公因子约分** —— `→ 3x / 2y` |
| L3 | **分母符号归一** —— `1/(-x-1)` 统一写成 `-1/(x+1)` |

**不做多项式因式分解**，所以 `(x^2-1)/(x-1)` 不会化成 `x+1`。
多元多项式 GCD 实现复杂、存在系数爆炸风险，收益不足以抵消成本。

### 约分的代价：定义域

L2 会改变定义域：约掉变量 `x` 等价于默认 `x ≠ 0`，即丢掉了「原式在 `x = 0` 处无定义」这一点。

```cpp
RationalFunction r = ...;      // (x^2 + x) / (x^2 - x)
r.simplify();                  // (x + 1) / (x - 1)
r.discardedConstraints();      // {x}  —— 结果仅在 x ≠ 0 时与原式等价
```

关心定义域就检查这些约束；忽略它则相当于接受「化简后定义域更宽」。
L1 和 L3 不产生任何约束（常数非零恒成立、符号不影响定义域）。

### 相等判断不依赖化简

`operator==` 内部用**交叉相乘**：`a/b == c/d ⟺ a*d == c*b`。
即使两个分式写法不同、靠 L1/L2 约不到一起，判等依然正确。
这也是不做多项式 GCD 的情况下语义仍然可靠的原因。

### 化为多项式（长除法）

```cpp
Result<Polynomial> toPolynomial() const;
```

当且仅当**分母整除分子**时成功，返回商；否则返回 `MathsError::NotAPolynomial`。

```cpp
// (x^2 - 1) / (x - 1)  →  x + 1
// 1 / x                →  Err(分式无法化简为多项式)
```

内部是多项式带余除法，按**字典序（lex）**确定首项。
这里不能用 `VarPowers` 的默认比较——它不满足单项式序的乘法相容性
（默认比较下 `x < y`，两边同乘 `x` 却得到 `x^2 > xy`，矛盾），项序不相容会让除法不终止。

**注意定义域**：这种归约会丢掉「原式在分母零点处无定义」这一信息。
`(a^2-1)/(a+1)` 化为 `a-1` 的前提是 `a ≠ -1`——归约后的式子在那里有定义，原式没有。
调用方需要自行保留该条件（本项目的 app 会在输出时补上「原式要求 … ≠ 0」）。

### 代入与求值

分式同样接受 `Scope`：

```cpp
Scope scope;
scope.assign(Variable("x"), Integer(2));

Result<RationalFunction> reduced = r.substitute(scope);   // 代入后仍是分式
Result<Fraction> value = r.evaluate(scope);               // 完全求值
```

代入后分母可能退化成零多项式（如 `1/(x-1)` 代入 `x = 1`），此时返回 `ZeroDenominator`。
**那不是程序错误，而是原式的极点**——分式在这里本来就没有定义。

## LaTeX 输出

`Monomial`、`Polynomial`、`RationalFunction` 都提供 `latex()`，与 `str()` 并存：

| 类型 | `str()` | `latex()` |
| --- | --- | --- |
| `Monomial` | `1/2 x^2 y` | `\frac{1}{2}x^{2}y` |
| `Polynomial` | `x^2 + 2 x y + y^2` | `x^{2} + 2xy + y^{2}` |
| `RationalFunction` | `(x + 1) / (x - 1)` | `\frac{x + 1}{x - 1}` |

`str()` 面向终端阅读，`latex()` 面向排版。

分式的 `latex()` **一律输出 `\frac{分子}{分母}`**，所以 `\frac{a}{b} + c` 的通分结果
稳定呈现为统一形式 —— 通分本身由 `operator+` 完成（`a/b + c/1 = (a·1 + c·b)/(b·1)`），
`latex()` 只负责不把它拆散：

```cpp
parseExpression("\\frac{x}{y} + 1").unwrap().latex();   // "\frac{x + y}{y}"
```

| 情形 | `str()` | `latex()` |
| --- | --- | --- |
| 常数分式 | `5/6` | `\frac{5}{6}` |
| 分母为 1 | `x` | `x`（不写成 `\frac{x}{1}`） |

## 示例程序

`apps/` 下是可直接运行的程序。

```bash
cmake -S . -B build -G Ninja
cmake --build build
./build/apps/maths_simplify        # Windows: build\apps\maths_simplify.exe
```

| 程序 | 说明 |
| --- | --- |
| `maths_simplify` | 交互式表达式化简：输入式子与代入条件，输出化简结果 |

```
式子: (x^2 - 1)/(x - 1)
条件（变量 = 常数，输入 0=0 结束）:
> x = 2
> 0=0

--- 结果 ---
化简结果: 3
可化为多项式: 3
常数结果: 3
```

支持的运算：`+ - * / ^` 与括号；变量名可含字母、数字、下划线。
**LaTeX 写法同样接受**：`\frac{a}{b}`、`\cdot`、`\times`、`\div`、`x^{2}`、`\left( \right)`；
输出一律采用 LaTeX。

条件形式：**变量 = 表达式**。

- 右边可以是常数，也可以是含其它变量的表达式：`s = v*t`
- 右边**不能含被赋值的变量本身** —— `x = 2x`、`x = x + 1` 是方程不是赋值，会被拒绝
- 左边是常数时可交换：`3 = x` 理解为 `x = 3`
- `x = x`、`2 = 2` 这类恒等式被识别出来并忽略
- **与式子无关的条件不记录**：式子 `2x` 配上 `s = v*t`，左右两边都不影响它
- `x + 1 = 2` 这类需要解方程的写法明确报错，不猜测

式子解析失败时程序会提示并**重新索取输入**，不会直接退出。

## 打包发布

```bash
sh scripts/build-releases.sh
```

脚本以 Release 模式**只构建 `apps/` 下的程序**（`-DMATHS_BUILD_TESTS=OFF` 加上聚合目标
`maths_apps`，不会连带编译几十个测试），把可执行文件汇总到 `releases/`。

`releases/` 已在 `.gitignore` 中忽略，需要发布时用 `git add -f releases/` 强制加入。

生成器与编译器可通过参数透传给 CMake：

```bash
sh scripts/build-releases.sh -G Ninja -DCMAKE_CXX_COMPILER=g++
```

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
  rational_function.hpp        分式（有理函数）与有限化简
  expression_parser.hpp        表达式与代入条件的解析
  random.hpp                   区间随机数
apps/                        示例程序
test/                        测试
  check.hpp                    断言宏
scripts/                     构建脚本
  build-releases.sh            一键打包可执行文件到 releases/
cmake/                       CMake 包配置模板
releases/                    打包产物（已 gitignore）
```

## 许可

MIT
