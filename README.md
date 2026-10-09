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
- **实代数数** — `√2`、`∛2`、`√2+√3` 这类无理数的精确表示（最小多项式 + 隔离区间），四则与开方全精确
- **根号（系数位置）** — `\sqrt{2}*x`、`x + \sqrt{2}` 可直接写进式子；有理指数 `2^{1/2}`
- **根式扩张（变量位置）** — `√x`、`√(x²+1) + √(x²+2)` 能精确运算、代入求值，且可直接写进式子（`parseRadicalExpression`）
- **集合与不等式** — 解 `x²−2 ≥ 0` 这类不等式，解集是端点精确（代数数）的区间并集
- **取值范围约束** — 变量能取哪些值用同一个集合类型表示：不等式的解、函数定义域、`x ≠ 0` 这类隐含前提
- **多变量限定条件** — `x−y=z`、`x²+y²≤1` 这类约束可精确判定成员（有理点与代数点），可分离时给出各维区间
- **多变量等式组** — Gröbner 基：判有无解、消元 / 参数化（`{x²+y²=1, x=y}` → `y²=1/2`）、投影
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

**`import maths;` 一次导入全部模块** —— 下表所有名字都从它导出，直接 `import maths;`
就能用，不必逐个写。伞模块是**两级**的：

    maths                      （9 个 export import）
     ├─ maths.numbers / maths.real_set / maths.algebraic_number
     ├─ maths.error / maths.result / maths.random
     ├─ maths.algebra          （再转发 19 个分区模块）
     ├─ maths.parser
     └─ maths.value            （`Expression`：一个入口做解析/代入/求值，不必自己判表示）

只要一个模块时按需单写（见上面「构建与运行」的例子）—— 编译更快、依赖更清楚。

⚠️ 标「—」的几行**还没有独立的文档页**，行为说明散在对应源文件的文件头注释里
（那些注释是随代码走的权威出处）。

| 模块 | 模块名 | 说明 | 文档 |
| --- | --- | --- | --- |
| 精确数值 | `maths.numbers` | `Integer`、`Fraction` | [文档](docs/src/numeric/numbers.md) |
| 实代数数 | `maths.algebraic_number` | `RealAlgebraicNumber`：最小多项式 + 隔离区间、四则、开方、根式解析与渲染 | [文档](docs/src/numeric/algebraic_number.md) |
| 实点集 | `maths.real_set` | 一维实点集 `RealSet`：区间并集、精确端点、解不等式、多项式的全部实根 | [文档](docs/src/numeric/real_set.md) |
| 约束 | `maths.algebra:constraint` | 取值范围约束：解不等式、函数定义域、`Scope` 接集合约束 | [文档](docs/src/algebra/constraint.md) |
| 多维约束 | `maths.algebra:constraint_system` | 多变量限定条件：原子约束、成员判定、可分离情形、线性投影（Fourier–Motzkin）| [文档](docs/src/algebra/constraint_system.md) |
| Gröbner 基 | `maths.algebra:groebner` | 多变量等式组：一致性判定、消元 / 参数化 / 投影（Buchberger，精确有理）| [文档](docs/src/algebra/groebner.md) |
| 代数表达式 | `maths.algebra:expression` | `Variable`、`Monomial`、`Polynomial`、带余除法 | [文档](docs/src/algebra/algebraic_expression.md) |
| 分式 | `maths.algebra:rational` | 有理函数、三层化简、长除法归约 | [文档](docs/src/algebra/rational_function.md) |
| 变量绑定 | `maths.algebra:scope` | `Scope` 与 `substitute` / `evaluate` | [文档](docs/src/algebra/scope.md) |
| 代数系数代数式 | `maths.algebra:algebraic` | 系数取实代数数的多项式与分式、代数作用域 | [文档](docs/src/algebra/algebraic.md) |
| 根式扩张 | `maths.algebra:radical` | `√x`、`√(x²+1) + √(x²+2)` 这类含变量根号的精确表示与运算 | [文档](docs/src/algebra/radical.md) |
| 一元函数 | `maths.algebra:function` | 一元实函数 `RealFunction`：规则 + `RealSet` 定义域，求值、四则、复合、集合拉回、像集 | [文档](docs/src/algebra/function.md) |
| 分段函数 | `maths.algebra:piecewise` | 分段函数：绝对值、逐点取大取小（规则装不下拐弯的函数，这类要靠分支装）| [文档](docs/src/algebra/piecewise.md) |
| 多变量有理函数 | `maths.algebra:rational_multi` | `MultiRationalFunction`：`ℚ(x₁,…,xₙ)` 上的分式 | — |
| 多元约束 | `maths.algebra:domain_multi` | 多元定义域与取值点 | — |
| 半代数区域 | `maths.algebra:region_multi` | `Region`（约束系统的析取范式）与 `admits(point)` | — |
| 多元规则 | `maths.algebra:rule` | `MultiRule`：多元有理 or 多元塔 | — |
| 多元函数 | `maths.algebra:function_multi` | 多元函数：规则 + 多元区域定义域 | — |
| 多元分段函数 | `maths.algebra:piecewise_multi` | 多元分段；**多元绝对值 `\|x-y\|` 靠它**（拆 `{f≥0}` 与 `{f<0}` 两支）| [文档](docs/src/algebra/piecewise_multi.md) |
| 套嵌根式（塔） | `maths.algebra:tower` | `TowerExtension`：`\sqrt{1+\sqrt{x}}` 这类套嵌；元素是**平表之比** | [文档](docs/src/algebra/tower.md) |
| 多元套嵌根式 | `maths.algebra:tower_multi` | `MultiTowerExtension`：多元的塔 | — |
| 聚合 | `maths.algebra:aggregate` | 集合 → 数：求和 / 求积 / 最值 / 平均 / 方差 / 标准差、上确界与下确界 | [文档](docs/src/algebra/aggregate.md) |
| 表达式解析 | `maths.parser` | 文本与 LaTeX → 式子、代入条件解析、代数版入口 | [文档](docs/src/parser/expression_parser.md) |
| 错误码 | `maths.error` | `MathsError`、`MathsException` | [文档](docs/src/core/maths_error.md) |
| 结果类型 | `maths.result` | `Result<T>` | [文档](docs/src/core/result.md) |
| 随机数 | `maths.random` | 区间随机数 | [文档](docs/src/numeric/random.md) |
| LaTeX 输出 | — | `str()` 与 `latex()` 对照、排版规则 | [文档](docs/src/latex.md) |

