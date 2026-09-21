#pragma once
#ifndef RANDOM_H
#define RANDOM_H
#include <limits>
#include <random>

#include "numbers.hpp"
#include "result.hpp"

/**
 * @brief 获取共享的随机数生成器（线程不安全）
 */
inline std::mt19937 &get_generator() {
  static std::mt19937 gen(std::random_device{}());
  return gen;
}

/**
 * @brief 在[l,r)范围内生成随机数
 *
 * @param l 下限
 * @param r 上限
 * @return Result<float> 随机数；区间非法时返回 MathsError::InvalidRange
 */
inline Result<float> random(float l, float r) {
  if (l >= r) {
    return std::unexpected(MathsError::InvalidRange);
  }
  std::uniform_real_distribution<float> dis(l, r);
  return dis(get_generator());
}

/**
 * @brief 在[l,r)范围内生成随机数
 *
 * @param l 下限
 * @param r 上限
 * @return Result<double> 随机数；区间非法时返回 MathsError::InvalidRange
 */
inline Result<double> random(double l, double r) {
  if (l >= r) {
    return std::unexpected(MathsError::InvalidRange);
  }
  std::uniform_real_distribution<double> dis(l, r);
  return dis(get_generator());
}

/**
 * @brief 在[l,r)范围内生成随机数
 *
 * @param l 下限
 * @param r 上限
 * @return Result<long double> 随机数；区间非法时返回 MathsError::InvalidRange
 */
inline Result<long double> random(long double l, long double r) {
  if (l >= r) {
    return std::unexpected(MathsError::InvalidRange);
  }
  std::uniform_real_distribution<long double> dis(l, r);
  return dis(get_generator());
}

/**
 * @brief 在闭区间[l,r]内生成随机整数
 *
 * @tparam T 随机数类型
 * @param l 下限
 * @param r 上限
 * @return Result<T> 随机数；区间非法时返回 MathsError::InvalidRange
 */
template <typename T> inline Result<T> random(const T &l, const T &r) {
  if (l > r) {
    return std::unexpected(MathsError::InvalidRange);
  }
  std::uniform_int_distribution<T> dis(l, r);
  return dis(get_generator());
}

namespace maths_detail {

// 尝试把 Integer 折成 long long；超出可表示范围时返回 false。
// 不能直接用 getVal()：|value| == 2^63 时它内部会触发有符号溢出。
inline bool tryToLongLong(const Integer &value, long long &out) {
  constexpr unsigned long long longLongMax = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
  const unsigned long long magnitude = value.getAbs();

  if (value.isNegative()) {
    if (magnitude > longLongMax + 1ULL) {
      return false;
    }
    out = magnitude == longLongMax + 1ULL ? std::numeric_limits<long long>::min() : -static_cast<long long>(magnitude);
    return true;
  }

  if (magnitude > longLongMax) {
    return false;
  }
  out = static_cast<long long>(magnitude);
  return true;
}

} // namespace maths_detail

/**
 * @brief 在闭区间[l, r]内均匀生成随机整数
 *
 * 不能复用上面的模板：std::uniform_int_distribution 要求模板参数是内建整型，
 * 而 Integer 是用户定义类型。这里改用 long long 取分布再包回 Integer，
 * 因此区间必须落在 long long 可表示范围内，否则返回错误码。
 *
 * @return Result<Integer> 随机数；区间非法或超出 long long 范围时返回 MathsError::InvalidRange
 */
inline Result<Integer> random(const Integer &l, const Integer &r) {
  if (l > r) {
    return std::unexpected(MathsError::InvalidRange);
  }

  long long low = 0;
  long long high = 0;
  if (!maths_detail::tryToLongLong(l, low) || !maths_detail::tryToLongLong(r, high)) {
    return std::unexpected(MathsError::InvalidRange);
  }

  std::uniform_int_distribution<long long> dis(low, high);
  return Integer(dis(get_generator()));
}

#endif // RANDOM_H
