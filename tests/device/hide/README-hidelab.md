# HideLab

HideLab is the Phase A acceptance system for `hide`. It is a test instrument,
not a production backend and not proof that `hide` is active.

## Contract

`hidelab_acceptance_matrix.json` is the frozen Hide 1.0 direct-VFS matrix.
Every case runs against the same disposable fixture with three observers:

- `target`: the package selected by a future hide rule; expected to receive
  `ENOENT` and never observe the hidden basename;
- `control`: a different application UID; expected to retain normal semantics;
- `oracle`: root-only verifier of the real fixture before and after every
  mutation attempt.

The first delivery is a no-backend baseline: Target and Control are both
expected to observe the fixture. That establishes the probes, fixture and
evidence pipeline. It is deliberately not a Hide 1.0 pass.

## APKs

`app-probe` uses the YingLi-Player toolchain: AGP 9.3.1, Gradle 9.5.0, JDK 21,
Kotlin 2.4.0 via the AGP built-in Kotlin plugin, and API 36. It produces:

```text
app/build/outputs/apk/target/debug/app-target-debug.apk
app/build/outputs/apk/control/debug/app-control-debug.apk
```

Both packages run the same Kotlin and JNI/raw-syscall probe. Their package
names differ, so an implementation must demonstrate real caller isolation.

## Result rules

Each observation is JSONL schema 1. Run metadata is schema 2. The runner must
derive one of `PASS`, `LEAK`, `OVERBLOCK`, `SEMANTIC_DRIFT`,
`DESTRUCTIVE_FAIL`, `STATE_LIE`, `CRASH`, `HANG`, `UNSUPPORTED`, or
`INFRA_ERROR`. A failed syscall with any Oracle-visible change is always
`DESTRUCTIVE_FAIL`, never a normal denial.

The runner supports baseline, cache-order, and concurrency scenarios:

```powershell
# No-backend visibility baseline, with cache-order observations
./tests/device/hide/run_hidelab_baseline.ps1 `
  -TargetApk ./tests/device/hide/app-probe/app/build/outputs/apk/target/debug/app-target-debug.apk `
  -ControlApk ./tests/device/hide/app-probe/app/build/outputs/apk/control/debug/app-control-debug.apk `
  -GrantAllFilesAccess -Scenario cache-order

# Explicit mutation attack baseline; this changes only the disposable fixture
./tests/device/hide/run_hidelab_baseline.ps1 `
  -TargetApk ./tests/device/hide/app-probe/app/build/outputs/apk/target/debug/app-target-debug.apk `
  -ControlApk ./tests/device/hide/app-probe/app/build/outputs/apk/control/debug/app-control-debug.apk `
  -GrantAllFilesAccess -AttackMutations -ConfirmMutation

# Backend validation mode; use only after a hide backend is active
./tests/device/hide/run_hidelab_baseline.ps1 `
  -TargetApk ./tests/device/hide/app-probe/app/build/outputs/apk/target/debug/app-target-debug.apk `
  -ControlApk ./tests/device/hide/app-probe/app/build/outputs/apk/control/debug/app-control-debug.apk `
  -GrantAllFilesAccess -AttackMutations -ConfirmMutation -ExpectTargetHidden `
  -Backend pathguard-hide1
```

Use `-Scenario concurrency` for a 20-thread x 100-iteration mixed
`stat/open/readdir` run. The no-backend baseline expects 2,000 successful
operations per class; hide validation expects zero successful operations.

Use `-Scenario reliability` for 1,000 sequential `stat/open/readdir` rounds.
This scenario also emits explicit `unsupported` observations for generation,
capacity, namespace and unload controls when no VFS backend control ABI is
loaded. `unsupported` is evidence of a missing backend, never a pass.

`-AttackMutations` is rejected unless `-ConfirmMutation` is present. The
runner snapshots the Root Oracle before and after Target and Control and
resets the disposable fixture between observers. Missing files are represented
as `MISSING` in the Oracle snapshot, so an attack that deletes or overwrites a
canary is recorded as `DESTRUCTIVE_FAIL` instead of aborting evidence capture
when `-ExpectTargetHidden` is selected. Without that switch, shared-storage
changes are the expected no-backend visibility baseline and are reported as
`BASELINE_MUTATION_VISIBLE`.

Do not run device mutation commands without explicit approval. The runner will
install two APKs, create and remove only the fixture it records in its manifest,
and archive evidence under ignored `build/device-evidence/`.

## VFS topology preflight

## Kernel backend capability gate

在任何 KPM、namei inline hook 或行为型模块实验之前，先执行只读能力采集：

```powershell
./tests/device/hide/collect_kernel_backend_capability.ps1
```

