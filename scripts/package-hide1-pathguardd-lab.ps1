param(
    [string]$OutputName = "pathguard-hide1-lab-myron-pathguardd-v1.zip",
    [string]$OutputDir = "download"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$package = Join-Path $root 'experimental/hide-vfs/package'
$ko = Join-Path $root 'experimental/hide-vfs/pathguard_hide1.ko'
$control = Join-Path $root 'experimental/hide-vfs/hide1_control'
$daemon = Join-Path $root 'module/bin/arm64-v8a/pathguardd'
$rules = Join-Path $root 'module/config/rules.toml'
$profileTemplate = Join-Path $package 'config/hide1_device_profile.json'
$dist = Join-Path $root $OutputDir
$output = Join-Path $dist $OutputName

foreach ($required in @($ko, $control, $daemon, $rules, $profileTemplate)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing required package input: $required"
    }
}
New-Item -ItemType Directory -Force -Path $dist | Out-Null
if (Test-Path -LiteralPath $output) {
    throw "Output already exists; choose another -OutputName: $output"
}

$koHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ko).Hash.ToLowerInvariant()
$daemonHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $daemon).Hash.ToLowerInvariant()
$rulesHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $rules).Hash.ToLowerInvariant()
$profile = (Get-Content -Raw -LiteralPath $profileTemplate).Replace('@MODULE_SHA256@', $koHash)
if ($profile.Contains('@MODULE_SHA256@')) {
    throw 'Device profile module hash placeholder was not replaced'
}
$buildInfo = @"
package=pathguard_hide1_pathguardd_lab
device=myron
kernel_release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
hide_module_sha256=$koHash
pathguardd_sha256=$daemonHash
rules_sha256=$rulesHash
automatic_lkm_load=fixed-device-profile-gated
daemon_start=service.sh
hide_activation=admission-and-rules-controlled
product_status=unsupported
"@

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($output, 'Create')
try {
    foreach ($name in @(
        'module.prop', 'customize.sh', 'post-fs-data.sh', 'service.sh',
        'action.sh', 'uninstall.sh', 'skip_mount', 'README.md')) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, (Join-Path $package $name), $name,
            [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    foreach ($entrySpec in @(
        @{ Path = $ko; Entry = 'bin/pathguard_hide1.ko' },
        @{ Path = $control; Entry = 'bin/hide1_control' },
        @{ Path = $daemon; Entry = 'bin/pathguardd' },
        @{ Path = $rules; Entry = 'config/rules.toml' })) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $entrySpec.Path, $entrySpec.Entry,
            [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    $entry = $archive.CreateEntry('config/hide1_device_profile.json')
    $writer = New-Object System.IO.StreamWriter($entry.Open())
    try { $writer.Write($profile) }
    finally { $writer.Dispose() }
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
Write-Host "Hide module SHA-256: $koHash"
Write-Host "pathguardd SHA-256: $daemonHash"
