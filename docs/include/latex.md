# LaTeX 输出

`Monomial`、`Polynomial`、`RationalFunction` 都提供 `latex()`，与 `str()` **并存**
—— 一个面向终端阅读，一个面向排版，不要用一个替换另一个。

## 两套输出对照

| 类型 | `str()` | `latex()` |
| --- | --- | --- |
| `Monomial` | `1/2 x^2 y` | `\frac{1}{2}x^{2}y` |
| `Polynomial` | `x^2 + 2 x y + y^2` | `x^{2} + 2xy + y^{2}` |
| `RationalFunction` | `(x + 1) / (x - 1)` | `\frac{x + 1}{x - 1}` |

`latex()` 里变量之间**直接相连**（LaTeX 隐含乘法），不加空格也不用 `\cdot`。

## 排版规则

| 规则 | 例子 |
| --- | --- |
| 单字符指数不加花括号 | `a^2`（不是 `a^{2}`） |
| 多位数指数加花括号 | `a^{12}` —— 否则 `a^12` 会被读成 `a^1·2` |
| 分数系数写成 `\frac{}{}` | `\frac{1}{2}x` |
| 整数直接输出，不写分母 | `3x` |
| 分式整体为负时负号提到 `\frac` 外 | `-\frac{3}{4}`（不是 `\frac{-3}{4}`） |
| 系数为 ±1 时省略数字 | `-x` |

## 分式

`RationalFunction::latex()` **一律输出 `\frac{分子}{分母}`**，不再像 `str()` 那样把常数分式写成 `5/6`。
所以 `\frac{a}{b} + c` 的通分结果稳定呈现为统一形式：

```cpp
parseExpression("\\frac{x}{y} + 1").unwrap().latex();   // "\frac{x + y}{y}"
```

通分本身由 `operator+` 完成（`a/b + c/1 = (a·1 + c·b)/(b·1)`），`latex()` 只负责不把它拆散。

| 情形 | `str()` | `latex()` |
| --- | --- | --- |
| 常数分式 | `5/6` | `\frac{5}{6}` |
| 分母为 1 | `x` | `x`（不写成 `\frac{x}{1}`） |

判断「分母为 1」时必须同时要求 `isConstant()`，否则分母是 `x` 这种系数为 1 的单项式会被误判。

负号位置只看分子首项 —— 分母首项已由 `normalizeSign` 归一为正。

## 输出顺序就是存储序

`latex()` 的项序与 `str()` 相同，都用 `maths_detail::displayOrderLess`
（次数降序 → 变量名升序 → 同变量指数降序）。

**这是预期行为，不是瑕疵**：输入 `s = v*t` 回显成 `s = tv` 是因为单项式因子按存储序输出，
而在 LaTeX 里 `tv` 与 `vt` 数学等价。不要为了「看着顺眼」去改成输入顺序。

数学运算（取首项、长除法）用的是另一套序 —— 字典序 `compareLex`，
两者不能混用，详见 [algebraic_expression.md](algebraic_expression.md#坑存储序--数学项序)。
