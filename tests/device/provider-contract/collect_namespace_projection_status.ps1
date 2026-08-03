param(
    [string]$OutputDirectory = 'build/device-evidence/provider-namespace-v1',
    [ValidateRange(1, 16)]
    [int]$RequireNamespaceCount = 1,
    [string]$ExpectedVersion = '0.1.46-dev'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (
    Split-Path -Parent $MyInvocation.MyCommand.Path)))
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 |
    Where-Object { $_ -match '\tdevice$' })
if ($devices.Count -ne 1) {
    throw "namespace probe requires exactly one ready device, got $($devices.Count)"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$output = Join-Path $root "$OutputDirectory/$stamp"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$logStart = Get-Date -Format 'MM-dd HH:mm:ss.fff'

$mediaQuery = 'content query --uri content://media/external_primary/file ' +
    '--user 0 --projection _id:_data:relative_path:_display_name ' +
    '--where "_data LIKE ''%/_pg/v1/ns_%''"'
& $adb shell $mediaQuery 2>&1 |
    Set-Content -LiteralPath (Join-Path $output 'media-namespace-query.txt') `
        -Encoding utf8
$documentsQuery = 'content query --uri ' +
    'content://com.android.externalstorage.documents/document/' +
    'primary%3ADownload%2Flocalsend-redirect/children --user 0 ' +
    '--projection document_id:_display_name:mime_type'
& $adb shell su -c $documentsQuery 2>&1 |
    Set-Content -LiteralPath (Join-Path $output 'documents-download.txt') `
        -Encoding utf8

$module = (& $adb shell su -c `
    'cat /data/adb/modules/pathguard_next/module.prop' 2>&1 | Out-String)
$module | Set-Content -LiteralPath (Join-Path $output 'module.prop') `
    -Encoding utf8
$status = (& $adb shell su -c `
    'cat /data/adb/modules/pathguard_next/run/status/*.status' 2>&1 | Out-String)
$status | Set-Content -LiteralPath (Join-Path $output 'provider-status.txt') `
    -Encoding utf8

$namespaceTree = (& $adb shell su -c `
    'find /storage/emulated/0/Download/localsend-redirect/_pg/v1 -maxdepth 4 -print 2>/dev/null | sort' `
    2>&1 | Out-String)
$namespaceTree | Set-Content -LiteralPath `
    (Join-Path $output 'namespace-tree.txt') -Encoding utf8
$flatFiles = (& $adb shell su -c `
    'find /storage/emulated/0/Download/localsend-redirect -mindepth 1 -maxdepth 1 ! -name _pg -print 2>/dev/null | sort' `
    2>&1 | Out-String)
$flatFiles | Set-Content -LiteralPath (Join-Path $output 'flat-target.txt') `
    -Encoding utf8
& $adb shell su -c `
    'find /storage/emulated/0/Download/localsend-source /storage/emulated/0/Pictures -maxdepth 2 -type f -print 2>/dev/null | sort' `
    2>&1 | Set-Content -LiteralPath (Join-Path $output 'logical-sources.txt') `
        -Encoding utf8

$logcat = (& $adb logcat -d -v threadtime -t $logStart 2>&1 | Out-String)
$filtered = (($logcat -split '\r?\n') | Select-String -Pattern @(
    'PathGuard', 'PathGuardLsplant', 'ProviderHooker', 'FATAL EXCEPTION',
    'Process: com.android.externalstorage',
    'Process: com.android.providers.media.module',
    'JNI WARNING', 'namespace'
) | ForEach-Object { $_.Line }) -join [Environment]::NewLine
Set-Content -LiteralPath (Join-Path $output 'logcat.txt') `
    -Value $filtered -Encoding utf8 -NoNewline

if ($module -notmatch ('(?m)^version=' + [regex]::Escape($ExpectedVersion) + '\s*$')) {
    throw "$ExpectedVersion is not active; evidence: $output"
}
foreach ($process in @(
    'process=com.android.externalstorage',
    'process=com.android.providers.media.module')) {
    if ($status -notmatch [regex]::Escape($process)) {
        throw "missing Provider status '$process'; evidence: $output"
    }
}
$namespaceIds = [regex]::Matches(
    $namespaceTree, '(?m)/ns_([a-z2-7]{26})(?:/|\s*$)') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
if ($namespaceIds.Count -lt $RequireNamespaceCount) {
    throw "observed $($namespaceIds.Count) Namespace(s), expected at least $RequireNamespaceCount; evidence: $output"
}
if ($namespaceTree -notmatch '(?m)/ns_[a-z2-7]{26}/.+$') {
    throw "no file observed below a valid Namespace; evidence: $output"
}
$mediaEvidence = Get-Content -LiteralPath `
    (Join-Path $output 'media-namespace-query.txt') -Raw
if ($mediaEvidence -match '(?m)^usage: adb shell content ' -or
        $mediaEvidence -match '(?m)^\[ERROR\] Unsupported argument:') {
    throw "MediaStore namespace query was not executed; evidence: $output"
}
$documentsEvidence = Get-Content -LiteralPath `
    (Join-Path $output 'documents-download.txt') -Raw
if ($documentsEvidence -match 'Error while accessing provider:' -or
        $documentsEvidence -match 'SecurityException: Permission Denial') {
    throw "DocumentsProvider namespace query failed; evidence: $output"
}
if ($logcat -match '(?s)FATAL EXCEPTION:[^\r\n]*\r?\n[^\r\n]*Process: ' +
        '(com\.android\.externalstorage|com\.android\.providers\.media\.module)') {
    throw "Provider runtime fault detected; evidence: $output"
}

Write-Output $output
