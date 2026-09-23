#pragma once
#ifndef MATHS_RESULT_HPP
#define MATHS_RESULT_HPP

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

#include <expected>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <maths/core/maths_error.hpp>

template <class T> class [[nodiscard]] Result {
public:
  using ValueType = T;

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

#endif // MATHS_RESULT_HPP
