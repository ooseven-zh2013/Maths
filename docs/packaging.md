# 打包发布

对应 `mcpp pack`。原先的 `scripts/build-releases.sh` 已随 CMake 一起移除 ——
打包这件事 mcpp 自己就做了，不需要额外的脚本。

## 用脚本（推荐）

```bash
sh scripts/pack-releases.sh             # 打包全部程序
sh scripts/pack-releases.sh simplify    # 只打包某一个
```

产物落在 `releases/<程序名>/`，里面是可以直接跑的 `exe` 加 `LICENSE`、`README.md`。

脚本本质上就是下面两步：

```bash
mcpp pack simplify --release --format dir -o simplify
cp -r target/dist/simplify releases/simplify
```

**为什么要多一步复制**：`--format dir` 时 `-o` 只取路径的最后一段当名字，
产物**始终**落在 `target/dist/` 下，不会跳到 `-o` 写的目录里。
归档格式（tar / zip）的 `-o` 倒是能直接指到任意路径，例如
`mcpp pack simplify --release -o releases/simplify.zip` 会老老实实生成在 `releases/` 下。
另外 `pack` 不会自己创建输出目录，目录不存在会直接报 `cannot write`。

`mcpp pack <target>` 打包指定目标；不带 target 时按包类型决定形态 ——
库出「接口 + 预编译二进制」，程序出自包含 bundle。
默认格式是 tar（Windows 目标自动换成 .zip），`--format dir` 可以出成一个目录。

## 发布

正式版本在仓库的 **GitHub Releases** 页面发布，把打包产物作为附件上传。

产物**不提交进仓库**（`target/` 与 `releases/` 都在 `.gitignore` 里）。

## 改完 apps/ 记得重新打包

改了 `apps/` 下的代码而没重新打包，发布出去的就还是旧产物。
