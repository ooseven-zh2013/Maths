#pragma once
#ifndef MATHS_ERROR_HPP
#define MATHS_ERROR_HPP

// 全库统一的错误分类。
// 异常路径（MathsException）与返回值路径（Result / Expr）共用这一套错误码，
// 因此调用方的错误处理逻辑只需写一次。

#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

enum class MathsError {
  // 算术
  DivisionByZero,
  ZeroDenominator,
  ZeroToNegativePower,
  NonIntegralPowerResult,
  // 代数
  ExponentOverflow,
  NotAMonomial,
  // 解析
  InvalidExpression,
  InvalidName,
  // 参数
  InvalidRange,
  // 作用域
  UndefinedVariable,
};

inline std::string_view describe(MathsError error) {
  switch (error) {
  case MathsError::DivisionByZero:
    return "除数不能为零";
  case MathsError::ZeroDenominator:
    return "分母不能为零";
  case MathsError::ZeroToNegativePower:
    return "0 的负数次幂无定义";
  case MathsError::NonIntegralPowerResult:
    return "指数运算结果不是整数";
  case MathsError::ExponentOverflow:
    return "变量指数超出可表示范围";
  case MathsError::NotAMonomial:
    return "多项式无法化简为单项式";
  case MathsError::InvalidExpression:
    return "不支持的表达式";
  case MathsError::InvalidName:
    return "非法的名字";
  case MathsError::InvalidRange:
    return "区间参数非法";
  case MathsError::UndefinedVariable:
    return "变量未定义";
  }
  return "未知错误";
}

inline std::ostream &operator<<(std::ostream &os, MathsError error) { return os << describe(error); }

// 运算符等无法返回 Result 的路径继续抛异常，但统一成这一个类型，
// 调用方 catch (const MathsException &) 即可接住全库所有错误。
class MathsException : public std::runtime_error {
public:
  explicit MathsException(MathsError error) : std::runtime_error(std::string(describe(error))), code_(error) {}

  MathsError code() const noexcept { return code_; }

private:
  MathsError code_;
};

#endif // MATHS_ERROR_HPP
