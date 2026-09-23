# 测试

对应 `test/` 目录。

按模块分子目录，不依赖第三方框架。断言宏定义在 `test/check.hpp`：

| 宏 | 用途 |
| --- | --- |
| `CHECK_EQ(actual, expected)` | 相等断言，失败时打印实际值与期望值 |
| `CHECK_TRUE(condition)` | 条件断言 |
| `CHECK_OK(result)` | 断言 `Result` 为 Ok |
| `CHECK_ERR(result, code)` | 断言 `Result` 为 Err 且错误码相符 |
| `CHECK_THROWS(expr, exception_type)` | 异常断言 |
| `TEST_SUMMARY()` | 输出汇总并以失败数作为退出码 |

自制断言而非 `assert`，是为了让检查在 Release 构建（`NDEBUG` 已定义）下依然生效 ——
用 `assert` 的话 Release 下所有断言会被编译掉，等于没测。

## 运行

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Debug 与 Release 都跑一遍：

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release
```

## 新增测试

新文件加进 `test/CMakeLists.txt` 的 `TEST_SOURCES` 即可被自动注册。
测试目标名由「子目录名_文件名」组成，例如 `test/integer/arithmetic.cpp` → `integer_arithmetic`。

## 测试里不要用裸 unwrap()

`parseExpression(...).unwrap()` 一旦失败就**抛异常中断整个测试**，只留一句 `terminate`，极难定位。
用辅助宏（如 parser 测试里的 `LATEX_OF`、`PARSE`）把失败转成断言失败并报出行号。
