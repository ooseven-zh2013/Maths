# 文档索引

`docs/` 的目录结构镜像源码：`include/maths/` 下的每个头文件对应一篇同名文档，
分类子目录（`core` / `numeric` / `algebra` / `parser`）两边一一对应；`apps/` 下的每个程序对应一篇。

## include（库）

### core — 与数学无关的基础设施

| 文档 | 对应头文件 | 内容 |
| --- | --- | --- |
| [maths_error.md](include/core/maths_error.md) | `include/maths/core/maths_error.hpp` | 统一错误码 `MathsError`、两条传递路径、`MathsException` |
| [result.md](include/core/result.md) | `include/maths/core/result.hpp` | `Result<T>` 的取法、运算短路传播、`Result<void>` 特化 |

### numeric — 精确数值

| 文档 | 对应头文件 | 内容 |
| --- | --- | --- |
| [numbers.md](include/numeric/numbers.md) | `include/maths/numeric/numbers.hpp` | `Integer`、`Fraction` 的精确数值运算 |
| [random.md](include/numeric/random.md) | `include/maths/numeric/random.hpp` | 区间随机数 |

### algebra — 符号代数

| 文档 | 对应头文件 | 内容 |
| --- | --- | --- |
| [algebraic_expression.md](include/algebra/algebraic_expression.md) | `include/maths/algebra/algebraic_expression.hpp` | `Variable`、`Monomial`、`Polynomial`、多项式带余除法 |
| [rational_function.md](include/algebra/rational_function.md) | `include/maths/algebra/rational_function.hpp` | 分式、三层化简、定义域约束、长除法归约 |
| [scope.md](include/algebra/scope.md) | `include/maths/algebra/scope.hpp` | 变量绑定表 `Scope`、代入与求值的实现 |

### parser — 文本 → 式子

| 文档 | 对应头文件 | 内容 |
| --- | --- | --- |
| [expression_parser.md](include/parser/expression_parser.md) | `include/maths/parser/expression_parser.hpp` | 表达式与代入条件的解析、相关性判定 |

### 横切

| 文档 | 对应头文件 | 内容 |
| --- | --- | --- |
| [latex.md](include/latex.md) | — | `str()` 与 `latex()` 两套输出的对照与排版规则 |

伞头 `include/maths/maths.hpp` 一次引入全部模块，不单独对应一篇文档。

## apps（程序）

| 文档 | 对应程序 | 内容 |
| --- | --- | --- |
| [simplify.md](apps/simplify.md) | `apps/simplify` → `maths_simplify` | 交互式表达式化简：语法、条件写法、结果说明 |

## 其他

| 文档 | 对应目录 | 内容 |
| --- | --- | --- |
| [test.md](test.md) | `test/` | 断言宏、跑测试、新增测试的注意事项 |
| [packaging.md](packaging.md) | `scripts/` | 打包脚本与在 GitHub Releases 上发布 |

## 怎么读

- 想「这个库能干什么」→ 仓库根目录的 `README.md`
- 想「某个类型怎么用」→ 上表按头文件名找
- 想「报错是什么意思」→ [maths_error.md](include/core/maths_error.md)
- 想「化简能做到哪一步」→ [rational_function.md](include/algebra/rational_function.md) 的「化简做到什么程度」
