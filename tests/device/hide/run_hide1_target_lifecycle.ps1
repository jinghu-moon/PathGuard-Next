param(
    [string]$TargetPackage = 'dev.pathguard.hideprobe.target',
    [string]$Activity = 'dev.pathguard.hideprobe.ProbeActivity',
    [UInt64]$ExpectedParentInode = 836528,
    [ValidateRange(1, 20)] [int]$SoakIterations = 5,
    [string]$OutputDirectory = 'build/device-evidence/hide1-target-lifecycle'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$adb = (Get-Command adb -ErrorAction Stop).Source
$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$out = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Adb([string[]]$Arguments) {
    $result = @(& $adb @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $($Arguments -join ' ')" }
    return $result
}

function Root([string]$Command) {
    return Adb @('shell', ('su -W -c "' + $Command + '"'))
}

function Status {
    $text = ((Root '/data/adb/modules/pathguard_hide1_lab/bin/hide1ctl status') -join ' ').Trim()
    $values = [ordered]@{ raw = $text }
    foreach ($name in @('state','lifecycle','generation','target_pid','target_mnt_ns','parent_inode','operation_mask')) {
        $match = [regex]::Match($text, "(?:^|\s)$name=(0x[0-9a-fA-F]+|-?\d+)(?:\s|$)")
        if ($match.Success) {
            $values[$name] = if ($match.Groups[1].Value.StartsWith('0x')) {
                [Convert]::ToUInt64($match.Groups[1].Value.Substring(2), 16)
            } else { [UInt64]$match.Groups[1].Value }
        }
    }
    return [pscustomobject]$values
}

function Identity {
    $pidText = ((Adb @('shell','pidof',$TargetPackage)) -join '').Trim()
    if ($pidText -notmatch '^\d+$') { return [pscustomobject]@{ pid = 0; namespace = '' } }
    $ns = ((Root "readlink /proc/$pidText/ns/mnt") -join '').Trim()
    return [pscustomobject]@{ pid = [int]$pidText; namespace = $ns }
}

function Wait-Active([int]$TargetPid, [string]$Namespace, [string]$Case) {
    $deadline = [DateTimeOffset]::Now.AddSeconds(12)
    do {
        Start-Sleep -Milliseconds 500
        $s = Status
        $match = $s.state -eq 2 -and $s.lifecycle -eq 2 -and
            $s.target_pid -eq $TargetPid -and $s.target_mnt_ns -gt 0 -and
            $s.parent_inode -eq $ExpectedParentInode -and $s.operation_mask -eq 0xfff
    } while (-not $match -and [DateTimeOffset]::Now -lt $deadline)
    if (-not $match) { throw "$Case did not become active: $($s.raw)" }
    return $s
}

function Wait-Inactive([string]$Case) {
    $deadline = [DateTimeOffset]::Now.AddSeconds(12)
    do {
        Start-Sleep -Milliseconds 500
        $s = Status
        # lifecycle=1 is the loaded-but-inactive module state after reboot.
        $match = $s.state -eq 0 -and $s.lifecycle -in @(0, 1) -and
            $s.operation_mask -eq 0 -and $s.parent_inode -eq 0
    } while (-not $match -and [DateTimeOffset]::Now -lt $deadline)
    if (-not $match) { throw "$Case did not become inactive: $($s.raw)" }
    return $s
}

function Start-Target {
    [void](Adb @('shell','am','start','-W','-n',"$TargetPackage/$Activity"))
    $deadline = [DateTimeOffset]::Now.AddSeconds(12)
    do {
        Start-Sleep -Milliseconds 300
        $identity = Identity
    } while ($identity.pid -eq 0 -and [DateTimeOffset]::Now -lt $deadline)
    if ($identity.pid -eq 0) { throw 'target did not start' }
    return $identity
}

function Capture-Log([string]$Name) {
    $log = ((Root 'tail -n 160 /data/adb/modules/pathguard_hide1_lab/run/daemon.log') -join "`n")
    Set-Content -LiteralPath (Join-Path $out "$Name-daemon.log") -Value $log -Encoding utf8
}

$initial = Identity
if ($initial.pid -eq 0) { $initial = Start-Target }
$initialStatus = Wait-Active $initial.pid $initial.namespace 'initial'
$iterations = [System.Collections.Generic.List[object]]::new()
$previous = $initial
for ($index = 1; $index -le $SoakIterations; $index++) {
    [void](Adb @('shell','am','force-stop',$TargetPackage))
    $inactive = Wait-Inactive "soak-$index-force-stop"
    Capture-Log "soak-$index-force-stop"
    $next = Start-Target
    $active = Wait-Active $next.pid $next.namespace "soak-$index-restart"
    $iterations.Add([ordered]@{
        iteration = $index
        before = $previous
        inactive = $inactive
        identity = $next
        active = $active
        pid_changed = $previous.pid -ne $next.pid
        namespace_changed = $previous.namespace -ne $next.namespace
        old_binding_not_reused = $previous.pid -ne $next.pid -or $previous.namespace -ne $next.namespace
    })
    $previous = $next
}
$final = Status
$pidChanged = @($iterations | Where-Object { $_.pid_changed }).Count -eq $iterations.Count
$namespaceChanged = @($iterations | Where-Object { $_.namespace_changed }).Count -eq $iterations.Count
$oldIdentityGone = @($iterations | Where-Object { $_.old_binding_not_reused }).Count -eq $iterations.Count
$summary = [ordered]@{
    schema = 1
    run_id = $runId
    target_package = $TargetPackage
    initial = $initial
    initial_status = $initialStatus
    soak_iterations = $SoakIterations
    iterations = @($iterations)
    pid_changed = $pidChanged
    namespace_changed = $namespaceChanged
    old_binding_not_reused = $oldIdentityGone
    final = $final
    conclusion = if ($oldIdentityGone -and $final.state -eq 2 -and $final.target_pid -eq $previous.pid) { 'PASS' } else { 'BLOCKED' }
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $out 'target-lifecycle.json') -Encoding utf8
Write-Output (Join-Path $out 'target-lifecycle.json')
