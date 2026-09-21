#include "check.hpp"
#include "random.hpp"
#include <iostream>
#include <limits>

int main() {
  std::cout << "=== random 测试 ===" << '\n';

  // 1. 区间非法：返回错误码而不是抛异常
  {
    CHECK_ERR(random(1.0, 0.0), MathsError::InvalidRange);
    CHECK_ERR(random(1.0, 1.0), MathsError::InvalidRange);
    CHECK_ERR(random(1.0f, 0.0f), MathsError::InvalidRange);
    CHECK_ERR(random(5, 1), MathsError::InvalidRange);
  }

  // 2. 浮点：结果落在 [l, r) 内
  {
    for (int i = 0; i < 100; ++i) {
      const double value = random(-1.0, 1.0).unwrap();
      CHECK_TRUE(value >= -1.0 && value < 1.0);
    }
  }

  // 3. 整数：结果落在闭区间 [l, r] 内，且两端都可能取到
  {
    bool sawLower = false;
    bool sawUpper = false;
    for (int i = 0; i < 200; ++i) {
      const int value = random(1, 3).unwrap();
      CHECK_TRUE(value >= 1 && value <= 3);
      sawLower = sawLower || value == 1;
      sawUpper = sawUpper || value == 3;
    }
    CHECK_TRUE(sawLower);
    CHECK_TRUE(sawUpper);
  }

  // 4. 整数单点区间合法（浮点则会因 l == r 被判非法）
  {
    CHECK_EQ(random(7, 7).unwrap(), 7);
    CHECK_OK(random(0.0, 1.0));
  }

  // 5. Integer：闭区间均匀，两端都能取到
  {
    bool sawLower = false;
    bool sawUpper = false;
    for (int i = 0; i < 200; ++i) {
      const Integer value = random(Integer(-2LL), Integer(2LL)).unwrap();
      CHECK_TRUE(value >= Integer(-2LL) && value <= Integer(2LL));
      sawLower = sawLower || value == Integer(-2LL);
      sawUpper = sawUpper || value == Integer(2LL);
    }
    CHECK_TRUE(sawLower);
    CHECK_TRUE(sawUpper);
  }

  // 6. Integer：单点区间与区间非法
  {
    CHECK_OK(random(Integer(7LL), Integer(7LL)));
    CHECK_EQ(random(Integer(7LL), Integer(7LL)).unwrap(), Integer(7LL));
    CHECK_ERR(random(Integer(5LL), Integer(1LL)), MathsError::InvalidRange);
  }

  // 7. Integer：long long 边界可正常使用
  {
    const Integer minValue(std::numeric_limits<long long>::min());
    const Integer maxValue(std::numeric_limits<long long>::max());
    CHECK_EQ(random(minValue, minValue).unwrap(), minValue);
    CHECK_EQ(random(maxValue, maxValue).unwrap(), maxValue);
  }

  // 8. Integer：超出 long long 可表示范围时返回错误码（而非静默截断）
  {
    const Integer beyondMax(static_cast<unsigned long long>(std::numeric_limits<long long>::max()) + 1ULL);
    CHECK_ERR(random(beyondMax, beyondMax), MathsError::InvalidRange);
    CHECK_ERR(random(Integer(0LL), beyondMax), MathsError::InvalidRange);
  }

  TEST_SUMMARY();
}
