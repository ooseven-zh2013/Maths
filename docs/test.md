# 测试

对应 `tests/` 目录。

不依赖第三方框架。断言宏定义在 `tests/check.hpp`：

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
mcpp test                     # 构建并跑 tests/ 下的全部测试
mcpp test integer_arithmetic  # 只跑名字匹配的那个
mcpp test --list              # 列出会被跑到的测试，不构建也不执行
mcpp test --profile release   # Release 构建下再跑一遍，断言必须同样全绿
```

`mcpp test` 自动发现 `tests/**/*.cpp`，一个文件一个可执行文件，测试名就是文件名。

## 新增测试

往 `tests/` 下加一个 `.cpp` 即可，不需要在任何地方登记 —— `mcpp test` 自动发现。

文件名沿用「类别_主题」的形式（如 `integer_arithmetic.cpp`）：测试名直接取自文件名，
不同类别下的同名文件会撞车，所以类别前缀不能省。

每个测试文件自己写 `int main()`，以 `TEST_SUMMARY()` 结尾 —— 用失败数作退出码，
`mcpp test` 才能真正感知失败。

## 测试文件怎么引库

**`#include` 必须写在 `import` 之前**：

```cpp
#include "check.hpp"     // 先 include（相对 tests/ 目录解析，不需要 -I）
#include <expected>

import maths;            // 再 import

using namespace maths;   // 类型都收进了 namespace maths
```

顺序反了 clang 会在标准库头里报一堆错（模块化的硬约束，不是风格问题）。

另外：库不再靠 `#include <maths/...>` 顺带把 `<expected>`、`<optional>` 这类标准库头带进来，
测试里用到的标准库头现在要自己写。

## 测试里不要用裸 unwrap()

`parseExpression(...).unwrap()` 一旦失败就**抛异常中断整个测试**，只留一句 `terminate`，极难定位。
用辅助宏（如 parser 测试里的 `LATEX_OF`、`PARSE`）把失败转成断言失败并报出行号。
