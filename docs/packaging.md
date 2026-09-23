# 打包发布

对应 `mcpp pack`。原先的 `scripts/build-releases.sh` 已随 CMake 一起移除 ——
打包这件事 mcpp 自己就做了，不需要额外的脚本。

```bash
mcpp pack                        # 库：模块接口 + 预编译二进制
mcpp pack simplify               # 打包指定的目标（这里是示例程序）
mcpp pack simplify --release     # Release 配置
mcpp pack simplify -o releases/  # 指定产物落点
```

`mcpp pack <target>` 打包指定目标；不带 target 时按包类型决定形态 ——
库出「接口 + 预编译二进制」，程序出自包含 bundle。
默认格式是 tar（Windows 目标自动换成 .zip），`--format dir` 可以出成一个目录。

## 发布

正式版本在仓库的 **GitHub Releases** 页面发布，把打包产物作为附件上传。

产物**不提交进仓库**（`target/` 与 `releases/` 都在 `.gitignore` 里）。

## 改完 apps/ 记得重新打包

改了 `apps/` 下的代码而没重新打包，发布出去的就还是旧产物。
