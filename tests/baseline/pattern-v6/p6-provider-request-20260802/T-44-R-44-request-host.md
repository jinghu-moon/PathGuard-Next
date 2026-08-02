# T-44～R-44 immutable Provider dispatch request

## Scope

- Copy validated dispatch facts into a versioned request with value-owned buffers.
- Use exact open-mode operations and reject unresolved update/delete dynamics.
- Keep the final request sink pass-through until mapping/provenance wiring is complete.

## Contract

- The request carries no `JNIEnv*`, Java local/global reference or borrowed pointer.
- Static methods require `operations == minimum_operations`.
- Open methods require a nonzero mode-derived subset of their declared mask.
- Update/delete remain incomplete until ContentValues/target extraction supplies dynamic operations.
- Invalid identifier/path facts fail open before the request sink.

## Verification

- `pathguard_provider_lsplant_bridge_test`: request matrix passed.
- MSVC Release CTest: `82/82` passed.
- NDK 29 LSPlant bridge: `arm64-v8a` and `armeabi-v7a` passed.
- Production integration guard: passed.
- `git diff --check`: passed.

## Remaining boundary

`DispatchProviderRequest` currently returns pass-through. It does not call daemon/store, construct Java
results or set capability bit 17. The next step is to add ContentValues/target dynamic extractors and
then consume the request through the existing `ProviderMappingDecisionV1` contract.

## Device result

`build/device-evidence/provider-lsplant-v1/20260802-115321` confirms `0.1.33-dev` loaded both
Provider hook groups with complete method/backup/self-test masks, `provider_bridge_errno=0`, active
admissions and clear capability bit 17. The log contains the native dispatcher installation and no
PathGuard JNI/LSPlant failure; this remains a passthrough regression only.

Device candidate: `dist/pathguard-next-v0.1.33-dev-universal.zip` (`versionCode=34`), SHA-256
`f8a6710474cfdaca7a2c5feb760a1db6a4a80d8be2c703d4a0ac7516db844a5c`. The same hash was verified
at `/sdcard/Download/pathguard-next-v0.1.33-dev-universal.zip`.
