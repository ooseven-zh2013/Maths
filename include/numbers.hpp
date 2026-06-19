#ifndef NUMBERS_HPP
#define NUMBERS_HPP
#pragma once

#include <compare>

class Integer {
public:
  using ull = unsigned long long;
  using ll = long long;

  // 初始化

  Integer() {
    val = 0;
    sign = false;
  }

  Integer(ll _val) { *this = _val; }

  Integer(ull _val) { *this = _val; }

  // 值获取

  inline ll getSign() const { return sign ? -1 : 1; }
  inline bool isNegative() const { return sign; }
  inline ull getAbs() const { return val; }
  inline ll getVal() const { return static_cast<ll>(getAbs()) * getSign(); }

  operator ull() const { return getAbs(); }

  operator ll() const { return getVal(); }

  // 赋值操作

  Integer &operator=(ll _val) {
    val = abs(_val);
    sign = _val < 0;
    return *this;
  }

  Integer &operator=(ull _val) {
    val = _val;
    sign = false;
    return *this;
  }

  Integer &operator=(const Integer &_val) {
    val = _val.getAbs();
    sign = _val.getSign();
    return *this;
  }

  // 比较操作

  std::strong_ordering operator<=>(ll _val) const {
    if (sign != (_val < 0)) {
      // 一正一负
      return (_val < 0) <=> sign;
    }
    if (sign) {
      // 同负
      return abs(_val) <=> val;
    } else {
      // 同正
      return val <=> abs(_val);
    }
  }
  std::strong_ordering operator<=>(ull _val) const {
    if (sign) {
      // 左负右正
      return std::strong_ordering::less;
    }
    // 同正
    return val <=> _val;
  }
  std::strong_ordering operator<=>(const Integer &_val) const {
    if (sign != _val.isNegative()) {
      // 一正一负
      return _val.isNegative() <=> sign;
    }
    return val <=> _val.getAbs();
  }

  bool operator==(ll _val) const { return (*this <=> _val) == 0; }
  bool operator==(ull _val) const { return (*this <=> _val) == 0; }
  bool operator==(const Integer &_val) const { return (*this <=> _val) == 0; }

  // 算术操作

  Integer operator-() const {
    Integer res(*this);
    res.sign ^= 1;
    return res;
  }

  Integer operator+(const Integer &_val) const {
    Integer res;
    if (sign == _val.isNegative()) {
      // 同号
      res.val = val + _val.getAbs();
      res.sign = sign;
    } else {
      // 异号
      res.val = val >= _val.getAbs() ? val - _val.getAbs() : _val.getAbs() - val;
      res.sign = val >= _val.getAbs() ? sign : _val.isNegative();
    }
    return res;
  }

  Integer operator-(const Integer &_val) const { return *this + (-_val); }

  // TODO 乘除幂（幂用快速幂（返回Fraction），除返回Fraction）

private:
  ull val;
  bool sign;
  static inline ull abs(ll _val) { return _val < 0 ? static_cast<ull>(-_val) : static_cast<ull>(_val); }
}; // INTEGER

#endif // NUMBERS_HPP