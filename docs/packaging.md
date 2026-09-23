# 打包发布

对应 `scripts/build-releases.sh`。

```bash
sh scripts/build-releases.sh
```

脚本以 Release 模式**只构建 `apps/` 下的程序**（`-DMATHS_BUILD_TESTS=OFF` 加上聚合目标
`maths_apps`，不会连带编译几十个测试），把可执行文件汇总到 `releases/`。

生成器与编译器可通过参数透传给 CMake：

```bash
sh scripts/build-releases.sh -G Ninja -DCMAKE_CXX_COMPILER=g++
```

它构建独立的 `build-release/`，与日常的 `build/` 互不影响。

`install(... COMPONENT apps)` + `cmake --install --component apps` 保证 `releases/` 里
只有可执行文件，不混入头文件。

## 发布

正式版本在仓库的 **GitHub Releases** 页面发布，把 `releases/` 里的产物作为附件上传。

产物**不提交进仓库**（`releases/` 与 `build-release/` 都在 `.gitignore` 里）。

## 改完 apps/ 记得重跑

改了 `apps/` 下的代码而没重跑脚本，`releases/` 里就还是旧产物。
