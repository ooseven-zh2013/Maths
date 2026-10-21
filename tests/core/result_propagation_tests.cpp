// 对应 src/core/result.cppm（模块 maths.result）：错误传播的三种写法。
//
// 为什么单独立一个文件：`map` / `andThen` / `MATHS_TRY` 是 2026-10-21 加的，
// 此前全库只有 `unwrap()`（570 处）和手写 `if (isErr()) return ...`（199 处）。
// 这三个补齐了「Result 却没有 ?」的缺口，语义必须钉死在这里。

#include "../../common/maths_try.hpp"
#include "../check.hpp"

#include <cstddef>
import maths;

using namespace maths;

namespace {

// 一个「被调用就计数」的源，用来验证 Err 时**不会**调用后续 lambda
std::size_t g_lambdaCalls = 0;

Result<int> succeed(int value) { return Result<int>::ok(value); }

Result<int> fail() { return Result<int>::err(MathsError::NotUnivariate); }

// 全库约定：错误码必须原样传播，不被吞也不被改
constexpr int kNotUnivariate = static_cast<int>(MathsError::NotUnivariate);

} // namespace

int main() {
  // ==================== map：变换成功的值 ====================
  {
    const Result<int> doubled = succeed(21).map([](const int &value) { return value * 2; });
    CHECK_TRUE(doubled.isOk());
    CHECK_EQ(doubled.unwrap(), 42);
  }

  // Err 时短路，且错误码原样保留 —— 这是「传播」的底线
  {
    const Result<int> mapped = fail().map([](const int &) {
      ++g_lambdaCalls; // 走到这里就是没短路
      return 1;
    });
    CHECK_TRUE(mapped.isErr());
    CHECK_EQ(static_cast<int>(mapped.unwrapErr()), kNotUnivariate);
    CHECK_EQ(g_lambdaCalls, std::size_t{0}); // lambda 一次都没被调
  }

  // ==================== andThen：串接下一个也可能失败的运算 ====================
  {
    const Result<int> chained = succeed(7).andThen([](const int &value) { return Result<int>::ok(value + 1); });
    CHECK_TRUE(chained.isOk());
    CHECK_EQ(chained.unwrap(), 8);
  }

  {
    const Result<int> chained = succeed(7).andThen([](const int &) { return fail(); });
    CHECK_TRUE(chained.isErr());
    CHECK_EQ(static_cast<int>(chained.unwrapErr()), kNotUnivariate);
  }

  // Err 时后续的 andThen 也不再调用
  {
    g_lambdaCalls = 0;
    const Result<int> chained = fail().andThen([](const int &) {
      ++g_lambdaCalls;
      return Result<int>::ok(0);
    });
    CHECK_TRUE(chained.isErr());
    CHECK_EQ(static_cast<int>(chained.unwrapErr()), kNotUnivariate);
    CHECK_EQ(g_lambdaCalls, std::size_t{0});
  }

  // 三步链：每步都可能失败，任一步失败都应停在那一步的错误上
  {
    g_lambdaCalls = 0;
    const Result<int> chained = succeed(1)
                                    .andThen([](const int &v) { return succeed(v + 1); })
                                    .andThen([](const int &) { return fail(); })
                                    .andThen([](const int &v) {
                                      ++g_lambdaCalls; // 前一步已失败，不该到这里
                                      return succeed(v);
                                    });
    CHECK_TRUE(chained.isErr());
    CHECK_EQ(static_cast<int>(chained.unwrapErr()), kNotUnivariate);
    CHECK_EQ(g_lambdaCalls, std::size_t{0});
  }

  // ==================== MATHS_TRY：像 Rust 的 ? ====================
  {
    // 两个 TRY 都成功 ⇒ 拿到两个值继续算
    const Result<int> sum = []() -> Result<int> {
      const int a = MATHS_TRY(succeed(20));
      const int b = MATHS_TRY(succeed(22));
      return Result<int>::ok(a + b);
    }();
    CHECK_TRUE(sum.isOk());
    CHECK_EQ(sum.unwrap(), 42);
  }

  {
    // 第一个 TRY 就失败 ⇒ 立刻返回那个错误，后面两行根本不执行
    g_lambdaCalls = 0;
    const Result<int> value = []() -> Result<int> {
      const int a = MATHS_TRY(fail());
      ++g_lambdaCalls; // 短路后不该到达
      const int b = MATHS_TRY(succeed(a));
      return Result<int>::ok(a + b);
    }();
    CHECK_TRUE(value.isErr());
    CHECK_EQ(static_cast<int>(value.unwrapErr()), kNotUnivariate);
    CHECK_EQ(g_lambdaCalls, std::size_t{0});
  }

  {
    // 第一个成功、第二个失败 ⇒ 错误来自第二个，且第一个的值已可用
    g_lambdaCalls = 0;
    const Result<int> value = []() -> Result<int> {
      const int a = MATHS_TRY(succeed(5));
      CHECK_EQ(a, 5); // 第一个 TRY 的值确实拿到了
      const int b = MATHS_TRY(fail());
      ++g_lambdaCalls; // 第二个 TRY 失败，不该到达
      return Result<int>::ok(a + b);
    }();
    CHECK_TRUE(value.isErr());
    CHECK_EQ(static_cast<int>(value.unwrapErr()), kNotUnivariate);
    CHECK_EQ(g_lambdaCalls, std::size_t{0});
  }

  // MATHS_TRY 与 map/andThen 混用：错误码同样原样传播
  {
    const Result<int> mixed = []() -> Result<int> {
      const int a = MATHS_TRY(succeed(3));
      return succeed(a).andThen([](const int &v) { return fail(); });
    }();
    CHECK_TRUE(mixed.isErr());
    CHECK_EQ(static_cast<int>(mixed.unwrapErr()), kNotUnivariate);
  }

  TEST_SUMMARY();
}
