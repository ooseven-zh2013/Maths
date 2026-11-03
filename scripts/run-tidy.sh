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
# 依赖：仓库根要有 compile_commands.json（已在 .gitignore 里；本机别删，clangd 要读它）。
# ⚠️ 缺了脚本会**自己现场生成**（`mcpp build --configure-only`），不再要求你先手动跑一遍 build。
#   生成不出来就直接报错退出 —— 拿「没检查」当「检查通过」是最糟的。
# ⚠️ 但自愈出来的 DB **不一定完整**（不含 bin 目标），所以后面还有一道
#   **覆盖面校验**：仓库里该检查的源文件只要有一个不在 DB 里，就报错退出、逐个列出。

set -e

TIDY="${CLANG_TIDY:-E:/Tools/clang-tidy-20.1.0/bin/clang-tidy.exe}"
PYTHON="${PYTHON:-python3}"
# mcpp 不在 PATH 的机器（本机就是）或 CI 上要换版本时，用 MCPP 覆盖，别改脚本。
MCPP="${MCPP:-mcpp}"
FILTER="${1:-}"

# 找不到就退而从 Python 包里找 —— CI 上就是这么装的（pip install clang-tidy==20.1.0），
# 让脚本自己认，比在 workflow 里塞一段「算 exe 路径」的 Python 一行式稳得多。
if [ ! -x "$TIDY" ]; then
  TIDY=$("$PYTHON" -c 'import clang_tidy, pathlib, sys
sys.stdout.write(str(pathlib.Path(clang_tidy.__file__).parent / "data" / "bin" / "clang-tidy"))' 2>/dev/null || true)
fi

if [ ! -x "$TIDY" ]; then
  echo "找不到 clang-tidy。试过：$CLANG_TIDY 与 Python 包里的 clang_tidy.data" >&2
  echo "装法：pip install clang-tidy==20.1.0（版本要与 mcpp 的 llvm 对齐）" >&2
  echo "或把 site-packages/clang_tidy/data 拷到项目外，然后 export CLANG_TIDY=/path/to/bin/clang-tidy" >&2
  echo "（git-bash 里要给 E:/... 这样的正斜杠绝对路径，原生 Windows 程序不认 /e/...）" >&2
  exit 1
fi

# compile_commands.json 缺了就**自己现场生成**，不指望前面正好跑过 `mcpp build`。
#
# ⚠️ 为什么必须有这段（别删回去）：mcpp **2026.11.2.2 起，普通 `mcpp build` 不再
#   顺带产出 compile_commands.json**（2026.10.24.1 还会）。这里原来写的是
#   「没有就报错 —— 先跑 mcpp build」，于是上游一改行为，CI 的 tidy job 在一个
#   **纯文档提交**上就红了（run 41629770652），跟代码改动一点关系都没有。
#   现在改成脚本自己保证前置条件 —— `--configure-only` 的帮助文本就是
#   "Generate compile_commands.json without compiling or linking"，
#   不编译、不链接，几秒钟出结果。
# ⚠️ 但**生成失败必须报错退出**：拿「没检查」当「检查通过」是最糟的。
# ⚠️ 判定用 -s（存在且**非空**）而不是 -f：0 字节的 DB 属于「有文件没内容」，
#   与没有一样，同样该走自愈 —— 否则后面会静默地一个文件都查不到。
if [ ! -s compile_commands.json ]; then
  echo "没有（或为空的）compile_commands.json —— 用 '$MCPP build --configure-only' 现场生成" >&2
  # 进度输出引到 stderr：stdout 后面要装文件清单，不能混进来。
  generate_failed=0
  "$MCPP" build --configure-only >&2 || generate_failed=$?
  if [ "$generate_failed" -ne 0 ] || [ ! -s compile_commands.json ]; then
    echo "生成 compile_commands.json 失败。" >&2
    echo "试过：$MCPP build --configure-only（\$MCPP 来自 MCPP 变量，默认 mcpp）" >&2
    if [ "$generate_failed" -ne 0 ]; then
      echo "  → 命令本身失败，退出码 $generate_failed（多半是 mcpp 不存在或工具链没就绪）" >&2
    else
      echo "  → 命令退出码 0，但当前目录下没有非空的 compile_commands.json" >&2
    fi
    echo "装 mcpp：pip install mcpp-bin==2026.10.24.1（版本别让它飘，同 .github/workflows/ci.yml）" >&2
    echo "mcpp 不在 PATH 时：export MCPP=/绝对/路径/mcpp（git-bash 要给 E:/... 正斜杠路径）" >&2
    exit 1
  fi
  # 条数用 Python 数（别用 grep -c，见下面「为什么用 Python 解析 JSON」）。
  # ⚠️ 这个数字要能被一眼看懂：完整 `mcpp build` 是 66 条，
  #   `--configure-only` 只有 65 —— 少的那条是 bin 目标，见下面的覆盖面校验。
  entries=$("$PYTHON" -c 'import json
with open("compile_commands.json", encoding="utf-8") as handle:
    print(len(json.load(handle)))' 2>/dev/null || echo "?")
  echo "已生成 compile_commands.json（$entries 条）" >&2
fi

# ---- 覆盖面校验：DB 里必须包含仓库里**每一个该检查的源文件** ----
#
# ⚠️ 为什么必须有（这是兜底路径的兜底，别删）：
#   `mcpp build --configure-only` 生成的 DB **不含 bin 目标** —— 实测 65 条，
#   而完整 `mcpp build` 是 66 条，少的正是 mcpp.toml 里
#   `[targets.simplify] kind = "bin", main = "apps/simplify/main.cpp"` 那一条。
#   而 CI 的 tidy job 跑的是**无 filter 全量**：一旦走到自愈路径，这个文件就
#   **静默不检查、脚本照样 exit 0** —— 正是本文件反复强调的
#   「拿没检查当检查通过」，只是缩小了一号。兜底路径有洞也是洞。
#   顺带兜住另一个场景：**新增了 .cppm 却没重建**，DB 过期 —— 那时也该报错，
#   而不是悄悄少查一个文件。
#
# ⚠️ 用 `git ls-files` 而不是 `find`：apps/polynomial/main.cpp 是 gitignore 的
#   练习程序、不属于任何 target，find 会把它算进来，造成永远消不掉的误报。
#
# ⚠️ `src/*.cppm` 那一条不能省：git 的路径规范里 `**` **要至少吃掉一层目录**，
#   光写 `src/**/*.cppm` 匹配不到 src/maths.cppm、src/value.cppm（实测少 2 个，
#   30 → 28），而这两个恰恰在 DB 里 —— 于是会变成「DB 齐全却报缺失」的假警报。
expected=$(git ls-files 'src/*.cppm' 'src/**/*.cppm' 'apps/**/*.cpp' 2>/dev/null | sort -u || true)

if [ -z "$expected" ]; then
  # 拿不到清单（不是 git 检出、或没装 git）→ **大声**说明跳过，不静默放过。
  echo "⚠️ git ls-files 没取到源文件清单，跳过覆盖面校验（不是 git 检出？）" >&2
else
  # 差集：把 DB 里的绝对路径归一成「仓库相对 POSIX 路径」再和 expected 比。
  missing=$(printf '%s\n' "$expected" | "$PYTHON" -c '
import json, os, sys

with open("compile_commands.json", encoding="utf-8") as handle:
    entries = json.load(handle)


def normalize(path):
    """DB 里可能是 Windows 反斜杠绝对路径；归一成仓库相对 POSIX 路径。"""
    path = path.replace(chr(92), "/")
    try:
        return os.path.relpath(path, os.getcwd()).replace(chr(92), "/")
    except ValueError:
        return path


have = set()
for entry in entries:
    name = entry.get("file")
    if not name:
        arguments = entry.get("arguments") or []
        name = arguments[0] if arguments else None
    if name:
        have.add(normalize(name))

for line in sys.stdin:
    path = line.strip()
    if path and path not in have:
        print(path)
')
  if [ -n "$missing" ]; then
    echo "compile_commands.json 不完整 —— 以下文件**不会被检查**：" >&2
    printf '%s\n' "$missing" | while IFS= read -r line; do echo "  · $line" >&2; done
    echo "疑为 \`mcpp build --configure-only\` 不含 bin 目标，或新增源文件后没重建。" >&2
    echo "处理：跑一次**完整**的 \`$MCPP build\` 再重试本脚本（它会产出完整的 DB）。" >&2
    exit 1
  fi
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
  # ⚠️ 两个都缺不得：
  #   --warnings-as-errors 缺了 → clang-tidy 默认只对 **error** 返回非零，警告一律放过，
  #     脚本会报「0 个有问题」而实际有 38 条。踩过。
  #   它**必须带值**（clang-tidy 20）→ 只写开关会报「requires a value!」，
  #     那样每个文件都算「有问题」，28 个全红但全是用法错。踩过。
  #   星号要引号包住，否则被 shell 当通配符展开。
  if ! "$TIDY" "$file" -p . --quiet --warnings-as-errors='*'; then
    failed=$((failed + 1))
  fi
done

echo "----"
echo "clang-tidy 查了 $count 个文件，$failed 个有问题"
# 有问题就非零退出，好让 CI 当门
[ "$failed" -eq 0 ]
