// common/maths_try.hpp —— `MATHS_TRY`：本库的「?」。
//
// 为什么它是**头文件**而不是模块：
//   宏不属于模块系统。写在 `export module maths.result` 里的 `#define`
//   **不会**随 `import maths.result;` 传给调用方 —— 预处理器在模块边界之前就跑完了。
//   所以凡是「必须靠宏才能做到的事」，只能放头文件里 `#include`。
//   本项目已有的先例是 `tests/check.hpp`（断言宏），同一套办法。
//
// 它解决什么问题：
//   库里有 `Result<T>`，却没有 C++ 的 `?`（Rust 的那个）。于是调用方面对两种选择，
//   两种都很糟：
//     1. 手写传播样板 `if (x.isErr()) return std::unexpected(x.unwrapErr());`
//        —— 全库有 **199 处**；
//     2. 直接 `.unwrap()` —— 全库 **570 处**，一遇错就 terminate，
//        等于把 `Result` 的安全保证全丢了。
//   `MATHS_TRY(expr)` 把第一种压成一个词，行为与 Rust 的 `?` 一致。
//
// 用法：
//   Result<int> f() {
//     const int a = MATHS_TRY(g());      // g() 失败 ⇒ 立刻 return 那个错误
//     const int b = MATHS_TRY(h(a));     // 成功 ⇒ 拿到值继续往下走
//     return Result<int>::ok(a + b);
//   }
//
// ⚠️ 只能在**返回 Result 的函数**里用。它展开成 `return ...`，用在返回 void /
//    T 的函数里会编译失败（这是故意的：类型系统帮你兜住用错地方）。
//
// ⚠️ 依赖 GCC/Clang 的**语句表达式**扩展 `({ ... })`。本项目的工具链是
//    mcpp 的 llvm（clang）三平台一致，所以可以用；换 MSVC 原生编译器时这个宏失效。

#pragma once

// ⚠️ include 必须写在所有 import **之前**（反了 clang 会在标准库头里报一堆错，见项目横切约定）
#include <expected> // std::unexpected

#define MATHS_TRY(...)                                                                                                 \
  __extension__({                                                                                                      \
    auto &&_maths_try_result = (__VA_ARGS__);                                                                          \
    if (!_maths_try_result.isOk()) {                                                                                   \
      return std::unexpected(std::move(_maths_try_result).unwrapErr());                                                \
    }                                                                                                                  \
    std::move(_maths_try_result).unwrap();                                                                             \
  })

// 上面的宏展开后等价于：
//   auto&& r = (expr);
//   if (!r.isOk()) return std::unexpected(std::move(r).unwrapErr());
//   std::move(r).unwrap();
//
// 三个刻意的细节：
//   · `auto &&` —— 左右值都能接，不强制拷贝；
//   · `std::move(r).unwrapErr()` / `.unwrap()` —— 走右值重载，把值搬出来而非拷贝；
//   · `std::unexpected(...)` 的返回类型由函数签名推导，所以调用方写的是
//     `return MATHS_TRY(...)` 的「值」，而错误路径上返回的是 `unexpected` —— 两者都能
//     构造成 `Result<T>`。
