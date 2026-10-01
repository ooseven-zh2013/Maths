export module maths.algebra:aggregate;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;
import maths.real_set;

export namespace maths {

// 「一堆数 → 一个数」：求和、求积、最大最小、平均、方差、标准差。
//
// ============================ 为什么输入是序列 ============================
//
// 求和**对集合没有意义**：{1, 1, 2} 作为集合就是 {1, 2}，但一份数据的和是 4。
// 所以主入口收的是 `std::vector<RealAlgebraicNumber>`（可以有重复、有顺序），
// 另配一组 `RealSet` 重载给真正的集合语义 —— 那组只收**有限点集**，
// 区间块会明确报错（连续集合上的求和是积分，不是求和，见下）。
//
// ============================ 全程精确 ============================
//
// 元素是实代数数，运算全在代数数环里做：`√2 + √3` 给的是 `√2 + √3` 这个精确元素，
// 不是 3.146…。代价是**次数会随元素个数涨** —— n 个独立根号相加，最小次数可达 2ⁿ，
// 中间系数也可能顶穿 `Fraction` 的表示范围（那时报 `NumericOverflow`）。
// 作业规模（几个到几十个数、根号种类不多）完全没问题。
//
// ============================ 连续集合为什么不做 ============================
//
// 区间上的「求和」是 ∫f dx，那会带出对数与反正切（超越函数），本库明确排除。
// 而且区间上的**最大最小值**已经在 `RealFunction::image` 里做掉了：
// 那是像集的端点，别在这里重复一遍。

namespace aggregate_detail {

inline RealAlgebraicNumber countOf(std::size_t size) {
  return RealAlgebraicNumber(Fraction(static_cast<long long>(size), 1));
}

// 有限点集的元素表。区间块（含无限块）不是有限点集 —— 那不是求和，是积分。
inline Result<std::vector<RealAlgebraicNumber>> pointsOf(const RealSet &set) {
  std::vector<RealAlgebraicNumber> values;
  for (const Interval &interval : set.intervals()) {
    if (!interval.isDegenerate()) {
      return std::unexpected(MathsError::NotFiniteSet);
    }
    values.push_back(interval.lower.value()); // 退化区间 [a, a] 的下端就是那个点
  }
  return values;
}

} // namespace aggregate_detail

// ==================== 序列（数据集） ====================

// Σxᵢ。空序列给 0（空和）。
inline RealAlgebraicNumber sumOf(const std::vector<RealAlgebraicNumber> &values) {
  RealAlgebraicNumber total(Fraction(0, 1));
  for (const RealAlgebraicNumber &value : values) {
    total = total + value;
  }
  return total;
}

// Πxᵢ。空序列给 1（空积）。
inline RealAlgebraicNumber productOf(const std::vector<RealAlgebraicNumber> &values) {
  RealAlgebraicNumber total(Fraction(1, 1));
  for (const RealAlgebraicNumber &value : values) {
    total = total * value;
  }
  return total;
}

inline Result<RealAlgebraicNumber> maximumOf(const std::vector<RealAlgebraicNumber> &values) {
  if (values.empty()) {
    return std::unexpected(MathsError::EmptyCollection);
  }
  RealAlgebraicNumber result = values.front();
  for (const RealAlgebraicNumber &value : values) {
    if (value.compareTo(result) == std::strong_ordering::greater) {
      result = value;
    }
  }
  return result;
}

inline Result<RealAlgebraicNumber> minimumOf(const std::vector<RealAlgebraicNumber> &values) {
  if (values.empty()) {
    return std::unexpected(MathsError::EmptyCollection);
  }
  RealAlgebraicNumber result = values.front();
  for (const RealAlgebraicNumber &value : values) {
    if (value.compareTo(result) == std::strong_ordering::less) {
      result = value;
    }
  }
  return result;
}

// 算术平均 x̄ = (Σxᵢ) / n。平均数的值是精确的代数数
// （√2 与 √3 的平均就是 (√2+√3)/2，不会退化成小数）。
inline Result<RealAlgebraicNumber> meanOf(const std::vector<RealAlgebraicNumber> &values) {
  if (values.empty()) {
    return std::unexpected(MathsError::EmptyCollection);
  }
  return sumOf(values) / aggregate_detail::countOf(values.size());
}

// 总体方差 s² = (1/n)·Σ(xᵢ − x̄)²
inline Result<RealAlgebraicNumber> varianceOf(const std::vector<RealAlgebraicNumber> &values) {
  if (values.empty()) {
    return std::unexpected(MathsError::EmptyCollection);
  }
  const Result<RealAlgebraicNumber> mean = meanOf(values);
  if (mean.isErr()) {
    return std::unexpected(mean.unwrapErr());
  }
  RealAlgebraicNumber total(Fraction(0, 1));
  for (const RealAlgebraicNumber &value : values) {
    const RealAlgebraicNumber deviation = value - mean.unwrap();
    total = total + deviation * deviation;
  }
  return total / aggregate_detail::countOf(values.size());
}

// 样本方差 s² = (1/(n−1))·Σ(xᵢ − x̄)²。少于两个观测就没有意义。
inline Result<RealAlgebraicNumber> sampleVarianceOf(const std::vector<RealAlgebraicNumber> &values) {
  if (values.size() < 2) {
    return std::unexpected(MathsError::EmptyCollection);
  }
  const Result<RealAlgebraicNumber> mean = meanOf(values);
  if (mean.isErr()) {
    return std::unexpected(mean.unwrapErr());
  }
  RealAlgebraicNumber total(Fraction(0, 1));
  for (const RealAlgebraicNumber &value : values) {
    const RealAlgebraicNumber deviation = value - mean.unwrap();
    total = total + deviation * deviation;
  }
  return total / aggregate_detail::countOf(values.size() - 1);
}

// 标准差 s = √(s²)。方差非负，所以开方一定落在实数上。
inline Result<RealAlgebraicNumber> standardDeviationOf(const std::vector<RealAlgebraicNumber> &values) {
  const Result<RealAlgebraicNumber> variance = varianceOf(values);
  if (variance.isErr()) {
    return std::unexpected(variance.unwrapErr());
  }
  return variance.unwrap().nthRoot(2);
}

inline Result<RealAlgebraicNumber> sampleStandardDeviationOf(const std::vector<RealAlgebraicNumber> &values) {
  const Result<RealAlgebraicNumber> variance = sampleVarianceOf(values);
  if (variance.isErr()) {
    return std::unexpected(variance.unwrapErr());
  }
  return variance.unwrap().nthRoot(2);
}

// ==================== 点集（集合语义） ====================
// 集合没有重复元素；而且求和/平均这些要求集合是**有限点集**，
// 含区间块时**明确报错**而不是去近似 —— 那是积分。

inline Result<RealAlgebraicNumber> sumOf(const RealSet &set) {
  const Result<std::vector<RealAlgebraicNumber>> values = aggregate_detail::pointsOf(set);
  if (values.isErr()) {
    return std::unexpected(values.unwrapErr());
  }
  return sumOf(values.unwrap());
}

inline Result<RealAlgebraicNumber> productOf(const RealSet &set) {
  const Result<std::vector<RealAlgebraicNumber>> values = aggregate_detail::pointsOf(set);
  if (values.isErr()) {
    return std::unexpected(values.unwrapErr());
  }
  return productOf(values.unwrap());
}

// 有限点集的平均数。空集报 EmptyCollection
inline Result<RealAlgebraicNumber> meanOf(const RealSet &set) {
  const Result<std::vector<RealAlgebraicNumber>> values = aggregate_detail::pointsOf(set);
  if (values.isErr()) {
    return std::unexpected(values.unwrapErr());
  }
  return meanOf(values.unwrap());
}

inline Result<RealAlgebraicNumber> varianceOf(const RealSet &set) {
  const Result<std::vector<RealAlgebraicNumber>> values = aggregate_detail::pointsOf(set);
  if (values.isErr()) {
    return std::unexpected(values.unwrapErr());
  }
  return varianceOf(values.unwrap());
}

// ==================== 上/下确界（任意非空集合） ====================
//
// 名字用 sup / inf 而不是 max / min，是因为**它们未必是这个集合的元素**：
// (1, 2) 的上确界是 2，但 2 ∉ (1, 2)。叫 max 会把这件事盖掉。
//
// 返回 `Bound` 而不是 `RealAlgebraicNumber`：集合无上界时上确界是 +∞，
// 那是个**合法答案**，不该当错误报掉。`Bound` 正好就是「实数或 ±∞」。
// ⚠️ 取 `value()` 之前先查 `isInfinite()`。

inline Result<Bound> supremumOf(const RealSet &set) {
  if (set.isEmpty()) {
    return std::unexpected(MathsError::EmptyCollection);
  }
  // 规范形已按左端点排序，所以最后一个区间的上端就是上确界
  return set.intervals().back().upper;
}

inline Result<Bound> infimumOf(const RealSet &set) {
  if (set.isEmpty()) {
    return std::unexpected(MathsError::EmptyCollection);
  }
  return set.intervals().front().lower;
}

} // namespace maths
