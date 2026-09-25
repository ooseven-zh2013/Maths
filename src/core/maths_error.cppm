export module maths.error;

import std;

export namespace maths {

// 全库统一的错误分类。
// 异常路径（MathsException）与返回值路径（Result / Expr）共用这一套错误码，
// 因此调用方的错误处理逻辑只需写一次。

enum class MathsError {
  // 算术
  DivisionByZero,
  ZeroDenominator,
  ZeroToNegativePower,
  NonIntegralPowerResult,
  // 代数
  ExponentOverflow,
  NotAMonomial,
  NotAPolynomial,
  // 解析
  InvalidExpression,
  InvalidName,
  // 参数
  InvalidRange,
  // 精确数值的表示上限：Fraction 对外以 long long 取值，
  // 超过一定量级会翻成负数，迭代类算法（Sturm 序列、结式）容易顶穿，
  // 因此主动报错而不是静默产出错误结果
  NumericOverflow,
  // 作用域
  UndefinedVariable,
  NotAnAssignment,
  CircularReference,
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
  case MathsError::NotAPolynomial:
    return "分式无法化简为多项式";
  case MathsError::InvalidExpression:
    return "不支持的表达式";
  case MathsError::InvalidName:
    return "非法的名字";
  case MathsError::InvalidRange:
    return "区间参数非法";
  case MathsError::NumericOverflow:
    return "数值超出可精确表示的范围";
  case MathsError::UndefinedVariable:
    return "变量未定义";
  case MathsError::NotAnAssignment:
    return "右边含被赋值的变量本身，那是方程不是赋值";
  case MathsError::CircularReference:
    return "该赋值会形成循环引用";
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

} // namespace maths
