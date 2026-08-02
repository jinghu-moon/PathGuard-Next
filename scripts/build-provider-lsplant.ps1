param(
    [string[]]$Abi = @('arm64-v8a', 'armeabi-v7a'),
    [string]$NdkRoot = '',
    [string]$CMakePath = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$known = @('armeabi-v7a', 'arm64-v8a', 'x86', 'x86_64')
foreach ($item in $Abi) {
    if ($known -notcontains $item) { throw "Unknown ABI: $item" }
}

$ndkCandidates = @()
if ($NdkRoot) { $ndkCandidates += $NdkRoot }
foreach ($envName in @('ANDROID_NDK_HOME','ANDROID_NDK_ROOT')) {
    $value = [Environment]::GetEnvironmentVariable($envName)
    if ($value) { $ndkCandidates += $value }
}
if ($env:LOCALAPPDATA) {
    $ndkCandidates += (Join-Path $env:LOCALAPPDATA 'Android/Sdk/ndk/29.0.14206865')
}
$ndk = $ndkCandidates |
    Where-Object { Test-Path -LiteralPath (Join-Path $_ 'build/cmake/android.toolchain.cmake') } |
    Select-Object -First 1
if (-not $ndk) { throw 'Android NDK 29.0.14206865 not found' }
$revision = (Select-String -LiteralPath (Join-Path $ndk 'source.properties') `
    -Pattern '^Pkg.Revision = ').Line.Substring('Pkg.Revision = '.Length).Trim()
if ($revision -ne '29.0.14206865') {
    throw "LSPlant requires Android NDK 29.0.14206865, got '$revision'"
}

$cmakeCandidates = @()
if ($CMakePath) { $cmakeCandidates += $CMakePath }
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakeCandidates += $cmakeCommand.Source }
$cmakeCandidates += 'C:/A_Softwares/cmake_v4.0.2/bin/cmake.exe'
$cmake = $cmakeCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $cmake) { throw 'CMake 3.28 or newer not found' }
$cmakeVersion = (& $cmake --version | Select-Object -First 1)
if ($cmakeVersion -notmatch 'cmake version (\d+)\.(\d+)') {
    throw "Cannot parse CMake version: $cmakeVersion"
}
if ([int]$Matches[1] -lt 3 -or
    ([int]$Matches[1] -eq 3 -and [int]$Matches[2] -lt 28)) {
    throw "LSPlant requires CMake 3.28 or newer, got '$cmakeVersion'"
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw 'Ninja not found'
}

$dobbyAar = Join-Path $root 'third_party/dobby/artifacts/dobby-1.2.aar'
$dobbyHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dobbyAar).Hash.ToLowerInvariant()
if ($dobbyHash -ne '251f48ae21686d7f69276c50644ca345f450e45110057437f7d76bb14cddf3a1') {
    throw "Pinned Dobby AAR hash mismatch: $dobbyHash"
}

$nativeSource = Join-Path $root 'provider-adapter/native'
$toolchain = Join-Path $ndk 'build/cmake/android.toolchain.cmake'
foreach ($item in $Abi) {
    $build = Join-Path $root "build/provider-lsplant/$item"
    & $cmake -S $nativeSource -B $build -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        "-DANDROID_ABI=$item" `
        '-DANDROID_PLATFORM=android-31' `
        '-DANDROID_STL=c++_static' `
        '-DCMAKE_BUILD_TYPE=Release'
    if ($LASTEXITCODE -ne 0) { throw "LSPlant configure failed for $item" }
    & $cmake --build $build --target pathguard_lsplant --parallel 2
    if ($LASTEXITCODE -ne 0) { throw "LSPlant build failed for $item" }
    $destination = Join-Path $root "module/provider/$item"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Force (Join-Path $build 'libpathguard_lsplant.so') `
        (Join-Path $destination 'libpathguard_lsplant.so')
    & $cmake `
        "-DELF=$(Join-Path $destination 'libpathguard_lsplant.so')" `
        "-DNM=$(Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-nm.exe')" `
        "-DREADELF=$(Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe')" `
        "-DABI=$item" `
        '-P' (Join-Path $root 'scripts/verify-provider-lsplant-elf.cmake')
    if ($LASTEXITCODE -ne 0) { throw "LSPlant ELF verification failed for $item" }
}

$javac = (Get-Command javac -ErrorAction Stop).Source
$buildTools = if ($env:LOCALAPPDATA) {
    Join-Path $env:LOCALAPPDATA 'Android/Sdk/build-tools/36.1.0'
} else { '' }
$d8 = Join-Path $buildTools 'd8.bat'
if (-not (Test-Path -LiteralPath $d8)) { throw 'Android build-tools 36.1.0 d8 not found' }
$hookerBuild = Join-Path $root 'build/provider-lsplant/hooker'
$classes = Join-Path $hookerBuild 'classes'
$dexOutput = Join-Path $hookerBuild 'dex'
New-Item -ItemType Directory -Force -Path $classes,$dexOutput | Out-Null
& $javac --release 11 -d $classes `
    (Join-Path $root 'provider-adapter/hooker/src/dev/pathguard/providerhook/ProviderHooker.java')
if ($LASTEXITCODE -ne 0) { throw 'Provider Hooker javac failed' }
$classFiles = Get-ChildItem -Recurse -File $classes -Filter *.class |
    ForEach-Object { $_.FullName }
& $d8 --min-api 31 --output $dexOutput $classFiles
if ($LASTEXITCODE -ne 0) { throw 'Provider Hooker D8 failed' }
$providerModule = Join-Path $root 'module/provider'
New-Item -ItemType Directory -Force -Path $providerModule | Out-Null
Copy-Item -Force (Join-Path $dexOutput 'classes.dex') `
    (Join-Path $providerModule 'provider-hooker.dex')
Write-Host "LSPlant bridge built: ABI=$($Abi -join ',') NDK=$revision"
