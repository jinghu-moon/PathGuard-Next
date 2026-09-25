param(
    [Parameter(Mandatory = $true)] [string]$AdmissionPath,
    [UInt64]$ExpectedParentInode = 836528,
    [string]$OutputDirectory = 'build/device-evidence/hide1-identity-fail-closed'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$adb = (Get-Command adb -ErrorAction Stop).Source
$admission = (Resolve-Path -LiteralPath $AdmissionPath).Path
$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$out = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Root([string]$Command) {
    & $adb shell ('su -W -c "' + $Command + '"') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "adb root command failed: $Command" }
}

function Get-Status {
    $text = ((& $adb shell su -W -c '/data/adb/modules/pathguard_hide1_lab/bin/hide1ctl status') -join ' ').Trim()
    if ($LASTEXITCODE -ne 0) { throw 'cannot read hide1 status' }
    $values = [ordered]@{ raw = $text }
    foreach ($name in @('state', 'lifecycle', 'generation', 'parent_inode', 'operation_mask')) {
        $match = [regex]::Match($text, "(?:^|\s)$name=(0x[0-9a-fA-F]+|-?\d+)(?:\s|$)")
        if ($match.Success) {
            $values[$name] = if ($match.Groups[1].Value.StartsWith('0x')) {
                [Convert]::ToUInt64($match.Groups[1].Value.Substring(2), 16)
            } else { [UInt64]$match.Groups[1].Value }
        }
    }
    return [pscustomobject]$values
}

function Wait-Inactive([string]$Case) {
    $deadline = [DateTimeOffset]::Now.AddSeconds(12)
    do {
        Start-Sleep -Milliseconds 500
        $status = Get-Status
        $matches = $status.state -eq 0 -and $status.lifecycle -eq 0 -and
            $status.generation -eq 0 -and $status.operation_mask -eq 0 -and
            $status.parent_inode -eq 0
    } while (-not $matches -and [DateTimeOffset]::Now -lt $deadline)
    if (-not $matches) { throw "$Case did not reach all-zero inactive state: $($status.raw)" }
    return $status
}

function Wait-Active([string]$Case) {
    $deadline = [DateTimeOffset]::Now.AddSeconds(12)
    do {
        Start-Sleep -Milliseconds 500
        $status = Get-Status
        $matches = $status.state -eq 2 -and $status.lifecycle -eq 2 -and
            $status.generation -gt 0 -and $status.operation_mask -eq 0xfff -and
            $status.parent_inode -eq $ExpectedParentInode
    } while (-not $matches -and [DateTimeOffset]::Now -lt $deadline)
    if (-not $matches) { throw "$Case did not restore active state: $($status.raw)" }
    return $status
}

function Install-Admission([string]$Path) {
    & $adb push $Path /sdcard/Download/pathguard-hide1-admission.json | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "failed to push admission: $Path" }
    & $adb shell su -W -c '/data/adb/modules/pathguard_hide1_lab/action.sh' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "failed to import admission: $Path" }
}

$valid = Get-Content -Raw -LiteralPath $admission | ConvertFrom-Json
$cases = @(
    @{ name = 'boot-id-mismatch'; property = 'boot_id'; value = '00000000-0000-0000-0000-000000000000' },
    @{ name = 'kernel-release-mismatch'; property = 'kernel_release'; value = 'invalid-kernel-release' },
    @{ name = 'fingerprint-mismatch'; property = 'fingerprint'; value = 'invalid/fingerprint:0/user/release-keys' },
    @{ name = 'kmi-mismatch'; property = 'kmi'; value = 'android0-0.0' },
    @{ name = 'module-hash-mismatch'; property = 'module_sha256'; value = ('f' * 64) },
    @{ name = 'admission-rejected'; property = 'admission'; value = 'rejected' }
)

$results = [System.Collections.Generic.List[object]]::new()
foreach ($case in $cases) {
    $candidate = $valid | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    $candidate.($case.property) = $case.value
    $candidatePath = Join-Path $out "$($case.name).json"
    $candidate | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $candidatePath -Encoding utf8
    Install-Admission $candidatePath
    $inactive = Wait-Inactive $case.name
    Install-Admission $admission
    $active = Wait-Active "$($case.name)-restore"
    $results.Add([ordered]@{
        name = $case.name
        mutated_property = $case.property
        revoked_state = $inactive.state
        revoked_lifecycle = $inactive.lifecycle
        revoked_generation = $inactive.generation
        revoked_operation_mask = $inactive.operation_mask
        revoked_parent_inode = $inactive.parent_inode
        restored_state = $active.state
        restored_generation = $active.generation
        restored_parent_inode = $active.parent_inode
        conclusion = 'PASS'
    })
}

$final = Get-Status
$summary = [ordered]@{
    schema = 1
    run_id = $runId
    admission = $admission
    expected_parent_inode = $ExpectedParentInode
    cases = @($results)
    final_state = $final.state
    final_lifecycle = $final.lifecycle
    final_generation = $final.generation
    final_parent_inode = $final.parent_inode
    final_operation_mask = $final.operation_mask
    conclusion = if (@($results | Where-Object conclusion -ne 'PASS').Count -eq 0 -and
        $final.state -eq 2 -and $final.lifecycle -eq 2 -and
        $final.parent_inode -eq $ExpectedParentInode) { 'PASS' } else { 'BLOCKED' }
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $out 'identity-fail-closed.json') -Encoding utf8
Write-Output (Join-Path $out 'identity-fail-closed.json')
