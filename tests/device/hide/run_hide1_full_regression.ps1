param(
    [Parameter(Mandatory = $true)] [string]$TargetApk,
    [Parameter(Mandatory = $true)] [string]$ControlApk,
    [switch]$GrantAllFilesAccess,
    [switch]$GrantReadMediaImages,
    [switch]$KeepTargetProcess,
    [string]$FixtureRoot,
    [string]$ExistingHiddenPath,
    [UInt64]$ExpectedParentInode,
    [ValidateRange(0, 4)] [int]$ShadowMode = 0,
    [switch]$RunMutations,
    [switch]$ConfirmMutation,
    [switch]$BaselineOnly,
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Backend = 'pathguard-hide1',
    [string]$OutputDirectory = 'build/device-evidence/hide1-regression'
)

$ErrorActionPreference = 'Stop'
if ($RunMutations -and -not $ConfirmMutation) {
    throw 'RunMutations changes the disposable fixture; pass -ConfirmMutation explicitly'
}
if (-not $BaselineOnly -and -not $RunMutations) {
    throw 'active full regression requires -RunMutations -ConfirmMutation; use -BaselineOnly for a read-only baseline'
}
if (-not $BaselineOnly -and -not $FixtureRoot) {
    throw 'active full regression requires a pre-created -FixtureRoot bound before INSTALL/ENABLE'
}
if (-not $BaselineOnly -and -not $KeepTargetProcess) {
    throw 'active full regression requires -KeepTargetProcess to preserve the INSTALL target identity'
}
if (-not $BaselineOnly -and $ShadowMode -ne 0) {
    throw 'active full regression requires shadow_mode=0; use a read-only runner for shadow_mode=4'
}
if ($FixtureRoot -and $ExistingHiddenPath) {
    throw 'FixtureRoot and ExistingHiddenPath are mutually exclusive'
}
$runner = Join-Path $PSScriptRoot 'run_hidelab_baseline.ps1'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$out = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $out | Out-Null
$adb = (Get-Command adb -ErrorAction Stop).Source

function Get-MountInfo {
    $lines = @(& $adb shell 'cat /proc/self/mountinfo')
    if ($LASTEXITCODE -ne 0 -or $lines.Count -eq 0) {
        throw 'cannot collect /proc/self/mountinfo'
    }
    return ($lines -join "`n").TrimEnd()
}

function Get-KernelShadowMode {
    $value = ((& $adb shell 'su -W -c "cat /sys/module/pathguard_hide1/parameters/shadow_mode"' 2>$null) -join '').Trim()
    if ($LASTEXITCODE -ne 0 -or $value -notmatch '^[0-4]$') {
        throw 'INFRA_ERROR: cannot read loaded pathguard_hide1 shadow_mode'
    }
    return [int]$value
}

if (-not $BaselineOnly) {
    $actualShadowMode = Get-KernelShadowMode
    if ($actualShadowMode -ne $ShadowMode) {
        throw "INFRA_ERROR: requested shadow_mode=$ShadowMode but loaded module reports shadow_mode=$actualShadowMode"
    }
}

$mountInfoBefore = Get-MountInfo
Set-Content -LiteralPath (Join-Path $out 'mountinfo-before.txt') -Value $mountInfoBefore -Encoding utf8
$device = ((& $adb shell getprop ro.product.device 2>$null) -join '').Trim()
$fingerprint = ((& $adb shell getprop ro.build.fingerprint 2>$null) -join '').Trim()
$kernelRelease = ((& $adb shell uname -r 2>$null) -join '').Trim()
$bootId = ((& $adb shell su -W -c 'cat /proc/sys/kernel/random/boot_id' 2>$null) -join '').Trim()
$moduleHashText = ((& $adb shell su -W -c 'sha256sum /data/adb/modules/pathguard_hide1_lab/bin/pathguard_hide1.ko' 2>$null) -join '').Trim()
$moduleHash = if ($moduleHashText -match '^(?<hash>[0-9a-fA-F]{64})\s+') { $Matches.hash.ToLowerInvariant() } else { '' }
$common = @('-TargetApk', $TargetApk, '-ControlApk', $ControlApk,
            '-OutputDirectory', "$OutputDirectory/$runId",
            '-Backend', $(if ($BaselineOnly) { 'none' } else { $Backend }))
