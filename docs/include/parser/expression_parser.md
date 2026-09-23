# maths/parser/expression_parser.hpp — 表达式与条件的解析

对应 `include/maths/parser/expression_parser.hpp`。

递归下降解析器，把文本解析成 `RationalFunction`。
放在 `include/` 而不是 `apps/` 是因为它是库级能力（字符串 → 式子），应该能被测试和复用。

结果**统一用 `RationalFunction` 承载**：除法必然引入分式，用统一类型可以省掉
「多项式还是分式」的分支判断。

## 语法

```
expr    := term (('+' | '-') term)*
term    := power (('*' | '/') power)*
power   := unary ('^' 非负整数)?
unary   := ('-' | '+')? primary
primary := 整数 | 变量名 | '(' expr ')'
```

幂用重复乘法实现（`RationalFunction` 未提供 `pow`），指数只接受非负整数。

```cpp
Result<RationalFunction> value = parseExpression("(x^2 - 1)/(x - 1)");
```

解析失败返回 `InvalidExpression`；存在无法消费的残留字符也算失败。

### 变量名

变量名 = **单个字母 + 可选下标**：`x`、`a_1`、`x_{i,j}`。
连续字母不合并成一个名字，而是按隐含乘法拆开：

| 输入 | 含义 |
| --- | --- |
| `xy` | `x*y` |
| `2x` | `2*x` |
| `{node}` | 名为 `node` 的变量（**多字母必须加花括号**，否则是 `n*o*d*e`） |
| `{node}_{car}` | 名为 `node`、下标为 `car` 的变量 |

隐含乘法让「输入 `xy`」与「输入 `x*y`」得到同一个式子，也与 `latex()` 的输出闭环。

### LaTeX 输入

`normalizeLatex` 做预处理，把 LaTeX 写法转成上面的普通语法 —— **解析器本身只处理一种语法**：

| LaTeX | 转换后 |
| --- | --- |
| `\frac{a}{b}` | `(a)/(b)`（支持嵌套） |
| `\cdot`、`\times` | `*` |
| `\div` | `/` |
| `x^{2}` | `x^2` |
| `\left`、`\right` | 忽略 |

```cpp
parseExpression("\\frac{x}{y} + 1").unwrap().latex();   // "\frac{x + y}{y}"
```

## 代入条件

```cpp
struct Assignment {
  Variable variable;
  RationalFunction value;   // 右边可以是含其它变量的表达式，如 s = v*t
};

Result<std::optional<Assignment>> parseAssignment(std::string_view text);
```

返回类型表达**三态**：解析失败 / 恒等式（无需记录）/ 有效赋值。
这里 `optional` 用得恰当 —— 问题不是"为什么失败"，而是"有没有赋值"。

### 判定规则

| 输入 | 行为 |
| --- | --- |
| `x = 2` | 绑定 ✓ |
| `x = 1/2 + 1/3` | 右边求值为 `5/6` ✓ |
| `x = v*t` | 绑定，允许含式子里的变量或其它变量 ✓ |
| `3 = x`、`vt = x` | **两侧交换** → `x = 3`、`x = vt` ✓ |
| `x + 1 = y` | 交换 → `y = x + 1`（y 单独在一侧，直接读出，**不算**解方程）✓ |
| `x = x` | 恒等式 → `nullopt`（删除语义见下） |
| `2 = 2` | 常数恒等式 → `nullopt` |
| `x = 2x`、`x = x + 1` | 拒绝 —— 那是方程不是赋值 → `NotAnAssignment` |
| `x + 1 = x` | 拒绝 —— 左边含右边那个变量，真方程 → `InvalidExpression` |
| `x + 1 = 2` | 拒绝 —— 需要解方程，**明确不猜** → `InvalidExpression` |
| `f(x) = 2` | 拒绝（`(` 导致解析后有残留字符）→ `InvalidExpression` |
| `a == b` | 拒绝（不支持 `==`）→ `InvalidExpression` |

交换规则：只要**左边不含右边那个变量**就交换。`vt = x` → `x = vt`。

### parseErase：删除绑定

`x = x` 表示**删掉变量 x 此前记录的约束**（`y = y`、`a_1 = a_1` 同理）。

```cpp
std::optional<Variable> parseErase(std::string_view text);
```

单独一个函数而不是塞进 `parseAssignment`，因为**删除与赋值是两种动作**。
`parseAssignment` 对 `x = x` 仍返回 `nullopt` —— 那确实「没有可记录的信息」，语义没破。

**调用顺序：先 `parseErase`，落空再 `parseAssignment`。**

三类输入的区分：

| 输入 | `parseErase` | 后续 |
| --- | --- | --- |
| `x = x` | 返回 `x` | 执行删除 |
| `5 = 5` | `nullopt` | 常数恒等式，非删除 |
| `x + 1 = 2` | `nullopt` | **不抢先判错**，交回 `parseAssignment` 报准确错误码 |

## 相关性判定

```cpp
bool isRelevantTo(const RationalFunction &expression, const Scope &scope, const Assignment &assignment);
```

式子 `2x` 配上 `s = v*t` 时，左右两边都不影响它，记录没有意义 —— 这种约束不记录。

判定四条，满足任一即相关：

1. 被赋值的变量直接出现在式子里
2. 被赋值的变量出现在**某条已有绑定的值**里 —— 那条绑定的有效值会变
3. 右边含式子里的某个变量
4. 右边含某条已有绑定的变量名 —— 代入链会继续展开

第 2 条是后来补的：只看原始式子不够。式子 `2x` 在 `x = s` 之后，`s = v*t` 就会影响结果，
因为 x 的有效值已经变成了 s。

## 辅助函数

| 函数 | 说明 |
| --- | --- |
| `asSingleVariable(value)` | 是否为「恰好等于某个变量」：系数 1、指数 1、分母 1 |
| `asConstant(value)` | 是否为常数（空 `Scope` 下可求值即说明不含变量） |

## 示例

```cpp
Scope scope;
scope.assign(Variable("s"), parseExpression("v*t").unwrap());
scope.assign(Variable("v"), Integer(3));
scope.assign(Variable("t"), Integer(4));

parseExpression("2s").unwrap().substitute(scope).unwrap().latex();   // "24"
```
