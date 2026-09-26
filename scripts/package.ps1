param(
    [string[]]$Abi = @('arm64-v8a', 'armeabi-v7a'),
    [switch]$AllowMissingNative,
    [switch]$WithoutLsplant,
    [switch]$IncludeHideLkm,
    [string]$HideLabZip = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$module = Join-Path $root 'module'
$dist = Join-Path $root 'dist'
$prop = Join-Path $module 'module.prop'
$hideTemp = $null
$manifestTemp = $null
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
if ($IncludeHideLkm -and $Abi -notcontains 'arm64-v8a') {
    throw 'Hide LKM packages require arm64-v8a.'
}
if ($IncludeHideLkm) {
    if (-not $HideLabZip) {
        $candidate = Get-ChildItem -LiteralPath (Join-Path $root 'build/device-evidence') -Recurse -File -Filter 'pathguard-hide1-lab-myron-*.zip' |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $candidate) { throw 'No myron HideLab ZIP was found under build/device-evidence.' }
        $HideLabZip = $candidate.FullName
    } elseif (-not [IO.Path]::IsPathRooted($HideLabZip)) {
        $HideLabZip = Join-Path $root $HideLabZip
    }
    if (-not (Test-Path -LiteralPath $HideLabZip -PathType Leaf)) {
        throw "HideLab package not found: $HideLabZip"
    }
    $hideTemp = Join-Path ([IO.Path]::GetTempPath()) ('pathguard-hide1-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $hideTemp | Out-Null
    Expand-Archive -LiteralPath $HideLabZip -DestinationPath $hideTemp
    foreach ($required in @('bin/pathguard_hide1.ko','bin/hide1_control','bin/hide1ctl','config/hide1_device_profile.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $hideTemp $required) -PathType Leaf)) {
            Remove-Item -LiteralPath $hideTemp -Recurse -Force -ErrorAction SilentlyContinue
            throw "HideLab package is missing $required"
        }
    }
    $hideProfile = Get-Content -Raw -LiteralPath (Join-Path $hideTemp 'config/hide1_device_profile.json') | ConvertFrom-Json
    if ($hideProfile.kernel_release -ne '6.12.23-android16-5-g16e473de48a3-abogki462654244-4k' -or
        $hideProfile.device -ne 'myron' -or $hideProfile.arch -ne 'aarch64') {
        Remove-Item -LiteralPath $hideTemp -Recurse -Force -ErrorAction SilentlyContinue
        throw 'HideLab profile is not for myron/android16-6.12.'
    }
    $hideKo = Join-Path $hideTemp 'bin/pathguard_hide1.ko'
    $hideKoHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $hideKo).Hash.ToLowerInvariant()
    if ($hideKoHash -ne $hideProfile.module_sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $hideTemp -Recurse -Force -ErrorAction SilentlyContinue
        throw 'HideLab profile module hash does not match pathguard_hide1.ko.'
    }
}
$daemonPath = Join-Path $module 'bin/arm64-v8a/pathguardd'
$daemonHash = if (Test-Path -LiteralPath $daemonPath -PathType Leaf) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $daemonPath).Hash.ToLowerInvariant()
} else { '' }
$manifest = [ordered]@{
    schema = 1
    package = 'pathguard_next'
    package_version = $version
    device = if ($IncludeHideLkm) { $hideProfile.device } else { 'generic' }
    fingerprint = if ($IncludeHideLkm) { $hideProfile.fingerprint } else { '' }
    kernel_release = if ($IncludeHideLkm) { $hideProfile.kernel_release } else { '' }
    kmi = if ($IncludeHideLkm) { $hideProfile.kmi } else { '' }
    toolchain = if ($IncludeHideLkm -and $hideProfile.toolchain) { $hideProfile.toolchain } else { 'android-clang-r536225-clang-19.0.1' }
    module_sha256 = if ($IncludeHideLkm) { $hideKoHash } else { '' }
    daemon_sha256 = $daemonHash
    supported_backends = [ordered]@{
        deny = 'provider'
        redirect = 'provider'
        hide = if ($IncludeHideLkm) { 'direct_vfs:myron/android16-6.12' } else { 'unsupported' }
    }
    hide_abi_version = if ($IncludeHideLkm -and $hideProfile.abi_version) { $hideProfile.abi_version } else { $null }
    product_status = if ($IncludeHideLkm) { 'supported_scope_myron' } else { 'provider_only' }
    admission_bundled = $false
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
if ($WithoutLsplant) {
    $archiveName = $archiveName -replace '\.zip$', '-no-lsplant.zip'
}
if ($IncludeHideLkm) {
    $archiveName = $archiveName -replace '\.zip$', '-hide1-myron.zip'
}
$zip = Join-Path $dist $archiveName
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    foreach ($file in @('module.prop','customize.sh','post-fs-data.sh','service.sh','boot-completed.sh','action.sh','uninstall.sh','skip_mount')) {
        Add-File $archive (Join-Path $module $file) $file
    }
    Get-ChildItem -LiteralPath (Join-Path $module 'config') -File -Recurse | ForEach-Object {
        if ($_.Name -ne 'hide1_auto_admit') {
            Add-File $archive $_.FullName $_.FullName.Substring($module.Length + 1).Replace('\','/')
        }
    }
    foreach ($item in $Abi) {
        Add-File $archive (Join-Path $module "zygisk/$item.so") "zygisk/$item.so"
        Add-File $archive (Join-Path $module "bin/$item/pathguardd") "bin/$item/pathguardd"
        Add-File $archive (Join-Path $module "bin/$item/pathguardctl") "bin/$item/pathguardctl"
        if (-not $WithoutLsplant) {
            Add-File $archive (Join-Path $module "provider/$item/libpathguard_lsplant.so") `
                "provider/$item/libpathguard_lsplant.so"
        }
    }
    if (-not $WithoutLsplant) {
        Add-File $archive (Join-Path $module 'provider/provider-hooker.dex') `
            'provider/provider-hooker.dex'
    }
    Add-File $archive (Join-Path $module 'THIRD_PARTY_NOTICES.md') `
        'THIRD_PARTY_NOTICES.md'
    if (-not $WithoutLsplant) {
        Add-File $archive (Join-Path $root 'third_party/lsplant/LICENSE') `
            'licenses/LSPlant-LGPL-3.0.txt'
        Add-File $archive (Join-Path $root 'third_party/dobby/LICENSE') `
            'licenses/Dobby-Apache-2.0.txt'
        Add-File $archive (Join-Path $root 'third_party/xdl/LICENSE') `
            'licenses/xDL-MIT.txt'
    }
    $manifestEntry = $archive.CreateEntry('build-manifest.json')
    $manifestWriter = New-Object System.IO.StreamWriter($manifestEntry.Open())
    try { $manifestWriter.Write(($manifest | ConvertTo-Json -Depth 6)) } finally { $manifestWriter.Dispose() }
    if ($IncludeHideLkm) {
        Add-File $archive (Join-Path $hideTemp 'bin/pathguard_hide1.ko') 'bin/pathguard_hide1.ko'
        Add-File $archive (Join-Path $hideTemp 'bin/hide1_control') 'bin/hide1_control'
        Add-File $archive (Join-Path $hideTemp 'bin/hide1ctl') 'bin/hide1ctl'
        Add-File $archive (Join-Path $hideTemp 'config/hide1_device_profile.json') 'config/hide1_device_profile.json'
        $autoAdmitPath = Join-Path $root 'module/config/hide1_auto_admit'
        Add-File $archive $autoAdmitPath 'config/hide1_auto_admit'
        $entry = $archive.CreateEntry('build-info.txt')
        $writer = New-Object System.IO.StreamWriter($entry.Open())
        try {
            $writer.WriteLine('package=pathguard_next_with_hide1_lkm')
            $writer.WriteLine('module_id=pathguard_next')
            $writer.WriteLine('device=myron')
            $writer.WriteLine('kernel_release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k')
            $writer.WriteLine("hide_module_sha256=$hideKoHash")
            $writer.WriteLine('hide_activation=admission-and-rules-controlled')
            $writer.WriteLine('automatic_admission=fixed-device-profile-current-boot')
            $writer.WriteLine('product_status=supported_scope_myron')
            $writer.WriteLine('admission_bundled=false')
            $writer.WriteLine("fingerprint=$($hideProfile.fingerprint)")
            $writer.WriteLine("kmi=$($hideProfile.kmi)")
            $writer.WriteLine("toolchain=$($manifest.toolchain)")
            $writer.WriteLine("daemon_sha256=$daemonHash")
            $writer.WriteLine('supported_backends=deny:provider;redirect:provider;hide:direct_vfs:myron/android16-6.12')
        } finally { $writer.Dispose() }
    }
} finally {
    $archive.Dispose()
    if ($hideTemp) { Remove-Item -LiteralPath $hideTemp -Recurse -Force -ErrorAction SilentlyContinue }
}
Write-Host "Created: $zip"
if ($IncludeHideLkm) { Write-Host "Hide LKM SHA-256: $hideKoHash" }
