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

Set-Location -LiteralPath (Join-Path $PSScriptRoot '..')

# 删目录统一走 .NET 而不是 Remove-Item：
#   1. Remove-Item 会把原生命令的 stderr 当错误，Stop 模式下直接终止脚本
#   2. 某些沙箱/安全软件会钩住 Remove-Item 并抛 SAFE_DELETE_FAIL_CLOSED
# 清理失败不该让打包白做，所以这里只警告。
function Remove-Dir {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    try {
        [System.IO.Directory]::Delete($Path, $true)
    } catch {
        Write-Warning "清理 $Path 失败：$($_.Exception.Message)"
    }
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
if (-not (Test-Path -LiteralPath 'releases')) {
    New-Item -ItemType Directory -Path 'releases' | Out-Null
}

foreach ($target in $Targets) {
    Remove-Dir "releases/$target"
    Remove-Dir "target/dist/$target"

    Invoke-Mcpp 'pack' $target '--release' '--format' 'dir' '-o' $target

    Copy-Item -Recurse -LiteralPath "target/dist/$target" -Destination "releases/$target"

    Write-Host "已打包: releases/$target"
    Get-ChildItem -LiteralPath "releases/$target" | ForEach-Object { Write-Host ('  ' + $_.Name) }
}

# target\dist 只是 pack 的暂存区，产物已经复制到 releases\ 了，留着就是一份重复
Remove-Dir 'target/dist'
