param(
    [string]$Allowlist,
    [Parameter(Mandatory = $true)] [string]$RegressionEvidence,
    [Parameter(Mandatory = $true)] [string]$ModulePath,
    [Parameter(Mandatory = $true)] [UInt64]$ExpectedGeneration,
    [string]$ModuleDir = '/data/adb/modules/pathguard_next',
    [string]$OutputDirectory = 'build/device-evidence/hide1-admission'
)

$ErrorActionPreference = 'Stop'
$scriptRoot = $PSScriptRoot
if (-not $Allowlist) { $Allowlist = Join-Path $scriptRoot 'hide1_device_kmi_allowlist.json' }
$adb = (Get-Command adb -ErrorAction Stop).Source
$readElf = (Get-Command readelf -ErrorAction SilentlyContinue)
if (-not $readElf) { throw 'admission requires a host readelf executable' }
$readElf = $readElf.Source

function Adb-Get([string[]]$Arguments) {
    $value = (& $adb @Arguments) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "adb failed: $($Arguments -join ' ')" }
    return $value.Trim()
}

function Adb-Test([string[]]$Arguments) {
    & $adb @Arguments | Out-Null
    return $LASTEXITCODE -eq 0
}

function Get-StatusValue([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text, "(?:^|\s)$([regex]::Escape($Name))=(?<value>-?\d+)(?:\s|$)")
    if (-not $match.Success) { return $null }
    return [Int64]$match.Groups['value'].Value
}

function Get-UndefinedSymbols([string]$Path) {
    $lines = @(& $readElf -Ws $Path 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "readelf failed for module: $Path" }
    return @($lines |
        Select-String ' UND\s+\S+$' |
        ForEach-Object {
            $parts = ($_.Line -split '\s+')
            if ($parts.Count -gt 0) { $parts[-1] }
        } |
        Where-Object { $_ -and $_ -ne 'UND' } |
        Sort-Object -Unique)
}

function Get-SymbolSetDigest([string[]]$Symbols) {
    $payload = [Text.Encoding]::UTF8.GetBytes(($Symbols -join "`n"))
    $digest = [Security.Cryptography.SHA256]::Create().ComputeHash($payload)
    return (-join ($digest | ForEach-Object { $_.ToString('x2') }))
}

function Get-Kallsyms([string]$Text) {
    $symbols = @{}
    foreach ($line in ($Text -split "`n")) {
        $parts = $line.Trim() -split '\s+'
        if ($parts.Count -lt 3 -or $parts[2] -eq '') { continue }
        $address = $parts[0]
        if ($address -match '^[0-9a-fA-F]+$') {
            $symbols[$parts[2]] = $address.ToLowerInvariant()
        }
    }
    return $symbols
}

function Get-KmiFromRelease([string]$Release) {
    if ($Release -match '^(?<linux>\d+\.\d+).*-(?<android>android\d+)-') {
        return "$($Matches.android)-$($Matches.linux)"
    }
    return $null
}

$allow = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $Allowlist) | ConvertFrom-Json
$modulePathResolved = (Resolve-Path -LiteralPath $ModulePath).Path
$evidencePathResolved = (Resolve-Path -LiteralPath $RegressionEvidence).Path
$moduleHash = (Get-FileHash -LiteralPath $modulePathResolved -Algorithm SHA256).Hash.ToLowerInvariant()
$undefinedSymbols = Get-UndefinedSymbols $modulePathResolved
$undefinedDigest = Get-SymbolSetDigest $undefinedSymbols

$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object { $_ -match '\sdevice(?:\s|$)' })
if ($devices.Count -ne 1) { throw "Hide 1.0 admission requires exactly one ready device, got $($devices.Count)" }
$serial = ($devices[0] -split '\s+')[0]

$fingerprint = Adb-Get @('-s', $serial, 'shell', 'getprop', 'ro.build.fingerprint')
$device = Adb-Get @('-s', $serial, 'shell', 'getprop', 'ro.product.device')
$kernelRelease = Adb-Get @('-s', $serial, 'shell', 'uname', '-r')
$bootId = Adb-Get @('-s', $serial, 'shell', 'cat', '/proc/sys/kernel/random/boot_id')
$unameMachine = Adb-Get @('-s', $serial, 'shell', 'uname', '-m')
$abi64 = Adb-Get @('-s', $serial, 'shell', 'getprop', 'ro.product.cpu.abilist64')
$reportedKmi = Adb-Get @('-s', $serial, 'shell', 'getprop', 'ro.boot.kernel.kmi')
if (-not $reportedKmi) { $reportedKmi = Adb-Get @('-s', $serial, 'shell', 'getprop', 'ro.boot.kmi') }
$derivedKmi = Get-KmiFromRelease $kernelRelease
$runtimeKmi = if ($reportedKmi) { $reportedKmi } else { $derivedKmi }
$arch = if ($unameMachine -eq 'aarch64' -or $abi64 -match 'arm64-v8a') { 'aarch64' } else { $unameMachine }

