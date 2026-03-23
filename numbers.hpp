#ifndef NUMBERS_HPP
#define NUMBERS_HPP
#pragma once

#include <iostream>
#include <numeric>

class Fraction;
class Integer {
  using ll = long long;
  using ull = unsigned long long;

public:
  Integer() = default;
  Integer(ll num) : val(static_cast<ull>(num >= 0 ? num : -num)), sign(num < 0) {}
  Integer(ull num) : val(num), sign(false) {}
  Integer(int num) : val(static_cast<ull>(num >= 0 ? num : -num)), sign(num < 0) {}
  Integer(unsigned int num) : val(static_cast<ull>(num)), sign(false) {}
  template <typename T>
  Integer(T num, typename std::enable_if<std::is_integral<T>::value>::type * = nullptr)
      : val(static_cast<ull>(num >= 0 ? num : -num)), sign(num < 0) {}

  operator ll() const { return sign ? -static_cast<ll>(val) : static_cast<ll>(val); }
  Integer operator-() const { return Integer(sign ? static_cast<ll>(val) : -static_cast<ll>(val)); }

  Integer operator+(const Integer &rhs) const {
    ll lhsVal = sign ? -static_cast<ll>(val) : static_cast<ll>(val);
    ll rhsVal = rhs.sign ? -static_cast<ll>(rhs.val) : static_cast<ll>(rhs.val);
    return Integer(lhsVal + rhsVal);
  }

  Integer operator-(const Integer &rhs) const {
    ll lhsVal = sign ? -static_cast<ll>(val) : static_cast<ll>(val);
    ll rhsVal = rhs.sign ? -static_cast<ll>(rhs.val) : static_cast<ll>(rhs.val);
    return Integer(lhsVal - rhsVal);
  }

  Integer operator*(const Integer &rhs) const {
    ll lhsVal = sign ? -static_cast<ll>(val) : static_cast<ll>(val);
    ll rhsVal = rhs.sign ? -static_cast<ll>(rhs.val) : static_cast<ll>(rhs.val);
    return Integer(lhsVal * rhsVal);
  }

  Fraction operator/(const Integer &rhs) const;
  Fraction operator^(const Integer &rhs) const;

  Integer &operator+=(const Integer &rhs) {
    *this = *this + rhs;
    return *this;
  }

  Integer &operator-=(const Integer &rhs) {
    *this = *this - rhs;
    return *this;
  }

  Integer &operator*=(const Integer &rhs) {
    *this = *this * rhs;
    return *this;
  }

  Integer &operator^=(const Integer &rhs);

  friend std::ostream &operator<<(std::ostream &os, const Integer &integer) {
    long long value = static_cast<long long>(integer);
    return os << value;
  }

private:
  ull pow(ull a, ull x) const {
    if (!x)
      return 1;
    ull res = pow(a, x / 2);
    return x % 2 ? res * res * a : res * res;
  }

  ull val;
  bool sign;
};

class Fraction {
  using ll = long long;
  using ull = unsigned long long;

public:
  Fraction(ll num_ = 1, ll den_ = 1) {
    if (den_ == 0)
      den_ = 1;
    ll g = std::gcd(num_ >= 0 ? num_ : -num_, den_ >= 0 ? den_ : -den_);
    num = static_cast<ull>((num_ >= 0 ? num_ : -num_) / g);
    den = static_cast<ull>((den_ >= 0 ? den_ : -den_) / g);
    sign = (num_ < 0) ^ (den_ < 0);
  }

  operator double() const { return static_cast<double>(num) / static_cast<double>(den) * (sign ? -1 : 1); }
  operator Integer() const { return Integer(static_cast<ll>(num) / static_cast<ll>(den) * (sign ? -1 : 1)); }

  Fraction operator+(const Fraction &rhs) const {
    ll lhsNum = sign ? -static_cast<ll>(num) : static_cast<ll>(num);
    ll rhsNum = rhs.sign ? -static_cast<ll>(rhs.num) : static_cast<ll>(rhs.num);
    ll newNum = lhsNum * static_cast<ll>(rhs.den) + rhsNum * static_cast<ll>(den);
    ll newDen = static_cast<ll>(den) * static_cast<ll>(rhs.den);
    return Fraction(newNum, newDen);
  }

  Fraction operator+(const Integer &rhs) const { return *this + Fraction(static_cast<ll>(rhs), 1); }

  Fraction operator-(const Fraction &rhs) const {
    ll lhsNum = sign ? -static_cast<ll>(num) : static_cast<ll>(num);
    ll rhsNum = rhs.sign ? -static_cast<ll>(rhs.num) : static_cast<ll>(rhs.num);
    ll newNum = lhsNum * static_cast<ll>(rhs.den) - rhsNum * static_cast<ll>(den);
    ll newDen = static_cast<ll>(den) * static_cast<ll>(rhs.den);
    return Fraction(newNum, newDen);
  }

