#include "check.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>

#include <maths/core/result.hpp>

namespace {

// 用于观测短路：记录运算体实际被执行了多少次
struct Tracked {
  int value = 0;

  static inline int additions = 0;
  static inline int divisions = 0;

  static void reset() {
    additions = 0;
    divisions = 0;
  }
};

Tracked operator+(const Tracked &lhs, const Tracked &rhs) {
  ++Tracked::additions;
  return Tracked{lhs.value + rhs.value};
}

Tracked operator-(const Tracked &lhs, const Tracked &rhs) { return Tracked{lhs.value - rhs.value}; }

Tracked operator*(const Tracked &lhs, const Tracked &rhs) { return Tracked{lhs.value * rhs.value}; }

// 除法会失败，因此直接返回 Result<Tracked>
Result<Tracked> operator/(const Tracked &lhs, const Tracked &rhs) {
  ++Tracked::divisions;
  if (rhs.value == 0) {
    return std::unexpected(MathsError::DivisionByZero);
  }
  return Tracked{lhs.value / rhs.value};
}

bool operator==(const Tracked &lhs, const Tracked &rhs) { return lhs.value == rhs.value; }

std::ostream &operator<<(std::ostream &os, const Tracked &value) { return os << value.value; }

} // namespace

int main() {
  std::cout << "=== Result 测试 ===" << '\n';

  using IntResult = Result<int>;

  // 1. 构造与状态
  {
    const IntResult okValue(42);
    CHECK_TRUE(okValue.isOk());
    CHECK_TRUE(!okValue.isErr());
    CHECK_TRUE(static_cast<bool>(okValue));

    const IntResult errValue = IntResult::err(MathsError::DivisionByZero);
    CHECK_TRUE(errValue.isErr());
    CHECK_TRUE(!errValue.isOk());
    CHECK_TRUE(!static_cast<bool>(errValue));
    CHECK_TRUE(errValue.error() == MathsError::DivisionByZero);

    const IntResult fromUnexpected = std::unexpected(MathsError::NotAMonomial);
    CHECK_TRUE(fromUnexpected.isErr());

    CHECK_EQ(IntResult::ok(7).unwrap(), 7);
  }

  // 2. 唯一出口
  {
    IntResult okValue(42);
    CHECK_EQ(okValue.unwrap(), 42);
    CHECK_EQ(okValue.unwrapOr(7), 42);

    IntResult errValue = IntResult::err(MathsError::DivisionByZero);
    CHECK_THROWS(errValue.unwrap(), MathsException);
    CHECK_THROWS(errValue.expect("需要值"), MathsException);
    CHECK_THROWS(okValue.unwrapErr(), std::runtime_error);
    CHECK_EQ(errValue.unwrapOr(7), 7);
    CHECK_EQ(errValue.unwrapOrElse([](MathsError) { return 99; }), 99);
    CHECK_TRUE(errValue.unwrapErr() == MathsError::DivisionByZero);
  }

  // 3. 算术运算
  {
    const IntResult a(10);
    const IntResult b(3);
    CHECK_EQ((a + b).unwrap(), 13);
    CHECK_EQ((a - b).unwrap(), 7);
    CHECK_EQ((a * b).unwrap(), 30);
    CHECK_EQ((-a).unwrap(), -10);
    CHECK_EQ((-IntResult::err(MathsError::DivisionByZero)).error(), MathsError::DivisionByZero);
  }

  // 4. 短路：左侧失败时右侧运算体不应执行
  {
    Tracked::reset();
    const Result<Tracked> broken = Result<Tracked>::err(MathsError::DivisionByZero);
    const Result<Tracked> healthy(Tracked{5});

    const auto sum = broken + healthy;
    CHECK_TRUE(sum.isErr());
    CHECK_EQ(Tracked::additions, 0); // 加法根本没执行
    CHECK_TRUE(sum.error() == MathsError::DivisionByZero);
  }

  // 5. 短路：两侧都失败时保留左侧错误（先左后右）
  {
    Tracked::reset();
    const Result<Tracked> lhsErr = Result<Tracked>::err(MathsError::DivisionByZero);
    const Result<Tracked> rhsErr = Result<Tracked>::err(MathsError::NotAMonomial);

    const auto sum = lhsErr + rhsErr;
    CHECK_TRUE(sum.isErr());
    CHECK_TRUE(sum.error() == MathsError::DivisionByZero);
    CHECK_EQ(Tracked::additions, 0);

    const auto reversed = rhsErr + lhsErr;
    CHECK_TRUE(reversed.error() == MathsError::NotAMonomial); // 仍是"左侧"优先
  }

  // 6. 除法的失败传播，且失败后继续参与运算依然短路
  {
    Tracked::reset();
    const Result<Tracked> numerator(Tracked{10});
    const Result<Tracked> zero(Tracked{0});

    const auto quotient = numerator / zero;
    CHECK_TRUE(quotient.isErr());
    CHECK_TRUE(quotient.error() == MathsError::DivisionByZero);
    CHECK_EQ(Tracked::divisions, 1); // 除法执行了一次并失败

    const auto continued = quotient + Result<Tracked>(Tracked{1});
    CHECK_TRUE(continued.isErr());
    CHECK_EQ(Tracked::additions, 0); // 后续加法未执行

    Tracked::reset();
    const auto success = numerator / Result<Tracked>(Tracked{2});
    CHECK_TRUE(success.isOk());
    CHECK_EQ(success.unwrap().value, 5);
    CHECK_EQ(Tracked::divisions, 1);
  }

  // 7. Result 与裸值混合运算
  {
    const Tracked five{5};
    CHECK_EQ((Result<Tracked>(Tracked{10}) + five).unwrap(), Tracked{15});
    CHECK_EQ((five + Result<Tracked>(Tracked{10})).unwrap(), Tracked{15});
    CHECK_EQ((Result<Tracked>(Tracked{10}) - five).unwrap(), Tracked{5});
    CHECK_EQ((five * Result<Tracked>(Tracked{2})).unwrap(), Tracked{10});
    CHECK_EQ((Result<Tracked>(Tracked{10}) * five).unwrap(), Tracked{50});

    // 裸值侧的错误状态同样短路
    const Result<Tracked> broken = Result<Tracked>::err(MathsError::DivisionByZero);
    const auto combined = five + broken;
    CHECK_TRUE(combined.isErr());
  }

  // 8. 编译期保证：Result 不能隐式变成裸值
  {
    static_assert(!std::is_convertible_v<IntResult, int>);
    static_assert(!std::is_constructible_v<int, IntResult>);
    static_assert(!std::is_convertible_v<Result<Tracked>, Tracked>);
    static_assert(!std::is_constructible_v<Tracked, Result<Tracked>>);
    CHECK_TRUE(true);
  }

  // 9. 组合操作
  {
    const IntResult okValue(21);
    const IntResult errValue = IntResult::err(MathsError::DivisionByZero);

    CHECK_EQ(okValue.map<int>([](int value) { return value * 2; }).unwrap(), 42);
    CHECK_EQ(okValue.andThen<IntResult>([](int value) { return IntResult(value + 1); }).unwrap(), 22);
    CHECK_TRUE(errValue.map<int>([](int value) { return value * 2; }).isErr());
    CHECK_TRUE(errValue.mapErr([](MathsError) { return MathsError::NotAMonomial; }).error() ==
               MathsError::NotAMonomial);
    CHECK_EQ(errValue.orElse([](MathsError) { return IntResult(7); }).unwrap(), 7);
    CHECK_EQ(okValue.orElse([](MathsError) { return IntResult(7); }).unwrap(), 21);

    CHECK_EQ(okValue.ok().value_or(0), 21);
    CHECK_TRUE(!okValue.err().has_value());
    CHECK_TRUE(errValue.err().value_or(MathsError::NotAMonomial) == MathsError::DivisionByZero);
    CHECK_TRUE(!errValue.ok().has_value());
  }

  // 10. 相等比较与流输出
  {
    CHECK_EQ(IntResult(1), IntResult(1));
    CHECK_TRUE(IntResult(1) != IntResult(2));
    CHECK_TRUE(IntResult::err(MathsError::DivisionByZero) == IntResult::err(MathsError::DivisionByZero));
    CHECK_TRUE(IntResult(1) != IntResult::err(MathsError::DivisionByZero));

    std::ostringstream os;
    os << IntResult(42);
    CHECK_EQ(os.str(), std::string("Ok(42)"));

    std::ostringstream os2;
    os2 << IntResult::err(MathsError::DivisionByZero);
    CHECK_EQ(os2.str(), std::string("Err(除数不能为零)"));
  }

  TEST_SUMMARY();
}
