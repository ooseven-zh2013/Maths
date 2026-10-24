# 打包与发布

对应 `mcpp pack`。**没有手写打包脚本，也没有 `releases/` 目录** —— 产物一律落在
`target/dist/`，和构建缓存同一个地盘。

## 本地打一份

```bash
mcpp pack simplify --format dir -o target/dist/simplify
```

产物：

```
target/dist/simplify/
├── simplify.exe   （Linux / macOS 上叫 simplify，无后缀）
├── LICENSE
└── README.md
```

跑一下：

```bash
./target/dist/simplify/simplify.exe
```

### 为什么是 `--format dir`

默认 `--format tar`（Windows 目标上是 `.zip`），出一份压缩包。想直接运行就用 `dir`。
两个都可以 `-o` 指输出路径。

## 其它打包模式

`mcpp pack` 自己带了几种模式，不需要我们写脚本去模拟：

| 模式 | 含义 |
| --- | --- |
| `vendored`（默认） | 只带本程序，运行时依赖系统里的库 |
| `self-contained` | 把运行时依赖一起带上，目标机器不用装 |
| `static` | 全静态 |
| `system` | 假定目标机器已有依赖 |

例：`mcpp pack simplify --mode static -o target/dist/simplify`。

## CI

`packaging` job（`ci.yml`，三平台）跑的就是上面那条命令，然后：

1. 校验产物存在且可执行
2. **真的跑一遍**，断言 `2x + 3x` 配 `x = 5` 得到 `化简结果: 25`
3. 不保留 `target/dist`（它是产物不是缓存；留在 CI 上没有意义）

## 发布

打标签即可：

```bash
git tag v0.1.0 && git push origin v0.1.0
```

`release.yml` 依次：verify（Ubuntu 构建 + 测试）→ package（三平台 `mcpp pack`，
按平台改名 `simplify-linux-x86_64` / `-macos-arm64` / `-windows-x86_64.exe`）→
publish（建/更新 GitHub Release 并挂附件）。

**产物一定是现打的**，不会夹带旧构建 —— 因为 `target/` 不进仓库。

## 历史：为什么砍掉了 `releases/` 与打包脚本

2026-10-24 之前是 `scripts/pack-releases.sh`（POSIX sh）与 `.ps1`（5.1 兼容语法）两个
手写脚本，把 `target/dist/<程序>/` 里的东西复制到 `releases/<程序>/`。

砍掉的理由：

1. **`mcpp pack` 本来就干这个**，脚本只是在复刻它的能力（还复刻得不如它全 ——
   没有 `self-contained`、`static` 这些模式）。
2. **两个脚本要让「做的事完全一致」**，为此踩过坑：`cp -R "src/." "dst/"` 少个点
   就会多套一层目录且不报错。维护成本换来的是零额外能力。
3. **两套产物路径会骗人**。`releases/` 里的 exe 是某次手动跑脚本留下的，不会随
   `mcpp build` 更新 —— 用户拿它测，测出来的是几天前甚至十几天前的二进制，
   于是把已经修好的 bug 又当新问题报一遍。（2026-10-24 就发生过：`releases/simplify/`
   里躺着一个 10 月 7 日的 exe，而代码已经修到 10 月 24 日。）
4. **`target/` 已经在 `.gitignore` 里**，产物天然不进仓库；多一个 `releases/`
   只是多一条要记得 ignore 的路径。

现在的原则：**构建产物只有一个出处 —— `target/`。** 想拿可执行文件就去
`target/dist/`，它一定和 `mcpp pack` 最近一次运行同步。
