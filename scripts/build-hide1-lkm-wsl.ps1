<#
.SYNOPSIS
Builds the fixed-device Hide 1.0 LKM in a one-shot WSL process.

.EXAMPLE
./scripts/build-hide1-lkm-wsl.ps1

.EXAMPLE
./scripts/build-hide1-lkm-wsl.ps1 -Clean -DeviceSerial f3ba305a
#>
[CmdletBinding()]
param(
    [string] $Distribution = '',
    [switch] $Clean,
    [string] $DeviceSerial = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
    throw 'This script must run from Windows PowerShell or PowerShell on Windows.'
}

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$repositoryRoot = (Resolve-Path -LiteralPath $repositoryRoot).Path
$kernelDirectory = Join-Path $repositoryRoot 'build/ddk-kdir-local-linux/android16-6.12'
$clangDirectory = Join-Path $repositoryRoot 'build/ddk-clang-r536225/extracted/clang-r536225/bin'
$paholeRoot = Join-Path $repositoryRoot '.tools/pahole-1.30/usr'
$moduleDirectory = Join-Path $repositoryRoot 'experimental/hide-vfs'
$modulePath = Join-Path $moduleDirectory 'pathguard_hide1.ko'
$deviceKernelRelease = '6.12.23-android16-5-g16e473de48a3-abogki462654244-4k'

$requiredInputs = @(
    (Join-Path $kernelDirectory '.config'),
    (Join-Path $clangDirectory 'clang'),
    (Join-Path $paholeRoot 'bin/pahole'),
    (Join-Path $paholeRoot 'lib/x86_64-linux-gnu/libbpf.so.1'),
    (Join-Path $moduleDirectory 'Makefile')
)
foreach ($requiredInput in $requiredInputs) {
    if (-not (Test-Path -LiteralPath $requiredInput -PathType Leaf)) {
        throw "Missing LKM build input: $requiredInput"
    }
}

$wslCommand = Get-Command 'wsl.exe' -ErrorAction Stop
$wslArguments = @()
if ($Distribution) {
    $wslArguments += @('--distribution', $Distribution)
}

$wslPathOutput = & $wslCommand.Source @wslArguments --exec wslpath -a -u $repositoryRoot 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "Unable to map the repository into WSL: $($wslPathOutput -join [Environment]::NewLine)"
}
$wslRepositoryRoot = ($wslPathOutput | Out-String).Trim()
if (-not $wslRepositoryRoot.StartsWith('/')) {
    throw "wslpath returned an invalid Linux path: $wslRepositoryRoot"
}

$cleanValue = if ($Clean) { '1' } else { '0' }
$linuxBuildScript = @'
set -euo pipefail

repository_root=$1
device_kernel_release=$2
clean_build=$3

kernel_directory="$repository_root/build/ddk-kdir-local-linux/android16-6.12"
clang_directory="$repository_root/build/ddk-clang-r536225/extracted/clang-r536225/bin"
pahole_root="$repository_root/.tools/pahole-1.30/usr"
module_directory="$repository_root/experimental/hide-vfs"
module_path="$module_directory/pathguard_hide1.ko"

test -d "$repository_root"
test -f "$kernel_directory/.config"
test -x "$clang_directory/clang"
test -x "$pahole_root/bin/pahole"
test -f "$pahole_root/lib/x86_64-linux-gnu/libbpf.so.1"
test -f "$module_directory/Makefile"

export PATH="$pahole_root/bin:$PATH"
export LD_LIBRARY_PATH="$pahole_root/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LLVM="$clang_directory/"

printf 'WSL distribution: %s\n' "${WSL_DISTRO_NAME:-unknown}"
printf 'Windows repository: %s\n' "$(wslpath -w "$repository_root")"
printf 'WSL repository: %s\n' "$repository_root"
printf 'Kernel directory: %s\n' "$kernel_directory"
printf 'Toolchain: '
"$clang_directory/clang" --version | head -n 1
printf 'pahole: '
"$pahole_root/bin/pahole" --version

if [ "$clean_build" = 1 ]; then
    make -C "$module_directory" \
        KDIR="$kernel_directory" \
        DEVICE_KERNEL_RELEASE="$device_kernel_release" \
        LLVM="$LLVM" \
        clean
fi

make -C "$module_directory" \
    KDIR="$kernel_directory" \
    DEVICE_KERNEL_RELEASE="$device_kernel_release" \
    LLVM="$LLVM"

test -s "$module_path"

section_headers=$(readelf -SW "$module_path")
printf '%s\n' "$section_headers" | grep -E '\.BTF|__versions|__version_ext_crcs'
printf '%s\n' "$section_headers" | grep -Eq '\.BTF[[:space:]]'
printf '%s\n' "$section_headers" | grep -Eq '__versions|__version_ext_crcs'

module_info=$(readelf -p .modinfo "$module_path")
printf '%s\n' "$module_info" | grep 'vermagic='
sha256sum "$module_path"
'@

Write-Host "Starting one-shot WSL build from Windows PowerShell"
Write-Host "Windows repository: $repositoryRoot"
Write-Host "WSL repository: $wslRepositoryRoot"

# PowerShell writes native-process stdin with CRLF. Strip carriage returns before
# Bash parses the script, while keeping all dynamic values as positional arguments.
$linuxBuildScript | & $wslCommand.Source @wslArguments --exec bash -c 'tr -d ''\r'' | bash -s -- "$@"' bash `
    $wslRepositoryRoot $deviceKernelRelease $cleanValue
if ($LASTEXITCODE -ne 0) {
    throw "WSL LKM build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) {
    throw "WSL reported success but the module is unavailable from Windows: $modulePath"
}
$module = Get-Item -LiteralPath $modulePath
if ($module.Length -eq 0) {
    throw "WSL produced an empty module: $modulePath"
}
$moduleHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $modulePath).Hash.ToLowerInvariant()

Write-Host 'Returned to Windows PowerShell'
Write-Host "Windows location: $((Get-Location).Path)"
Write-Host "Module: $modulePath"
Write-Host "Module size: $($module.Length) bytes"
Write-Host "Module SHA-256: $moduleHash"

$adbCommand = Get-Command 'adb.exe' -ErrorAction SilentlyContinue
if ($adbCommand) {
    Write-Host "Windows ADB: $($adbCommand.Source)"
} elseif ($DeviceSerial) {
    throw 'Device verification was requested, but Windows adb.exe was not found.'
} else {
    Write-Warning 'Windows adb.exe was not found; the LKM build itself succeeded.'
}

if ($DeviceSerial) {
    $deviceState = & $adbCommand.Source -s $DeviceSerial get-state 2>&1
    $adbExitCode = $LASTEXITCODE
    $reportedState = if ($deviceState) { @($deviceState)[-1].ToString().Trim() } else { '' }
    if ($adbExitCode -ne 0 -or $reportedState -ne 'device') {
        throw "ADB device '$DeviceSerial' is unavailable: $($deviceState -join [Environment]::NewLine)"
    }
    Write-Host "ADB device: $DeviceSerial (device)"
}
