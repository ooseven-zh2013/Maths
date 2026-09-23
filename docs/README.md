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
| [random.md](src/numeric/random.md) | `src/numeric/random.cppm` | 区间随机数 |

### algebra — 符号代数

| 文档 | 对应模块文件 | 内容 |
| --- | --- | --- |
| [algebraic_expression.md](src/algebra/algebraic_expression.md) | `src/algebra/expression.cppm` | `Variable`、`Monomial`、`Polynomial`、多项式带余除法 |
| [rational_function.md](src/algebra/rational_function.md) | `src/algebra/rational.cppm` | 分式、三层化简、定义域约束、长除法归约 |
| [scope.md](src/algebra/scope.md) | `src/algebra/scope.cppm` | 变量绑定表 `Scope`、代入与求值的实现 |

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
| [simplify.md](apps/simplify.md) | `apps/simplify` → `maths_simplify` | 交互式表达式化简：语法、条件写法、结果说明 |

## 其他

| 文档 | 对应目录 | 内容 |
| --- | --- | --- |
| [test.md](test.md) | `test/` | 断言宏、跑测试、新增测试的注意事项 |
| [packaging.md](packaging.md) | `scripts/` | 打包脚本与在 GitHub Releases 上发布 |

## 怎么读

- 想「这个库能干什么」→ 仓库根目录的 `README.md`
- 想「某个类型怎么用」→ 上表按模块名找
- 想「报错是什么意思」→ [maths_error.md](src/core/maths_error.md)
- 想「化简能做到哪一步」→ [rational_function.md](src/algebra/rational_function.md) 的「化简做到什么程度」
