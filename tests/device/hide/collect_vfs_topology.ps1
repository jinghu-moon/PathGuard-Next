param(
    [Parameter(Mandatory = $true)] [string]$PreflightReader,
    [string]$CoverageReader,
    [string]$OutputDirectory = 'build/device-evidence/vfs-topology',
    [string[]]$Paths = @('/storage/emulated/0', '/sdcard', '/storage/self/primary')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$adb = (Get-Command adb -ErrorAction Stop).Source
$ready = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice(?:\s|$)"
})
if ($ready.Count -ne 1) {
    throw "topology collector requires exactly one ready device, got $($ready.Count)"
}

foreach ($local in @($PreflightReader, $CoverageReader)) {
    if ($local -and -not (Test-Path -LiteralPath $local -PathType Leaf)) {
        throw "reader not found: $local"
    }
}

$stamp = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$out = Join-Path (Join-Path $root $OutputDirectory) $stamp
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $adb shell 'cat /proc/sys/kernel/random/boot_id' |
    Set-Content -LiteralPath (Join-Path $out 'boot_id.txt') -Encoding utf8
& $adb shell 'uname -a' |
    Set-Content -LiteralPath (Join-Path $out 'uname.txt') -Encoding utf8
 $mountInfo = @(& $adb shell 'cat /proc/self/mountinfo')
 $mountInfo | Set-Content -LiteralPath (Join-Path $out 'mountinfo.txt') -Encoding utf8
& $adb shell 'cat /proc/self/mounts' |
    Set-Content -LiteralPath (Join-Path $out 'mounts.txt') -Encoding utf8

if ($CoverageReader) {
    $remoteCoverage = '/data/local/tmp/pathguard_vfs_coverage_status_reader'
    & $adb push $CoverageReader $remoteCoverage | Out-Null
    & $adb shell "chmod 755 $remoteCoverage"
    & $adb shell "su -c '$remoteCoverage'" |
        Set-Content -LiteralPath (Join-Path $out 'coverage.txt') -Encoding utf8
    & $adb shell "rm -f $remoteCoverage"
}

$remotePreflight = '/data/local/tmp/pathguard_vfs_preflight_status_reader'
& $adb push $PreflightReader $remotePreflight | Out-Null
& $adb shell "chmod 755 $remotePreflight"

$records = [System.Collections.Generic.List[object]]::new()
foreach ($path in $Paths) {
    $safe = ($path.Trim('/') -replace '[^A-Za-z0-9._-]', '_')
    $preflight = (& $adb shell su -c "$remotePreflight $path") -join "`n"
    $stat = (& $adb shell su -c "stat -c path=%n,inode=%i,mode=%f,dev=%D $path") -join "`n"
    $mountMatches = @($mountInfo | Where-Object {
        $_ -match '/storage/emulated|/mnt/user/0/emulated|/mnt/androidwritable/0/emulated|/mnt/installer/0/emulated|/mnt/pass_through/0/emulated'
    })
    $preflight | Set-Content -LiteralPath (Join-Path $out "$safe.preflight.txt") -Encoding utf8
    $stat | Set-Content -LiteralPath (Join-Path $out "$safe.stat.txt") -Encoding utf8
    $mountMatches | Set-Content -LiteralPath (Join-Path $out "$safe.mountinfo.txt") -Encoding utf8
    $records.Add([ordered]@{
        path = $path
        preflight = $preflight.Trim()
        stat = $stat.Trim()
        mountinfo = @($mountMatches | ForEach-Object { $_.ToString().Trim() })
    })
}
& $adb shell "rm -f $remotePreflight"

[ordered]@{
    schema = 1
    collected_at = [DateTimeOffset]::Now.ToString('o')
    paths = $records
} | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $out 'topology.json') -Encoding utf8

Write-Output $out
