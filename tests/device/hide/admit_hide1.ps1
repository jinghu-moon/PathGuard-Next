param(
    [string]$Allowlist = "$PSScriptRoot/hide1_device_kmi_allowlist.json",
    [string]$OutputDirectory = 'build/device-evidence/hide1-admission'
)

$ErrorActionPreference = 'Stop'
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice(?:\s|$)' })
if ($devices.Count -ne 1) { throw "Hide 1.0 admission requires exactly one ready device, got $($devices.Count)" }
$serial = ($devices[0] -split '\s+')[0]

function Adb-Get([string[]]$Arguments) {
    $value = (& $adb -s $serial @Arguments) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $($Arguments -join ' ')" }
    return $value.Trim()
}

function Adb-Test([string[]]$Arguments) {
    & $adb -s $serial @Arguments | Out-Null
    return $LASTEXITCODE -eq 0
}

$allow = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $Allowlist) | ConvertFrom-Json
$entry = @($allow.entries | Where-Object {
    $_.fingerprint -eq (Adb-Get @('shell', 'getprop', 'ro.build.fingerprint')) -and
    $_.kernel_release -eq (Adb-Get @('shell', 'uname', '-r'))
}) | Select-Object -First 1
$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$output = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $output | Out-Null

$module = 'missing'
if ((Adb-Test @('shell', 'test', '-d', '/sys/module/pathguard_hide1')) -and
    (Adb-Test @('shell', 'test', '-e', '/dev/pathguard_hide1'))) {
    $module = 'live'
}
$status = [ordered]@{
    schema = 1
    run_id = $runId
    serial = $serial
    device = Adb-Get @('shell', 'getprop', 'ro.product.device')
    fingerprint = Adb-Get @('shell', 'getprop', 'ro.build.fingerprint')
    kernel_release = Adb-Get @('shell', 'uname', '-r')
    kmi = if ($entry) { $entry.kmi } else { $null }
    module_vermagic_release = if ($entry) { $entry.module_vermagic_release } else { $null }
    reference_symbol_crcs = if ($entry) { $entry.reference_symbol_crcs } else { $null }
    allowlist_match = [bool]$entry
    module_state = if ($module) { $module } else { 'missing' }
    admission = if (-not $entry) { 'unsupported' } elseif ($module -ne 'live') { 'unsupported' } else { 'pending_hidelab' }
    ota_recheck_required = $true
    reason = if (-not $entry) { 'fingerprint_or_kernel_release_not_allowlisted' } elseif ($module -ne 'live') { 'pathguard_hide1_module_not_live' } else { 'HideLab full regression required' }
} | ConvertTo-Json
Set-Content -LiteralPath (Join-Path $output 'admission.json') -Value $status -Encoding utf8
$status
