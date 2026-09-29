export module maths.real_set;

import std;
import maths.error;
import maths.result;
import maths.numbers;
import maths.algebraic_number;

export namespace maths {

// 一维实点集：**有限个区间的并集**，端点是实代数数。
//
// 为什么只需要这一种表示：一元多项式（不）等式的任何布尔组合，解集一定能写成
// 有限个区间 / 点 —— 这就是一维半代数集的规范形。所以不存在「另一种形式的集合」
// 需要来回转换，`RealSet` 本身就是那个规范形。
//
// 端点是 `RealAlgebraicNumber` 而不是浮点：`√2` 这个端点也是**精确**的，
// 区间的相交、合并、判等全部走精确比较，不采样也不近似。
//
// 空集 = 没有任何区间；孤立点是退化区间 `[a, a]`；全集是 `(-∞, +∞)`。

// 不等关系。`RealSet::solve` 用它表示「f(x) ⋈ 0」里的 ⋈。
enum class Relation { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

// 关系的 LaTeX 记法（`\le` / `\ge` 都用排版习惯的写法）
inline std::string relationLatex(Relation relation) {
  switch (relation) {
  case Relation::Equal:
    return "=";
  case Relation::NotEqual:
    return "\\ne";
  case Relation::Less:
    return "<";
  case Relation::LessEqual:
    return "\\le";
  case Relation::Greater:
    return ">";
  case Relation::GreaterEqual:
    return "\\ge";
  }
  return "?";
}

// 区间端点：有限值（带是否取到）或 ±∞
class Bound {
public:
  static Bound finite(const RealAlgebraicNumber &value, bool closed) {
    Bound result;
    result.value_ = value;
    result.closed_ = closed;
    return result;
  }

  static Bound negativeInfinity() {
    Bound result;
    result.infinite_ = -1;
    return result;
  }

  static Bound positiveInfinity() {
    Bound result;
    result.infinite_ = 1;
    return result;
  }

  bool isInfinite() const { return infinite_ != 0; }
  int infiniteDirection() const { return infinite_; } // -1 / 0 / +1
  bool isClosed() const { return closed_; }
  const RealAlgebraicNumber &value() const { return value_.value(); }

  std::strong_ordering compareTo(const Bound &rhs) const {
    if (isInfinite() || rhs.isInfinite()) {
      return infinite_ <=> rhs.infinite_;
    }
    return value_.value() <=> rhs.value_.value();
  }

  bool operator==(const Bound &rhs) const {
    if (isInfinite() || rhs.isInfinite()) {
      return infinite_ == rhs.infinite_;
    }
    return closed_ == rhs.closed_ && value_.value() == rhs.value_.value();
  }

  std::string latex() const {
    if (isInfinite()) {
      return infinite_ < 0 ? "-\\infty" : "+\\infty";
    }
    return value_.value().latex();
  }

  std::string str() const {
    if (isInfinite()) {
      return infinite_ < 0 ? "-inf" : "+inf";
    }
    return value_.value().str();
  }

private:
  std::optional<RealAlgebraicNumber> value_;
  bool closed_{false};
  int infinite_{0};
};

struct Interval {
  Bound lower;
  Bound upper;

  bool isDegenerate() const {
    return !lower.isInfinite() && !upper.isInfinite() && lower.compareTo(upper) == std::strong_ordering::equal &&
           lower.isClosed() && upper.isClosed();
  }

  bool contains(const RealAlgebraicNumber &point) const {
    const std::strong_ordering below = lower.compareTo(Bound::finite(point, true));
    if (below == std::strong_ordering::greater) {
      return false;
    }
    if (below == std::strong_ordering::equal && !lower.isClosed()) {
      return false;
    }
    const std::strong_ordering above = upper.compareTo(Bound::finite(point, true));
    if (above == std::strong_ordering::less) {
      return false;
    }
    if (above == std::strong_ordering::equal && !upper.isClosed()) {
      return false;
    }
    return true;
  }
};

class RealSet {
public:
  RealSet() = default; // 空集

