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

Shared Target Namespace Projection evidence is collected after LocalSend has
received at least one test file:

```powershell
.\tests\device\provider-contract\collect_namespace_projection_status.ps1
# After both logical sources have been exercised:
.\tests\device\provider-contract\collect_namespace_projection_status.ps1 -RequireNamespaceCount 2
```
