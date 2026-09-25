param(
    [string]$RulesPath = 'module/config/rules.toml',
    [string]$FixtureRoot = '/storage/emulated/0/Pictures/PathGuardHideLab/20260925-000308',
    [UInt64]$ExpectedParentInode = 836528,
    [string]$OutputDirectory = 'build/device-evidence/hide1-rules-hot-reload'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$adb = (Get-Command adb -ErrorAction Stop).Source
$source = (Resolve-Path -LiteralPath (Join-Path $root $RulesPath)).Path
$original = Get-Content -Raw -LiteralPath $source
$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$out = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $out | Out-Null
$hidden = "$FixtureRoot/hidden"
$hidden2 = "$FixtureRoot/hidden2"
$aliasParent = $FixtureRoot.Replace('/storage/emulated/0', '/storage/self/primary')

function Adb([string[]]$Arguments) {
    $result = @(& $adb @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $($Arguments -join ' ')" }
    return $result
}
function Root([string]$Command) { return Adb @('shell', ('su -W -c "' + $Command + '"')) }
function Status {
    $raw = ((Root '/data/adb/modules/pathguard_hide1_lab/bin/hide1ctl status') -join ' ').Trim()
    $o = [ordered]@{ raw = $raw }
    foreach ($n in @('state','lifecycle','generation','target_pid','target_mnt_ns','parent_inode','operation_mask')) {
        $m = [regex]::Match($raw, "(?:^|\s)$n=(0x[0-9a-fA-F]+|-?\d+)(?:\s|$)")
        if ($m.Success) { $o[$n] = if ($m.Groups[1].Value.StartsWith('0x')) { [Convert]::ToUInt64($m.Groups[1].Value.Substring(2),16) } else { [UInt64]$m.Groups[1].Value } }
    }
    return [pscustomobject]$o
}
function Wait-Status([bool]$Active, [string]$Case) {
    $deadline = [DateTimeOffset]::Now.AddSeconds(15)
    do { Start-Sleep -Milliseconds 500; $s = Status
        $identity = if ($Active) { ((Adb @('shell','pidof','dev.pathguard.hideprobe.target') 2>$null) -join '').Trim() } else { '' }
        $namespace = if ($Active -and $identity -match '^\d+$') { ((Root "readlink /proc/$identity/ns/mnt") -join '').Trim() } else { '' }
        $namespaceId = if ($namespace -match '\[(\d+)\]') { [UInt64]$Matches[1] } else { 0 }
        $ok = if ($Active) { $identity -match '^\d+$' -and $namespaceId -gt 0 -and $s.target_pid -eq [UInt64]$identity -and $s.target_mnt_ns -eq $namespaceId -and $s.state -eq 2 -and $s.lifecycle -eq 2 -and $s.parent_inode -eq $ExpectedParentInode -and $s.operation_mask -eq 0xfff } else { $s.state -eq 0 -and $s.lifecycle -eq 0 -and $s.parent_inode -eq 0 -and $s.operation_mask -eq 0 }
    } while (-not $ok -and [DateTimeOffset]::Now -lt $deadline)
    if (-not $ok) { throw "$Case status mismatch: $($s.raw)" }
    return $s
}
function Write-RemoteRules([string]$Text, [string]$Name) {
    $local = Join-Path $out "$Name.toml"
    Set-Content -LiteralPath $local -Value $Text -Encoding utf8
    Adb @('push', $local, "/sdcard/$Name.toml") | Out-Null
    Root "cp /sdcard/$Name.toml /data/adb/modules/pathguard_hide1_lab/config/rules.toml.new"
    Root "chmod 600 /data/adb/modules/pathguard_hide1_lab/config/rules.toml.new"
    Root "mv -f /data/adb/modules/pathguard_hide1_lab/config/rules.toml.new /data/adb/modules/pathguard_hide1_lab/config/rules.toml"
}
function Start-Probe([string]$Path, [string]$Name, [bool]$ExpectActive = $true, [bool]$KeepTarget = $false) {
    $run = "$runId-$Name"
    if (-not $KeepTarget) { [void](Adb @('shell','am','force-stop','dev.pathguard.hideprobe.target')) }
    [void](Adb @('shell','am','start','-W','-n','dev.pathguard.hideprobe.target/dev.pathguard.hideprobe.ProbeActivity','--esa','observe_paths',$Path,'--es','scenario','baseline','--es','run_id',$run))
    if ($ExpectActive) {
        [void](Wait-Status $true "probe-$Name-active")
        # The first Activity run starts before daemon binding can complete. Re-run
        # through onNewIntent after the identity barrier so observations belong
        # to the active binding.
        [void](Adb @('shell','am','start','-W','-n','dev.pathguard.hideprobe.target/dev.pathguard.hideprobe.ProbeActivity','--esa','observe_paths',$Path,'--es','scenario','baseline','--es','run_id',$run))
    }
    $deadline = [DateTimeOffset]::Now.AddSeconds(20)
    $metadataReady = $false
    do {
        Start-Sleep -Milliseconds 300
        $state = ((Adb @('shell','run-as','dev.pathguard.hideprobe.target','cat','files/hide-h0/status') 2>$null) -join '').Trim()
        if ($state -eq 'complete') {
            $metadataText = ((Adb @('shell','run-as','dev.pathguard.hideprobe.target','cat','files/hide-h0/metadata.json') 2>$null) -join '').Trim()
            if ($metadataText) {
                try { $metadataReady = (($metadataText | ConvertFrom-Json).run_id -eq $run) } catch { $metadataReady = $false }
            }
        }
    } while ((($state -ne 'complete') -or -not $metadataReady) -and -not $state.StartsWith('failed:') -and [DateTimeOffset]::Now -lt $deadline)
    if ($state -ne 'complete' -or -not $metadataReady) { throw "probe $Name failed: $state" }
    $jsonl = ((Adb @('exec-out','run-as','dev.pathguard.hideprobe.target','cat','files/hide-h0/observations.jsonl')) -join "`n")
    Set-Content -LiteralPath (Join-Path $out "$Name-observations.jsonl") -Value $jsonl -Encoding utf8
    $rows = @($jsonl -split "`n" | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
    $direct = @($rows | Where-Object { $_.test -eq 'external.0.lstat' -and $_.path -eq $Path } | Select-Object -First 1)
    if ($direct.Count -ne 1) { throw "probe $Name has no direct lstat result for $Path" }
    return [pscustomobject]@{ path = $Path; lstat_return = $direct.return_value; lstat_errno = $direct.errno; status = $direct.status }
}
function Read-RulesStatus([string]$Name) {
    $text = ((Root 'cat /data/adb/modules/pathguard_hide1_lab/run/rules-status.json 2>/dev/null') -join '').Trim()
    Set-Content -LiteralPath (Join-Path $out "$Name-rules-status.json") -Value $text -Encoding utf8
    if (-not $text) { return $null }
    return $text | ConvertFrom-Json
}

try {
    Root "mkdir -p $hidden2; printf 'hot-reload hidden2' > $hidden2/canary.txt"
    $baseline = Wait-Status $true 'baseline'
    $cases = [System.Collections.Generic.List[object]]::new()

    $withoutHide = [regex]::Replace($original, '(?ms)\n# Experimental Hide 1\.0-LKM rule.*?hide_rules = \[.*?\n\]\s*\z', '')
    Write-RemoteRules $withoutHide 'delete-hide'
    $inactive = Wait-Status $false 'delete-hide'
    $visible = Start-Probe $hidden 'delete-hide' $false
    $cases.Add([pscustomobject]@{ name='delete-hide-rule'; status=$inactive; probe=$visible; expected_visible=($visible.lstat_return -eq 0) })

    $modified = $original.Replace(
        'parent = "' + $FixtureRoot + '", basename = "hidden"',
        'parent = "' + $aliasParent + '", basename = "hidden2"')
    Write-RemoteRules $modified 'modify-parent-basename'
    $active2 = Wait-Status $true 'modify-parent-basename'
    $hiddenResult = Start-Probe "$FixtureRoot/hidden2" 'modify-parent-basename'
    $cases.Add([pscustomobject]@{ name='modify-parent-basename'; status=$active2; probe=$hiddenResult; expected_hidden=($hiddenResult.lstat_return -ne 0) })

    $invalid = $modified.Replace('basename = "hidden2"', 'basename = "bad/name"')
    Write-RemoteRules $invalid 'invalid-rule'
    Start-Sleep -Seconds 3
    $invalidStatus = Status
    $invalidRulesStatus = Read-RulesStatus 'invalid-rule'
    $stillHidden = Start-Probe "$FixtureRoot/hidden2" 'invalid-rule' $true $true
    $cases.Add([pscustomobject]@{ name='invalid-rule'; status=$invalidStatus; rules_status=$invalidRulesStatus; probe=$stillHidden; old_snapshot_preserved=($invalidStatus.state -eq 2 -and $stillHidden.lstat_return -ne 0) })

    Write-RemoteRules $original 'restore-original'
    $restored = Wait-Status $true 'restore-original'
    $restoredProbe = Start-Probe $hidden 'restore-original'
    $cases.Add([pscustomobject]@{ name='restore-original'; status=$restored; probe=$restoredProbe; expected_hidden=($restoredProbe.lstat_return -ne 0) })
    $final = Status
    $summary = [ordered]@{ schema=1; run_id=$runId; fixture_root=$FixtureRoot; alias_parent=$aliasParent; cases=$cases; final=$final; conclusion=if ((@($cases | Where-Object { $_.expected_visible -eq $false -or $_.expected_hidden -eq $false -or $_.old_snapshot_preserved -eq $false }).Count -eq 0) -and $final.state -eq 2) { 'PASS' } else { 'BLOCKED' } }
    $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $out 'rules-hot-reload.json') -Encoding utf8
    Write-Output (Join-Path $out 'rules-hot-reload.json')
} finally {
    Write-RemoteRules $original 'restore-final'
    try {
        [void](Wait-Status $true 'restore-final')
    } catch {
        Write-Warning $_
    }
}
