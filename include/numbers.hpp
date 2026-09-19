#pragma once
#ifndef NUMBERS_HPP
#define NUMBERS_HPP
#include <compare>
#include <concepts>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

class Integer; // 前向声明

class Fraction {
public:
  using ll = long long;
  using ull = unsigned long long;

#if defined(__SIZEOF_INT128__)
  // 交叉相乘等中间计算使用的更宽类型（GCC/Clang 扩展），避免比较时溢出。
  // __extension__ 用于抑制 -Wpedantic 对扩展整型的告警。
  __extension__ using wide = unsigned __int128;
#else
  using wide = ull;
#endif

  // ==================== 构造函数 ====================

  Fraction() {
    a = 0;
    b = 1;
    sign = false;
  }

  Fraction(std::string_view sv) {
    if (sv.size() >= 6 && sv.substr(0, 6) == "\\frac{") {
      size_t numEnd = findMatchingBrace(sv, 5);
      if (numEnd == std::string_view::npos || numEnd + 1 >= sv.size() || sv[numEnd + 1] != '{') {
        throw std::invalid_argument("不支持的表达式");
      }
      size_t denEnd = findMatchingBrace(sv, numEnd + 1);
      if (denEnd == std::string_view::npos) {
        throw std::invalid_argument("不支持的表达式");
      }
      Fraction fracResult = Fraction(sv.substr(6, numEnd - 6)) / Fraction(sv.substr(numEnd + 2, denEnd - numEnd - 2));
      if (denEnd == sv.size() - 1) {
        *this = fracResult;
      } else {
        if (sv[denEnd + 1] != '/') {
          throw std::invalid_argument("不支持的表达式");
        }
        *this = fracResult / Fraction(sv.substr(denEnd + 2));
      }
    } else {
      size_t split = sv.rfind('/');
      if (split != std::string_view::npos) {
        Fraction _a = Fraction(sv.substr(0, split));
        Fraction _b = Fraction(sv.substr(split + 1));
        *this = _a / _b;
      } else {
        *this = Fraction(std::stoll(std::string(sv)));
      }
    }
  }

  // 统一整型构造：单一模板，避免 (ll,ll) 与 (ull,ull) 双重载在 int 字面量上产生歧义
  template <std::integral T, std::integral U = T> Fraction(T _a, U _b = U{1}) { setParts(_a, _b); }

  // ==================== 赋值运算符 ====================

  Fraction &operator=(std::pair<ll, ll> _val) {
    a = abs(_val.first);
    b = abs(_val.second);
    sign = (_val.first < 0) ^ (_val.second < 0);
    simplify();
    return *this;
  }

  Fraction &operator=(std::pair<ull, ull> _val) {
    a = _val.first;
    b = _val.second;
    sign = false;
    simplify();
    return *this;
  }

  Fraction &operator=(std::pair<ll, ull> _val) {
    a = abs(_val.first);
    b = _val.second;
    sign = _val.first < 0;
    simplify();
    return *this;
  }

  Fraction &operator=(std::pair<ull, ll> _val) {
    a = _val.first;
    b = abs(_val.second);
    sign = _val.second < 0;
    simplify();
    return *this;
  }

  // ==================== 类型转换 ====================

  operator double() const {
    double result = static_cast<double>(a) / static_cast<double>(b);
    return sign ? -result : result;
  }

  // ==================== 值获取方法 ====================

  inline ll getNumerator() const { return sign ? -static_cast<ll>(a) : static_cast<ll>(a); }
  inline ll getDenominator() const { return static_cast<ll>(b); }
  inline bool isNegative() const { return sign; }

  // ==================== 比较运算符 ====================

  std::strong_ordering operator<=>(const Fraction &_val) const {
    // 交叉相乘比较：a/b <=> c/d 等价于 a*d <=> c*b
    // 中间结果提升到 wide，避免大数比较时溢出
    const wide left = static_cast<wide>(a) * static_cast<wide>(_val.b);
    const wide right = static_cast<wide>(_val.a) * static_cast<wide>(b);

    if (sign != _val.isNegative()) {
      // 一正一负
      return _val.isNegative() <=> sign;
    }

    if (sign) {
      // 同负：反转比较结果
      return threeWay(right, left);
    }
    // 同正
    return threeWay(left, right);
  }

  std::strong_ordering operator<=>(ll _val) const { return *this <=> Fraction(_val, 1LL); }

  bool operator==(const Fraction &_val) const { return (*this <=> _val) == 0; }
  bool operator==(ll _val) const { return (*this <=> _val) == 0; }

