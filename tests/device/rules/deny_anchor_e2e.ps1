param(
    [Parameter(Mandatory = $true)] [string]$Probe
)

$ErrorActionPreference = 'Stop'
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice\s*$"
})
if ($devices.Count -ne 1) {
    throw "deny anchor e2e requires exactly one device, got $($devices.Count)"
}
if (-not (Test-Path -LiteralPath $Probe)) { throw "probe not found: $Probe" }
$remote = '/data/local/tmp/pathguard-deny-anchor-probe'
try {
    & $adb push $Probe $remote | Out-Null
    & $adb shell chmod 700 $remote
    $result = (& $adb shell "su -c $remote") -join "`n"
    if ($LASTEXITCODE -ne 0 -or $result -notmatch 'deny-anchor passed') {
        throw "deny anchor e2e failed: $result"
    }
    Write-Host $result
} finally {
    & $adb shell rm -f $remote | Out-Null
}