脚本只查询设备标识、`uname -r`、`/proc/config.gz` 中的 KPM/KALLSYMS/KPROBES 配置、
SukiSU `ksud kpm version/num/list` 和 `/proc/kallsyms` 可见性。它不执行 KPM
`load/unload`、`insmod`、syscall hook 或文件系统 mutation。只有同时满足
`CONFIG_KPM=y`、`CONFIG_KALLSYMS=y`，且三个 KPM 只读查询没有失败文本、版本非空、
数量为整数时，报告才会给出 `eligible_for_kpm_probe`；不能只看 `ksud` 的进程退出码，
因为 SukiSU CLI 可能打印内核负错误码后仍返回 0。否则保持 `unsupported` 或
`indeterminate`，不能继续行为型 namei hook。结果保存在被 Git 忽略的
`build/device-evidence/kernel-backend-capability/`。

在进入只读 FUSE-aware 后端前，先使用 `collect_vfs_topology.ps1` 采集同一设备、同一
mount namespace 中的 `/storage/emulated/0`、`/sdcard` 和 `/storage/self/primary`：

```powershell
./tests/device/hide/collect_vfs_topology.ps1 `
  -PreflightReader ./build/device-evidence/vfs-preflight-local/20260913/device/status_reader-v2 `
  -CoverageReader ./build/device-evidence/vfs-coverage-probe-local/20260913/status_reader
```

采集结果必须同时包含 `mountinfo`、preflight 的 superblock/inode/dentry identity 和
coverage 命中计数。若 alias 指向不同 superblock，后端必须分别绑定；不能把一个 alias
上的 operation shadow 视为整个共享存储的隐藏证明。

`run_hide1_full_regression.ps1` 默认带 `-ExpectTargetHidden`，用于真实后端验收；
当前无后端设备运行该脚本应得到 `blocked`（通常为 `LEAK` 或
`DESTRUCTIVE_FAIL`）。需要重建无后端基线时显式传 `-BaselineOnly`，不得把基线
结果当作 Hide 1.0 通过。

设备后端已完成 INSTALL/ENABLE 后，使用 `-KeepTargetProcess` 运行采集。该选项要求 target
APK 已安装，跳过 target 的重装和 `force-stop`；采集前会锁定已运行的 target PID 和 mount namespace，
采集期间及结束时都必须保持完全一致。namespace 或 PID 变化会直接报错，禁止把失配结果当作 HideLab 证据。
runner 还要求 `status=complete` 对应当前 run id/scenario，避免复用上一次运行的陈旧结果。共享存储目标若依赖媒体权限，同时传入
`-GrantReadMediaImages -GrantAllFilesAccess`；仅设置 all-files AppOp 不足以证明 target
能访问 `/storage/emulated/0`。

若需要对已有且可访问的媒体目录（例如设备基线中的
`/storage/emulated/0/Pictures/Nagram`）做只读后端验证，可传
`-ExistingHiddenPath /storage/emulated/0/Pictures/Nagram`。此模式不会重置或删除该目录，
并自动禁止 `-AttackMutations`；它只适用于 baseline/cache/reliability 等只读场景。一次性
fixture 仍应使用默认路径，以保留 Root Oracle 和清理保证。
### Pre-opened directory-FD mutation protocol

The held-FD case is a two-phase test. The target process must open the governed
directory before `ENABLE`, keep the same PID and mount namespace, and only then
run mutations after `ENABLE`. Use `-KeepFixture` in the preparation phase so
the root oracle fixture is not deleted:

```powershell
./run_hidelab_baseline.ps1 `
  -TargetApk .../app-target-debug.apk `
  -ControlApk .../app-control-debug.apk `
  -Scenario prepare-hidden-fd -KeepTargetProcess -KeepFixture
```

After the module is installed and the target binding is enabled, rerun with
the exact `fixture_root` from the preparation evidence:

```powershell
./run_hidelab_baseline.ps1 `
  -TargetApk .../app-target-debug.apk `
  -ControlApk .../app-control-debug.apk `
  -Scenario preopen-hidden-fd -ExistingHiddenPath <fixture_root>/hidden `
  -KeepTargetProcess -ExpectTargetHidden -AttackMutations -ConfirmMutation
```

The second invocation must use the same target PID/process. A fresh process
would not retain the original directory FD and is not evidence for this case.

For the diagnostic-only `do_symlinkat` probe, use `symlink-held-fd` instead of
the full mutation scenario. It issues exactly one `symlinkat` through the
retained target FD and requires the existing shared-storage result
`EACCES/no-side-effect`; it does not create, truncate, unlink, rename, or mknod:

```powershell
./run_hidelab_baseline.ps1 `
  -TargetApk .../app-target-debug.apk `
  -ControlApk .../app-control-debug.apk `
  -Scenario symlink-held-fd -ExistingHiddenPath <fixture_root>/hidden `
  -KeepTargetProcess -KeepFixture
```

For a backend that implements strict hidden-path semantics at this stage, add
`-ExpectTargetHidden`. The target must then return `ENOENT/no-side-effect`,
while the control observer must retain the baseline `EACCES/no-side-effect`.
