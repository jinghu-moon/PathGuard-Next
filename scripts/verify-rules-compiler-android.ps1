param(
    [Parameter(Mandatory = $true)]
    [string]$HostProbe,
    [Parameter(Mandatory = $true)]
    [string]$AndroidProbe
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $HostProbe)) {
    throw "Host parity probe not found: $HostProbe"
}
if (-not (Test-Path -LiteralPath $AndroidProbe)) {
    throw "Android parity probe not found: $AndroidProbe"
}
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice\s*$"
})
if ($devices.Count -ne 1) {
    throw "Host/Android parity requires exactly one connected device, got $($devices.Count)"
}

$hostOutput = ((& $HostProbe) -join "`n").Trim()
if ($LASTEXITCODE -ne 0) { throw 'Host parity probe failed' }
$remote = '/data/local/tmp/pathguard_rules_parity_probe'
& $adb push $AndroidProbe $remote | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'adb push parity probe failed' }
try {
    & $adb shell chmod 700 $remote
    if ($LASTEXITCODE -ne 0) { throw 'cannot chmod Android parity probe' }
    $androidOutput = ((& $adb shell $remote) -join "`n").Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Android parity probe failed' }
    if ($androidOutput -ne $hostOutput) {
        throw "Host/Android compiler output mismatch`nHOST:`n$hostOutput`nANDROID:`n$androidOutput"
    }
    Write-Host 'Host/Android rules compiler bytes, diagnostics and requirements: identical'
} finally {
    & $adb shell rm -f $remote | Out-Null
}