  static RealSet empty() { return RealSet(); }

  static RealSet realLine() {
    RealSet result;
    result.intervals_.push_back(Interval{Bound::negativeInfinity(), Bound::positiveInfinity()});
    return result;
  }

  // 规范形入口：排序、合并重叠或相接的区间，并校验每个区间自身的合法性。
  static Result<RealSet> make(std::vector<Interval> intervals) {
    for (const Interval &interval : intervals) {
      const std::strong_ordering order = interval.lower.compareTo(interval.upper);
      if (order == std::strong_ordering::greater) {
        return std::unexpected(MathsError::InvalidRange); // 下界大于上界
      }
      if (order == std::strong_ordering::equal && !(interval.lower.isClosed() && interval.upper.isClosed())) {
        return std::unexpected(MathsError::InvalidRange); // 退化成一点时必须两端都取到
      }
    }

    std::sort(intervals.begin(), intervals.end(), [](const Interval &lhs, const Interval &rhs) {
      return lhs.lower.compareTo(rhs.lower) == std::strong_ordering::less;
    });

    std::vector<Interval> merged;
    for (const Interval &interval : intervals) {
      if (merged.empty()) {
        merged.push_back(interval);
        continue;
      }
      Interval &last = merged.back();
      const std::strong_ordering order = interval.lower.compareTo(last.upper);
      // 重叠，或恰好相接且两侧都取到（如 [0,1] 与 [1,2]）才合并；
      // (0,1) 与 (1,2) 不能并成 (0,2) —— 那样会把 1 也算进去
      const bool overlaps = order == std::strong_ordering::less || (order == std::strong_ordering::equal &&
                                                                    last.upper.isClosed() && interval.lower.isClosed());
      if (!overlaps) {
        merged.push_back(interval);
        continue;
      }
      if (interval.upper.compareTo(last.upper) == std::strong_ordering::greater) {
        last.upper = interval.upper;
      }
    }

    RealSet result;
    result.intervals_ = std::move(merged);
    return result;
  }

  static Result<RealSet> point(const RealAlgebraicNumber &value) {
    return make({Interval{Bound::finite(value, true), Bound::finite(value, true)}});
  }

  // [low, high]（要求 low ≤ high）
  static Result<RealSet> closedInterval(const RealAlgebraicNumber &low, const RealAlgebraicNumber &high) {
    return make({Interval{Bound::finite(low, true), Bound::finite(high, true)}});
  }

  const std::vector<Interval> &intervals() const { return intervals_; }
  bool isEmpty() const { return intervals_.empty(); }

  bool isRealLine() const {
    return intervals_.size() == 1 && intervals_.front().lower.isInfinite() &&
           intervals_.front().lower.infiniteDirection() < 0 && intervals_.front().upper.isInfinite() &&
           intervals_.front().upper.infiniteDirection() > 0;
  }

  bool contains(const RealAlgebraicNumber &point) const {
    for (const Interval &interval : intervals_) {
      if (interval.contains(point)) {
        return true;
      }
    }
    return false;
  }

  RealSet unite(const RealSet &rhs) const {
    std::vector<Interval> all = intervals_;
    all.insert(all.end(), rhs.intervals_.begin(), rhs.intervals_.end());
    return make(std::move(all)).unwrap(); // 合并不会失败：输入各自合法
  }

  Result<RealSet> intersect(const RealSet &rhs) const {
    std::vector<Interval> result;
    for (const Interval &lhs : intervals_) {
      for (const Interval &rhsInterval : rhs.intervals_) {
        const Bound lower =
            lhs.lower.compareTo(rhsInterval.lower) == std::strong_ordering::greater ? lhs.lower : rhsInterval.lower;
        const Bound upper =
            lhs.upper.compareTo(rhsInterval.upper) == std::strong_ordering::less ? lhs.upper : rhsInterval.upper;
        const std::strong_ordering order = lower.compareTo(upper);
        if (order == std::strong_ordering::greater) {
          continue;
        }
        if (order == std::strong_ordering::equal) {
          if (lower.isClosed() && upper.isClosed()) {
            result.push_back(Interval{lower, upper});
          }
          continue;
        }
        result.push_back(Interval{lower, upper});
      }
    }
    return make(std::move(result));
  }

