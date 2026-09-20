[CmdletBinding()]
param(
    [string]$OutputDirectory = 'build/device-evidence/kernel-backend-capability'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$adb = (Get-Command adb -ErrorAction Stop).Source
$ready = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match '\sdevice(?:\s|$)'
})
if ($ready.Count -ne 1) {
    throw "kernel backend capability collector requires exactly one ready device, got $($ready.Count)"
}

$stamp = [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss')
$out = Join-Path (Join-Path $root $OutputDirectory) $stamp
New-Item -ItemType Directory -Force -Path $out | Out-Null

function Invoke-RootReadOnly {
    param([Parameter(Mandatory = $true)][string]$Command)

    $output = @(& $adb shell su -W -c $Command 2>&1)
    [ordered]@{
        command = $Command
        exit_code = $LASTEXITCODE
        output = (($output | ForEach-Object { $_.ToString() }) -join "`n").Trim()
    }
}

$queries = [ordered]@{
    device = 'getprop ro.boot.product.device; getprop ro.product.device'
    fingerprint = 'getprop ro.build.fingerprint'
    kernel_release = 'uname -r'
    kernel_config = 'test -r /proc/config.gz && gzip -dc /proc/config.gz 2>/dev/null | grep -E ''^(CONFIG_(KPM|KPROBES|KPROBE_EVENTS|KALLSYMS|KALLSYMS_ALL|MODULES|MODULE_SIG|MODULE_SIG_FORCE|CFI_CLANG|TRIM_UNUSED_KSYMS)=|# CONFIG_(KPM|KPROBES|KPROBE_EVENTS|KALLSYMS|KALLSYMS_ALL|MODULES|MODULE_SIG|MODULE_SIG_FORCE|CFI_CLANG|TRIM_UNUSED_KSYMS) is not set)'' || echo CONFIG_UNAVAILABLE'
    su_version = 'command -v ksud 2>/dev/null || true; ksud --version 2>&1 || true'
    kpm_version = 'ksud kpm version 2>&1'
    kpm_num = 'ksud kpm num 2>&1'
    kpm_list = 'ksud kpm list 2>&1'
    kpm_help = 'ksud kpm --help 2>&1'
    kallsyms = @'
grep -E ' (kallsyms_lookup_name|kallsyms_on_each_symbol|kallsyms_on_each_match_symbol|ksu_register_syscall_hook|ksu_unregister_syscall_hook|ksu_syscall_table|ksu_dispatcher_nr)$' /proc/kallsyms 2>/dev/null || true
'@
    loaded_modules = 'cat /proc/modules | grep -E "^(sukisu|kernelsu|pathguard)" || true'
}

$results = [ordered]@{}
foreach ($entry in $queries.GetEnumerator()) {
    $result = Invoke-RootReadOnly -Command ([string]$entry.Value)
    $results[$entry.Key] = $result
    @(
        "command=$($result.command)",
        "exit_code=$($result.exit_code)",
        $result.output
    ) | Set-Content -LiteralPath (Join-Path $out "$($entry.Key).txt") -Encoding utf8
}

$configText = [string]$results.kernel_config.output
$kpmConfig = if ($configText -match '(?m)^CONFIG_KPM=y$') { 'enabled' }
             elseif ($configText -match '(?m)^# CONFIG_KPM is not set$') { 'disabled' }
             else { 'unknown' }
$kallsymsConfig = if ($configText -match '(?m)^CONFIG_KALLSYMS=y$') { 'enabled' }
                 elseif ($configText -match '(?m)^# CONFIG_KALLSYMS is not set$') { 'disabled' }
                 else { 'unknown' }
$kpmVersionOutput = [string]$results.kpm_version.output
$kpmNumOutput = [string]$results.kpm_num.output
$kpmListOutput = [string]$results.kpm_list.output
$kpmVersionValid = [int]$results.kpm_version.exit_code -eq 0 -and
    -not [string]::IsNullOrWhiteSpace($kpmVersionOutput) -and
    $kpmVersionOutput -notmatch '(?i)(failed|not found|no such|enotty|error)'
$kpmNumValid = [int]$results.kpm_num.exit_code -eq 0 -and
    $kpmNumOutput -match '(?m)(^|\s)-?\d+(\s|$)' -and
    $kpmNumOutput -notmatch '(?i)(failed|not found|no such|enotty|error)'
$kpmListValid = [int]$results.kpm_list.exit_code -eq 0 -and
    $kpmListOutput -notmatch '(?i)(failed|not found|no such|enotty|error)'
$kpmQuerySucceeded = $kpmVersionValid -and $kpmNumValid -and $kpmListValid
$state = if ($kpmConfig -eq 'enabled' -and $kallsymsConfig -eq 'enabled' -and $kpmQuerySucceeded) {
    'eligible_for_kpm_probe'
} elseif ($kpmConfig -eq 'disabled' -or $kpmQuerySucceeded -eq $false) {
    'unsupported'
} else {
    'indeterminate'
}

$report = [ordered]@{
    schema = 1
    collected_at = [DateTimeOffset]::Now.ToString('o')
    device = $results.device.output
    fingerprint = $results.fingerprint.output
    kernel_release = $results.kernel_release.output
    kpm_config = $kpmConfig
    kallsyms_config = $kallsymsConfig
    kpm_queries_succeeded = $kpmQuerySucceeded
    kpm_query_checks = [ordered]@{
        version = $kpmVersionValid
        num = $kpmNumValid
        list = $kpmListValid
    }
    decision = $state
    safety = [ordered]@{
        behavior_changed = $false
        kpm_load_attempted = $false
        kpm_unload_attempted = $false
        module_insert_attempted = $false
    }
    queries = $results
}
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $out 'capability.json') -Encoding utf8

Write-Output $out
