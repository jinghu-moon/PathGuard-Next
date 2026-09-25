param(
    [Parameter(Mandatory = $true)] [string]$AdmissionPath,
    [UInt64]$ExpectedParentInode = 836528,
    [string]$OutputDirectory = 'build/device-evidence/hide1-admission-revocation'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$adb = (Get-Command adb -ErrorAction Stop).Source
$admission = (Resolve-Path -LiteralPath $AdmissionPath).Path
$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$out = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Adb-Root([string]$Command) {
    & $adb shell ('su -W -c "' + $Command + '"') | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "adb root command failed: $Command" }
}

function Get-Status {
    $text = ((& $adb shell su -W -c '/data/adb/modules/pathguard_hide1_lab/bin/hide1ctl status') -join '').Trim()
    if ($LASTEXITCODE -ne 0) { throw 'cannot read hide1 status' }
    $values = [ordered]@{}
    foreach ($name in @('state', 'lifecycle', 'generation', 'parent_inode', 'operation_mask')) {
        $match = [regex]::Match($text, "(?:^|\s)$name=(0x[0-9a-fA-F]+|-?\d+)(?:\s|$)")
        if ($match.Success) {
            $values[$name] = if ($match.Groups[1].Value.StartsWith('0x')) {
                [Convert]::ToUInt64($match.Groups[1].Value.Substring(2), 16)
            } else { [UInt64]$match.Groups[1].Value }
        }
    }
    $values.raw = $text
    return [pscustomobject]$values
}

function Wait-Status([bool]$Active, [string]$CaseName) {
    $deadline = [DateTimeOffset]::Now.AddSeconds(8)
    do {
        Start-Sleep -Milliseconds 500
        $status = Get-Status
        $matches = if ($Active) {
            $status.state -eq 2 -and $status.lifecycle -eq 2 -and
                $status.operation_mask -eq 0xfff -and
                $status.parent_inode -eq $ExpectedParentInode
        } else {
            $status.state -eq 0 -and $status.lifecycle -eq 0 -and
                $status.operation_mask -eq 0 -and $status.parent_inode -eq 0
        }
    } while (-not $matches -and [DateTimeOffset]::Now -lt $deadline)
    if (-not $matches) {
        throw "$CaseName did not reach expected $($(if ($Active) { 'active' } else { 'inactive' })) state: $($status.raw)"
    }
    return $status
}

function Install-Admission([string]$Path) {
    & $adb push $Path /sdcard/Download/pathguard-hide1-admission.json | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "failed to push admission: $Path" }
    & $adb shell su -W -c '/data/adb/modules/pathguard_hide1_lab/action.sh' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "failed to import admission: $Path" }
}

function Record-Case([string]$Name, [scriptblock]$Action) {
    $before = Get-Status
    & $Action
    $after = Wait-Status $false $Name
    Install-Admission $admission
    $restored = Wait-Status $true "$Name restore"
    [pscustomobject]@{
        name = $Name
        before_state = $before.state
        revoked_state = $after.state
        revoked_lifecycle = $after.lifecycle
        revoked_generation = $after.generation
        revoked_parent_inode = $after.parent_inode
        revoked_operation_mask = $after.operation_mask
        restored_state = $restored.state
        restored_generation = $restored.generation
        restored_parent_inode = $restored.parent_inode
        conclusion = 'PASS'
    }
}

$cases = [System.Collections.Generic.List[object]]::new()
$valid = Get-Content -Raw -LiteralPath $admission | ConvertFrom-Json

$cases.Add((Record-Case 'delete-admission' {
    Adb-Root 'rm -f /data/adb/modules/pathguard_hide1_lab/run/admission.json'
}))

$malformed = Join-Path $out 'admission-malformed.json'
Set-Content -LiteralPath $malformed -Value '{invalid-json' -Encoding ascii
$cases.Add((Record-Case 'malformed-json' {
    Install-Admission $malformed
}))

$rejected = $valid | ConvertTo-Json -Depth 20 | ConvertFrom-Json
$rejected.admission = 'rejected'
$rejectedPath = Join-Path $out 'admission-rejected.json'
$rejected | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $rejectedPath -Encoding utf8
$cases.Add((Record-Case 'admission-not-admitted' {
    Install-Admission $rejectedPath
}))

$oldBoot = $valid | ConvertTo-Json -Depth 20 | ConvertFrom-Json
$oldBoot.boot_id = '00000000-0000-0000-0000-000000000000'
$oldBootPath = Join-Path $out 'admission-old-boot.json'
$oldBoot | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $oldBootPath -Encoding utf8
$cases.Add((Record-Case 'old-boot-admission' {
    Install-Admission $oldBootPath
}))

$cases.Add((Record-Case 'runtime-replacement' {
    Install-Admission $malformed
}))

Install-Admission $admission
$final = Wait-Status $true 'final restore'
$summary = [ordered]@{
    schema = 1
    run_id = $runId
    admission = $admission
    expected_parent_inode = $ExpectedParentInode
    cases = $cases
    final_state = $final.state
    final_lifecycle = $final.lifecycle
    final_generation = $final.generation
    final_parent_inode = $final.parent_inode
    final_operation_mask = $final.operation_mask
    conclusion = if (@($cases | Where-Object conclusion -ne 'PASS').Count -eq 0 -and
        $final.state -eq 2 -and $final.lifecycle -eq 2) { 'PASS' } else { 'BLOCKED' }
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $out 'negative-admission.json') -Encoding utf8
Write-Output (Join-Path $out 'negative-admission.json')
