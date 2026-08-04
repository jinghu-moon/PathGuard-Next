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
live WAL directly. It requires observed `Download/localsend-source/` and
`Pictures/` app-path writes. Mount-only, direct-syscall, or otherwise unhooked
file operations remain outside the audit coverage and must not be inferred
from missing records.
