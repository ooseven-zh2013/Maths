#!/bin/sh
# 跑 clang-tidy —— 静态检查。
#
# 为什么需要这个脚本：mcpp 的 llvm 包是**精简版、不含 clang-tidy**，索引里也没登记。
# 本机这份是单独装的（与工具链同版本，装在项目外），路径由 CLANG_TIDY 给，
# 默认 E:\Tools\clang-tidy-20.1.0\bin\clang-tidy.exe。
#
# 用法：
#   sh scripts/run-tidy.sh            # 全部源文件（慢，见下面耗时）
#   sh scripts/run-tidy.sh src/parser # 只跑路径含这个子串的文件
#
# ⚠️ **耗时**：模块文件约 57 秒一个（要读 .pcm），全量 28 个模块文件 ≈ 27 分钟。
#   日常只跑改动的那几个 —— 它不是能随手当快速检查用的东西。
#
# 依赖：仓库根要有 compile_commands.json（`mcpp build` 生成，已在 .gitignore 里）。
# 读不到就直接报错退出 —— 拿「没检查」当「检查通过」是最糟的。

set -e

TIDY="${CLANG_TIDY:-E:/Tools/clang-tidy-20.1.0/bin/clang-tidy.exe}"
PYTHON="${PYTHON:-python3}"
FILTER="${1:-}"

if [ ! -x "$TIDY" ]; then
  echo "找不到 clang-tidy：$TIDY" >&2
  echo "装法：pip install clang-tidy==20.1.0，再把 site-packages/clang_tidy/data 拷到项目外" >&2
  echo "（git-bash 里要给 E:/... 这样的正斜杠绝对路径，原生 Windows 程序不认 /e/...）" >&2
  exit 1
fi

if [ ! -f compile_commands.json ]; then
  echo "没有 compile_commands.json —— 先跑 mcpp build" >&2
  exit 1
fi

# 从 compile_commands.json 取 "file" 字段。
#
# ⚠️ 刻意**用 Python 解析 JSON**，不用 sed/tr 洗：路径里的反斜杠是 JSON 转义，
# 洗一遍容易变成 `E://Projects//...`（双斜杠在 `[ -f ]` 眼里不是合法路径），
# 结果是「静默地一个文件都没查到」。另外这文件是 CRLF 的，忘了 strip 回车
# 会让每个文件名后面挂个看不见的字符 —— 同样表现为「查到 0 个文件」。
# CI 的 format job 已经在用 pip 版 clang-format，所以 Python 在 CI 上一定有。
files=$("$PYTHON" -c '
import json
with open("compile_commands.json", encoding="utf-8") as handle:
    entries = json.load(handle)
for entry in entries:
    name = entry.get("file")
    if not name:
        arguments = entry.get("arguments") or []
        name = arguments[0] if arguments else None
    if name:
        print(name.replace(chr(92), "/").strip())
' | sort -u)

if [ -n "$FILTER" ]; then
  files=$(echo "$files" | grep -- "$FILTER" || true)
fi

count=$(echo "$files" | grep -c . || true)
if [ "$count" -eq 0 ]; then
  echo "没有匹配的文件（filter='"$FILTER"'）" >&2
  exit 1
fi

failed=0
# shellcheck disable=SC2086
for file in $files; do
  [ -f "$file" ] || continue
  # --quiet：只报真实问题，不逐条列被抑制的十万条模块噪声
  if ! "$TIDY" "$file" -p . --quiet; then
    failed=$((failed + 1))
  fi
done

echo "----"
echo "clang-tidy 查了 $count 个文件，$failed 个有问题"
# 有问题就非零退出，好让 CI 当门
[ "$failed" -eq 0 ]