  Fraction operator-(const Integer &rhs) const { return *this - Fraction(static_cast<ll>(rhs), 1); }

  Fraction operator*(const Fraction &rhs) const {
    ll lhsNum = sign ? -static_cast<ll>(num) : static_cast<ll>(num);
    ll rhsNum = rhs.sign ? -static_cast<ll>(rhs.num) : static_cast<ll>(rhs.num);
    ll newNum = lhsNum * rhsNum;
    ll newDen = static_cast<ll>(den) * static_cast<ll>(rhs.den);
    return Fraction(newNum, newDen);
  }

  Fraction operator*(const Integer &rhs) const { return *this * Fraction(static_cast<ll>(rhs), 1); }

  Fraction operator/(const Fraction &rhs) const {
    ll lhsNum = sign ? -static_cast<ll>(num) : static_cast<ll>(num);
    ll rhsNum = rhs.sign ? -static_cast<ll>(rhs.num) : static_cast<ll>(rhs.num);
    ll newNum = lhsNum * static_cast<ll>(rhs.den);
    ll newDen = static_cast<ll>(den) * rhsNum;
    return Fraction(newNum, newDen);
  }

  Fraction operator/(const Integer &rhs) const { return *this / Fraction(static_cast<ll>(rhs), 1); }

  Fraction operator^(const Integer &rhs) const {
    ll expVal = static_cast<ll>(rhs);
    if (expVal == 0)
      return Fraction(1, 1);

    if (expVal < 0) {
      Fraction baseInverse(sign ? -1 : 1, 1);
      baseInverse.num = den;
      baseInverse.den = num;
      baseInverse.sign = sign;
      return baseInverse ^ Integer(-expVal);
    }

    ull newNum = pow(num, static_cast<ull>(expVal));
    ull newDen = pow(den, static_cast<ull>(expVal));
    bool newSign = sign && (expVal % 2 != 0);

    Fraction res;
    res.num = newNum;
    res.den = newDen;
    res.sign = newSign;
    return res;
  }

  Fraction &operator+=(const Fraction &rhs) {
    *this = *this + rhs;
    return *this;
  }

  Fraction &operator+=(const Integer &rhs) {
    *this = *this + rhs;
    return *this;
  }

  Fraction &operator-=(const Fraction &rhs) {
    *this = *this - rhs;
    return *this;
  }

  Fraction &operator-=(const Integer &rhs) {
    *this = *this - rhs;
    return *this;
  }

  Fraction &operator*=(const Fraction &rhs) {
    *this = *this * rhs;
    return *this;
  }

  Fraction &operator*=(const Integer &rhs) {
    *this = *this * rhs;
    return *this;
  }

  Fraction &operator/=(const Fraction &rhs) {
    *this = *this / rhs;
    return *this;
  }

  Fraction &operator/=(const Integer &rhs) {
    *this = *this / rhs;
    return *this;
  }

  Fraction &operator^=(const Integer &rhs) {
    *this = *this ^ rhs;
    return *this;
  }

  friend std::ostream &operator<<(std::ostream &os, const Fraction &fraction) {
    long long num_val = fraction.sign ? -static_cast<long long>(fraction.num) : static_cast<long long>(fraction.num);
    long long den_val = static_cast<long long>(fraction.den);

    if (den_val == 1) {
      return os << num_val;
    } else {
      return os << num_val << "/" << den_val;
    }
  }

  ull getNum() const { return num; }
  ull getDen() const { return den; }
  bool getSign() const { return sign; }

private:
  ull pow(ull base, ull exp) const {
    if (exp == 0)
      return 1;
    ull res = pow(base, exp / 2);
    return exp % 2 ? res * res * base : res * res;
  }

  ull num = 0, den = 1;
  bool sign = false;
};

inline Fraction Integer::operator/(const Integer &rhs) const {
  ll lhsVal = sign ? -static_cast<ll>(val) : static_cast<ll>(val);
  ll rhsVal = rhs.sign ? -static_cast<ll>(rhs.val) : static_cast<ll>(rhs.val);
  return Fraction(lhsVal, rhsVal);
}

inline Fraction Integer::operator^(const Integer &rhs) const {
  ll rhsVal = static_cast<ll>(rhs);
  if (rhsVal < 0) {
    if (sign || val == 0)
      throw "Division by zero";
    ull result = pow(val, static_cast<ull>(-rhsVal));
    return Fraction(1, static_cast<ll>(result));
  }
  ull result = pow(val, static_cast<ull>(rhsVal));
  return Fraction(sign && (rhsVal % 2 != 0) ? -static_cast<ll>(result) : static_cast<ll>(result));
}

Integer &Integer::operator^=(const Integer &rhs) {
  *this = *this ^ rhs;
  return *this;
}

#endif // NUMBERS_HPP