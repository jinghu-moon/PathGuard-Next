param(
    [string]$NdkRoot = 'C:/A_Softwares/android-ndk-r27d',
    [int]$Api = 31,
    [string]$Abi = 'arm64-v8a',
    [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$expectedRevision = '27.3.13750724'
if ($Api -ne 31) { throw "RF1 D0 requires Android API 31, got $Api" }
if ($Abi -ne 'arm64-v8a') { throw "RF1 D0 first target must be arm64-v8a, got $Abi" }
$properties = Join-Path $NdkRoot 'source.properties'
if (-not (Test-Path -LiteralPath $properties)) {
    throw "NDK source.properties not found: $properties"
}
$revision = (Select-String -LiteralPath $properties -Pattern '^Pkg.Revision = ' | ForEach-Object {
    $_.Line.Substring('Pkg.Revision = '.Length).Trim()
})
if ($revision -ne $expectedRevision) {
    throw "RF1 D0 requires NDK r27d $expectedRevision, got $revision"
}

$toolchain = Join-Path $NdkRoot 'toolchains/llvm/prebuilt/windows-x86_64/bin'
$compiler = Join-Path $toolchain "aarch64-linux-android$Api-clang++.cmd"
$strip = Join-Path $toolchain 'llvm-strip.exe'
if (-not (Test-Path -LiteralPath $compiler) -or -not (Test-Path -LiteralPath $strip)) {
    throw 'Required NDK clang/strip tools are missing'
}
Write-Host "RF1 D0 NDK revision: $revision (r27d)"
Write-Host "RF1 D0 ABI/API: $Abi / $Api"
& $compiler --version
if ($LASTEXITCODE -ne 0) { throw 'cannot query NDK clang version' }
if ($VerifyOnly) { exit 0 }

$outputDir = Join-Path $root 'build/rf1-d0/android'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$output = Join-Path $outputDir 'libpathguard_rules_cpp_d0.so'
$compileArguments = @(
    '-std=c++20', '-O3', '-fPIC', '-fno-exceptions', '-fno-rtti', '-shared',
    '-I', (Join-Path $root 'third_party/tomlplusplus'),
    '-I', (Join-Path $root 'core/include'),
    (Join-Path $root 'tests/d0/cpp_android_probe.cpp'),
    (Join-Path $root 'core/src/binary.cpp'),
    (Join-Path $root 'core/src/path.cpp'),
    (Join-Path $root 'core/src/validation.cpp'),
    '-o', $output
)
& $compiler @compileArguments
if ($LASTEXITCODE -ne 0) { throw "C++ D0 Android build failed: $LASTEXITCODE" }
$stripped = Join-Path $outputDir 'libpathguard_rules_cpp_d0.stripped.so'
Copy-Item -LiteralPath $output -Destination $stripped -Force
& $strip --strip-unneeded $stripped
if ($LASTEXITCODE -ne 0) { throw "C++ D0 strip failed: $LASTEXITCODE" }

$zygisk = Join-Path $root 'module/zygisk/arm64-v8a.so'
if (Test-Path -LiteralPath $zygisk) {
    $nm = Join-Path $toolchain 'llvm-nm.exe'
    $strings = Join-Path $toolchain 'llvm-strings.exe'
    $symbols = (& $nm -D $zygisk) -join "`n"
    $text = (& $strings $zygisk) -join "`n"
    $symbolLeak = $symbols -match '(?i)toml|pg_rules|rust|rules_compiler'
    $textLeak = $text -match '(?i)toml\+\+|toml_edit|pg_rules_compile|pathguard_rules_compiler'
    if ($symbolLeak -or $textLeak) {
        throw 'Zygisk contains forbidden parser/compiler symbols'
    }
    Write-Host 'RF1 D0 Zygisk dependency scan: clean'
}
Write-Host "RF1 D0 stripped bytes: $((Get-Item -LiteralPath $stripped).Length)"