$entry = @($allow.entries | Where-Object {
    $_.device -eq $device -and
    $_.fingerprint -eq $fingerprint -and
    $_.kernel_release -eq $kernelRelease
}) | Select-Object -First 1

$moduleLive = (Adb-Test @('-s', $serial, 'shell', 'test', '-d', '/sys/module/pathguard_hide1')) -and
    (Adb-Test @('-s', $serial, 'shell', 'test', '-e', '/dev/pathguard_hide1'))
$deviceModuleHash = $null
if ($moduleLive) {
    $hashText = Adb-Get @('-s', $serial, 'shell', 'su', '-W', '-c', "sha256sum $ModuleDir/bin/pathguard_hide1.ko")
    if ($hashText -match '^(?<hash>[0-9a-fA-F]{64})\s+') { $deviceModuleHash = $Matches.hash.ToLowerInvariant() }
}

$statusText = $null
if ($moduleLive) {
    $statusText = Adb-Get @('-s', $serial, 'shell', 'su', '-W', '-c', "$ModuleDir/bin/hide1ctl status")
}
$moduleState = if ($moduleLive) { 'live' } else { 'missing' }
$activeState = (Get-StatusValue $statusText 'state') -eq 2
$runningLifecycle = (Get-StatusValue $statusText 'lifecycle') -eq 2
$statusGeneration = Get-StatusValue $statusText 'generation'
$statusShadowMode = if ($moduleLive) {
    $shadowModeText = Adb-Get @('-s', $serial, 'shell', 'su', '-W', '-c', 'cat /sys/module/pathguard_hide1/parameters/shadow_mode')
    $shadowModeValue = 0
    if (-not [int]::TryParse($shadowModeText, [Globalization.NumberStyles]::Integer,
                             [Globalization.CultureInfo]::InvariantCulture,
                             [ref]$shadowModeValue)) {
        throw "invalid shadow_mode from device: $shadowModeText"
    }
    $shadowModeValue
} else { $null }
$statusParentInode = Get-StatusValue $statusText 'parent_inode'
$kallsyms = if ($moduleLive) { Get-Kallsyms (Adb-Get @('-s', $serial, 'shell', 'su', '-W', '-c', 'cat /proc/kallsyms')) } else { @{} }
$unresolvedSymbols = @($undefinedSymbols | Where-Object { -not $kallsyms.ContainsKey($_) })

$evidence = Get-Content -Raw -LiteralPath $evidencePathResolved | ConvertFrom-Json
$requiredScenarios = @('baseline', 'cache-order', 'concurrency', 'reliability', 'mutation')
$evidenceScenarioNames = @($evidence.scenarios | ForEach-Object { $_.phase })
$failedEvidenceScenarios = @($evidence.scenarios | Where-Object { $_.conclusion -ne 'PASS' } | ForEach-Object { $_.phase })
$evidenceParentInodes = @($evidence.scenarios | ForEach-Object { [string]$_.parent_inode } | Sort-Object -Unique)