  // ==================== 算术运算符 ====================

  Fraction operator-() const {
    Fraction res(*this);
    if (res.a != 0) { // 零取反后仍是规范零，避免输出 "-0"
      res.sign ^= 1;
    }
    return res;
  }

  Fraction operator+(const Fraction &_val) const {
    // a/b + c/d = (a*d + c*b) / (b*d)
    ll num1 = getNumerator();
    ll den1 = getDenominator();
    ll num2 = _val.getNumerator();
    ll den2 = _val.getDenominator();

    ll newNum = num1 * den2 + num2 * den1;
    ll newDen = den1 * den2;

    return Fraction(newNum, newDen);
  }

  Fraction operator-(const Fraction &_val) const { return *this + (-_val); }

  Fraction operator*(const Fraction &_val) const {
    // a/b * c/d = (a*c) / (b*d)
    ll num1 = getNumerator();
    ll den1 = getDenominator();
    ll num2 = _val.getNumerator();
    ll den2 = _val.getDenominator();

    return Fraction(num1 * num2, den1 * den2);
  }

  Fraction operator/(const Fraction &_val) const {
    if (_val.a == 0) {
      throw std::domain_error("除数不能为零");
    }
    // a/b ÷ c/d = (a*d) / (b*c)
    ll num1 = getNumerator();
    ll den1 = getDenominator();
    ll num2 = _val.getNumerator();
    ll den2 = _val.getDenominator();

    return Fraction(num1 * den2, den1 * num2);
  }

  // ==================== 复合赋值运算符 ====================

  Fraction &operator+=(const Fraction &_val) {
    *this = *this + _val;
    return *this;
  }

  Fraction &operator-=(const Fraction &_val) {
    *this = *this - _val;
    return *this;
  }

  Fraction &operator*=(const Fraction &_val) {
    *this = *this * _val;
    return *this;
  }

  Fraction &operator/=(const Fraction &_val) {
    *this = *this / _val;
    return *this;
  }

  Fraction &operator^=(const Integer &exp) {
    *this = this->pow(exp);
    return *this;
  }

  // ==================== 幂运算 ====================

  // 幂运算：指数为 Integer，返回 Fraction（实现在 Integer 类之后）
  Fraction pow(const Integer &exp) const;

  // ==================== 友元函数 ====================

  friend std::ostream &operator<<(std::ostream &os, const Fraction &frac) {
    if (frac.sign) {
      os << "-";
    }
    if (frac.b == 1) {
      os << frac.a; // 整数形式
    } else {
      os << frac.a << "/" << frac.b; // 分数形式
    }
    return os;
  }

  friend std::istream &operator>>(std::istream &is, Fraction &frac) {
    ll numerator, denominator;
    char slash;
    is >> numerator;
    if (!is) {
      return is;
    }
    if (is.peek() == '/') {
      is >> slash >> denominator;
    } else {
      denominator = 1;
    }
    frac = std::make_pair(numerator, denominator);
    return is;
  }

private:
  // ==================== 成员变量 ====================

  ull a, b;
  bool sign;

  // ==================== 私有辅助方法 ====================

  // 三路比较（不依赖扩展整型的 operator<=>）
  static inline std::strong_ordering threeWay(wide x, wide y) {
    if (x < y) {
      return std::strong_ordering::less;
    }
    if (x > y) {
      return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
  }

  // 判定整型是否为负（无符号恒为非负）
  template <std::integral T> static constexpr bool isNegativeValue(T _val) {
    if constexpr (std::is_signed_v<T>) {
      return _val < T{0};
    } else {
      return false;
    }
  }

  // 取整型绝对值：走无符号回绕，规避最小值取负的未定义行为
  template <std::integral T> static constexpr ull magnitude(T _val) {
    if constexpr (std::is_signed_v<T>) {
      if (_val < T{0}) {
        using U = std::make_unsigned_t<T>;
        return static_cast<ull>(static_cast<U>(0) - static_cast<U>(_val));
      }
    }
    return static_cast<ull>(_val);
  }

  // 由整型对设置数值（符号-绝对值表示）
  template <std::integral T, std::integral U> void setParts(T _a, U _b) {
    a = magnitude(_a);
    b = magnitude(_b);
    sign = isNegativeValue(_a) ^ isNegativeValue(_b);
    simplify();
  }

  // 计算最大公约数
  static inline ull gcd(ull x, ull y) {
    while (y != 0) {
      ull temp = y;
      y = x % y;
      x = temp;
    }
    return x;
  }

  // 约分
  void simplify() {
    if (b == 0) {
      throw std::domain_error("分母不能为零");
    }
    if (a == 0) {
      b = 1;
      sign = false;
      return;
    }
    ull divisor = gcd(a, b);
    a /= divisor;
    b /= divisor;
  }

  // 找匹配的右花括号（从 start 位置的 { 开始，按嵌套深度匹配）
  static size_t findMatchingBrace(std::string_view sv, size_t start) {
    int depth = 0;
    for (size_t i = start; i < sv.size(); ++i) {
      if (sv[i] == '{')
        ++depth;
      else if (sv[i] == '}') {
        --depth;
        if (depth == 0)
          return i;
      }
    }
    return std::string_view::npos;
  }

  // 取 ll 绝对值：走无符号回绕，规避 LLONG_MIN 取负的未定义行为
  static inline ull abs(ll _val) {
    return _val < 0 ? static_cast<ull>(0) - static_cast<ull>(_val) : static_cast<ull>(_val);
  }
};
class Integer {
public:
  using ull = unsigned long long;
  using ll = long long;