依赖方向单向：`maths.error → maths.result → maths.numbers → maths.algebra → maths.parser`
（`maths.random` 依赖 `maths.numbers`）。

伞模块 `maths` 位于这条链的**最上层**，只做转发、不含实现 —— 所以它没有依赖方向上的包袱，
但也别在库内部 `import maths;`（会造成循环），内部一律用具体的分区模块。

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
    algebraic_number.cppm           maths.algebraic_number —— 实代数数 RealAlgebraicNumber
    real_set.cppm                   maths.real_set —— 一维实点集 RealSet
    random.cppm                     maths.random —— 区间随机数
  algebra/                        符号代数（主接口 export import 全部分区）
    algebra.cppm                    maths.algebra —— 主接口
    expression.cppm                 :expression —— Name、Variable、Monomial、Polynomial
    rational.cppm                   :rational —— 分式与有限化简
    scope.cppm                      :scope —— Scope、代入与求值
    algebraic.cppm                  :algebraic —— 系数取实代数数的代数式
    constraint.cppm                 :constraint —— 取值范围约束、解不等式、定义域
    constraint_system.cppm          :constraint_system —— 多维点集的约束式表示
    groebner.cppm                   :groebner —— Gröbner 基（Buchberger）
    radical.cppm                    :radical —— 根式扩张（√ 里有变量）
    function.cppm                   :function —— 一元实函数（规则 + 定义域）
    piecewise.cppm                  :piecewise —— 分段函数（绝对值等）
    aggregate.cppm                  :aggregate —— 聚合（集合 → 数）
  parser/parser.cppm               maths.parser —— 表达式与代入条件的解析
tests/                          测试（mcpp test 自动发现，一文件一可执行文件）
  check.hpp                       断言宏
apps/                           示例程序
mcpp.toml                       项目清单：包名、目标、编译标志、工具链
docs/                           文档（模块、程序、测试、打包）
```

## 许可

MIT
