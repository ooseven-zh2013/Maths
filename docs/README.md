# 文档索引

`docs/` 的目录结构镜像源码：`src/` 下的每个模块接口单元对应一篇同名文档，
分类子目录（`core` / `numeric` / `algebra` / `parser`）两边一一对应；`apps/` 下的每个程序对应一篇。

## src（库模块）

### core — 与数学无关的基础设施

| 文档 | 对应模块文件 | 内容 |
| --- | --- | --- |
| [maths_error.md](src/core/maths_error.md) | `src/core/maths_error.cppm` | 统一错误码 `MathsError`、两条传递路径、`MathsException` |
| [result.md](src/core/result.md) | `src/core/result.cppm` | `Result<T>` 的取法、运算短路传播、`Result<void>` 特化 |

### numeric — 精确数值

| 文档 | 对应模块文件 | 内容 |
| --- | --- | --- |
| [numbers.md](src/numeric/numbers.md) | `src/numeric/numbers.cppm` | `Integer`、`Fraction` 的精确数值运算 |
| [algebraic_number.md](src/numeric/algebraic_number.md) | `src/numeric/algebraic_number.cppm` | 实代数数 `RealAlgebraicNumber`（最小多项式 + 隔离区间）、根式解析与渲染 |
| [real_set.md](src/numeric/real_set.md) | `src/numeric/real_set.cppm` | 一维实点集 `RealSet`（区间并集 + 代数端点）、精确解不等式、多项式的全部实根 |
| [random.md](src/numeric/random.md) | `src/numeric/random.cppm` | 区间随机数 |

### algebra — 符号代数

| 文档 | 对应模块文件 | 内容 |
| --- | --- | --- |
| [algebraic_expression.md](src/algebra/algebraic_expression.md) | `src/algebra/expression.cppm` | `Variable`、`Monomial`、`Polynomial`、多项式带余除法 |
| [rational_function.md](src/algebra/rational_function.md) | `src/algebra/rational.cppm` | 分式、三层化简、定义域约束、长除法归约 |
| [scope.md](src/algebra/scope.md) | `src/algebra/scope.cppm` | 变量绑定表 `Scope`、代入与求值的实现 |
| [algebraic.md](src/algebra/algebraic.md) | `src/algebra/algebraic.cppm` | 系数取实代数数的代数式（代数栈按系数参数化、`toAlgebraic` 提升）|
| [radical.md](src/algebra/radical.md) | `src/algebra/radical.cppm` | 根式扩张 `RadicalExtension`：`√x`、`√(x²+1) + √(x²+2)` 这类含变量根号 |
| [constraint.md](src/algebra/constraint.md) | `src/algebra/constraint.cppm` | 取值范围约束 `RangeConstraint`、解不等式、定义域、`Scope` 的集合约束 |
| [constraint_system.md](src/algebra/constraint_system.md) | `src/algebra/constraint_system.cppm` | 多维点集：原子约束 `AtomConstraint` 与合取 `ConstraintSystem`、成员判定、可分离情形 |

### parser — 文本 → 式子

| 文档 | 对应模块文件 | 内容 |
| --- | --- | --- |
| [expression_parser.md](src/parser/expression_parser.md) | `src/parser/parser.cppm` | 表达式与代入条件的解析、相关性判定 |

### 横切

| 文档 | 对应模块文件 | 内容 |
| --- | --- | --- |
| [latex.md](src/latex.md) | — | `str()` 与 `latex()` 两套输出的对照与排版规则 |

伞模块 `src/maths.cppm` 一次引入全部模块，不单独对应一篇文档。

## apps（程序）

| 文档 | 对应程序 | 内容 |
| --- | --- | --- |
| [simplify.md](apps/simplify.md) | `apps/simplify/main.cpp` → 产物 `releases/simplify/` | 交互式表达式化简：语法、条件写法、结果说明 |

## 其他

| 文档 | 对应目录 | 内容 |
| --- | --- | --- |
| [test.md](test.md) | `tests/` | 断言宏、跑测试、新增测试的注意事项 |
| [packaging.md](packaging.md) | `scripts/` | 打包脚本与在 GitHub Releases 上发布 |

## 怎么读

- 想「这个库能干什么」→ 仓库根目录的 `README.md`
- 想「某个类型怎么用」→ 上表按模块名找
- 想「报错是什么意思」→ [maths_error.md](src/core/maths_error.md)
- 想「化简能做到哪一步」→ [rational_function.md](src/algebra/rational_function.md) 的「化简做到什么程度」
