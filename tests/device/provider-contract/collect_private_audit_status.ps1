param(
    [string]$OutputDirectory = 'build/device-evidence/private-audit-v1',
    [ValidateRange(1, 1000)]
    [int]$RequireRecordCount = 1,
    [string[]]$RequireSourcePrefix = @(
        '/storage/emulated/0/Download/localsend-source/',
        '/storage/emulated/0/Pictures/'
    ),
    [string]$ExpectedTargetPrefix =
        '/storage/emulated/0/Download/localsend-redirect/',
    [string]$ExpectedVersion = '0.1.56-dev'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (
    Split-Path -Parent $MyInvocation.MyCommand.Path)))
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 |
    Where-Object { $_ -match '\sdevice(?:\s|$)' })
if ($devices.Count -ne 1) {
    throw "private audit probe requires exactly one ready device, got $($devices.Count)"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$output = Join-Path $root "$OutputDirectory/$stamp"
New-Item -ItemType Directory -Force -Path $output | Out-Null

$module = (& $adb shell su -c `
    'cat /data/adb/modules/pathguard_next/module.prop' 2>&1 | Out-String)
$module | Set-Content -LiteralPath (Join-Path $output 'module.prop') `
    -Encoding utf8
if ($module -notmatch ('(?m)^version=' + [regex]::Escape($ExpectedVersion) + '\s*$')) {
    throw "$ExpectedVersion is not active; evidence: $output"
}

$deviceAbi = ((& $adb shell getprop ro.product.cpu.abi 2>&1 | Out-String).Trim())
$moduleAbi = switch -Regex ($deviceAbi) {
    '^arm64-v8a$' { 'arm64-v8a'; break }
    '^armeabi-v7a$' { 'armeabi-v7a'; break }
    '^x86_64$' { 'x86_64'; break }
    '^x86$' { 'x86'; break }
    default { throw "unsupported device ABI '$deviceAbi'; evidence: $output" }
}
$control = "/data/adb/modules/pathguard_next/bin/$moduleAbi/pathguardctl"
$auditText = (& $adb shell su -c `
    "$control audit /data/adb/modules/pathguard_next --json" `
    2>&1 | Out-String).Trim()
$auditText | Set-Content -LiteralPath (Join-Path $output 'private-audit.json') `
    -Encoding utf8
(& $adb shell "su -c 'ls -ld /data/adb/modules/pathguard_next/run /data/adb/modules/pathguard_next/run/audit.sock /data/adb/modules/pathguard_next/run/audit-v1.wal 2>&1'" `
    2>&1 | Out-String) | Set-Content -LiteralPath `
    (Join-Path $output 'private-store-permissions.txt') -Encoding utf8
(& $adb logcat -d -v threadtime -t 8000 2>&1 | Out-String) -split '\r?\n' |
    Select-String -Pattern @('private audit', 'audit server unavailable', 'PathGuard') |
    ForEach-Object { $_.Line } |
    Set-Content -LiteralPath (Join-Path $output 'audit-logcat.txt') -Encoding utf8

try {
    $audit = $auditText | ConvertFrom-Json -ErrorAction Stop
} catch {
    throw "private audit output is not valid JSON; evidence: $output"
}
if ($audit.schema -ne 'pathguard.private-audit.v1') {
    throw "unexpected private audit schema; evidence: $output"
}
$records = @($audit.records)
if ($records.Count -lt $RequireRecordCount) {
    throw "observed $($records.Count) audit record(s), expected at least $RequireRecordCount; evidence: $output"
}
foreach ($record in $records) {
    if (-not ([string]$record.target).StartsWith(
            $ExpectedTargetPrefix, [StringComparison]::Ordinal)) {
        throw "audit target escaped the flat redirect root; evidence: $output"
    }
}
foreach ($prefix in $RequireSourcePrefix) {
    $matched = @($records | Where-Object {
        ([string]$_.logical_source).StartsWith(
            $prefix, [StringComparison]::Ordinal)
    })
    if ($matched.Count -eq 0) {
        throw "missing audit source prefix '$prefix'; evidence: $output"
    }
}

Write-Output $output