$failures = [System.Collections.Generic.List[string]]::new()
if (-not $entry) { $failures.Add('device_fingerprint_kernel_not_allowlisted') }
if ($entry -and $entry.arch -ne $arch) { $failures.Add("architecture_mismatch:$arch") }
if ($entry -and $entry.kmi -ne $runtimeKmi) { $failures.Add("kmi_mismatch:$runtimeKmi") }
if ($entry -and $entry.kmi -ne $derivedKmi) { $failures.Add("kmi_release_derivation_mismatch:$derivedKmi") }
if ($entry -and $entry.module_sha256 -ne $moduleHash) { $failures.Add('module_artifact_hash_not_allowlisted') }
if ($entry -and $entry.module_sha256 -ne $deviceModuleHash) { $failures.Add('loaded_module_hash_mismatch') }
if ($entry -and [int]$entry.undefined_symbol_count -ne $undefinedSymbols.Count) { $failures.Add('undefined_symbol_count_mismatch') }
if ($entry -and $entry.undefined_symbols_sha256 -ne $undefinedDigest) { $failures.Add('undefined_symbol_set_mismatch') }
if ($unresolvedSymbols.Count -gt 0) { $failures.Add('undefined_symbols_unresolved') }
if (-not $moduleLive) { $failures.Add('pathguard_hide1_module_not_live') }
if (-not $activeState -or -not $runningLifecycle) { $failures.Add('module_not_active_running') }
if ($statusGeneration -ne [Int64]$ExpectedGeneration) { $failures.Add('generation_mismatch') }
if ($statusShadowMode -ne 0) { $failures.Add('shadow_mode_mismatch') }
if ($evidence.schema -lt 3 -or -not $evidence.active -or $evidence.shadow_mode -ne 0) { $failures.Add('invalid_full_regression_metadata') }
if (-not $evidence.device -or $evidence.device -ne $device) { $failures.Add('regression_device_mismatch') }
if (-not $evidence.fingerprint -or $evidence.fingerprint -ne $fingerprint) { $failures.Add('regression_fingerprint_mismatch') }
if (-not $evidence.kernel_release -or $evidence.kernel_release -ne $kernelRelease) { $failures.Add('regression_kernel_mismatch') }
if (-not $evidence.boot_id -or $evidence.boot_id -ne $bootId) { $failures.Add('regression_boot_id_mismatch') }
if (-not $evidence.module_sha256 -or $evidence.module_sha256 -ne $deviceModuleHash) { $failures.Add('regression_module_hash_mismatch') }
if ([Int64]$evidence.status_generation -ne $statusGeneration) { $failures.Add('regression_generation_mismatch') }
if ([Int64]$evidence.status_parent_inode -ne $statusParentInode) { $failures.Add('regression_parent_inode_mismatch') }
if ([string]$evidence.status_shadow_mode -ne [string]$statusShadowMode) { $failures.Add('regression_shadow_mode_mismatch') }
if ($evidence.conclusion -ne 'candidate_pass_requires_admission') { $failures.Add('full_regression_not_candidate_pass') }
if (@($requiredScenarios | Where-Object { $_ -notin $evidenceScenarioNames }).Count -gt 0) { $failures.Add('full_regression_missing_scenarios') }
if ($failedEvidenceScenarios.Count -gt 0) { $failures.Add('full_regression_scenario_failed') }
if (-not $evidence.mountinfo_unchanged) { $failures.Add('mountinfo_changed') }
if ($evidenceParentInodes.Count -ne 1 -or $statusParentInode -ne [Int64]$evidenceParentInodes[0]) { $failures.Add('parent_inode_binding_mismatch') }

$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$output = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $output | Out-Null
$decision = if ($failures.Count -eq 0) { 'admitted' } elseif ($failures -contains 'full_regression_not_candidate_pass' -or $failures -contains 'full_regression_missing_scenarios') { 'pending_hidelab' } else { 'unsupported' }
$status = [ordered]@{
    schema = 2
    run_id = $runId
    serial = $serial
    device = $device
    fingerprint = $fingerprint
    kernel_release = $kernelRelease
    boot_id = $bootId
    arch = $arch
    uname_machine = $unameMachine
    kmi = $runtimeKmi
    kmi_source = if ($reportedKmi) { 'boot_property' } else { 'kernel_release_derived' }
    allowlist_match = [bool]$entry
    module_state = $moduleState
    module_sha256 = $deviceModuleHash
    module_artifact_sha256 = $moduleHash
    undefined_symbol_count = $undefinedSymbols.Count
    undefined_symbols_sha256 = $undefinedDigest
    undefined_symbols = $undefinedSymbols
    unresolved_symbols = $unresolvedSymbols
    status_state = Get-StatusValue $statusText 'state'
    status_lifecycle = Get-StatusValue $statusText 'lifecycle'
    status_generation = $statusGeneration
    status_shadow_mode = $statusShadowMode
    status_parent_inode = $statusParentInode
    regression_evidence = $evidencePathResolved
    regression_run_id = $evidence.run_id
    regression_conclusion = $evidence.conclusion
    regression_scenarios = $evidenceScenarioNames
    regression_failed_scenarios = $failedEvidenceScenarios
    mountinfo_unchanged = [bool]$evidence.mountinfo_unchanged
    admission = $decision
    product_state = 'supported_scope_myron'
    ota_recheck_required = $true
    failures = @($failures)
}
$json = $status | ConvertTo-Json -Depth 8
Set-Content -LiteralPath (Join-Path $output 'admission.json') -Value $json -Encoding utf8
$json
Write-Output "HIDE1_ADMISSION_EVIDENCE:$output/admission.json"