  // ==================== 构造函数 ====================

  Integer() {
    val = 0;
    sign = false;
  }

  Integer(ll _val) { *this = _val; }

  Integer(ull _val) { *this = _val; }

  // ==================== 赋值运算符 ====================

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

  // ==================== 类型转换 ====================

  operator ull() const { return getAbs(); }

  operator ll() const { return getVal(); }

  // ==================== 值获取方法 ====================

  inline ll getSign() const { return sign ? -1 : 1; }
  inline bool isNegative() const { return sign; }
  inline ull getAbs() const { return val; }
  inline ll getVal() const { return static_cast<ll>(getAbs()) * getSign(); }

  // ==================== 比较运算符 ====================

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
    if (sign) {
      // 同负：绝对值大的反而小
      return _val.getAbs() <=> val;
    }
    // 同正
    return val <=> _val.getAbs();
  }

  bool operator==(ll _val) const { return (*this <=> _val) == 0; }
  bool operator==(ull _val) const { return (*this <=> _val) == 0; }
  bool operator==(const Integer &_val) const { return (*this <=> _val) == 0; }

  // ==================== 算术运算符 ====================

  Integer operator-() const {
    Integer res(*this);
    if (res.val != 0) { // 零取反后仍是规范零，避免输出 "-0"
      res.sign ^= 1;
    }
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

  Integer operator*(const Integer &_val) const {
    Integer res;
    res.val = val * _val.getAbs();
    res.sign = sign ^ _val.isNegative();
    return res;
  }

  Integer operator/(const Integer &_val) const {
    if (_val.val == 0) {
      throw std::domain_error("除数不能为零");
    }
    Integer res;
    res.val = val / _val.getAbs();
    res.sign = sign ^ _val.isNegative();
    return res;
  }

  Integer operator%(const Integer &_val) const {
    if (_val.val == 0) {
      throw std::domain_error("除数不能为零");
    }
    Integer res;
    res.val = val % _val.getAbs();
    res.sign = sign; // 余数符号与被除数相同
    return res;
  }

  // ==================== 复合赋值运算符 ====================

  Integer &operator+=(const Integer &_val) {
    *this = *this + _val;
    return *this;
  }

  Integer &operator-=(const Integer &_val) {
    *this = *this - _val;
    return *this;
  }

  Integer &operator*=(const Integer &_val) {
    *this = *this * _val;
    return *this;
  }

  Integer &operator/=(const Integer &_val) {
    *this = *this / _val;
    return *this;
  }

  Integer &operator%=(const Integer &_val) {
    *this = *this % _val;
    return *this;
  }

  Integer &operator^=(const Integer &exp) {
    Fraction result = this->pow(exp);
    if (result.getDenominator() != 1) {
      throw std::domain_error("指数运算结果不是整数");
    }
    *this = Integer(result.getNumerator());
    return *this;
  }

  // ==================== 自增自减运算符 ====================

  Integer &operator++() {
    *this += Integer(1LL);
    return *this;
  }

  Integer operator++(int) {
    Integer temp(*this);
    ++(*this);
    return temp;
  }

  Integer &operator--() {
    *this -= Integer(1LL);
    return *this;
  }

  Integer operator--(int) {
    Integer temp(*this);
    --(*this);
    return temp;
  }

  // ==================== 幂运算 ====================

  // 幂运算：支持负整数指数，返回 Fraction
  Fraction pow(const Integer &exp) const {
    if (exp.val == 0) {
      // 任何数的0次幂为1
      return Fraction(1LL, 1LL);
    }

    if (val == 0) {
      if (exp.sign) {
        // 0的负数次幂无定义
        throw std::domain_error("0的负数次幂无定义");
      }
      // 0的正数次幂为0
      return Fraction(0LL, 1LL);
    }

    // 快速幂计算 |base|^|exp|
    ull base = val;
    ull exponent = exp.getAbs();
    ull result = 1;

    while (exponent > 0) {
      if (exponent & 1) {
        result *= base;
      }
      base *= base;
      exponent >>= 1;
    }

    // 确定结果的符号和分数形式
    bool resultSign = sign && (exp.getAbs() % 2 == 1); // 只有奇数次幂才保留负号

    if (!exp.sign) {
      // 正指数：返回整数
      return Fraction(resultSign ? -static_cast<ll>(result) : static_cast<ll>(result), 1);
    } else {
      // 负指数：返回分数 1/base^exp
      return Fraction(resultSign ? -1LL : 1LL, static_cast<ll>(result));
    }
  }

  // ==================== 友元函数 ====================

  friend std::ostream &operator<<(std::ostream &os, const Integer &num) {
    if (num.sign) {
      os << "-";
    }
    os << num.val;
    return os;
  }

  friend std::istream &operator>>(std::istream &is, Integer &num) {
    ll parsed;
    is >> parsed;
    num = parsed;
    return is;
  }

private:
  // ==================== 成员变量 ====================

  ull val;
  bool sign;

  // ==================== 私有辅助方法 ====================

  // 找匹配的右花括号（从 start 位置的 { 开始，按嵌套深度匹配）
  static size_t findMatchingBrace(std::string_view sv, size_t start) {
    int depth = 0;
    for (size_t i = start; i < sv.size(); ++i) {
      if (sv[i] == '{')
        ++depth;
      else if (sv[i] == '}') {
        --depth;
        if (depth == 0)
          return i;
      }
    }
    return std::string_view::npos;
  }

  // 取 ll 绝对值：走无符号回绕，规避 LLONG_MIN 取负的未定义行为
  static inline ull abs(ll _val) {
    return _val < 0 ? static_cast<ull>(0) - static_cast<ull>(_val) : static_cast<ull>(_val);
  }
}; // INTEGER

// Fraction::pow 的实现（在 Integer 类定义之后）
inline Fraction Fraction::pow(const Integer &exp) const {
  if (exp == 0LL) {
    // 任何数的0次幂为1
    return Fraction(1LL, 1LL);
  }

  if (a == 0) {
    if (exp.isNegative()) {
      // 0的负数次幂无定义
      throw std::domain_error("0的负数次幂无定义");
    }
    // 0的正数次幂为0
    return Fraction(0LL, 1LL);
  }

  // 分别计算分子和分母的幂
  ll baseNum = getNumerator();
  ll baseDen = getDenominator();
  ull absExp = exp.getAbs();

  // 快速幂计算分子的幂
  ull absNum = static_cast<ull>(baseNum < 0 ? -baseNum : baseNum);
  ull resultNum = 1;
  ull tempNum = absNum;
  while (absExp > 0) {
    if (absExp & 1) {
      resultNum *= tempNum;
    }
    tempNum *= tempNum;
    absExp >>= 1;
  }

  // 快速幂计算分母的幂
  ull resultDen = 1;
  ull tempDen = static_cast<ull>(baseDen);
  absExp = exp.getAbs(); // 重置指数
  while (absExp > 0) {
    if (absExp & 1) {
      resultDen *= tempDen;
    }
    tempDen *= tempDen;
    absExp >>= 1;
  }

  // 确定符号：只有当底数为负且指数为奇数时结果为负
  bool resultSign = sign && (exp.getAbs() % 2 == 1);

  if (!exp.isNegative()) {
    // 正指数：(num/den)^exp = num^exp / den^exp
    ll finalNum = resultSign ? -static_cast<ll>(resultNum) : static_cast<ll>(resultNum);
    return Fraction(finalNum, static_cast<ll>(resultDen));
  } else {
    // 负指数：(num/den)^(-exp) = den^exp / num^exp
    ll finalNum = resultSign ? -static_cast<ll>(resultDen) : static_cast<ll>(resultDen);
    return Fraction(finalNum, static_cast<ll>(resultNum));
  }
}

// ==================== 幂运算符 ^ ====================

inline Fraction operator^(const Integer &base, const Integer &exp) { return base.pow(exp); }

inline Fraction operator^(const Fraction &base, const Integer &exp) { return base.pow(exp); }

#endif // NUMBERS_HPP