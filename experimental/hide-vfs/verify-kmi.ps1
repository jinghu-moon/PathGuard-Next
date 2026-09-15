param(
    [Parameter(Mandatory = $true)] [string] $KernelBuild,
    [Parameter(Mandatory = $true)] [string] $Module,
    [Parameter(Mandatory = $true)] [string] $ModuleVermagicRelease,
    [string] $ReadElf = "readelf"
)

$ErrorActionPreference = "Stop"

$uts = Join-Path $KernelBuild "include/generated/utsrelease.h"
$symvers = Join-Path $KernelBuild "Module.symvers"
if (-not (Test-Path -LiteralPath $uts)) { throw "缺少 prepared Kbuild 输出: $uts" }
if (-not (Test-Path -LiteralPath $symvers)) { throw "缺少设备 Module.symvers: $symvers" }
if (-not (Test-Path -LiteralPath $Module)) { throw "模块不存在: $Module" }

$utsText = Get-Content -Raw -LiteralPath $uts
if ($utsText -notmatch [regex]::Escape("#define UTS_RELEASE `"$ModuleVermagicRelease`"")) {
    throw "Kbuild release 与模块 vermagic release 不匹配"
}

$modInfo = & $ReadElf -p .modinfo $Module 2>&1
if ($LASTEXITCODE -ne 0) { throw "readelf 无法读取 .modinfo" }
$expectedMagic = "vermagic=$ModuleVermagicRelease SMP preempt mod_unload modversions aarch64"
if (-not ($modInfo -match [regex]::Escape($expectedMagic))) {
    throw "模块 vermagic 不匹配: 期望 $expectedMagic"
}

$sections = & $ReadElf -SW $Module 2>&1
if ($LASTEXITCODE -ne 0) { throw "readelf 无法读取 section headers" }
$crcSections = @($sections | Select-String -Pattern "__versions|__version_ext_crcs")
$nonEmptyCrc = $false
foreach ($section in $crcSections) {
    if ($section.Line -match "\s(?<size>[0-9A-Fa-f]{6})\s+[0-9A-Fa-f]{2}\s+") {
        if ([Convert]::ToInt32($Matches.size, 16) -gt 0) { $nonEmptyCrc = $true }
    }
}
if ($crcSections.Count -eq 0 -or -not $nonEmptyCrc) {
    throw "模块没有非空 symbol CRC section，拒绝作为可加载产物"
}

Write-Output "KMI static checks passed: release, vermagic, and non-empty CRC section"
