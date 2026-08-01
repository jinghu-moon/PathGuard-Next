param(
    [string]$Apk = 'tests/device/provider-contract/app/build/outputs/apk/debug/providerContract-debug.apk',
    [string]$OutputDirectory = 'build/device-evidence/provider-contract-v1',
    [ValidateRange(30, 300)]
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$package = 'dev.pathguard.providercontract'
$component = "$package/.ProbeActivity"
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$apkPath = (Resolve-Path -LiteralPath (Join-Path $root $Apk) -ErrorAction Stop).Path
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice(?:\s|$)"
})
if ($devices.Count -ne 1) {
    throw "provider contract probe requires exactly one ready device, got $($devices.Count)"
}

$stamp = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$runOutput = Join-Path (Join-Path $root $OutputDirectory) $stamp
New-Item -ItemType Directory -Force -Path $runOutput | Out-Null

& $adb install -r $apkPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cannot install provider contract probe' }
& $adb shell am force-stop $package

Write-Host '设备即将打开系统目录选择器。请选择一个允许创建和删除临时文件的测试目录。'
& $adb shell am start -W -n $component --ez auto_select_saf true | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cannot start provider contract probe' }

$deadline = [DateTimeOffset]::Now.AddSeconds($TimeoutSeconds)
$status = ''
do {
    Start-Sleep -Milliseconds 500
    $status = ((& $adb shell run-as $package cat files/provider-contract/status 2>$null) -join '').Trim()
} while (
    $status -notin @('complete', 'failed', 'cancelled') -and
    [DateTimeOffset]::Now -lt $deadline
)

foreach ($name in @('metadata.json', 'observations.jsonl', 'status')) {
    & $adb exec-out run-as $package cat "files/provider-contract/$name" |
        Set-Content -LiteralPath (Join-Path $runOutput $name) -Encoding utf8
}
& $adb shell getprop ro.build.fingerprint |
    Set-Content -LiteralPath (Join-Path $runOutput 'fingerprint.txt') -Encoding utf8
& $adb shell getprop ro.build.version.sdk |
    Set-Content -LiteralPath (Join-Path $runOutput 'sdk.txt') -Encoding utf8
& $adb shell uname -a |
    Set-Content -LiteralPath (Join-Path $runOutput 'kernel.txt') -Encoding utf8
& $adb shell dumpsys package $package |
    Set-Content -LiteralPath (Join-Path $runOutput 'probe-package.txt') -Encoding utf8
& $adb shell dumpsys package providers |
    Set-Content -LiteralPath (Join-Path $runOutput 'providers.txt') -Encoding utf8
& $adb shell pm list packages --apex-only --show-versioncode |
    Set-Content -LiteralPath (Join-Path $runOutput 'apex-packages.txt') -Encoding utf8

if ($status -ne 'complete') {
    throw "provider contract probe ended with status '$status'; evidence: $runOutput"
}

$rows = @(Get-Content -LiteralPath (Join-Path $runOutput 'observations.jsonl') |
    Where-Object { $_.Trim() } |
    ForEach-Object { $_ | ConvertFrom-Json -ErrorAction Stop })
$required = @(
    'media_store:insert',
    'media_store:open_write',
    'media_store:query',
    'media_store:open_read',
    'media_store:rename',
    'media_store:delete',
    'documents_provider:create',
    'documents_provider:query',
    'documents_provider:open_write',
    'documents_provider:open_read',
    'documents_provider:rename',
    'documents_provider:delete'
)
$observed = @{}
foreach ($row in $rows) {
    $observed["$($row.domain):$($row.operation)"] = [bool]$row.passed
}
$missing = @($required | Where-Object {
    -not $observed.ContainsKey($_) -or -not $observed[$_]
})
if ($missing.Count -ne 0) {
    throw "provider contract operations failed or missing: $($missing -join ', '); evidence: $runOutput"
}

Write-Output $runOutput