  // 相对 ℝ 的补集：把区间之间的空隙翻出来，端点取到与否取反。
  //
  // cursor 是「当前待处理的空隙起点」，它的 closed 表示该点是否落在空隙里
  // （即原集合是否**没**取到它）。处理后 cursor 移到原区间的右端点并取反。
  Result<RealSet> complement() const {
    std::vector<Interval> result;
    Bound cursor = Bound::negativeInfinity();
    for (const Interval &interval : intervals_) {
      const std::strong_ordering order = cursor.compareTo(interval.lower);
      // 空隙非空：要么确实错开，要么重合但「原集合不含该点而空隙含该点」
      const bool gapNonEmpty = order == std::strong_ordering::less || (order == std::strong_ordering::equal &&
                                                                       cursor.isClosed() && !interval.lower.isClosed());
      if (gapNonEmpty) {
        // 能走到这里说明 interval.lower 有限（它严格大于 cursor ≥ −∞）
        result.push_back(Interval{cursor, Bound::finite(interval.lower.value(), !interval.lower.isClosed())});
      }
      cursor = interval.upper.isInfinite() ? interval.upper
                                           : Bound::finite(interval.upper.value(), !interval.upper.isClosed());
    }
    // 末尾补上 (cursor, +∞)：cursor 是 +∞ 时说明原集合一直延伸到 +∞，没有尾巴；
    // 停在 −∞ 则说明原集合是空集，补集就是整条实轴
    if (!(cursor.isInfinite() && cursor.infiniteDirection() > 0)) {
      result.push_back(Interval{cursor, Bound::positiveInfinity()});
    }
    return make(std::move(result));
  }

  // ==================== 解不等式 ====================
  // f(x) ⋈ 0 的解集。临界点（f 的实根）用 Sturm 精确隔离，每段取**有理样本点**判号 ——
  // 全程精确，不含浮点近似。
  static Result<RealSet> solve(const UnivariatePolynomial &polynomial, Relation relation) {
    if (polynomial.isZero()) {
      const bool alwaysTrue =
          relation == Relation::Equal || relation == Relation::LessEqual || relation == Relation::GreaterEqual;
      return alwaysTrue ? realLine() : empty();
    }

    const std::vector<RealAlgebraicNumber> roots = RealAlgebraicNumber::realRoots(polynomial);

    if (relation == Relation::Equal || relation == Relation::NotEqual) {
      std::vector<Interval> points;
      for (const RealAlgebraicNumber &root : roots) {
        points.push_back(Interval{Bound::finite(root, true), Bound::finite(root, true)});
      }
      const Result<RealSet> zeroSet = make(std::move(points));
      if (zeroSet.isErr()) {
        return zeroSet;
      }
      return relation == Relation::Equal ? zeroSet : zeroSet.unwrap().complement();
    }

    if (roots.empty()) {
      // 常数多项式：整条实轴符号恒定
      const Fraction constant = polynomial.coefficient(0);
      const bool positive = constant > 0LL;
      return satisfiedBySign(relation, positive) ? realLine() : empty();
    }

    // ≤ / ≥ 时零点本身属于解集，所以区间的根端点要标成「取到」；
    // 标成不取到的话，相邻区间就没法和零点合并成闭区间（如 [−1,0]）。
    const bool rootsIncluded = relation == Relation::LessEqual || relation == Relation::GreaterEqual;

    std::vector<Interval> result;
    const std::size_t segments = roots.size() + 1;
    for (std::size_t index = 0; index < segments; ++index) {
      const Result<Fraction> sample = sampleIn(index, roots);
      if (sample.isErr()) {
        return std::unexpected(sample.unwrapErr());
      }
      const bool positive = polynomial.evaluate(sample.unwrap()) > 0LL;
      if (!satisfiedBySign(relation, positive)) {
        continue;
      }
      const Bound lower = index == 0 ? Bound::negativeInfinity() : Bound::finite(roots[index - 1], rootsIncluded);
      const Bound upper =
          index + 1 == segments ? Bound::positiveInfinity() : Bound::finite(roots[index], rootsIncluded);
      result.push_back(Interval{lower, upper});
    }
    return make(std::move(result));
  }

