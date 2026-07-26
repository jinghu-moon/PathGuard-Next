param(
    [Parameter(Mandatory = $true)]
    [string]$Package,
    [ValidateRange(1, 200)]
    [int]$Samples = 20,
    [string]$OutputDirectory = 'build/device-evidence'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$output = Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw 'adb not found'
}

$device = & adb get-state
if ($LASTEXITCODE -ne 0 -or $device.Trim() -ne 'device') {
    throw 'no ready adb device'
}

$metadata = @(
    "captured_at=$([DateTimeOffset]::Now.ToString('o'))"
    "package=$Package"
    "samples=$Samples"
    "fingerprint=$((& adb shell getprop ro.build.fingerprint).Trim())"
    "sdk=$((& adb shell getprop ro.build.version.sdk).Trim())"
    "kernel=$((& adb shell uname -a).Trim())"
    "selinux=$((& adb shell getenforce).Trim())"
)
$metadata | Set-Content -LiteralPath (Join-Path $output 'metadata.txt') -Encoding utf8

$summary = Join-Path $output 'pathguard-perf-lines.txt'
Remove-Item -LiteralPath $summary -ErrorAction SilentlyContinue
for ($index = 1; $index -le $Samples; ++$index) {
    & adb logcat -c
    & adb shell am force-stop $Package
    & adb shell monkey -p $Package -c android.intent.category.LAUNCHER 1 | Out-Null
    Start-Sleep -Seconds 2
    $log = & adb logcat -d -v threadtime
    $samplePath = Join-Path $output ("sample-{0:D3}.log" -f $index)
    $log | Set-Content -LiteralPath $samplePath -Encoding utf8
    $log | Select-String -Pattern 'PathGuard.*(perf topology_candidate|perf probe_warm|perf probe_total|perf companion|perf zygisk_post|probe_runtime|mount_step)' |
        ForEach-Object { "sample=$index $($_.Line)" } |
        Add-Content -LiteralPath $summary -Encoding utf8
}

Write-Output $output
