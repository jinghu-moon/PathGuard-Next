# T-43～R-43 Provider File reverse bounded extractor

## Scope

- Freeze `ExternalStorageProvider.getDocIdForFile(File)` as a File-path input at callback index 1.
- Extract an absolute UTF-8 path into a fixed 4096-byte value.
- Keep reverse lookup, document-ID generation, route binding and provenance disabled.

## Contract

- The value must be an instance of `java.io.File`.
- The native bridge calls only the pinned `File.getPath()` method.
- The encoded path is limited to 4095 bytes, has no NUL/control characters, and must start with `/`.
- Null, wrong type, relative/empty path, malformed UTF-16, overflow or JNI exception fails open.

## Verification

- Host matrix and UTF-8/path-boundary tests: pending implementation.
- MSVC Release CTest: pending implementation.
- NDK 29 LSPlant bridge and production guard: pending implementation.
- Capability bit 17 remains clear by contract.

## Remaining boundary

The path is only a bounded fact for a later reverse provenance lookup. No directory scan, document ID
rewrite or ProviderMappingDecision is enabled by T-43～R-43.

## Device result

`build/device-evidence/provider-lsplant-v1/20260802-114618` confirms `0.1.32-dev` installed the
native dispatcher and complete Provider hook groups. Both Providers reported
`provider_bridge_errno=0`; configured actions stayed active, capability bit 17 stayed clear, and the
captured log contained no PathGuard JNI/LSPlant failure. This remains a passthrough regression rather
than proof of a successful reverse rewrite.

Device candidate: `dist/pathguard-next-v0.1.32-dev-universal.zip` (`versionCode=33`), SHA-256
`80089364c1d056cae8b59e4dce2a6239bc864acd1b75f98d8535196958a42155`. The same hash was verified
at `/sdcard/Download/pathguard-next-v0.1.32-dev-universal.zip`.
