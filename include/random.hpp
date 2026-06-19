#ifndef RANDOM_H
#define RANDOM_H
#pragma once

#include <random>
#include <stdexcept>

/**
 * @brief 在[l,r)范围内生成随机数
 *
 * @param l 下限
 * @param r 上限
 * @return float 随机数
 */
inline float random(float l, float r) {
  if (l >= r) {
    throw std::invalid_argument("下限l必须小于上限r");
  }

  std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_real_distribution<float> dis(l, r);
  return dis(gen);
}

/**
 * @brief 在[l,r)范围内生成随机数
 *
 * @param l 下限
 * @param r 上限
 * @return double 随机数
 */
inline double random(double l, double r) {
  if (l >= r) {
    throw std::invalid_argument("下限l必须小于上限r");
  }

  std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_real_distribution<double> dis(l, r);
  return dis(gen);
}

/**
 * @brief 在[l,r)范围内生成随机数
 *
 * @param l 下限
 * @param r 上限
 * @return long double 随机数
 */
inline long double random(long double l, long double r) {
  if (l >= r) {
    throw std::invalid_argument("下限l必须小于上限r");
  }

  std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_real_distribution<long double> dis(l, r);
  return dis(gen);
}

/**
 * @brief 在区间[l,r]生成随机数
 *
 * @tparam T 随机数类型
 * @param l 下限
 * @param r 上限
 * @return T 随机数
 */
template <typename T> inline T random(const T& l,const T& r) {
  if (l > r) {
    throw std::invalid_argument("下限l必须小于等于上限r");
  }

  std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<T> dis(l, r);
  return dis(gen);
}

#endif // RANDOM_H