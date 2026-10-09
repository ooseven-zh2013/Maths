# value — `Expression`

对应 `src/value.cppm`，模块 `maths.value`。

## 它解决什么

库里每种数学对象是一个**独立类型**，而它们**不共享任何操作**：

| 表示 | 类型 | 典型输入 |
| --- | --- | --- |
| 有理式 | `RationalFunction` | `x^2+1`、`1/(x+1)` |
| 代数数 | `RealAlgebraicNumber` | `\sqrt{2}` |
| 代数系数式 | `AlgebraicRationalFunction` | `\sqrt{2}*x` |
| 根式 | `RadicalExtension` | `\sqrt{x}`、`\sqrt{x^2+1}` |
| 单变量塔 | `TowerExtension` | `\sqrt{1+\sqrt{x}}` |
| 多元塔 | `MultiTowerExtension` | `\sqrt{x^2+y^2}` |
| 一元分段 | `PiecewiseFunction` | `\|x\|`、`\sqrt{x^2}` |
| 多元分段 | `MultiPiecewiseFunction` | `\|x-y\|` |

于是调用方要做任何一件事（求值、代入、化简、打印）都**得先判断「这是哪种类型」**——
分派散在调用方各处，判断错一次就是一类 bug。

`Expression` 把「哪种表示」关进自己内部，对外只给几个动作。

```cpp
auto e = Expression::parse("|x^2+y^2-2xy|");   // 一个入口，内部自己决定表示
e.variables();                                 // {x, y}
e.evaluate(scope);                             // → 精确值
e.latex();  e.str();
```

`apps/simplify` 用了它之后，本地那套「七档级联 + 13 处类型分派」整块删掉了。

## 接口

| 成员 | 说明 |
| --- | --- |
| `static Result<Expression> parse(std::string_view)` | **唯一的解析入口**，内部按下面「档位顺序」逐档尝试 |
| `variables()` | 出现在表达式里的变量 |
| `writtenAsAbsoluteValue()` | 输入形如 `\|…\|` 或 `√(g²)`（调用方据此回显成 `\|…\|` 而非 √ 写法） |
| `latex()` / `str()` | 两种渲染：`latex()` 面向排版、`str()` 面向终端 |
| `simplify()` | 各表示**都已经是规范形**，目前原样返回（见下「不做的事」） |
| `substitute(const Scope&)` | 代入。支持有理式与根式 |
| `evaluate(const Scope&)` | 在给定点求值 → `RealAlgebraicNumber`（**精确值**） |
| `representation()` | 当前用的哪种表示（**实现细节**，仅为诊断与测试公开） |
| `asRational()` / `asAlgebraicNumber()` / … | **逃生舱**：返回对应类型的指针，不是那种表示则 `nullptr` |

## 档位顺序（动它会改变语义）

```
1. 有理式          —— 最基础，且它的语法诊断最准
2. 代数数          —— 纯数值，给「精确值」；排在有理式**之后**是刻意的（1/2 该走上一档）
3. 代数系数的分式   —— \sqrt{2}*x：根号包常数而式子带变量
4. 根式扩张        —— \sqrt{x}、\sqrt{x}+\sqrt{x+1}
5. 多元塔          —— \sqrt{x^2+y^2}；**只在变量 > 1 时**接
6. 多元分段        —— |x-y|；**只在变量 > 1 时**接
7. 一元分段        —— |x| 与 \sqrt{x^2}（同一个东西）
8. 单变量塔        —— \sqrt{1+\sqrt{x}}
```

两条最要紧的：

⚠️ **第 5、6 档的「变量 > 1」守卫不能省。** `parseMultiPiecewiseExpression("|x|")` 是
**成功**的（它构造得出两支，只是只有 1 个变量）。少了守卫，单变量绝对值会被多元档抢走，
于是被要求「一次给全的点」。

⚠️ **第 2 档必须排在有理式之后。** 代数数那档给的是「精确值」而不是「化简结果」——
`1/2` 该走有理式那档，不该被当成代数数。

## 失败时问哪个错

多档都失败时，`parse` 报**最具体**的那条原因：

- `InvalidExpression` 是各档共有的兜底码，**不算「更具体」**
- 纯数值输入只有代数数那档给得出 `ZeroDenominator` / `DivisionByZero` /
  `NumericOverflow`（`2^{1/0}`、`0^{-1}`、`(-4)^{1/2}`）
- 根号那档的 `RadicandIsSquare` / `RadicandsNotIndependent` 是在**整条输入按根号语法
  解析成功之后**才抛的（语法没问题，卡住的是「`√(x²)` 是 `|x|`」这种数学限制）

## 为什么它单独成一个模块

不是 `maths.parser` 的分区，而是顶层模块 `maths.value`。

**原因**：分区**不能 import 自己的主模块**。`Expression::parse` 正是靠
`maths.parser` 里那些 `parseXxxExpression` 建起来的；若让它当 `maths.parser` 的分区，
就会形成 `maths.parser` → `:value` → `maths.parser` 的**循环依赖**（实测 clang 报得明确）。

## 不做的事

- **不做最简根式化**：`\sqrt{4x}` 保持 `\sqrt{4x}`，不会化成 `2\sqrt{x}`。
  这是刻意的（见 [tower.md](tower.md) 的说明）。
- **不替代底层类型**：`asXxx()` 那些逃生舱就是给「代码确实知道自己在处理哪种表示」的
  场合留的（例如渲染要读 `PiecewiseFunction` 的分支、给塔算定义域）。
  判据：**如果你先问「它是哪种表示」再决定怎么做，那段代码就是分派** ——
  能换成 `variables` / `evaluate` / `latex` 就换掉。

## 相关

- 解析语法：[expression_parser.md](parser/expression_parser.md)
- 精确值怎么表示：[algebraic_number.md](numeric/algebraic_number.md)
- 一个用它的完整例子：[../examples/一元方程求解器.md](../examples/一元方程求解器.md)
