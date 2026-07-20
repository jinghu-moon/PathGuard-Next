param(
    [string[]]$Abi = @('arm64-v8a'),
    [switch]$AllowMissingNative,
    [switch]$SkipNative,
    [ValidateRange(0, 10000)]
    [int]$ZygiskTestMountDelayMs = 0
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $root

cmake -S $root -B (Join-Path $root 'build') -DPATHGUARD_BUILD_TESTS=ON
cmake --build (Join-Path $root 'build') --config Release --parallel 2
ctest --test-dir (Join-Path $root 'build') -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Host tests failed with exit code $LASTEXITCODE" }

if (-not $SkipNative) {
    & (Join-Path $root 'scripts/build-native.ps1') -Abi $Abi `
        -ZygiskTestMountDelayMs $ZygiskTestMountDelayMs
}

& (Join-Path $root 'scripts/package.ps1') -Abi $Abi -AllowMissingNative:$AllowMissingNative
