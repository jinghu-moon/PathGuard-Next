# T-38～R-38 Provider callback safety boundary

## 范围

- Change scope: LSPlant Java Hooker callback dispatcher boundary
- Production mapping: dispatcher is unset by default; no URI/document ID/FD rewrite is enabled
- Production bit 17: remains `unsupported`

## Implementation

- `ProviderHooker` captures the target return type before LSPlant installation.
- A version-neutral `Dispatcher`/`DispatchResult` seam is available for the future provenance
  adapter; it is not connected to daemon/store or Android private objects yet.
- Dispatcher `null`, `pass`, exception, incompatible return type, or malformed result all call the
  original LSPlant backup method.
- A rewrite result is accepted only when its Java value is compatible with the frozen target return
  type, including primitive wrappers.
- Existing receiver validation and backup invocation remain unchanged, so the 0.1.29 passthrough
  behavior is preserved.

## Verification

- Java 11 compilation passed.
- JDK Host dispatcher behavior test passed: default/pass, compatible rewrite,
  incompatible return, dispatcher exception and primitive wrapper checks.
- Production integration guard passed, including dispatcher and return-type contracts.
- NDK 29 LSPlant bridge build passed for `arm64-v8a` and `armeabi-v7a`.
- MSVC Release CTest passed `82/82`.
- No runtime status or capability code was changed; bit 17 remains clear.
- Development ZIP rebuilt with the dispatcher seam: `dist/pathguard-next-v0.1.29-dev-universal.zip`.
  SHA-256: `d95e4c9d24d497cf67097da55cd6e6fc052bf05bce64b5e7494715f2c3554b05`.
  The same ZIP was copied to `/sdcard/Download/pathguard-next-v0.1.29-dev-universal-dispatcher.zip`;
  device `sha256sum` matched. It was not installed automatically because Magisk installation is
  an interactive device-state change.

## Remaining boundary

The dispatcher has no production implementation that constructs `ProviderRouteBindingV1` from a
real Provider query/insert/open/rename/delete call. Therefore this is a safety seam, not virtual
mapping completion. V-65 remains pending and the current device still reports real mapping as
`unsupported/not_observed`.

## Device regression

- Collector evidence: `build/device-evidence/provider-lsplant-v1/20260802-103041`.
- Module identity: `0.1.29-dev` / versionCode `30`.
- ExternalStorageProvider: `resolved=3 installed=3 backup=3 self_tested=3 errno=0`.
- MediaProvider: `resolved=2044 installed=2044 backup=2044 self_tested=2044 errno=0`.
- Both Provider records: `action_total=2`, both admissions `active`,
  `observed_capabilities=65536`; bit 17 remains clear.
- Enhanced collector exited successfully; no Provider fatal, null receiver, or invalid JNI
  reference diagnostic was observed in this round.
- This is a passthrough regression result only. It does not activate dispatcher rewrite or bit 17.
