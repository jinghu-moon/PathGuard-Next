param(
    [string]$Matrix = "$PSScriptRoot/hidelab_acceptance_matrix.json",
    [string]$Runner = "$PSScriptRoot/run_hide1_full_regression.ps1",
    [string]$Probe = "$PSScriptRoot/hide_vfs_probe.cpp"
)

$ErrorActionPreference = 'Stop'

function Require-Text([string]$Path, [string[]]$Needles) {
    $text = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $Path)
    foreach ($needle in $Needles) {
        if ($text.IndexOf($needle, [StringComparison]::Ordinal) -lt 0) {
            throw "offline HideLab contract missing '$needle' in $Path"
        }
    }
}

$matrixObject = Get-Content -Raw -LiteralPath (Resolve-Path -LiteralPath $Matrix) |
    ConvertFrom-Json
$requiredIds = @(
    'java_file_list', 'nio_directory_stream', 'libc_readdir',
    'raw_getdents_4k', 'raw_getdents_32k', 'raw_getdents_64k',
    'raw_getdents_128k', 'stat_lstat_statx', 'access_faccessat2',
    'open_openat_openat2_opath', 'opendir_descendant', 'alias_and_relative',
    'create_mkdir_link_symlink', 'truncate_unlink_rmdir',
    'rename_source_and_destination', 'cache_cold_and_positive_dentry',
    'concurrent_lookup_open_readdir', 'enable_disable_generation',
    'mount_observability'
)
$actualIds = @($matrixObject.cases | ForEach-Object id)
foreach ($id in $requiredIds) {
    if ($id -notin $actualIds) { throw "acceptance matrix missing case '$id'" }
}
if ($actualIds.Count -ne $requiredIds.Count) {
    throw "acceptance matrix has unexpected case count: $($actualIds.Count)"
}

Require-Text $Runner @(
    'active full regression requires',
    "'baseline', 'cache-order', 'concurrency', 'reliability'",
    "if (`$RunMutations) { `$requiredScenarios += 'mutation' }",
    "@('-Scenario', 'mutation', '-AttackMutations', '-ConfirmMutation')",
    'mountinfo-before.txt', 'mountinfo-after.txt',
    'candidate_pass_requires_admission', "`$FixtureRoot", "@('-FixtureRoot', `$FixtureRoot)",
    'ShadowMode', 'shadow_mode=0', 'Get-KernelShadowMode',
    'actualShadowMode', 'numeric parent_inode',
    '$expectedParentInodeProvided',
    'if ($expectedParentInodeProvided -and'
)
$baselineRunner = Join-Path (Split-Path -Parent $Runner) 'run_hidelab_baseline.ps1'
Require-Text $baselineRunner @(
    'InitializeFixture', 'ExpectedParentInode',
    'refusing to reset an explicitly bound FixtureRoot',
    'test -d $fixtureRoot && test -d $hiddenPath && echo READY || echo MISSING',
    'fixture is not prepared', 'parent_inode = $parentInode', 'shadow_mode = if'
)
Require-Text $Probe @(
    '.openat2', '.faccessat2', '.getdents64_', 'scenario != "mutation"',
    'external.mutation.openat_create',
    'external.mutation.openat_truncate',
    'external.mutation.mkdirat',
    'external.mutation.unlinkat',
    'external.mutation.rename_source',
    'external.mutation.rename_destination',
    'external.mutation.linkat',
    'external.mutation.symlinkat',
    'external.fd_mutation.mknod',
    'sandbox.hidden.openat_dot',
    'sandbox.hidden.openat_parent',
    'sandbox.symlink_alias'
)

$package = Join-Path (Split-Path -Parent $PSScriptRoot) '../../experimental/hide-vfs/package'
$package = [System.IO.Path]::GetFullPath($package)
Require-Text (Join-Path $package 'service.sh') @(
    'hide1_device_profile.json', ' insmod ', 'shadow_mode=0',
    'module_sha256', 'automatic load denied', 'pathguardd'
)
Require-Text (Join-Path $package 'post-fs-data.sh') @(
    'boot-state', 'boot_id=', 'module_sha256=', 'automatic_enable='
)
Require-Text (Join-Path $package 'config/hide1_device_profile.json') @(
    '"device": "myron"', '"arch": "aarch64"',
    '"kernel_release":', '"module_sha256": "@MODULE_SHA256@"'
)
$packager = Join-Path (Split-Path -Parent $PSScriptRoot) '../../scripts/package-hide1-pathguardd-lab.ps1'
$packager = [System.IO.Path]::GetFullPath($packager)
Require-Text $packager @(
    'hide1_device_profile.json', '@MODULE_SHA256@',
    'Get-FileHash -Algorithm SHA256 -LiteralPath $ko'
)
if ((Get-Content -Raw -LiteralPath $packager).Contains('hide1_device_kmi_allowlist.json')) {
    throw 'package profile must not use the multi-device regression allowlist'
}

Write-Output 'hidelab-offline-contract: PASS'
