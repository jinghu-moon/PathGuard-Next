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
  -GrantAllFilesAccess -AttackMutations -ConfirmMutation -ExpectTargetHidden
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

`run_hide1_full_regression.ps1` 默认带 `-ExpectTargetHidden`，用于真实后端验收；
当前无后端设备运行该脚本应得到 `blocked`（通常为 `LEAK` 或
`DESTRUCTIVE_FAIL`）。需要重建无后端基线时显式传 `-BaselineOnly`，不得把基线
结果当作 Hide 1.0 通过。
