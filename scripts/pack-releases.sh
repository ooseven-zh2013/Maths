#!/bin/sh
# 把程序打包到 releases/<程序名>/
#
# 适用平台：Linux / macOS / WSL / Git Bash —— 纯 POSIX sh。
# 刻意不用 bash 的两样东西：数组、set -o pipefail —— Ubuntu 的 /bin/sh 是 dash，
# 两者都不支持（CI 上就是这么炸的）。Windows 另有 scripts/pack-releases.ps1。
#
# 用法：
#   sh scripts/pack-releases.sh            打包全部程序
#   sh scripts/pack-releases.sh simplify   只打包某一个
#
# 为什么不是 `mcpp pack -o releases/<名字>`：
#   --format dir 时 -o 只取路径的最后一段当名字，产物仍然落在 target/dist/ 下，
#   不会跳到 -o 写的目录里去。所以打包完还得再复制一次。
#   （归档格式 tar/zip 的 -o 是生效的，但这里要的是可直接运行的目录。）
#
# 新增程序时：改下面的 TARGETS（空格分隔），与 mcpp.toml 里
# [targets.*] kind = "bin" 保持一致。

set -eu

cd "$(dirname "$0")/.."

TARGETS="simplify"

if [ $# -gt 0 ]; then
  TARGETS="$*"
fi

# 目录要事先存在：pack 不会自己建，目录不存在会直接报 cannot write
mkdir -p releases

for target in $TARGETS; do
  rm -rf "releases/${target}" "target/dist/${target}"

  mcpp pack "${target}" --release --format dir -o "${target}"
  cp -r "target/dist/${target}" "releases/${target}"

  echo "已打包: releases/${target}"
  ls -1 "releases/${target}"
done

# target/dist 只是 pack 的暂存区，产物已经复制到 releases/ 了，留着就是一份重复
rm -rf target/dist
