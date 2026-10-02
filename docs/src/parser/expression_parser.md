# maths.parser — 表达式与条件的解析

对应 `src/parser/parser.cppm`。

递归下降解析器，把文本解析成式子。按**系数类型**参数化，因此同一套语法既能产
`RationalFunction`（有理系数），也能产 `AlgebraicRationalFunction`（系数取实代数数）。
放在 `src/parser/` 而不是 `apps/` 是因为它是库级能力（字符串 → 式子），应该能被测试和复用。

结果**统一用 `RationalFunction` 承载**：除法必然引入分式，用统一类型可以省掉
「多项式还是分式」的分支判断。

## 语法

```
expr    := term (('+' | '-') term)*
term    := power (('*' | '/') power)*
power   := unary ('^' 指数)?
unary   := ('-' | '+')? primary
primary := 整数 | 变量名 | '(' expr ')'
```

幂用重复乘法实现（`RationalFunction` 未提供 `pow`）。指数可以写成：

| 写法 | 例子 | 说明 |
| --- | --- | --- |
| 非负整数 | `x^2`、`x^{12}` | 花括号里必须是**纯非负整数** |
| 有理指数 | `2^{1/2}`、`2^{-1/2}`、`2^{\frac{1}{2}}` | `p`、`p/q`、`\frac{p}{q}`，允许负号 |

`x^{1/2}` 这类**变量底的分数指数**解析失败（见下）：`x^{1/2}` 不是 `x/2`，
也不在本解析器能力范围内。分母为 0（`2^{1/0}`）报 `ZeroDenominator`。

> 指数必须**整串校验**：`std::stoull` 遇到非法字符只解析前缀且不抛异常
> （`"1/2"` 直接返回 `1`），早期实现因此把 `2^{1/2}` 静默算成 `2`。

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
| `x^{2}` | `x^2`（内容不是纯整数时**不**展开，交给指数解析器报错）|
| `\left`、`\right` | 忽略 |
| `\sqrt{2}`、`\sqrt[3]{2}` | 原样保留，由解析器识别（**根号内只许常数**）|

```cpp
parseExpression("\\frac{x}{y} + 1").unwrap().latex();   // "\frac{x + y}{y}"
```

## 含变量根号的入口：`parseRadicalExpression`

`\sqrt{x}`、`\sqrt{x^2+1}`、`2\sqrt{x}`、`\sqrt{x}\sqrt{x+1}` 这类**根号包变量**的式子走这条入口，
返回一个根式扩张元素（`RadicalExtension`，见 [radical.md](../algebra/radical.md)），
域按式子里出现的根号**自动扩张**：

```cpp
parseRadicalExpression("\\sqrt{x}").unwrap().latex();                  // "\\sqrt{x}"
parseRadicalExpression("2\\sqrt{x}").unwrap().latex();                 // "2\\sqrt{x}"
parseRadicalExpression("\\sqrt{x} + 1").unwrap().latex();              // "1 + \\sqrt{x}"
parseRadicalExpression("(\\sqrt{x}+1)^2").unwrap().latex();            // "x + 1 + 2\\sqrt{x}"
parseRadicalExpression("\\sqrt{x}\\cdot\\sqrt{x+1}").unwrap();         // 两个生成元（自动扩域）
parseRadicalExpression("\\sqrt{x^2+1} + \\sqrt{x^2+2}").unwrap().latex();
                                                                       // "\\sqrt{x^2 + 1} + \\sqrt{x^2 + 2}"
```

结果可以代入求值：`√x | x=4 = 2`；`x = −1` 时该点无实值，返回 `NegativeEvenRoot`。

三个入口的分工：

| 入口 | 返回 | 根号能出现在哪 |
| --- | --- | --- |
| `parseExpression` | `RationalFunction`（系数 ℚ） | 不行 |
| `parseAlgebraicExpression` | `AlgebraicRationalFunction`（系数取实代数数） | **系数位置**（`\sqrt{2}*x`）|
| `parseRadicalExpression` | `RadicalExtension`（函数域元素） | **根号包变量**（`\sqrt{x}`）|

