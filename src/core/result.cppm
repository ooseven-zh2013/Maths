export module maths.result;

import std;
import maths.error;

export namespace maths {

// 全库统一的结果类型：成功携带 T，失败携带 MathsError。
// 错误类型固定为 MathsError，使所有类的运算结果在类型名称与语义上保持一致。
//
// 两条硬规则：
//   1. 运算短路传播：先左后右，一旦某一侧已失败就不再求值，且保留左侧的错误。
//   2. 不提供 operator T()：想拿到裸值必须显式 unwrap() / unwrapOr() / expect()，
//      因此"忘记检查返回值"在编译期就无法通过。
//
// 刻意不提供 < > <= >=：比较需要"值"的语义，失败时无意义，请先 unwrap()。
// operator<< 输出的是结果本身（Ok(...) / Err(...)），不涉及解包。

template <class T> class [[nodiscard]] Result {
public:
  using ValueType = T;

  // 显式默认：T 平凡时（Result<bool>、Result<Fraction>…）本就可以平凡析构，
  // 但隐式生成的析构对「这个类型能不能装进 variant/optional 并省掉一层间接」影响不明显，
  // 写出来编译器才知道。clang-tidy performance-trivially-destructible 提的正是这条。
  //
  // ⚠️ 下面这个 NOLINT 是**上游已知 bug**，不是「查不动就不查」。已查证：
  //
  //   llvm/llvm-project PR #178471（issue #178102，2026-01-28 修）：
  //   「C++20 modules 下，当一个类**同时经 #include 与 module import** 可见时，
  //     它的析构函数会在 redeclaration chain 里出现多次，其中一个实例
  //     `isFirstDecl()` 为 false，这条检查就误以为它是「out-of-line 的 defaulted 析构」
  //     而报出来。**该场景只在 modules 下发生**。」
  //
  // 本项目正是 C++20 modules，所以必中。现象完全吻合：
  //   · clang-tidy 20.1.7 报，20.1.0 不报（redeclaration chain 的 AST 行为不同）
  //   · 报的位置正是这个**已经写了 `= default`** 的析构
  //   · 最小例子复现不出 —— 缺「同时经 include 与 import 可见」这一环
  //
  // 修法在上游（把 `isFirstDecl()` 换成 `isOutOfLine()`），但那版是 22.x ——
  // 而 22.x 读不了本工具链 20.1.7 的 BMI（见 .github/workflows/ci.yml 的说明），
  // 所以升级不是选项。NOLINT 是这里唯一正确的选择。
  // NOLINTNEXTLINE(performance-trivially-destructible)
  ~Result() = default;

  // ==================== 构造 ====================

  Result(const T &value) : data_(std::in_place_index<0>, value) {}
  Result(T &&value) : data_(std::in_place_index<0>, std::move(value)) {}

  // 对应 Rust 的 Err(e)，返回语句里写作 return std::unexpected(MathsError::X);
  Result(std::unexpected<MathsError> error) : data_(std::in_place_index<1>, error.error()) {}

  static Result ok(const T &value) { return Result(value); }
  static Result ok(T &&value) { return Result(std::move(value)); }
  static Result err(MathsError error) { return Result(std::unexpected<MathsError>(error)); }

  // ==================== 状态查询 ====================

  bool isOk() const noexcept { return data_.index() == 0; }
  bool isErr() const noexcept { return data_.index() == 1; }
  explicit operator bool() const noexcept { return isOk(); }

  // ==================== 唯一出口 ====================

  T &unwrap() & {
    requireOk();
    return std::get<0>(data_);
  }

  const T &unwrap() const & {
    requireOk();
    return std::get<0>(data_);
  }

  T unwrap() && {
    requireOk();
    return std::move(std::get<0>(data_));
  }

  T unwrapOr(T fallback) const { return isOk() ? std::get<0>(data_) : std::move(fallback); }

  template <class F> T unwrapOrElse(F fallback) const { return isOk() ? std::get<0>(data_) : fallback(error()); }

  T expect(std::string_view message) const {
    if (isErr()) {
      throw MathsException(error());
    }
    (void)message;
    return std::get<0>(data_);
  }

  MathsError unwrapErr() const {
    requireErr("unwrapErr");
    return error();
  }

  const MathsError &error() const { return std::get<1>(data_); }

  // ==================== 函数式传播 ====================
  //
  // 这两个是给「不想写 MATHS_TRY 宏、也不想手写 if-isErr-return」的场合用的：
  // 纯库内实现，**跨模块可用**（宏不行，宏不随 import 传递，见 common/maths_try.hpp）。
  //
  // 传播语义与 MATHS_TRY 一致：Err 时短路，且**原样保留错误**，不调用 f。

  // map：变换成功的值。f 失败的情形由调用方保证不会发生（返回 T 而非 Result<T>）。
  template <class F> auto map(F &&f) const {
    using U = std::invoke_result_t<F, const T &>;
    if (isErr()) {
      return Result<U>::err(error());
    }
    return Result<U>::ok(f(std::get<0>(data_)));
  }

  // andThen：串下一个**也可能失败**的运算。f 返回 Result<U>。
  // 这是「链式版 MATHS_TRY」：
  //   MATHS_TRY 写法                        andThen 写法
  //   const A a = MATHS_TRY(f());           return f().andThen([](const A &a) {
  //   const B b = MATHS_TRY(g(a));            return g(a);
  //   return Result<C>::ok(h(a, b));        }).andThen([](const B &b) {
  //                                          ... 需要 a 时得靠闭包捕获
  //                                        });
  // 单步链 andThen 更短；**多步且后一步要用前几步的值**时 MATHS_TRY 更清晰
  // （闭包捕获会越写越丑）。两者并存，按形状选。
  template <class F> auto andThen(F &&f) const {
    using U = typename std::invoke_result_t<F, const T &>::ValueType;
    if (isErr()) {
      return Result<U>::err(error());
    }
    return f(std::get<0>(data_));
  }

  // ==================== 运算：短路传播 ====================
  // 两侧都失败时保留左侧错误（先左后右）

  Result operator+(const Result &rhs) const {
    return chain(rhs, [](const T &lhsValue, const T &rhsValue) { return Result(lhsValue + rhsValue); });
  }

  Result operator-(const Result &rhs) const {
    return chain(rhs, [](const T &lhsValue, const T &rhsValue) { return Result(lhsValue - rhsValue); });
  }

  Result operator*(const Result &rhs) const {
    return chain(rhs, [](const T &lhsValue, const T &rhsValue) { return Result(lhsValue * rhsValue); });
  }

  // 除法本身可能失败，T::operator/ 已经返回 Result<T>
  Result operator/(const Result &rhs) const {
    return chain(rhs, [](const T &lhsValue, const T &rhsValue) { return lhsValue / rhsValue; });
  }

  Result operator-() const {
    if (isErr()) {
      return *this;
    }
    return Result(-std::get<0>(data_));
  }

  Result operator+(const T &rhs) const { return *this + Result(rhs); }
  Result operator-(const T &rhs) const { return *this - Result(rhs); }
  Result operator*(const T &rhs) const { return *this * Result(rhs); }
  Result operator/(const T &rhs) const { return *this / Result(rhs); }

  friend Result operator+(const T &lhs, const Result &rhs) { return Result(lhs) + rhs; }
  friend Result operator-(const T &lhs, const Result &rhs) { return Result(lhs) - rhs; }
  friend Result operator*(const T &lhs, const Result &rhs) { return Result(lhs) * rhs; }
  friend Result operator/(const T &lhs, const Result &rhs) { return Result(lhs) / rhs; }

  // ==================== 组合 ====================

  template <class U, class F> Result<U> map(F transform) const {
    if (isErr()) {
      return Result<U>(std::unexpected<MathsError>(error()));
    }
    return Result<U>(transform(std::get<0>(data_)));
  }

  template <class U, class F> Result<U> andThen(F transform) const {
    if (isErr()) {
      return Result<U>(std::unexpected<MathsError>(error()));
    }
    return transform(std::get<0>(data_));
  }

  template <class F> Result<T> orElse(F transform) const {
    if (isOk()) {
      return *this;
    }
    return transform(error());
  }

  template <class F> Result<T> mapErr(F transform) const {
    if (isOk()) {
      return *this;
    }
    return Result<T>(std::unexpected<MathsError>(transform(error())));
  }

  std::optional<T> ok() const {
    if (isOk()) {
      return std::get<0>(data_);
    }
    return std::nullopt;
  }

  std::optional<MathsError> err() const {
    if (isErr()) {
      return error();
    }
    return std::nullopt;
  }

  bool operator==(const Result &rhs) const { return data_ == rhs.data_; }

private:
  void requireOk() const {
    if (isErr()) {
      throw MathsException(error());
    }
  }

  void requireErr(const char *operation) const {
    if (isOk()) {
      throw std::runtime_error(std::string(operation) + " 被调用，但结果是 Ok");
    }
  }

  template <class F> Result chain(const Result &rhs, F operation) const {
    if (isErr()) {
      return *this; // 左侧优先
    }
    if (rhs.isErr()) {
      return rhs;
    }
    return operation(std::get<0>(data_), std::get<0>(rhs.data_));
  }

  std::variant<T, MathsError> data_;
};

// ==================== Result<void>：只表示成功或失败，不携带值 ====================

template <> class [[nodiscard]] Result<void> {
public:
  using ValueType = void;

  Result() = default;

  static Result err(MathsError error) {
    Result result;
    result.error_ = error;
    return result;
  }

  bool isOk() const noexcept { return !error_.has_value(); }
  bool isErr() const noexcept { return error_.has_value(); }
  explicit operator bool() const noexcept { return isOk(); }

  void unwrap() const {
    if (isErr()) {
      throw MathsException(error());
    }
  }

  MathsError unwrapErr() const {
    if (isOk()) {
      throw std::runtime_error("unwrapErr 被调用，但结果是 Ok");
    }
    return *error_;
  }

  const MathsError &error() const { return *error_; }

  bool operator==(const Result &rhs) const { return error_ == rhs.error_; }

private:
  std::optional<MathsError> error_;
};

// ==================== 流输出（输出结果本身，不涉及解包） ====================

template <class T> std::ostream &operator<<(std::ostream &os, const Result<T> &result) {
  if (result.isErr()) {
    return os << "Err(" << describe(result.unwrapErr()) << ")";
  }
  if constexpr (requires(std::ostream &out) { out << result.unwrap(); }) {
    return os << "Ok(" << result.unwrap() << ")";
  } else {
    return os << "Ok(...)";
  }
}

} // namespace maths
