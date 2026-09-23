#pragma once
#ifndef MATHS_MATHS_HPP
#define MATHS_MATHS_HPP

// 伞头（umbrella header）：把全库 8 个模块一次引入，省去记住每个头文件在哪个子目录。
//
// 下面的顺序由 clang-format 按字母排列（同一分组内），不代表依赖关系；
// 每个头文件自己会 include 它需要的下层模块。
// 只需部分模块时请直接 include 具体头文件——
// 显式写出所用模块能在源码里留下真实的依赖关系。

#include <maths/algebra/algebraic_expression.hpp>
#include <maths/algebra/rational_function.hpp>
#include <maths/algebra/scope.hpp>
#include <maths/core/maths_error.hpp>
#include <maths/core/result.hpp>
#include <maths/numeric/numbers.hpp>
#include <maths/numeric/random.hpp>
#include <maths/parser/expression_parser.hpp>

#endif
