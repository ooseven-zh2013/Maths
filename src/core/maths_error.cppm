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
  // 「降一阶」失败：值本身不是更低一层的类型
  NotAnInteger,
  NotARational,
  // 负数开偶次根：实数域上无定义，和「区间参数非法」不是一回事，单列一个码，
  // 免得用户看到「区间参数非法」以为是语法问题
  NegativeEvenRoot,
  // 被开方数是完全平方（如 √(x²)、√((x+1)²)）：开方结果是 |x|、|x+1|，
  // 不是单值的代数函数。本库不引入绝对值节点，因此明确拒收而不是猜一个分支
  RadicandIsSquare,
  // 多个根号落在同一平方类（如 √x 与 √(4x)，后者等于 2√x）：这时它们不是独立的
  // 生成元，必须先折叠成最简根式。本库不做最简根式化，所以明确拒收
  RadicandsNotIndependent,
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
  case MathsError::NotAnInteger:
    return "分数不是整数";
  case MathsError::NotARational:
    return "实代数数不是有理数";
  case MathsError::NegativeEvenRoot:
    return "负数不能开偶次根";
  case MathsError::RadicandIsSquare:
    return "被开方数是完全平方，开方结果带绝对值（如 √(x²) = |x|），本库不引入 |x|";
  case MathsError::RadicandsNotIndependent:
    return "多个根号落在同一平方类（如 √x 与 √(4x)），请先化成最简根式再写";
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
