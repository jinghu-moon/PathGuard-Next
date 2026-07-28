param(
    [Parameter(Mandatory = $true)]
    [string]$Apk,
    [string[]]$ObservePath = @(
        '/storage/emulated/0/Pictures/Nagram',
        '/storage/emulated/0/DCIM/Screenshots'
    ),
    [string]$OutputDirectory = 'build/device-evidence/hide-h0-app',
    [switch]$GrantReadMediaImages,
    [switch]$GrantAllFilesAccess,
    [ValidateRange(10, 180)]
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
$package = 'dev.pathguard.hideprobe'
$component = "$package/.ProbeActivity"
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$apkPath = (Resolve-Path -LiteralPath $Apk -ErrorAction Stop).Path
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice(?:\s|$)"
})
if ($devices.Count -ne 1) {
    throw "hide H0 app probe requires exactly one ready device, got $($devices.Count)"
}
foreach ($path in $ObservePath) {
    if ($path -notmatch '^/[^\r\n,]+$') {
        throw "observe path must be an absolute path without control characters or commas: $path"
    }
}

$stamp = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$runOutput = Join-Path (Join-Path $root $OutputDirectory) $stamp
New-Item -ItemType Directory -Force -Path $runOutput | Out-Null

& $adb install -r $apkPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cannot install hide H0 app probe' }
if ($GrantReadMediaImages) {
    & $adb shell pm grant $package android.permission.READ_MEDIA_IMAGES
    if ($LASTEXITCODE -ne 0) { throw 'cannot grant READ_MEDIA_IMAGES to hide H0 app probe' }
}
if ($GrantAllFilesAccess) {
    & $adb shell appops set $package MANAGE_EXTERNAL_STORAGE allow
    if ($LASTEXITCODE -ne 0) { throw 'cannot grant MANAGE_EXTERNAL_STORAGE app-op' }
}

& $adb shell am force-stop $package
$joinedPaths = $ObservePath -join ','
& $adb shell am start -W -n $component --esa observe_paths $joinedPaths | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'cannot start hide H0 app probe' }

$deadline = [DateTimeOffset]::Now.AddSeconds($TimeoutSeconds)
$status = ''
do {
    Start-Sleep -Milliseconds 250
    $status = ((& $adb shell run-as $package cat files/hide-h0/status 2>$null) -join '').Trim()
    if ($status.StartsWith('failed:')) { throw $status }
} while ($status -ne 'complete' -and [DateTimeOffset]::Now -lt $deadline)
if ($status -ne 'complete') {
    throw "hide H0 app probe timed out after $TimeoutSeconds seconds"
}

& $adb exec-out run-as $package cat files/hide-h0/metadata.json |
    Set-Content -LiteralPath (Join-Path $runOutput 'metadata.json') -Encoding utf8
& $adb exec-out run-as $package cat files/hide-h0/observations.jsonl |
    Set-Content -LiteralPath (Join-Path $runOutput 'observations.jsonl') -Encoding utf8
& $adb shell dumpsys package $package |
    Set-Content -LiteralPath (Join-Path $runOutput 'package.txt') -Encoding utf8
& $adb shell appops get $package |
    Set-Content -LiteralPath (Join-Path $runOutput 'appops.txt') -Encoding utf8

$rows = @(Get-Content -LiteralPath (Join-Path $runOutput 'observations.jsonl') |
    Where-Object { $_.Trim() } |
    ForEach-Object { $_ | ConvertFrom-Json -ErrorAction Stop })
if ($rows.Count -eq 0) { throw 'hide H0 app probe emitted no observations' }
if (@($rows | Where-Object { $_.schema -ne 1 }).Count -ne 0) {
    throw 'hide H0 app probe emitted an unsupported schema version'
}
Write-Output $runOutput
