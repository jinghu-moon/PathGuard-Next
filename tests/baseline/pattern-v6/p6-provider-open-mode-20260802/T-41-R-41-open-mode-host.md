# T-41～R-41 Provider open mode bounded extractor

## Scope

- Freeze Android Provider open-mode strings to PathGuard operation masks.
- Extract the mode from the two version-pinned open callbacks with bounded JNI work.
- Keep native dispatch pass-through; do not create a route binding or enable capability bit 17.

## Contract

| Mode | Operations |
| --- | --- |
| `r` | `kOperationOpenRead` |
| `w`, `wt`, `wa` | `kOperationOpenWrite` |
| `rw`, `rwt` | `kOperationOpenRead | kOperationOpenWrite` |
| null, empty, non-ASCII, longer than 3, unknown or wrong type | pass-through |

The callback argument count is checked before extraction. The value at index 2 must be an instance
of `java.lang.String`. JNI exceptions are cleared at the native boundary and produce pass-through.
The extractor uses fixed stack buffers and performs no blocking I/O or heap allocation.

## Verification

- `pathguard_provider_lsplant_bridge_test`: passed.
- MSVC Release CTest: `82/82` passed.
- NDK 29 LSPlant bridge: `arm64-v8a` and `armeabi-v7a` passed.
- Production integration guard: passed.
- `git diff --check`: passed.

## Device result

`build/device-evidence/provider-lsplant-v1/20260802-110216` confirms `0.1.30-dev` loaded the native
dispatcher in both Provider processes. ExternalStorageProvider reported `3/3/3/3`; MediaProvider
reported `2044/2044/2044/2044`; both reported `provider_bridge_errno=0`. Both configured actions
remained active, no PathGuard dispatcher/JNI failure was logged, and capability bit 17 remained
clear.

Device candidate: `dist/pathguard-next-v0.1.30-dev-universal.zip` (`versionCode=31`), SHA-256
`22ef2b9544c7be842a75430d5761f15d2c9212db218106862521cf4b01cb9f02`. The same hash was
verified after transfer to `/sdcard/Download/pathguard-next-v0.1.30-dev-universal.zip`.

This is a passthrough regression, not proof of rewritten Provider results. Real
query/insert/FD/reverse mapping remains unimplemented and capability bit 17 must stay clear.
