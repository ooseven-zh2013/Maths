# maths.algebra:aggregate — 集合 → 数（求和、最值、平均、方差）

对应 `src/algebra/aggregate.cppm`。

把「一堆数」变成「一个数」。全程在实代数数环里做，`√2 + √3` 给的是 `√2 + √3`
这个精确元素，不是 3.146…。

## 为什么输入是「序列」而不是「集合」

**求和对集合没有意义**：`{1, 1, 2}` 作为集合就是 `{1, 2}`，但一份数据的和是 4。

所以主入口收 `std::vector<RealAlgebraicNumber>`（可以有重复、有顺序），
另配一组 `RealSet` 重载给真正的集合语义 —— 那组只收**有限点集**。

```cpp
std::vector<RealAlgebraicNumber> data = {1, 2, 3};

sumOf(data);                 // 6
productOf(data);             // 6
maximumOf(data).unwrap();    // 3
minimumOf(data).unwrap();    // 1
meanOf(data).unwrap();       // 2
varianceOf(data).unwrap();   // 2/3      （总体方差，÷n）
sampleVarianceOf(data).unwrap();              // 1  （样本方差，÷(n−1)）
standardDeviationOf(data).unwrap();           // √6/3
sampleStandardDeviationOf(data).unwrap();     // 1
```

## 接口

| 分类 | 成员 | 空序列的行为 |
| --- | --- | --- |
| 序列 | `sumOf(vector)` | 0（空和） |
| | `productOf(vector)` | 1（空积） |
| | `maximumOf(vector)` / `minimumOf(vector)` | `EmptyCollection` |
| | `meanOf(vector)` | `EmptyCollection` |
| | `varianceOf(vector)`（÷n） | `EmptyCollection` |
| | `sampleVarianceOf(vector)`（÷(n−1)） | `EmptyCollection`（少于 2 个观测也一样） |
| | `standardDeviationOf(vector)` / `sampleStandardDeviationOf(vector)` | `EmptyCollection` |
| 点集 | `sumOf(set)` / `productOf(set)` / `meanOf(set)` / `varianceOf(set)` | 空集 → `EmptyCollection` |
| 确界 | `supremumOf(set)` / `infimumOf(set)` | `EmptyCollection` |

## 空和与空积不是错误

`sumOf({})` 给 **0**、`productOf({})` 给 **1** —— 这是空和 / 空积的惯例值，
不是「没有答案」。其余统计量对空样本确实没有定义，报 `EmptyCollection`。

## 精确性

元素是实代数数，运算全在代数数环里做。代价是**次数会随元素个数涨**：
n 个独立根号相加，最小多项式的次数可达 2ⁿ，中间系数也可能顶穿 `Fraction`
的表示范围（那时报 `NumericOverflow`）。

作业规模（几个到几十个数、根号种类不多）完全没问题。想省事就先把数据整理成
同类项（`√2 + √2` 走 `α+α` 的廉价特例，直接给 `2√2`，不会真去建四次域）。

## 有限点集 vs 连续集合

| 输入 | `sumOf` | 最大 / 最小 |
| --- | --- | --- |
| 有限点集（每一块都是单点） | ✅ 精确求和 | 用 `maximumOf(vector)`，或 `supremumOf(set)` |
| 含区间块 | ❌ `NotFiniteSet` | ✅ [RealFunction::image](function.md) 的端点 |

**区间上的「求和」是积分**，会带出对数与反正切（超越函数），本库明确排除。
所以 `sumOf(closedInterval(0,1))` 报 `NotFiniteSet` 而不是给个近似值。

**区间上的最大最小值另有一套**：那是像集的端点，已经由 `RealFunction::image` /
`PiecewiseFunction::image` 做掉了，不要在聚合里重复。

## 上/下确界：为什么返回 `Bound`

名字用 `sup` / `inf` 而不是 `max` / `min`，是因为**它们未必属于这个集合**：

```
(1, 2) 的上确界是 2，但 2 ∉ (1, 2)
```

叫 `max` 会把这件事盖掉。而 `supremumOf(realLine())` 的答案是 `+∞` ——
那是个**合法答案**，不该当错误报掉。`Bound` 正好就是「实数或 ±∞」：

```cpp
Bound high = supremumOf(halfOpen).unwrap();
high.isInfinite();       // false
high.value().str();      // "1"

supremumOf(RealSet::realLine()).unwrap().isInfinite();   // true
```

⚠️ 取 `value()` 之前先查 `isInfinite()` —— 无穷的 `Bound` 里没有值。

## 相关

- 点集与区间 → [real_set.md](../numeric/real_set.md)
- 连续集合上的最值 → [function.md](function.md) 的「集合视角」
- 实代数数 → [algebraic_number.md](../numeric/algebraic_number.md)
