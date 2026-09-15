param(
    [Parameter(Mandatory = $true)] [string]$TargetApk,
    [Parameter(Mandatory = $true)] [string]$ControlApk,
    [switch]$GrantAllFilesAccess,
    [switch]$RunMutations,
    [switch]$ConfirmMutation,
    [switch]$BaselineOnly,
    [string]$OutputDirectory = 'build/device-evidence/hide1-regression'
)

$ErrorActionPreference = 'Stop'
if ($RunMutations -and -not $ConfirmMutation) {
    throw 'RunMutations changes the disposable fixture; pass -ConfirmMutation explicitly'
}
$runner = Join-Path $PSScriptRoot 'run_hidelab_baseline.ps1'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$runId = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$out = Join-Path (Join-Path $root $OutputDirectory) $runId
New-Item -ItemType Directory -Force -Path $out | Out-Null
$common = @('-TargetApk', $TargetApk, '-ControlApk', $ControlApk,
            '-OutputDirectory', "$OutputDirectory/$runId")
if ($GrantAllFilesAccess) { $common += '-GrantAllFilesAccess' }
if (-not $BaselineOnly) { $common += '-ExpectTargetHidden' }
$scenarios = @('baseline', 'cache-order', 'concurrency', 'reliability')
$results = [System.Collections.Generic.List[object]]::new()
foreach ($scenario in $scenarios) {
    $args = @('-NoProfile', '-File', $runner) + $common + @('-Scenario', $scenario)
    $marker = ((& powershell @args | Where-Object { $_ -like 'HIDELAB_EVIDENCE:*' } | Select-Object -Last 1) -join '').Trim()
    if (-not $marker) { throw 'HideLab did not emit an evidence marker' }
    $path = $marker.Substring('HIDELAB_EVIDENCE:'.Length)
    if (-not (Test-Path -LiteralPath $path)) { throw "HideLab did not return an evidence directory: $path" }
    $summaryPath = Join-Path $path 'summary.json'
    $results.Add((Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json))
}
if ($RunMutations) {
    $args = @('-NoProfile', '-File', $runner) + $common + @('-AttackMutations', '-ConfirmMutation')
    $marker = ((& powershell @args | Where-Object { $_ -like 'HIDELAB_EVIDENCE:*' } | Select-Object -Last 1) -join '').Trim()
    if (-not $marker) { throw 'HideLab did not emit an evidence marker' }
    $path = $marker.Substring('HIDELAB_EVIDENCE:'.Length)
    if (-not (Test-Path -LiteralPath $path)) { throw "HideLab did not return an evidence directory: $path" }
    $summaryPath = Join-Path $path 'summary.json'
    $results.Add((Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json))
}
$conclusion = if (@($results | Where-Object { $_.conclusion -in @('LEAK','OVERBLOCK','SEMANTIC_DRIFT','DESTRUCTIVE_FAIL','CRASH','HANG') }).Count -gt 0) {
    'blocked'
} elseif (@($results | Where-Object { $_.conclusion -eq 'PASS' }).Count -eq $results.Count) {
    'candidate_pass_requires_admission'
} else {
    'baseline_or_unsupported'
}
[ordered]@{ schema = 1; run_id = $runId; scenarios = $results; conclusion = $conclusion } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $out 'full-regression.json') -Encoding utf8
Write-Output (Join-Path $out 'full-regression.json')
