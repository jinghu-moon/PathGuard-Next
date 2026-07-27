param(
    [string[]]$Abi = @('arm64-v8a', 'armeabi-v7a'),
    [string]$NdkRoot = '',
    [int]$Api = 31,
    [ValidateRange(0, 10000)]
    [int]$ZygiskTestMountDelayMs = 0,
    [ValidateRange(0, 10000)]
    [int]$ZygiskTestPreLeaseDelayMs = 0,
    [switch]$ZygiskTestCrashAfterMount,
    [switch]$ZygiskTestRollbackFailure,
    [string]$HostParityProbe = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$native = Join-Path $root 'native'
$candidates = @()
if ($Api -ne 31) { throw "PathGuard requires Android API 31, got $Api" }
if ($NdkRoot) {
    $candidates += (Join-Path $NdkRoot 'ndk-build.cmd')
} else {
    foreach ($name in @('ndk-build','ndk-build.cmd')) { $cmd = Get-Command $name -ErrorAction SilentlyContinue; if ($cmd) { $candidates += $cmd.Source } }
    foreach ($envName in @('ANDROID_NDK_HOME','ANDROID_NDK_ROOT')) { $value = [Environment]::GetEnvironmentVariable($envName); if ($value) { $candidates += (Join-Path $value 'ndk-build.cmd') } }
    $candidates += 'C:/A_Softwares/android-ndk-r27d/ndk-build.cmd'
}
$ndk = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $ndk) { throw 'ndk-build not found' }
$ndkRoot = Split-Path -Parent $ndk
$properties = Join-Path $ndkRoot 'source.properties'
$revision = if (Test-Path -LiteralPath $properties) {
    (Select-String -LiteralPath $properties -Pattern '^Pkg.Revision = ' |
        ForEach-Object { $_.Line.Substring('Pkg.Revision = '.Length).Trim() })
} else { '' }
if ($revision -ne '27.3.13750724') {
    throw "PathGuard requires Android NDK r27d (27.3.13750724), got '$revision'"
}
$applicationMk = Get-Content -Raw (Join-Path $native 'Application.mk')
if ($applicationMk -notmatch '(?m)^APP_PLATFORM := android-31\s*$') {
    throw 'PathGuard requires Android API 31 in native/Application.mk'
}
$known = @('armeabi-v7a','arm64-v8a','x86','x86_64')
foreach ($item in $Abi) { if ($known -notcontains $item) { throw "Unknown ABI: $item" } }
if ($Abi -notcontains 'arm64-v8a') { throw 'arm64-v8a is the required first release gate' }
Write-Host "NDK revision: $revision; API: 31; ABI: $($Abi -join ',')"

$common = @(
    '-C', $native,
    "NDK_PROJECT_PATH=$native",
    "APP_BUILD_SCRIPT=$native/Android.mk",
    "NDK_APPLICATION_MK=$native/Application.mk",
    "APP_PLATFORM=android-$Api",
    "APP_ABI=$($Abi -join ' ')"
)
& $ndk @common 'APP_MODULES=pathguardd pathguardctl pathguard_rules_parity_probe pathguard_rules_benchmark pathguard_policy_reader_probe pathguard_deny_anchor_probe'
if ($LASTEXITCODE -ne 0) { throw "daemon/cli ndk-build failed: $LASTEXITCODE" }

foreach ($item in $Abi) {
    $bin = Join-Path $root "module/bin/$item"
    New-Item -ItemType Directory -Force -Path $bin | Out-Null
    Copy-Item -Force (Join-Path $native "libs/$item/pathguardd") (Join-Path $bin 'pathguardd')
    Copy-Item -Force (Join-Path $native "libs/$item/pathguardctl") (Join-Path $bin 'pathguardctl')
}

$zygisk = @($common) + @('APP_MODULES=pathguard_zygisk', 'APP_STL=none', '-B')
if ($ZygiskTestMountDelayMs -gt 0) {
    $zygisk += "PATHGUARD_TEST_MOUNT_DELAY_MS=$ZygiskTestMountDelayMs"
}
if ($ZygiskTestPreLeaseDelayMs -gt 0) {
    $zygisk += "PATHGUARD_TEST_PRE_LEASE_DELAY_MS=$ZygiskTestPreLeaseDelayMs"
}
if ($ZygiskTestCrashAfterMount) {
    $zygisk += 'PATHGUARD_TEST_CRASH_AFTER_MOUNT=1'
}
if ($ZygiskTestRollbackFailure) {
    $zygisk += 'PATHGUARD_TEST_ROLLBACK_FAILURE=1'
}
& $ndk @zygisk
if ($LASTEXITCODE -ne 0) { throw "zygisk ndk-build failed: $LASTEXITCODE" }

foreach ($item in $Abi) {
    Copy-Item -Force (Join-Path $native "libs/$item/libpathguard_zygisk.so") (Join-Path $root "module/zygisk/$item.so")
    $toolchain = Join-Path $ndkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin'
    $verifyArgs = @(
        "-DELF=$(Join-Path $root "module/zygisk/$item.so")",
        "-DNM=$(Join-Path $toolchain 'llvm-nm.exe')",
        "-DSTRINGS=$(Join-Path $toolchain 'llvm-strings.exe')",
        "-DLINK_MAP=$(Join-Path $native "obj/local/$item/pathguard_zygisk.map")",
        '-P', (Join-Path $root 'scripts/verify-zygisk-elf.cmake')
    )
    & cmake @verifyArgs
    if ($LASTEXITCODE -ne 0) { throw "Zygisk ELF isolation failed for $item" }
}

if ($HostParityProbe) {
    & (Join-Path $root 'scripts/verify-rules-compiler-android.ps1') `
        -HostProbe $HostParityProbe `
        -AndroidProbe (Join-Path $native 'obj/local/arm64-v8a/pathguard_rules_parity_probe')
    if ($LASTEXITCODE -ne 0) { throw 'Host/Android rules compiler parity failed' }
}
