# T-45～R-45 ContentValues dynamic operation extractor

## Scope

- Extract update operations from the pinned `ContentValues` argument.
- Map non-empty values to metadata mutation and `_display_name` presence to rename.
- Keep delete target classification and route/provenance wiring disabled.

## Contract

- Only `ContentValues.size()` and `containsKey("_display_name")` are read.
- Empty, null, wrong-type or JNI-failure values return incomplete.
- The resulting mask is `kOperationMetadataMutation` with optional `kOperationRename`.
- No arbitrary key enumeration, Java result construction, daemon/store I/O or capability changes occur.

## Verification

- `ProviderContentValuesOperations` and dispatch dynamic-kind matrix passed.
- MSVC Release CTest: `82/82` passed.
- NDK 29 LSPlant bridge: `arm64-v8a` and `armeabi-v7a` passed.
- Production integration guard and `git diff --check`: passed.

## Remaining boundary

MediaProvider delete still requires a trusted file/directory target fact and therefore remains
incomplete. Real mapping is still disabled and capability bit 17 remains clear.

## Device result

`build/device-evidence/provider-lsplant-v1/20260802-120913` confirms `0.1.34-dev` loaded both
Provider groups with complete method/backup/self-test masks, `provider_bridge_errno=0`, active
admissions and clear capability bit 17. The log contains no PathGuard JNI/LSPlant failure; this is a
passthrough regression only.

Device candidate: `dist/pathguard-next-v0.1.34-dev-universal.zip` (`versionCode=35`), SHA-256
`1f3ed4b45aa760efc7be402a144d3c8d038b59a8cf047b0ffae7bc614216ec4b`. The same hash was verified
at `/sdcard/Download/pathguard-next-v0.1.34-dev-universal.zip`.
