param(
    [string]$OutputName = "pathguard-hide1-lab-myron-iop-v1.zip",
    [string]$OutputDir = "download"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$package = Join-Path $root 'experimental/hide-vfs/package'
$ko = Join-Path $root 'experimental/hide-vfs/pathguard_hide1.ko'
$control = Join-Path $root 'experimental/hide-vfs/hide1_control'
$dist = Join-Path $root $OutputDir
$output = Join-Path $dist $OutputName

if (-not (Test-Path -LiteralPath $ko)) {
    throw "Missing kernel module: $ko"
}
if (-not (Test-Path -LiteralPath $control)) {
    throw "Missing control binary: $control"
}
New-Item -ItemType Directory -Force -Path $dist | Out-Null
if (Test-Path -LiteralPath $output) {
    throw "Output already exists; choose another -OutputName: $output"
}

$koHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ko).Hash.ToLowerInvariant()
$controlHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $control).Hash.ToLowerInvariant()
$buildInfo = @"
package=pathguard_hide1_lab
device=myron
kernel_release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
module_sha256=$koHash
control_sha256=$controlHash
automatic_load=no
automatic_enable=no
default_shadow_mode=1
source=experimental/hide-vfs/pathguard_hide1.c
"@

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($output, 'Create')
try {
    $files = @(
        'module.prop', 'customize.sh', 'post-fs-data.sh', 'service.sh',
        'action.sh', 'uninstall.sh', 'skip_mount', 'README.md'
    )
    foreach ($name in $files) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, (Join-Path $package $name), $name,
            [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $ko, 'bin/pathguard_hide1.ko',
        [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $control, 'bin/hide1_control',
        [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    $entry = $archive.CreateEntry('bin/hide1ctl')
    $writer = New-Object System.IO.StreamWriter($entry.Open())
    try { $writer.Write((Get-Content -Raw -LiteralPath (Join-Path $package 'bin/hide1ctl'))) }
    finally { $writer.Dispose() }
    $entry = $archive.CreateEntry('build-info.txt')
    $writer = New-Object System.IO.StreamWriter($entry.Open())
    try { $writer.Write($buildInfo) }
    finally { $writer.Dispose() }
} finally {
    $archive.Dispose()
}

Write-Host "Created: $output"
Write-Host "Module SHA-256: $koHash"
