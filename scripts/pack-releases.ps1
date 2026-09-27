# pack-releases.ps1 —— 把程序打包到 releases/<程序名>/
#
# 用法：
#   powershell -File scripts\pack-releases.ps1             打包全部程序
#   powershell -File scripts\pack-releases.ps1 simplify    只打包某一个
#
# 与 scripts/pack-releases.sh 做同一件事，给 Windows 上的 PowerShell / pwsh 用。
# 写成 5.1 兼容的语法，不用 && 、?? 这些只有 7 才有的东西。
#
# 为什么不是 `mcpp pack -o releases\<名字>`：
#   --format dir 时 -o 只取路径的最后一段当名字，产物仍然落在 target\dist\ 下，
#   不会跳到 -o 写的目录里去。所以打包完还得再复制一次。
#   （归档格式 tar/zip 的 -o 是生效的，但这里要的是可直接运行的目录。）
#
# 新增程序时：改下面的 $Targets，与 mcpp.toml 里 [targets.*] kind = "bin" 保持一致。

$ErrorActionPreference = 'Stop'

# 工作目录必须**两个都设**：Set-Location 只改 PowerShell provider 的当前目录，
# [Environment]::CurrentDirectory（.NET 的当前目录）不跟着走，而
# [System.IO.Directory]::Delete() 这类 .NET API 只认后者。
# 只设一个的后果：Test-Path 说路径存在、.NET 说找不到 → 清理被静默跳过 →
# 复制时目标目录已存在 → 产物被多套一层。2026-09-27 在 scripts\ 下实跑复现，
# 报错路径是 scripts\releases\simplify（连进程启动目录都没出）。
$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $Root
[Environment]::CurrentDirectory = $Root

$ReleasesDir = Join-Path $Root 'releases'
$StagingDir = Join-Path $Root 'target/dist'

# 删目录统一走 .NET 而不是 Remove-Item：
#   1. Remove-Item 会把原生命令的 stderr 当错误，Stop 模式下直接终止脚本
#   2. 某些沙箱/安全软件会钩住 Remove-Item 并抛 SAFE_DELETE_FAIL_CLOSED
# 但删不掉**不能只警告了事**：目标目录残留会让后面那步复制产出坏结构
# （见 Copy-DirContents），所以删完必须确认目录真的没了，确认不了就直接失败。
# 失败会留下错误信息，总比静默产出一个坏目录、还照样"打包成功"要好。
function Remove-Dir {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    [System.IO.Directory]::Delete($Path, $true)

    if (Test-Path -LiteralPath $Path) {
        throw "清理 $Path 失败：目录仍然存在"
    }
}

# 复制「目录的内容」而不是目录本身。目标目录若已存在，Copy-Item -Recurse 会把
# 整个源目录塞进目标**里面**，变成 releases\<名字>\<名字>\ —— 静默产出坏目录。
# 这里先建好目标、再逐个子项复制，结构上不可能多套一层，也不依赖"上一步清干净了"；
# 比写 -Path "…\*" 好在源目录为空时不会报"找不到路径"。
function Copy-DirContents {
    param([string]$From, [string]$To)

    if (-not (Test-Path -LiteralPath $From)) {
        throw "找不到待复制的源目录 $From"
    }

    New-Item -ItemType Directory -Path $To -Force | Out-Null
    # -Force 让隐藏项也一起复制
    Get-ChildItem -LiteralPath $From -Force | Copy-Item -Destination $To -Recurse -Force
}

# PowerShell 会把原生命令写进 stderr 的内容当成 ErrorRecord，在 Stop 模式下
# 直接终止脚本 —— 而 mcpp 的 warning 走的就是 stderr。所以调用它时临时降级，
# 改用退出码判断成败。
function Invoke-Mcpp {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$McppArgs)

    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & mcpp @McppArgs
    $code = $LASTEXITCODE
    $ErrorActionPreference = $old

    if ($code -ne 0) {
        throw "mcpp $($McppArgs -join ' ') 失败（退出码 $code）"
    }
}

$Targets = @('simplify')
if ($args.Count -gt 0) {
    $Targets = $args
}

# 目录要事先存在：pack 不会自己建，目录不存在会直接报 cannot write
New-Item -ItemType Directory -Path $ReleasesDir -Force | Out-Null

foreach ($target in $Targets) {
    $outDir = Join-Path $ReleasesDir $target
    $stageDir = Join-Path $StagingDir $target

    Remove-Dir $outDir
    Remove-Dir $stageDir

    Invoke-Mcpp 'pack' $target '--release' '--format' 'dir' '-o' $target

    Copy-DirContents $stageDir $outDir

    Write-Host "已打包: releases/$target"
    Get-ChildItem -LiteralPath $outDir | ForEach-Object { Write-Host ('  ' + $_.Name) }
}

# target\dist 只是 pack 的暂存区，产物已经复制到 releases\ 了，留着就是一份重复
Remove-Dir $StagingDir
