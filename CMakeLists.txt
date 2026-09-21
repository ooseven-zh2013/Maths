cmake_minimum_required(VERSION 3.15)
project(Maths VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成 compile_commands.json，供 clangd / clang-tidy 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# ==================== 头文件库 ====================

add_library(Maths INTERFACE)
add_library(Maths::Maths ALIAS Maths)

target_include_directories(Maths INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(Maths INTERFACE cxx_std_23)

# ==================== 编译告警（仅本仓库测试使用，不污染下游） ====================

add_library(maths_warnings INTERFACE)
if(MSVC)
  target_compile_options(maths_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(maths_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

option(MATHS_WARNINGS_AS_ERRORS "把编译告警视为错误" OFF)
if(MATHS_WARNINGS_AS_ERRORS)
  if(MSVC)
    target_compile_options(maths_warnings INTERFACE /WX)
  else()
    target_compile_options(maths_warnings INTERFACE -Werror)
  endif()
endif()

option(MATHS_BUILD_TESTS "构建 Maths 测试" ON)

# ==================== 安装与导出 ====================

install(TARGETS Maths EXPORT MathsTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT MathsTargets
  NAMESPACE Maths::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Maths
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MathsConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/MathsConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Maths
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/MathsConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/MathsConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/MathsConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Maths
)

# ==================== 测试 ====================

if(MATHS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

# ==================== 示例程序 ====================

option(MATHS_BUILD_APPS "构建 apps/ 下的示例程序" ON)

if(MATHS_BUILD_APPS)
  add_subdirectory(apps)
endif()
