param(
    [string[]]$Abi = @('arm64-v8a', 'armeabi-v7a'),
    [switch]$AllowMissingNative
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$module = Join-Path $root 'module'
$dist = Join-Path $root 'dist'
$prop = Join-Path $module 'module.prop'
$version = ((Select-String -LiteralPath $prop -Pattern '^version=(.+)$').Matches[0].Groups[1].Value).Trim()
$known = @('armeabi-v7a', 'arm64-v8a', 'x86', 'x86_64')

foreach ($item in $Abi) {
    if ($known -notcontains $item) { throw "Unknown ABI: $item" }
}
if ($Abi -contains 'arm64-v8a' -and $Abi -notcontains 'armeabi-v7a') {
    throw 'arm64 packages must also include armeabi-v7a for Zygote32'
}
if ($Abi -contains 'x86_64' -and $Abi -notcontains 'x86') {
    throw 'x86_64 packages must also include x86 for Zygote32'
}
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Add-File($archive, $path, $entry) {
    if (Test-Path -LiteralPath $path) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $path, $entry, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    } elseif (-not $AllowMissingNative -or $entry -notlike 'zygisk/*' -and $entry -notlike 'bin/*') {
        throw "Missing module file: $path"
    }
}

if ($Abi.Count -eq 1) {
    $archiveName = "pathguard-next-v$version-$($Abi[0]).zip"
} else {
    $archiveName = "pathguard-next-v$version-universal.zip"
}
$zip = Join-Path $dist $archiveName
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    foreach ($file in @('module.prop','customize.sh','post-fs-data.sh','service.sh','boot-completed.sh','action.sh','uninstall.sh','skip_mount')) {
        Add-File $archive (Join-Path $module $file) $file
    }
    Get-ChildItem -LiteralPath (Join-Path $module 'config') -File -Recurse | ForEach-Object {
        Add-File $archive $_.FullName $_.FullName.Substring($module.Length + 1).Replace('\','/')
    }
    foreach ($item in $Abi) {
        Add-File $archive (Join-Path $module "zygisk/$item.so") "zygisk/$item.so"
        Add-File $archive (Join-Path $module "bin/$item/pathguardd") "bin/$item/pathguardd"
        Add-File $archive (Join-Path $module "bin/$item/pathguardctl") "bin/$item/pathguardctl"
        Add-File $archive (Join-Path $module "provider/$item/libpathguard_lsplant.so") `
            "provider/$item/libpathguard_lsplant.so"
    }
    Add-File $archive (Join-Path $module 'provider/provider-hooker.dex') `
        'provider/provider-hooker.dex'
    Add-File $archive (Join-Path $module 'THIRD_PARTY_NOTICES.md') `
        'THIRD_PARTY_NOTICES.md'
    Add-File $archive (Join-Path $root 'third_party/lsplant/LICENSE') `
        'licenses/LSPlant-LGPL-3.0.txt'
    Add-File $archive (Join-Path $root 'third_party/dobby/LICENSE') `
        'licenses/Dobby-Apache-2.0.txt'
    Add-File $archive (Join-Path $root 'third_party/xdl/LICENSE') `
        'licenses/xDL-MIT.txt'
} finally { $archive.Dispose() }
Write-Host "Created: $zip"
