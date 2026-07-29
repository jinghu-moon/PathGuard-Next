param(
    [Parameter(Mandatory = $true)]
    [string]$Probe,
    [string[]]$ObservePath = @(),
    [string]$OutputDirectory = 'build/device-evidence/hide-h0'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$probePath = (Resolve-Path -LiteralPath $Probe -ErrorAction Stop).Path
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice(?:\s|$)"
})
if ($devices.Count -ne 1) {
    throw "hide H0 baseline requires exactly one ready device, got $($devices.Count)"
}
foreach ($path in $ObservePath) {
    if ($path -notmatch '^/[^\s''"]+$') {
        throw "observe path must be an absolute shell-safe path without whitespace or quotes: $path"
    }
}

$stamp = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$remoteProbe = "/data/local/tmp/pathguard-hide-h0-probe-$stamp"
$remoteSandbox = "/data/local/tmp/pathguard-hide-h0-$stamp"
$output = Join-Path $root $OutputDirectory
$runOutput = Join-Path $output $stamp
New-Item -ItemType Directory -Force -Path $runOutput | Out-Null

$serial = ((& $adb get-serialno) -join '').Trim()
$metadata = [ordered]@{
    schema = 1
    captured_at = [DateTimeOffset]::Now.ToString('o')
    execution_context = 'adb_shell'
    serial = $serial
    device = ((& $adb shell getprop ro.product.device) -join '').Trim()
    model = ((& $adb shell getprop ro.product.model) -join '').Trim()
    fingerprint = ((& $adb shell getprop ro.build.fingerprint) -join '').Trim()
    sdk = ((& $adb shell getprop ro.build.version.sdk) -join '').Trim()
    abi = ((& $adb shell getprop ro.product.cpu.abi) -join '').Trim()
    kernel = ((& $adb shell uname -a) -join '').Trim()
    selinux = ((& $adb shell getenforce) -join '').Trim()
    identity = ((& $adb shell id) -join '').Trim()
    observe_paths = @($ObservePath)
}
$metadata | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $runOutput 'metadata.json') -Encoding utf8

$command = "$remoteProbe --sandbox $remoteSandbox"
foreach ($path in $ObservePath) {
    $command += " --observe $path"
}

$observationsPath = Join-Path $runOutput 'observations.jsonl'
try {
    & $adb push $probePath $remoteProbe | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'cannot push hide H0 probe' }
    & $adb shell chmod 700 $remoteProbe
    if ($LASTEXITCODE -ne 0) { throw 'cannot chmod hide H0 probe' }
    & $adb shell mkdir $remoteSandbox
    if ($LASTEXITCODE -ne 0) { throw 'cannot create hide H0 sandbox' }
    & $adb shell chmod 700 $remoteSandbox
    if ($LASTEXITCODE -ne 0) { throw 'cannot chmod hide H0 sandbox' }

    $lines = @(& $adb shell $command)
    $probeExit = $LASTEXITCODE
    $lines | Set-Content -LiteralPath $observationsPath -Encoding utf8
    $parsed = @($lines | Where-Object { $_.Trim() } | ForEach-Object {
        $_ | ConvertFrom-Json -ErrorAction Stop
    })
    if ($probeExit -ne 0) {
        throw "hide H0 probe failed with exit code $probeExit"
    }
    if ($parsed.Count -eq 0 -or
        $parsed[-1].test -ne 'probe.complete' -or
        $parsed[-1].return_value -ne 0) {
        throw 'hide H0 probe did not emit a successful completion record'
    }
    if (@($parsed | Where-Object { $_.schema -ne 1 }).Count -ne 0) {
        throw 'hide H0 probe emitted an unsupported schema version'
    }
    Write-Output $runOutput
} finally {
    & $adb shell rmdir $remoteSandbox 2>$null | Out-Null
    & $adb shell rm -f $remoteProbe 2>$null | Out-Null
}