if ($GrantAllFilesAccess) { $common += '-GrantAllFilesAccess' }
if ($GrantReadMediaImages) { $common += '-GrantReadMediaImages' }
if ($KeepTargetProcess) { $common += '-KeepTargetProcess' }
if ($FixtureRoot) { $common += @('-FixtureRoot', $FixtureRoot) }
if ($ExistingHiddenPath) { $common += @('-ExistingHiddenPath', $ExistingHiddenPath) }
if ($PSBoundParameters.ContainsKey('ExpectedParentInode')) {
    $common += @('-ExpectedParentInode', [string]$ExpectedParentInode)
}
$common += @('-ShadowMode', [string]$ShadowMode)
if ($FixtureRoot) { $common += '-KeepFixture' }
if (-not $BaselineOnly) { $common += '-ExpectTargetHidden' }
$scenarios = @('baseline', 'cache-order', 'concurrency', 'reliability')
$results = [System.Collections.Generic.List[object]]::new()
$boundHiddenPath = "$FixtureRoot/hidden"
$observedParentInode = $null
$expectedParentInodeProvided = $PSBoundParameters.ContainsKey('ExpectedParentInode')
function Add-ScenarioEvidence([string]$Path) {
    $summary = Get-Content -Raw -LiteralPath (Join-Path $Path 'summary.json') | ConvertFrom-Json
    if ($FixtureRoot -and ($summary.fixture_root -ne $FixtureRoot -or $summary.hidden_path -ne $boundHiddenPath)) {
        throw "INFRA_ERROR: scenario identity mismatch: expected $FixtureRoot / $boundHiddenPath, got $($summary.fixture_root) / $($summary.hidden_path)"
    }
    if ($summary.shadow_mode -ne $ShadowMode) {
        throw "INFRA_ERROR: shadow mode mismatch: expected $ShadowMode, got $($summary.shadow_mode)"
    }
    if ($null -eq $summary.parent_inode -or [string]$summary.parent_inode -notmatch '^\d+$') {
        throw 'INFRA_ERROR: scenario summary has no numeric parent_inode'
    }
    if ($null -eq $script:observedParentInode) {
        $script:observedParentInode = [UInt64]$summary.parent_inode
    } elseif ([UInt64]$summary.parent_inode -ne $script:observedParentInode) {
        throw "INFRA_ERROR: parent inode changed across scenarios: expected $script:observedParentInode, got $($summary.parent_inode)"
    }
    if ($expectedParentInodeProvided -and
        [UInt64]$summary.parent_inode -ne $ExpectedParentInode) {
        throw "INFRA_ERROR: parent inode differs from expected binding: expected $ExpectedParentInode, got $($summary.parent_inode)"
    }
    $results.Add($summary)
}
foreach ($scenario in $scenarios) {
    $args = @('-NoProfile', '-File', $runner) + $common + @('-Scenario', $scenario)
    $marker = ((& powershell @args | Where-Object { $_ -like 'HIDELAB_EVIDENCE:*' } | Select-Object -Last 1) -join '').Trim()
    if (-not $marker) { throw 'HideLab did not emit an evidence marker' }
    $path = $marker.Substring('HIDELAB_EVIDENCE:'.Length)
    if (-not (Test-Path -LiteralPath $path)) { throw "HideLab did not return an evidence directory: $path" }
    Add-ScenarioEvidence $path
}
if ($RunMutations) {
    $args = @('-NoProfile', '-File', $runner) + $common + @('-Scenario', 'mutation', '-AttackMutations', '-ConfirmMutation')
    $marker = ((& powershell @args | Where-Object { $_ -like 'HIDELAB_EVIDENCE:*' } | Select-Object -Last 1) -join '').Trim()
    if (-not $marker) { throw 'HideLab did not emit an evidence marker' }
    $path = $marker.Substring('HIDELAB_EVIDENCE:'.Length)
    if (-not (Test-Path -LiteralPath $path)) { throw "HideLab did not return an evidence directory: $path" }
    Add-ScenarioEvidence $path
}
$mountInfoAfter = Get-MountInfo
Set-Content -LiteralPath (Join-Path $out 'mountinfo-after.txt') -Value $mountInfoAfter -Encoding utf8
$mountInfoUnchanged = $mountInfoBefore -eq $mountInfoAfter
$failures = @($results | Where-Object { $_.conclusion -in @('LEAK','OVERBLOCK','SEMANTIC_DRIFT','DESTRUCTIVE_FAIL','CRASH','HANG','STATE_LIE','INFRA_ERROR') })
$mountFailure = -not $mountInfoUnchanged
$requiredScenarios = @('baseline', 'cache-order', 'concurrency', 'reliability')
if ($RunMutations) { $requiredScenarios += 'mutation' }
$missingScenarios = @($requiredScenarios | Where-Object { $_ -notin @($results.phase) })
$conclusion = if ($failures.Count -gt 0 -or $mountFailure -or $missingScenarios.Count -gt 0) {
    'blocked'
} elseif (@($results | Where-Object { $_.conclusion -eq 'PASS' }).Count -eq $results.Count -and $mountInfoUnchanged) {
    'candidate_pass_requires_admission'
} else {
    'baseline_or_unsupported'
}
$statusText = ((& $adb shell su -W -c '/data/adb/modules/pathguard_hide1_lab/bin/hide1ctl status' 2>$null) -join '').Trim()
$statusGeneration = if ($statusText -match '(?:^|\s)generation=(\d+)(?:\s|$)') { [UInt64]$Matches[1] } else { 0 }
$statusParentInode = if ($statusText -match '(?:^|\s)parent_inode=(\d+)(?:\s|$)') { [UInt64]$Matches[1] } else { 0 }
$statusShadowMode = ((& $adb shell su -W -c 'cat /sys/module/pathguard_hide1/parameters/shadow_mode' 2>$null) -join '').Trim()
[ordered]@{
    schema = 3
    run_id = $runId
    backend = if ($BaselineOnly) { 'none' } else { $Backend }
    shadow_mode = $ShadowMode
    active = -not $BaselineOnly
    mutation_enabled = [bool]$RunMutations
    required_scenarios = $requiredScenarios
    missing_scenarios = $missingScenarios
    scenarios = $results
    mountinfo_unchanged = $mountInfoUnchanged
    device = $device
    fingerprint = $fingerprint
    kernel_release = $kernelRelease
    boot_id = $bootId
    module_sha256 = $moduleHash
    status_generation = $statusGeneration
    status_parent_inode = $statusParentInode
    status_shadow_mode = $statusShadowMode
    conclusion = $conclusion
} |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $out 'full-regression.json') -Encoding utf8
Write-Output (Join-Path $out 'full-regression.json')
