# R1 safety and performance validation

## Required device tiers

- strict `open_tree_move_mount` device;
- strict `proc_fd_mount` device;
- explicitly authorized `legacy_string` device;
- unsupported attach backend;
- at least two ROM families and one low/mid performance tier each.

Kernel capability tiers and performance tiers are independent dimensions.

## Fault-injection builds

Use `scripts/build-native.ps1` with one injection at a time:

```powershell
./scripts/build-native.ps1 -ZygiskTestPreLeaseDelayMs 450
./scripts/build-native.ps1 -ZygiskTestMountDelayMs 450
./scripts/build-native.ps1 -ZygiskTestCrashAfterMount
./scripts/build-native.ps1 -ZygiskTestRollbackFailure
```

Expected results:

- pre-lease delay: `cancelled`, no namespace mutation;
- post-mount delay: verified rollback before the app continues;
- worker crash: `namespace_tainted`, target namespace members terminated;
- rollback failure: `namespace_tainted`, no `rollback_complete` report;
- storage remount/topology change: old generation rejected before mutation;
- policy replacement between specialize and lease: `ESTALE`, no mutation.

After every case, verify the target PID start time, mount namespace inode,
`/proc/<pid>/mountinfo`, runtime status record, and absence/presence of Provider hooks.

## Performance collection

Run:

```powershell
./tests/device/collect_r1_evidence.ps1 -Package <package> -Samples 50
```

Report cold and warm P50/P95/P99/max for readiness, topology capture, probe, setns,
per-mount apply/verify, rollback, and total wait. Results collected before the full
root/filesystem/device/parent verification change must not be mixed with current data.

## Alioth production snapshot result (2026-07-26)

- Environment: alioth, Android 13, Linux 4.19.157, SELinux Enforcing, Magisk 30.6.
- Configuration: one LocalSend redirect, strict `proc_fd`, production build.
- Samples: 50 force-stop/cold-start cycles.
- Outcome: 50/50 committed, 50/50 `mi_snapshots=2`, 50/50 probe cache hits.
- Safety: no cancellation, rollback failure, namespace taint, or mount remaining after
  force-stop; runtime status remained `active`, `complete`, and `fd_pinned`.
- P95: mount transaction 82.429ms, companion total 215.900ms, app-side wait 199.176ms.
- P99/max: mount transaction 102.982ms, companion total 237.772ms, app-side wait
  218.472ms.
- Evidence: `build/device-evidence/snapshot-prod-50-20260726-120101/`.

This passes the current alioth production-path gate only. Owner-death and rollback-failure
injection builds, other backend tiers, and the cross-ROM matrix remain separate gates.

## Alioth owner-death injection result (2026-07-26)

- Injection: `PATHGUARD_TEST_CRASH_AFTER_MOUNT=1`, dual-ABI universal package.
- Samples: one detailed run plus five repeated LocalSend cold-start cycles.
- Outcome: every run reported `EOWNERDEAD(130)`, `namespace_tainted`, and
  `committed=0`; no run reported `rollback_complete` or `committed=1`.
- Namespace termination: every repeated run reported `matched=1`, `signaled=1`, and
  `remaining=0`; the LocalSend PID was absent after each cycle.
- Runtime status: `enforcement=failed`, `transaction=namespace_tainted`,
  `reason=owner_death`, and `error=130` for the terminated PID.
- Residual check: no `/proc/<pid>/mountinfo` contained the configured visible mount after
  the namespace members were terminated.
- Evidence: `build/device-evidence/crash-after-mount-20260726/`.

## Alioth rollback-failure injection result (2026-07-26)

- Injection: `PATHGUARD_TEST_MOUNT_DELAY_MS=450` plus
  `PATHGUARD_TEST_ROLLBACK_FAILURE=1`, dual-ABI universal package.
- Injection scope correction: the first build also affected capability-probe cleanup and
  therefore stopped at `ENOTSUP`. The injection was moved from the shared executor into
  the real transaction rollback call site so probe mounts always use the production
  unmount path.
- Samples: one detailed run plus five repeated LocalSend cold-start cycles with the
  corrected build.
- Probe: every repeated run reported `probe_errno=0`, `primitives=16`, and selected strict
  `proc_fd`.
- Outcome: every run mounted and journaled the redirect, received cancellation during the
  post-mount delay, then reported injected `EIO(5)`, `namespace_tainted`, and
  `committed=0`; no run reported `rollback_complete` or `committed=1`.
- Namespace termination: every repeated run reported `remaining=0`, and the LocalSend PID
  was absent after each cycle.
- Runtime status: `enforcement=failed`, `backend=2`, `security=fd_pinned`,
  `transaction=namespace_tainted`, `reason=rollback_failed`, and `error=5`.
- Residual check: no `/proc/<pid>/mountinfo` contained the configured visible mount after
  namespace termination.
- Evidence: `build/device-evidence/rollback-failure-v2-20260726/`.

## Final production recovery (2026-07-26)

- Production package was rebuilt after narrowing the rollback injection scope and adding
  the confirmed mount ID to unmount-failure diagnostics; all injection flags were disabled.
- Device Zygisk hashes matched the local arm64 and arm32 production artifacts after reboot.
- Ten additional LocalSend force-stop/cold-start cycles were 10/10 `committed=1`, 10/10
  `mi_snapshots=2`, and 10/10 `result=0`, with no cancellation, rollback failure, or taint.
- P95: mount transaction 84.620ms, companion total 204.759ms, app-side wait 186.686ms.
- Runtime status was `active`, `backend=2`, `complete`, `fd_pinned`, and `error=0`.
- Redirect visibility was correct, and a final force-stop plus full `/proc` scan found no
  remaining configured mount.
- Evidence: `build/device-evidence/final-production-20260726/`.
