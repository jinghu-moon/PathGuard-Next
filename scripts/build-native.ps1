param(
    [string[]]$Abi = @('arm64-v8a'),
    [ValidateRange(0, 10000)]
    [int]$ZygiskTestMountDelayMs = 0,
    [ValidateRange(0, 10000)]
    [int]$ZygiskTestPreLeaseDelayMs = 0
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$native = Join-Path $root 'native'
$candidates = @()
foreach ($name in @('ndk-build','ndk-build.cmd')) { $cmd = Get-Command $name -ErrorAction SilentlyContinue; if ($cmd) { $candidates += $cmd.Source } }
foreach ($envName in @('ANDROID_NDK_HOME','ANDROID_NDK_ROOT')) { $value = [Environment]::GetEnvironmentVariable($envName); if ($value) { $candidates += (Join-Path $value 'ndk-build.cmd') } }
$candidates += 'C:/A_Softwares/android-ndk-r27d/ndk-build.cmd'
$ndk = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $ndk) { throw 'ndk-build not found' }
$known = @('armeabi-v7a','arm64-v8a','x86','x86_64')
foreach ($item in $Abi) { if ($known -notcontains $item) { throw "Unknown ABI: $item" } }

$common = @(
    '-C', $native,
    "NDK_PROJECT_PATH=$native",
    "APP_BUILD_SCRIPT=$native/Android.mk",
    "NDK_APPLICATION_MK=$native/Application.mk",
    "APP_ABI=$($Abi -join ' ')"
)
& $ndk @common 'APP_MODULES=pathguardd pathguardctl'
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
& $ndk @zygisk
if ($LASTEXITCODE -ne 0) { throw "zygisk ndk-build failed: $LASTEXITCODE" }

foreach ($item in $Abi) {
    Copy-Item -Force (Join-Path $native "libs/$item/libpathguard_zygisk.so") (Join-Path $root "module/zygisk/$item.so")
}
