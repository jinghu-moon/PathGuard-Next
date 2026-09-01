# Provider contract device probe

This probe establishes the public Android operation baseline required before a
production Provider contract adapter can be admitted. It does not set bit 17.

Build the APK with the repository's existing Android probe wrapper:

```powershell
Push-Location "tests/device/hide/app-probe"
.\gradlew.bat :providerContract:assembleDebug
Pop-Location
```

Run the interactive probe with exactly one ready device:

```powershell
.\tests\device\provider-contract\run_provider_contract_probe.ps1
```

The system directory picker opens after the automatic MediaStore checks.
Select a disposable directory where the probe may create, rename, open, and
delete one temporary document. Evidence is archived under
`build/device-evidence/provider-contract-v1/<timestamp>/`.

Passing this probe proves the public operation contract only. A version-pinned
adapter profile, virtual source/target mapping, FD identity, reverse mapping,
Provider restart recovery, and fail-open injection must also pass before
`provider_query_insert_mapping` can become active.

The Namespace Projection collector below is retained only for archived
`0.1.45-dev`/`0.1.46-dev` evidence. It is not a production acceptance gate:

```powershell
.\tests\device\provider-contract\collect_namespace_projection_status.ps1
```

For `0.1.47-dev` and later, install and reboot first. Exercise both LocalSend
save modes and receive at least three files, then collect the flat redirect
gate:

```powershell
.\tests\device\provider-contract\collect_flat_redirect_status.ps1
```

The flat gate requires files directly under `localsend-redirect`, no files
below the archived `_pg` layout, active native Provider redirect status, and
no LSPlant Java bridge loaded in either Provider process.

For a build whose app-path file rules explicitly set `audit = true`, exercise
both LocalSend save modes and collect the private, best-effort audit state
separately:

```powershell
.\tests\device\provider-contract\collect_private_audit_status.ps1
```

This gate reads the daemon snapshot through `audit.sock`; it never opens the
live WAL directly. For `0.1.58-dev` it requires observed
`Download/localsend-source/` and `Pictures/` app-path writes plus at least one
identity-verified `metadata_phase=settled` record. Mount-only, direct-syscall,
or otherwise unhooked file operations remain outside the audit coverage and
must not be inferred from missing records.

To determine whether a real completion boundary is observable for settled
metadata, open LocalSend first, run the bounded trace collector, and receive
one large file before its timer expires:

```powershell
.\tests\device\provider-contract\collect_audit_completion_trace.ps1
```

The collector attaches only to LocalSend. It records bounded
`close/fsync/fdatasync/ftruncate` events with decoded file descriptors for 45
seconds, then stops only the trace process it created. It must never ptrace
MediaProvider or ExternalStorageProvider: resolving decoded descriptors while
MediaProvider is stopped can recursively enter the same FUSE daemon and
deadlock the shared-storage stack. A remote watchdog also stops the tracer if
the host command is interrupted.
This evidence decides where a settled audit event can be emitted; a fixed
delay is not accepted as a completion signal.

The collector relaxes read permissions only on its own
`/data/local/tmp/pathguard-audit-trace-*` directory and trace files. A failed
`adb pull` or an empty local trace directory is an explicit collection failure;
`0 files pulled` is not evidence that no completion event occurred.