  // ==================== 输出 ====================
  // 空集 ∅；全集 ℝ；单点 {a}；其余是区间与 ∪ 的组合。

  std::string latex() const {
    if (intervals_.empty()) {
      return "\\varnothing";
    }
    if (isRealLine()) {
      return "\\mathbb{R}";
    }
    std::string result;
    for (std::size_t index = 0; index < intervals_.size(); ++index) {
      if (index != 0) {
        result += " \\cup ";
      }
      result += renderInterval(true, intervals_[index]);
    }
    return result;
  }

  std::string str() const {
    if (intervals_.empty()) {
      return "{}";
    }
    if (isRealLine()) {
      return "R";
    }
    std::string result;
    for (std::size_t index = 0; index < intervals_.size(); ++index) {
      if (index != 0) {
        result += " U ";
      }
      result += renderInterval(false, intervals_[index]);
    }
    return result;
  }

private:
  static bool satisfiedBySign(Relation relation, bool positive) {
    switch (relation) {
    case Relation::Less:
    case Relation::LessEqual:
      return !positive;
    case Relation::Greater:
    case Relation::GreaterEqual:
      return positive;
    case Relation::Equal:
    case Relation::NotEqual:
      return false; // 这两个在上面单独处理
    }
    return false;
  }

  // 第 index 段（共 roots.size() + 1 段）里的一个**有理**样本点。
  // 有理是关键：代入后是精确有理运算，符号判断不会因近似而错。
  static Result<Fraction> sampleIn(std::size_t index, const std::vector<RealAlgebraicNumber> &roots) {
    if (index == 0) {
      return roots.front().lowerBound() - Fraction(1, 1); // 比最小的根还小
    }
    if (index == roots.size()) {
      return roots.back().upperBound() + Fraction(1, 1); // 比最大的根还大
    }

    // 相邻两个根：精化到隔离区间分离，再取中间的有理数
    RealAlgebraicNumber left = roots[index - 1];
    RealAlgebraicNumber right = roots[index];
    for (int attempt = 0; attempt < 128; ++attempt) {
      if (left.upperBound() < right.lowerBound()) {
        return (left.upperBound() + right.lowerBound()) * Fraction(1, 2);
      }
      left.refine();
      right.refine();
    }
    return std::unexpected(MathsError::InvalidRange); // 两个根相同时才会到这里
  }

  static std::string renderInterval(bool useLatex, const Interval &interval) {
    if (interval.isDegenerate()) {
      const std::string value = useLatex ? interval.lower.latex() : interval.lower.str();
      return useLatex ? "\\{" + value + "\\}" : "{" + value + "}";
    }
    const char openLeft = interval.lower.isClosed() ? '[' : '(';
    const char openRight = interval.upper.isClosed() ? ']' : ')';
    const std::string lower = useLatex ? interval.lower.latex() : interval.lower.str();
    const std::string upper = useLatex ? interval.upper.latex() : interval.upper.str();
    std::string result;
    result += openLeft;
    result += lower;
    result += useLatex ? ", " : ", ";
    result += upper;
    result += openRight;
    return result;
  }

  std::vector<Interval> intervals_;
};

inline std::ostream &operator<<(std::ostream &os, const RealSet &value) { return os << value.str(); }

} // namespace maths