> 为什么这条入口**不复用** `ParserOf<Coefficient>`：那个模板要求「系数运算不失败且返回值」
> （`PolynomialOf` 内部把 `lhsCoeff * rhsCoeff` 当值用），而根式扩张的运算可能失败
> （同一平方类、跨变量），所以改用专门的求值器：`\sqrt{…}` 直接产出元素，其余整块交给
> 有理解析器，错误一路以 `Result` 传出。

边界（都明确报错）：

| 写法 | 结果 |
| --- | --- |
| `\sqrt[3]{x}` | `InvalidExpression` —— 只支持二次根 |
| `\sqrt{\sqrt{x}}` | `InvalidExpression` —— 嵌套根号需要更大的结构 |
| `\sqrt{2}` | `InvalidExpression` —— 常数根号属于实代数数，请走 `parseAlgebraicExpression` / `RealAlgebraicNumber` |
| `\sqrt{x} + \sqrt{y}` | `InvalidExpression` —— 被开方数必须含同一个变量 |
| `\sqrt{x} + \sqrt{4x}` | `RadicandsNotIndependent` —— 同一平方类，需要最简根式化 |
| `\sqrt{x}*y` | ✅ 合法 —— 被开方数仍只有一个变量，系数的其它字母不受限 |

## 代数版入口：根号出现在系数位置

`parseAlgebraicExpression` 用同一个 `ParserOf<Coefficient>` 模板、把系数换成
`RealAlgebraicNumber`，于是式子里的根号不再被拒：

```cpp
parseAlgebraicExpression("\\sqrt{2}*x").unwrap().latex();     // "\\sqrt{2}x"
parseAlgebraicExpression("x + \\sqrt{2}").unwrap().latex();   // "x + \\sqrt{2}"
parseAlgebraicExpression("\\sqrt[3]{2}*y").unwrap().latex();  // "\\sqrt[3]{2}y"
parseAlgebraicExpression("\\sqrt{2}/2").unwrap().latex();     // "\\frac{\\sqrt{2}}{2}"
```

两条入口有明确分工：

| 入口 | 系数域 | 根号 |
| --- | --- | --- |
| `parseExpression` | ℚ（`RationalFunction`） | **拒收** —— 有理系数域里没有根式 |
| `parseAlgebraicExpression` | 实代数数（`AlgebraicRationalFunction`） | 系数位置的根号可以写 |

`parseExpression` 是前者的子集：纯有理式子走两条路结果等价（有断言的对照）。
**根号包变量**（`\sqrt{x}`）两条路都拒收 —— 那需要函数域，见
[radical.md](../algebra/radical.md)。

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

## `parsePiecewiseExpression` —— 根式 → 分段函数

`parseRadicalExpression` 拒收被开方数是**完全平方**的根号（`\sqrt{x^2}` = `|x|`，
不是单个代数函数）。这一档把那种输入接成**分段函数**：

```cpp
parsePiecewiseExpression("\\sqrt{x^2}");              // { x 当 [0,+∞) ; −x 当 (−∞,0) }
parsePiecewiseExpression("\\sqrt{(x+1)^2}");         // |x+1|
parsePiecewiseExpression("\\sqrt{x^2}*\\sqrt{x^2+1}"); // 两支，规则里还留着那个真根号
parsePiecewiseExpression("\\sqrt{x}");               // 单支，等价于 parseRadicalExpression
```

做法：把这些 `\sqrt{g²}` 逐个换成占位变量，交给**现有的根式解析器**解析
（此时没有完全平方的根号了，一定通过），再把占位变量代成 `±g` 并按符号分成
2<sup>k</sup> 支（k 上限 4）。语法、优先级、报错因此与 `parseRadicalExpression` 完全一致。

- **只支持一元**：`PiecewiseFunction` 本身就是一元的（多元的定义域是多维点集）
- 老入口对这类输入**仍然拒收**（`RadicandIsSquare`）—— 域论上的限制没变，
  变的是「函数层能装下它」
