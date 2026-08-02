param(
    [string]$OutputDirectory = 'build/device-evidence/provider-lsplant-v1'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (
    Split-Path -Parent $MyInvocation.MyCommand.Path)))
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 |
    Where-Object { $_ -match '\tdevice$' })
if ($devices.Count -ne 1) {
    throw "provider LSPlant probe requires exactly one ready device, got $($devices.Count)"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$output = Join-Path $root "$OutputDirectory/$stamp"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$logStart = Get-Date -Format 'MM-dd HH:mm:ss.fff'

& $adb shell content query `
    --uri 'content://media/external_primary/images/media' `
    --user 0 `
    --projection '_id' `
    --where '_id=-1' 2>&1 |
    Set-Content -LiteralPath (Join-Path $output 'trigger-media.txt') -Encoding utf8
& $adb shell content query `
    --uri 'content://com.android.externalstorage.documents/root' `
    --user 0 2>&1 |
    Set-Content -LiteralPath (Join-Path $output 'trigger-documents-root.txt') -Encoding utf8
& $adb shell content query `
    --uri 'content://com.android.externalstorage.documents/document/primary%3ADownload' `
    --user 0 2>&1 |
    Set-Content -LiteralPath (Join-Path $output 'trigger-documents-download.txt') -Encoding utf8

& $adb shell su -c 'cat /data/adb/modules/pathguard_next/module.prop' |
    Set-Content -LiteralPath (Join-Path $output 'module.prop') -Encoding utf8
$status = ''
for ($attempt = 0; $attempt -lt 12; ++$attempt) {
    $status = (& $adb shell su -c 'cat /data/adb/modules/pathguard_next/run/status/*.status' 2>&1 | Out-String)
    if ($status -match 'process=com.android.externalstorage' -and
        $status -match 'process=com.android.providers.media.module') {
        break
    }
    Start-Sleep -Milliseconds 750
}
$status | Set-Content -LiteralPath (Join-Path $output 'provider-status.txt') -Encoding utf8
$logcat = (& $adb logcat -d -v threadtime -t $logStart 2>&1 | Out-String)
($logcat -split '\r?\n') |
    Select-String -Pattern @(
        'PathGuard',
        'PathGuardLsplant',
        'ProviderHooker',
        'ClassNotFoundException',
        'NoSuchMethodException',
        'FATAL EXCEPTION',
        'Process: com.android.externalstorage',
        'Process: com.android.providers.media.module',
        'null receiver',
        'JNI WARNING: DeleteLocalRef'
    ) |
    Set-Content -LiteralPath (Join-Path $output 'logcat.txt') -Encoding utf8

$required = @(
    'process=com.android.externalstorage',
    'process=com.android.providers.media.module',
    'provider_bridge_build_matched=true',
    'provider_bridge_library_loaded=true',
    'provider_bridge_lsplant_initialized=true',
    'provider_bridge_hooker_dex_loaded=true',
    'provider_bridge_errno=0'
)
foreach ($value in $required) {
    if ($status -notmatch [regex]::Escape($value)) {
        throw "missing Provider LSPlant status '$value'; evidence: $output"
    }
}
if ($status -notmatch 'provider_bridge_self_tested_hooks=3' -or
    $status -notmatch 'provider_bridge_self_tested_hooks=2044') {
    throw "Provider LSPlant method groups are incomplete; evidence: $output"
}
foreach ($match in [regex]::Matches($status, '(?m)^observed_capabilities=(\d+)$')) {
    if (([uint64]$match.Groups[1].Value -band [uint64]131072) -ne 0) {
        throw "passthrough bridge incorrectly enabled capability bit 17; evidence: $output"
    }
}
$providerFatal = '(?s)FATAL EXCEPTION:[^\r\n]*\r?\n[^\r\n]*Process: ' +
    '(com\.android\.externalstorage|com\.android\.providers\.media\.module)'
if ($logcat -match $providerFatal -or
    $logcat -match '(?s)null receiver.*ProviderHooker\.callback' -or
    $logcat -match '(?m)(externalstorage|MediaProvider).*JNI WARNING: DeleteLocalRef') {
    throw "Provider LSPlant runtime fault detected; evidence: $output"
}

Write-Output $output
