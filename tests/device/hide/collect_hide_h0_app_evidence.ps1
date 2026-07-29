param(
    [string]$OutputDirectory = 'build/device-evidence/hide-h0-selector'
)

$ErrorActionPreference = 'Stop'
$package = 'dev.pathguard.hideprobe'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice(?:\s|$)"
})
if ($devices.Count -ne 1) {
    throw "hide H0 collector requires exactly one ready device, got $($devices.Count)"
}
if (-not ((& $adb shell pm path $package) -join '').Trim()) {
    throw 'hide H0 app probe is not installed'
}

$stamp = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$runOutput = Join-Path (Join-Path $root $OutputDirectory) $stamp
New-Item -ItemType Directory -Force -Path $runOutput | Out-Null

foreach ($name in @('metadata.json', 'observations.jsonl', 'selector-observations.jsonl')) {
    & $adb shell run-as $package test -f "files/hide-h0/$name"
    if ($LASTEXITCODE -eq 0) {
        & $adb exec-out run-as $package cat "files/hide-h0/$name" |
            Set-Content -LiteralPath (Join-Path $runOutput $name) -Encoding utf8
    }
}
& $adb shell dumpsys package $package |
    Set-Content -LiteralPath (Join-Path $runOutput 'package.txt') -Encoding utf8
& $adb shell appops get $package |
    Set-Content -LiteralPath (Join-Path $runOutput 'appops.txt') -Encoding utf8

$selectorPath = Join-Path $runOutput 'selector-observations.jsonl'
if (Test-Path -LiteralPath $selectorPath) {
    Get-Content -LiteralPath $selectorPath |
        Where-Object { $_.Trim() } |
        ForEach-Object { $_ | ConvertFrom-Json -ErrorAction Stop } |
        Out-Null
}
Write-Output $runOutput
