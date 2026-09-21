#!/bin/sh
# 一键编译 apps/ 下的所有程序，并把可执行文件汇总到 releases/。
#
# 用法：
#   sh scripts/build-releases.sh
#
# 额外的 CMake 参数会透传给配置命令，例如：
#   sh scripts/build-releases.sh -G Ninja -DCMAKE_CXX_COMPILER=g++
#
# releases/ 已在 .gitignore 中忽略，平时不会被提交；
# 需要发布时用 git add -f releases/ 强制加入。

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# MSYS / Git Bash 下 pwd 给出的是 /e/... 形式，而 cmake 是原生程序不认这种路径，
# 会被解释成「当前盘符根目录 + \e\...」。有 cygpath 就转成 Windows 形式，
# 在 Linux/macOS 上则是原样返回。
to_native_path() {
  if command -v cygpath > /dev/null 2>&1; then
    cygpath -w "$1"
  else
    printf '%s' "$1"
  fi
}

build_dir="$project_root/build-release"
output_dir="$project_root/releases"
native_root=$(to_native_path "$project_root")
native_build=$(to_native_path "$build_dir")
native_output=$(to_native_path "$output_dir")

echo "项目根目录: $project_root"
echo "构建目录:   $build_dir"
echo "输出目录:   $output_dir"
echo

# 关掉测试、只构建 maths_apps 聚合目标，避免连带编译几十个测试可执行文件
cmake -S "$native_root" -B "$native_build" "$@" \
  -DCMAKE_BUILD_TYPE=Release \
  -DMATHS_BUILD_TESTS=OFF \
  -DMATHS_WARNINGS_AS_ERRORS=ON
cmake --build "$native_build" --config Release --target maths_apps

# 只装 apps 组件，因此 releases/ 里不会出现头文件或 CMake 包配置
cmake --install "$native_build" --prefix "$native_output" --config Release --component apps

echo
echo "完成，产物位于 $output_dir:"
ls -1 "$output_dir"
