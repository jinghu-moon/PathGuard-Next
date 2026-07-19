param(
    [string[]]$Abi = @('arm64-v8a'),
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

foreach ($item in $Abi) {
    $zip = Join-Path $dist "pathguard-next-v$version-$item.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    $archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
    try {
        foreach ($file in @('module.prop','customize.sh','post-fs-data.sh','service.sh','boot-completed.sh','action.sh','uninstall.sh','skip_mount')) {
            Add-File $archive (Join-Path $module $file) $file
        }
        Get-ChildItem -LiteralPath (Join-Path $module 'config') -File -Recurse | ForEach-Object {
            Add-File $archive $_.FullName $_.FullName.Substring($module.Length + 1).Replace('\','/')
        }
        Add-File $archive (Join-Path $module "zygisk/$item.so") "zygisk/$item.so"
        Add-File $archive (Join-Path $module "bin/$item/pathguardd") "bin/$item/pathguardd"
        Add-File $archive (Join-Path $module "bin/$item/pathguardctl") "bin/$item/pathguardctl"
    } finally { $archive.Dispose() }
    Write-Host "Created: $zip"
}
