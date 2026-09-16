param(
    [Parameter(Mandatory = $true)] [string]$TargetApk,
    [Parameter(Mandatory = $true)] [string]$ControlApk,
    [string]$OutputDirectory = 'build/device-evidence/hidelab-baseline',
    [switch]$GrantAllFilesAccess,
    [switch]$GrantReadMediaImages,
    [switch]$KeepTargetProcess,
    [string]$ExistingHiddenPath,
    [ValidateSet('baseline', 'cache-order', 'concurrency', 'reliability')]
    [string]$Scenario = 'baseline',
    [switch]$AttackMutations,
    [switch]$ConfirmMutation,
    [switch]$ExpectTargetHidden,
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Backend = 'none',
    [ValidateRange(10, 180)] [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
if ($AttackMutations -and -not $ConfirmMutation) {
    throw 'AttackMutations changes the shared-storage fixture; pass -ConfirmMutation explicitly'
}
if ($ExistingHiddenPath -and $AttackMutations) {
    throw 'AttackMutations is not allowed with ExistingHiddenPath; use a disposable fixture'
}
$targetPackage = 'dev.pathguard.hideprobe.target'
$controlPackage = 'dev.pathguard.hideprobe.control'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice(?:\s|$)' })
if ($devices.Count -ne 1) { throw "HideLab requires exactly one ready device, got $($devices.Count)" }

$targetApkPath = (Resolve-Path -LiteralPath $TargetApk -ErrorAction Stop).Path
$controlApkPath = (Resolve-Path -LiteralPath $ControlApk -ErrorAction Stop).Path
$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
if ($ExistingHiddenPath) {
    if ($ExistingHiddenPath -notmatch '^/storage/emulated/0/Pictures/[A-Za-z0-9._/-]+$' -or $ExistingHiddenPath.EndsWith('/')) {
        throw "unexpected existing hidden path: $ExistingHiddenPath"
    }
    $hiddenPath = $ExistingHiddenPath
    $fixtureRoot = $hiddenPath
} else {
    $fixtureRoot = "/storage/emulated/0/Pictures/PathGuardHideLab/$runId"
    $hiddenPath = "$fixtureRoot/hidden"
    if ($fixtureRoot -notmatch '^/storage/emulated/0/Pictures/PathGuardHideLab/\d{8}-\d{6}$') {
        throw "unexpected fixture path: $fixtureRoot"
    }
}
$runOutput = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $runOutput | Out-Null

function Invoke-Adb([string[]]$Arguments) {
    & $adb @Arguments
    if ($LASTEXITCODE -ne 0) { throw "adb command failed: $($Arguments -join ' ')" }
}

function Invoke-Root([string]$Command) {
    if ($Command -match '[\r\n"]') { throw 'Root command contains an unsupported character' }
    # SukiSU requires the whitelist-preserving -W path for shared-storage
    # operations; plain su -c may leave the shell in the ksu SELinux domain.
    & $adb shell ('su -W -c "' + $Command + '"')
    if ($LASTEXITCODE -ne 0) { throw "root command failed: $Command" }
}

function Get-OracleSnapshot([string]$Name) {
    $snapshot = @(Invoke-Root "test -d $fixtureRoot && find $fixtureRoot -exec stat -c '%F|%n|%s|%i' {} \; | sort; if test -f $hiddenPath/canary.txt; then sha256sum $hiddenPath/canary.txt; else printf 'MISSING|%s\n' $hiddenPath/canary.txt; fi") -join "`n"
    Set-Content -LiteralPath (Join-Path $runOutput "$Name.txt") -Value $snapshot -Encoding utf8
    return $snapshot
}

function Reset-Fixture {
    if ($ExistingHiddenPath) { return }
    if ($fixtureRoot -notmatch '^/storage/emulated/0/Pictures/PathGuardHideLab/\d{8}-\d{6}$') {
        throw "refusing to reset unexpected fixture path: $fixtureRoot"
    }
    Invoke-Root "rm -rf $fixtureRoot; mkdir -p $hiddenPath; printf 'PathGuard HideLab canary $runId' > $hiddenPath/canary.txt; mkdir -p $hiddenPath/nested; printf 'nested $runId' > $hiddenPath/nested/nested.txt; printf 'visible $runId' > $fixtureRoot/visible.txt; printf 'visible-link $runId' > $fixtureRoot/visible-link.txt"
}

function Invoke-Probe([string]$Role, [string]$Package) {
    $pinnedPid = $null
    $pinnedNamespace = $null
    if ($Role -eq 'target' -and $KeepTargetProcess) {
        $pinnedPid = ((& $adb shell pidof $Package 2>$null) -join '').Trim()
        if (-not $pinnedPid -or $pinnedPid -notmatch '^\d+$') {
            throw 'KeepTargetProcess requires the already-bound target PID to be alive'
        }
        $pinnedNamespace = ((& $adb shell su -W -c "readlink /proc/$pinnedPid/ns/mnt" 2>$null) -join '').Trim()
        if (-not $pinnedNamespace -or $pinnedNamespace -notmatch '^mnt:\[\d+\]$') {
            throw "cannot read target mount namespace for PID $pinnedPid"
        }
    }
    if (-not ($KeepTargetProcess -and $Role -eq 'target')) {
        Invoke-Adb @('shell', 'am', 'force-stop', $Package)
    }
    $startArguments = @('shell', 'am', 'start', '-W', '-n', "$Package/dev.pathguard.hideprobe.ProbeActivity", '--esa', 'observe_paths', $hiddenPath, '--es', 'scenario', $Scenario, '--es', 'run_id', $runId)
    if ($AttackMutations) { $startArguments += @('--ez', 'attack_mutations', 'true') }
    Invoke-Adb $startArguments
    $deadline = [DateTimeOffset]::Now.AddSeconds($TimeoutSeconds)
    do {
        Start-Sleep -Milliseconds 250
        $status = ((& $adb shell run-as $Package cat files/hide-h0/status 2>$null) -join '').Trim()
        $metadataReady = $false
        if ($status -eq 'complete') {
            $metadataProbe = ((& $adb shell run-as $Package cat files/hide-h0/metadata.json 2>$null) -join '').Trim()
            if ($metadataProbe) {
                try {
                    $metadataCandidate = $metadataProbe | ConvertFrom-Json -ErrorAction Stop
                    $metadataReady = $metadataCandidate.run_id -eq $runId -and
                        $metadataCandidate.scenario -eq $Scenario
                } catch {
                    $metadataReady = $false
                }
            }
        }
    } while ((($status -ne 'complete') -or -not $metadataReady) -and
             -not $status.StartsWith('failed:') -and
             [DateTimeOffset]::Now -lt $deadline)
    if ($status -ne 'complete' -or -not $metadataReady) {
        throw "HideLab $Role did not complete current run: $status"
    }
    & $adb exec-out run-as $Package cat files/hide-h0/metadata.json |
        Set-Content -LiteralPath (Join-Path $runOutput "$Role-metadata.json") -Encoding utf8
    & $adb exec-out run-as $Package cat files/hide-h0/observations.jsonl |
        Set-Content -LiteralPath (Join-Path $runOutput "$Role-observations.jsonl") -Encoding utf8
    if ($Role -eq 'target' -and $KeepTargetProcess) {
        $metadata = Get-Content -Raw -LiteralPath (Join-Path $runOutput "$Role-metadata.json") |
            ConvertFrom-Json -ErrorAction Stop
        $pidNow = ((& $adb shell pidof $Package 2>$null) -join '').Trim()
        if (-not $pidNow -or $pidNow -notmatch '^\d+$' -or $pidNow -ne $pinnedPid) {
            throw 'HideLab target exited while namespace was pinned'
        }
        $nsNow = ((& $adb shell su -W -c "readlink /proc/$pidNow/ns/mnt" 2>$null) -join '').Trim()
        if ($nsNow -ne $pinnedNamespace -or
            ($metadata.mount_namespace -and $nsNow -ne $metadata.mount_namespace)) {
            throw "HideLab target mount namespace changed: $pinnedNamespace -> $nsNow"
        }
    }
}

function Assert-BaselineVisible([string]$Role) {
    $rows = @(Get-Content -LiteralPath (Join-Path $runOutput "$Role-observations.jsonl") |
        Where-Object { $_.Trim() } |
        ForEach-Object { $_ | ConvertFrom-Json -ErrorAction Stop })
    $expected = [ordered]@{
        'external.0.lstat' = 0
        'external.0.open' = 0
        'external.0.readdir' = 1
        'external.0.getdents64_4096' = 1
        'external.0.getdents64_32768' = 1
        'external.0.getdents64_65536' = 1
        'external.0.getdents64_131072' = 1
    }
    foreach ($test in $expected.Keys) {
        $row = @($rows | Where-Object { $_.test -eq $test -and $_.path -eq $hiddenPath })
        if ($row.Count -ne 1 -or $row[0].status -ne 'observed' -or $row[0].return_value -ne $expected[$test]) {
            throw "HideLab $Role baseline did not observe $test on $hiddenPath"
        }
    }
}

function Assert-CacheOrder([string]$Role, [bool]$ExpectHidden) {
    $rows = @(Get-Content -LiteralPath (Join-Path $runOutput "$Role-observations.jsonl") |
        Where-Object { $_.Trim() } |
        ForEach-Object { $_ | ConvertFrom-Json -ErrorAction Stop })
    $expected = @(
        'external.0.cache.cold_open',
        'external.0.cache.cold_opendir',
        'external.0.cache.stat_then_open',
        'external.0.cache.readdir_then_open',
        'external.0.cache.positive_warm_then_open'
    )
    foreach ($test in $expected) {
        $row = @($rows | Where-Object { $_.test -eq $test -and $_.path -eq $hiddenPath })
        if ($row.Count -ne 1 -or $row[0].status -ne 'observed') {
            throw "HideLab $Role cache-order did not record $test on $hiddenPath"
        }
        if ($ExpectHidden -and ($row[0].return_value -ne -1 -or $row[0].errno -ne 2)) {
            $kind = if ($row[0].return_value -eq -1) { 'SEMANTIC_DRIFT' } else { 'LEAK' }
            throw "$kind`: HideLab $Role cache-order failed $test"
        }
        if (-not $ExpectHidden -and $row[0].return_value -ne 0) {
            throw "OVERBLOCK: HideLab $Role cache-order failed $test"
        }
    }
}

function Read-Observations([string]$Role) {
    return @(Get-Content -LiteralPath (Join-Path $runOutput "$Role-observations.jsonl") |
        Where-Object { $_.Trim() } |
        ForEach-Object { $_ | ConvertFrom-Json -ErrorAction Stop })
}

function Assert-HideDirectVfs([string]$Role) {
    $rows = Read-Observations $Role
    foreach ($test in @('java.external.0.exists', 'java.external.0.isDirectory',
                        'java.external.0.nio_exists', 'java.external.0.nio_isDirectory')) {
        $row = @($rows | Where-Object { $_.test -eq $test -and $_.path -eq $hiddenPath })
        if ($row.Count -ne 1 -or $row[0].return_value -ne 0) {
            throw "LEAK: HideLab $Role Java path test exposed hidden target: $test"
        }
    }
    $enoentTests = @(
        'external.0.stat', 'external.0.lstat', 'external.0.access',
        'external.0.open', 'external.0.opendir', 'external.0.fstatat',
        'external.0.statx', 'external.0.faccessat', 'external.0.openat'
    )
    foreach ($test in $enoentTests) {
        $row = @($rows | Where-Object { $_.test -eq $test -and $_.path -eq $hiddenPath })
        if ($row.Count -ne 1 -or $row[0].return_value -ne -1) {
            throw "LEAK: HideLab $Role exposed hidden target for $test"
        }
        if ($row[0].errno -ne 2) {
            throw "SEMANTIC_DRIFT: HideLab $Role returned errno $($row[0].errno) for $test"
        }
    }
    foreach ($test in @('java.external.0.list', 'java.external.0.nio_directoryStream', 'external.0.readdir',
                        'external.0.getdents64_4096', 'external.0.getdents64_32768',
                        'external.0.getdents64_65536', 'external.0.getdents64_131072')) {
        $row = @($rows | Where-Object { $_.test -eq $test -and $_.path -eq $hiddenPath })
        if ($row.Count -ne 1 -or $row[0].return_value -ne 0 -or $row[0].side_effect) {
            throw "LEAK: HideLab $Role did not omit hidden basename for $test"
        }
    }
    $mutationRows = @($rows | Where-Object { $_.test -like 'external.mutation.*' })
    foreach ($row in $mutationRows) {
        if ($row.return_value -ne -1 -or $row.errno -ne 2 -or $row.side_effect) {
            $kind = if ($row.return_value -eq -1 -and $row.errno -ne 2) { 'SEMANTIC_DRIFT' } else { 'LEAK' }
            throw "$kind`: HideLab $Role mutation was not fail-closed: $($row.test)"
        }
    }
}

function Assert-Concurrency([string]$Role, [bool]$ExpectHidden) {
    $rows = Read-Observations $Role
    $expected = if ($ExpectHidden) { 0 } else { 2000 }
    foreach ($test in @('concurrency.stat', 'concurrency.open', 'concurrency.readdir')) {
        $row = @($rows | Where-Object { $_.test -eq $test -and $_.path -eq $hiddenPath })
        if ($row.Count -ne 1) { throw "HideLab $Role concurrency did not record $test" }
        if ($row[0].return_value -ne $expected) {
            $kind = if ($ExpectHidden) { 'LEAK' } else { 'OVERBLOCK' }
            throw "$kind`: HideLab $Role concurrency $test returned $($row[0].return_value), expected $expected"
        }
    }
}

function Assert-Reliability([string]$Role, [bool]$ExpectHidden) {
    $rows = Read-Observations $Role
    $expected = if ($ExpectHidden) { 0 } else { 1000 }
    foreach ($test in @('reliability.stat', 'reliability.open', 'reliability.readdir')) {
        $row = @($rows | Where-Object { $_.test -eq $test -and $_.path -eq $hiddenPath })
        if ($row.Count -ne 1) { throw "HideLab $Role reliability did not record $test" }
        if ($row[0].return_value -ne $expected) {
            $kind = if ($ExpectHidden) { 'LEAK' } else { 'OVERBLOCK' }
            throw "$kind`: HideLab $Role reliability $test returned $($row[0].return_value), expected $expected"
        }
    }
    foreach ($test in @('reliability.generation', 'reliability.capacity',
                        'reliability.namespace', 'reliability.unload')) {
        $row = @($rows | Where-Object { $_.test -eq $test -and $_.path -eq $hiddenPath })
        if ($row.Count -ne 1 -or $row[0].status -ne 'unsupported') {
            throw "STATE_LIE: HideLab $Role reported $test without a backend control ABI"
        }
    }
}

try {
    if ($KeepTargetProcess) {
        $targetPackagePath = ((& $adb shell pm path $targetPackage 2>$null) -join '').Trim()
        if (-not $targetPackagePath) { throw 'KeepTargetProcess requires an installed target APK' }
    } else {
        Invoke-Adb @('install', '-r', $targetApkPath)
    }
    Invoke-Adb @('install', '-r', $controlApkPath)
    if ($GrantReadMediaImages) {
        Invoke-Adb @('shell', 'pm', 'grant', $targetPackage, 'android.permission.READ_MEDIA_IMAGES')
        Invoke-Adb @('shell', 'pm', 'grant', $controlPackage, 'android.permission.READ_MEDIA_IMAGES')
    }
    if ($GrantAllFilesAccess) {
        Invoke-Adb @('shell', 'appops', 'set', $targetPackage, 'MANAGE_EXTERNAL_STORAGE', 'allow')
        Invoke-Adb @('shell', 'appops', 'set', $controlPackage, 'MANAGE_EXTERNAL_STORAGE', 'allow')
    }
    Reset-Fixture
    $before = Get-OracleSnapshot 'oracle-before'
    $targetBefore = Get-OracleSnapshot 'oracle-before-target'
    Invoke-Probe 'target' $targetPackage
    $targetAfter = Get-OracleSnapshot 'oracle-after-target'
    if ($AttackMutations) { Reset-Fixture }
    $controlBefore = Get-OracleSnapshot 'oracle-before-control'
    Invoke-Probe 'control' $controlPackage
    $controlAfter = Get-OracleSnapshot 'oracle-after-control'
    $targetOracleChanged = $targetBefore -ne $targetAfter
    $controlOracleChanged = $controlBefore -ne $controlAfter
    $targetError = $null
    $controlError = $null
    try {
        if ($Scenario -eq 'baseline') {
            if ($ExpectTargetHidden) { Assert-HideDirectVfs 'target' } else { Assert-BaselineVisible 'target' }
        } elseif ($Scenario -eq 'cache-order') {
            Assert-CacheOrder 'target' $ExpectTargetHidden
        } elseif ($Scenario -eq 'concurrency') {
            Assert-Concurrency 'target' $ExpectTargetHidden
        } else {
            Assert-Reliability 'target' $ExpectTargetHidden
        }
    } catch {
        $targetError = $_.Exception.Message
        if (-not $ExpectTargetHidden) { throw }
    }
    try {
        if ($Scenario -eq 'baseline') { Assert-BaselineVisible 'control' }
        elseif ($Scenario -eq 'cache-order') { Assert-CacheOrder 'control' $false }
        elseif ($Scenario -eq 'concurrency') { Assert-Concurrency 'control' $false }
        else { Assert-Reliability 'control' $false }
    } catch {
        $controlError = $_.Exception.Message
        if (-not $ExpectTargetHidden) { throw }
    }
    if (-not $AttackMutations -and ($targetOracleChanged -or $controlOracleChanged)) {
        throw 'Root Oracle detected mutation during read-only probe'
    }
    $after = Get-OracleSnapshot 'oracle-after'
    if (-not $AttackMutations -and $before -ne $after) { throw 'Root Oracle detected fixture mutation during no-backend baseline' }
    $summary = [ordered]@{
        schema = 2; run_id = $runId; phase = $Scenario; backend = $Backend; attack_mutations = [bool]$AttackMutations; fixture_root = $fixtureRoot
        target_package = $targetPackage; control_package = $controlPackage
        fixture_unchanged = ($before -eq $after); target_oracle_changed = $targetOracleChanged; control_oracle_changed = $controlOracleChanged
        conclusion = if ($ExpectTargetHidden -and $targetOracleChanged) { 'DESTRUCTIVE_FAIL' } elseif ($ExpectTargetHidden -and $controlError) { 'OVERBLOCK' } elseif ($ExpectTargetHidden -and $targetError -like 'SEMANTIC_DRIFT:*') { 'SEMANTIC_DRIFT' } elseif ($ExpectTargetHidden -and $targetError) { 'LEAK' } elseif ($ExpectTargetHidden) { 'PASS' } elseif ($AttackMutations) { 'BASELINE_MUTATION_VISIBLE' } elseif ($Scenario -eq 'cache-order') { 'BASELINE_CACHE_ORDER_VISIBLE_NOT_HIDE_PASS' } else { 'BASELINE_VISIBLE_NOT_HIDE_PASS' }
        target_error = $targetError; control_error = $controlError
    } | ConvertTo-Json
    Set-Content -LiteralPath (Join-Path $runOutput 'summary.json') -Value $summary -Encoding utf8
    Write-Output "HIDELAB_EVIDENCE:$runOutput"
} finally {
    if (-not $ExistingHiddenPath -and $fixtureRoot -match '^/storage/emulated/0/Pictures/PathGuardHideLab/\d{8}-\d{6}$') {
        Invoke-Root "rm -rf $fixtureRoot" 2>$null
    }
}
