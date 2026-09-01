param(
    [string]$OutputDirectory = 'build/device-evidence/audit-completion-trace-v1',
    [ValidateRange(15, 120)]
    [int]$DurationSeconds = 45,
    [string]$LocalSendPackage = 'org.localsend.localsend_app'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (
    Split-Path -Parent $MyInvocation.MyCommand.Path)))
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 |
    Where-Object { $_ -match '\sdevice(?:\s|$)' })
if ($devices.Count -ne 1) {
    throw "completion trace requires exactly one ready device, got $($devices.Count)"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$output = Join-Path $root "$OutputDirectory/$stamp"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$remote = "/data/local/tmp/pathguard-audit-trace-$stamp"

$processId = ((& $adb shell pidof $LocalSendPackage 2>&1 | Out-String).Trim())
if ($processId -notmatch '^\d+$') {
    throw "process '$LocalSendPackage' is not running; open LocalSend first"
}
$tracePids = @()
$manifest = @("name=localsend process=$LocalSendPackage pid=$processId")
$manifest | Set-Content -LiteralPath (Join-Path $output 'processes.txt') `
    -Encoding utf8
& $adb shell "su -c 'mkdir -p $remote && chmod 0700 $remote'" | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "cannot create remote trace directory $remote"
}
try {
    $command = "nohup /system/bin/strace -ff -tt -T -yy -s 32 " +
        "-e trace=fsync,fdatasync,close,ftruncate " +
        "-p $processId -o $remote/localsend >/dev/null 2>&1 & " +
        "tracer=`$!; (sleep $($DurationSeconds + 5); " +
        "kill -INT `$tracer 2>/dev/null) >/dev/null 2>&1 & echo `$tracer"
    $tracePid = ((& $adb shell "su -c '$command'" 2>&1 | Out-String).Trim())
    if ($tracePid -notmatch '^\d+$') {
        throw "cannot attach strace to '$LocalSendPackage': $tracePid"
    }
    $tracePids += $tracePid
    $manifest += "trace_pid=$tracePid watchdog_seconds=$($DurationSeconds + 5)"
    $manifest | Set-Content -LiteralPath (Join-Path $output 'processes.txt') `
        -Encoding utf8
    Write-Host "Tracing LocalSend for $DurationSeconds seconds. Receive one large file now."
    Start-Sleep -Seconds $DurationSeconds
} finally {
    foreach ($tracePid in $tracePids) {
        & $adb shell "su -c 'kill -INT $tracePid 2>/dev/null || true'" | Out-Null
    }
    Start-Sleep -Milliseconds 500
    & $adb shell "su -c 'chmod 0755 $remote && chmod 0644 $remote/* 2>/dev/null || true'" |
        Out-Null
    & $adb pull $remote $output | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "failed to pull completion trace from $remote"
    }
}

$localTraceDirectory = Join-Path $output "pathguard-audit-trace-$stamp"
$traceFiles = @(Get-ChildItem -LiteralPath $localTraceDirectory -File `
    -ErrorAction SilentlyContinue)
if ($traceFiles.Count -eq 0) {
    throw "completion trace produced no readable files; remote: $remote; evidence: $output"
}
$matches = $traceFiles | Select-String -Pattern @(
    'localsend-redirect', '/Pictures/', 'fsync\(', 'fdatasync\(', 'close\('
) | ForEach-Object { "{0}:{1}:{2}" -f $_.Path, $_.LineNumber, $_.Line }
$matches | Set-Content -LiteralPath (Join-Path $output 'relevant-events.txt') `
    -Encoding utf8
Write-Output $output
