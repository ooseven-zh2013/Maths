# 打包发布

对应 `mcpp pack`。原先的 `scripts/build-releases.sh` 已随 CMake 一起移除 ——
打包这件事 mcpp 自己就做了，不需要额外的脚本。

## 用脚本（推荐）

Linux / macOS / WSL / Git Bash：

```bash
sh scripts/pack-releases.sh             # 打包全部程序
sh scripts/pack-releases.sh simplify    # 只打包某一个
```

纯 POSIX sh，没用 GNU 专有选项，所以 Linux 和 macOS 用同一个文件就够了 ——
CI 的 `packaging` job 会在两个系统上各跑一遍验证。

Windows（PowerShell / pwsh，与上面做同一件事）：

```powershell
powershell -File scripts\pack-releases.ps1
powershell -File scripts\pack-releases.ps1 simplify
```

产物落在 `releases/<程序名>/`，里面是可以直接跑的 `exe` 加 `LICENSE`、`README.md`。

脚本本质上就是下面两步：

```bash
mcpp pack simplify --release --format dir -o simplify
cp -r target/dist/simplify releases/simplify
```

不过脚本里的复制写的是 `cp -R "target/dist/simplify/." "releases/simplify/"`（PowerShell 版
对应 `Copy-Item -Path "…\*"`）—— 复制的是**目录的内容**，不是目录本身。差了那个后缀，
目标目录已存在时 `cp -r 源 目标` / `Copy-Item -Recurse` 会把整个源目录塞进目标**里面**，
产出 `releases/simplify/simplify/`，而且不报错。清理那一步同理：删不掉就必须失败，
不能只警告了事，否则残留的旧目录会污染下一次打包的结果。

**为什么要多一步复制**：`--format dir` 时 `-o` 只取路径的最后一段当名字，
产物**始终**落在 `target/dist/` 下，不会跳到 `-o` 写的目录里。
归档格式（tar / zip）的 `-o` 倒是能直接指到任意路径，例如
`mcpp pack simplify --release -o releases/simplify.zip` 会老老实实生成在 `releases/` 下。
另外 `pack` 不会自己创建输出目录，目录不存在会直接报 `cannot write`。

`mcpp pack <target>` 打包指定目标；不带 target 时按包类型决定形态 ——
库出「接口 + 预编译二进制」，程序出自包含 bundle。
默认格式是 tar（Windows 目标自动换成 .zip），`--format dir` 可以出成一个目录。

## 发布

**打一个 `v` 开头的标签就够了** —— 剩下的由 `.github/workflows/release.yml` 做：

```
git tag v0.1.0
git push origin v0.1.0
```

它依次做三件事：

| 步骤 | 做什么 |
| --- | --- |
| `verify` | 在 Ubuntu 上把这个标签指向的提交构建 + 测试一遍（标签可能指在任何提交上）|
| `package` | 三个平台各自跑上面的打包脚本，跑一遍产物确认能算对，按平台与架构改名后上传 |
| `publish` | 三份产物齐了才创建（已存在就更新）GitHub Release，把附件挂上去 |

所以**发布出去的产物一定是从这个标签的提交现打的**，不会夹带旧构建。
三个平台的可执行文件都叫 `simplify`，脚本会先改成 `simplify-linux-x86_64` /
`simplify-macos-arm64` / `simplify-windows-x86_64.exe` 再挂，避免互相覆盖。

附件传错了想重来：在 Actions 页面手动重跑 Release workflow、填同一个标签即可
（已存在的 Release 会用新产物覆盖附件）。

产物**不提交进仓库**（`target/` 与 `releases/` 都在 `.gitignore` 里）——
本地的 `releases/` 只是脚本跑完顺手留下的中间产物，发布走的是上面那条流水线。
