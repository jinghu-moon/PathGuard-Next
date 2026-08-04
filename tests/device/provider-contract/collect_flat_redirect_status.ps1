param(
    [string]$OutputDirectory = 'build/device-evidence/provider-flat-v1',
    [ValidateRange(1, 32)]
    [int]$RequireFlatFileCount = 3,
    [string]$ExpectedVersion = '0.1.56-dev'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (
    Split-Path -Parent $MyInvocation.MyCommand.Path)))
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 |
    Where-Object { $_ -match '\sdevice(?:\s|$)' })
if ($devices.Count -ne 1) {
    throw "flat redirect probe requires exactly one ready device, got $($devices.Count)"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$output = Join-Path $root "$OutputDirectory/$stamp"
New-Item -ItemType Directory -Force -Path $output | Out-Null

$module = (& $adb shell su -c `
    'cat /data/adb/modules/pathguard_next/module.prop' 2>&1 | Out-String)
$module | Set-Content -LiteralPath (Join-Path $output 'module.prop') `
    -Encoding utf8
$status = (& $adb shell su -c `
    'cat /data/adb/modules/pathguard_next/run/status/*.status' 2>&1 | Out-String)
$status | Set-Content -LiteralPath (Join-Path $output 'runtime-status.txt') `
    -Encoding utf8

$flatFiles = (& $adb shell su -c `
    'find /storage/emulated/0/Download/localsend-redirect -mindepth 1 -maxdepth 1 -type f -print 2>/dev/null | sort' `
    2>&1 | Out-String)
$flatFiles | Set-Content -LiteralPath (Join-Path $output 'flat-target-files.txt') `
    -Encoding utf8
$legacyFiles = (& $adb shell su -c `
    'find /storage/emulated/0/Download/localsend-redirect/_pg -type f -print 2>/dev/null | sort' `
    2>&1 | Out-String)
$legacyFiles | Set-Content -LiteralPath (Join-Path $output 'legacy-namespace-files.txt') `
    -Encoding utf8
& $adb shell su -c `
    'find /storage/emulated/0/Download/localsend-source -maxdepth 2 -type f -print 2>/dev/null; find /storage/emulated/0/Pictures -maxdepth 1 -type f -print 2>/dev/null' `
    2>&1 | Set-Content -LiteralPath (Join-Path $output 'physical-source-files.txt') `
        -Encoding utf8

$providerMapLines = foreach ($process in @(
    'com.android.providers.media.module',
    'com.android.externalstorage')) {
    $processId = ((& $adb shell pidof $process 2>&1 | Out-String).Trim())
    "process=$process pid=$processId"
    if ($processId -match '^\d+$') {
        $maps = @(& $adb shell su -c "cat /proc/$processId/maps" 2>&1)
        if ($LASTEXITCODE -ne 0) {
            "maps_error=$($maps -join ' ')"
        } else {
            $maps | Select-String -Pattern 'lsplant|provider-hooker' |
                ForEach-Object { $_.Line }
        }
    }
}
$providerMaps = ($providerMapLines -join [Environment]::NewLine)
$providerMaps | Set-Content -LiteralPath (Join-Path $output 'provider-lsplant-maps.txt') `
    -Encoding utf8
$logcat = (& $adb logcat -d -v threadtime -t 4000 2>&1 | Out-String)
$filtered = (($logcat -split '\r?\n') | Select-String -Pattern @(
    'PathGuard', 'PathGuardLsplant', 'ProviderHooker', 'FATAL EXCEPTION',
    'Process: com.android.externalstorage',
    'Process: com.android.providers.media.module', 'JNI WARNING'
) | ForEach-Object { $_.Line }) -join [Environment]::NewLine
Set-Content -LiteralPath (Join-Path $output 'logcat.txt') `
    -Value $filtered -Encoding utf8 -NoNewline

if ($module -notmatch ('(?m)^version=' + [regex]::Escape($ExpectedVersion) + '\s*$')) {
    throw "$ExpectedVersion is not active; evidence: $output"
}
$records = @($status -split '(?m)(?=^schema=pathguard\.runtime_status\.v2\s*$)')
foreach ($process in @(
    'com.android.providers.media.module',
    'com.android.externalstorage')) {
    $record = $records | Where-Object {
        $_ -match ('(?m)^process=' + [regex]::Escape($process) + '\s*$')
    } | Select-Object -Last 1
    if (-not $record) {
        throw "missing Provider status '$process'; evidence: $output"
    }
    if ($record -notmatch '(?m)^enforcement=active\s*$' -or
        $record -notmatch '(?m)^provider_bridge_lsplant_initialized=false\s*$' -or
        $record -notmatch '(?m)^provider_bridge_library_loaded=false\s*$') {
        throw "Provider redirect/LSPlant separation failed for '$process'; evidence: $output"
    }
}
$flat = @($flatFiles -split '\r?\n' | Where-Object { $_.Trim() -ne '' })
if ($flat.Count -lt $RequireFlatFileCount) {
    throw "observed $($flat.Count) flat file(s), expected at least $RequireFlatFileCount; evidence: $output"
}
if ($legacyFiles.Trim() -ne '') {
    throw "legacy Namespace contains files; evidence: $output"
}
if ($providerMaps -match '(?im)(libpathguard_lsplant|provider-hooker)') {
    throw "LSPlant is mapped in a Provider process; evidence: $output"
}
if ($providerMaps -match '(?m)^maps_error=') {
    throw "Provider maps could not be read; evidence: $output"
}
foreach ($process in @(
    'com.android.providers.media.module',
    'com.android.externalstorage')) {
    if ($providerMaps -notmatch ('(?m)^process=' + [regex]::Escape($process) +
            ' pid=\d+\s*$')) {
        throw "Provider maps were not collected for '$process'; evidence: $output"
    }
}
if ($logcat -match '(?s)FATAL EXCEPTION:[^\r\n]*\r?\n[^\r\n]*Process: ' +
        '(com\.android\.externalstorage|com\.android\.providers\.media\.module)') {
    throw "Provider runtime fault detected; evidence: $output"
}

Write-Output $output
