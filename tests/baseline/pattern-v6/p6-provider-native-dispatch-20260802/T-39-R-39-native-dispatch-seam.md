# T-39～R-39 Provider native dispatcher seam

## Scope

- LSPlant Hooker DEX now registers a versioned native `nativeDispatch` entry.
- `installNativeDispatcher()` enables the Java callback to call that entry before invoking the
  original backup.
- Native implementation deliberately returns pass-through (`null`) until a validated
  `ProviderRouteBindingV1` and provenance response are available.

## Safety behavior

- Native dispatch performs no blocking I/O and does not access the daemon/store.
- Java always converts the current native result to `DispatchResult.pass()`.
- Existing dispatcher exception/type checks remain active.
- URI/document ID/Cursor/ParcelFileDescriptor/rename/delete/reverse objects are not modified.
- Capability bit 17 remains clear.

## Verification

- JDK Host dispatcher test passed.
- Production integration guard passed for Java/native registration and signatures.
- NDK 29 LSPlant bridge build passed for `arm64-v8a` and `armeabi-v7a`.
- MSVC Release CTest passed `82/82`.

## Device boundary

The native dispatcher seam has not yet been installed on the device. The next candidate ZIP must
be installed interactively before collecting a new Provider fault-gate result. Even after install,
this seam is still passthrough and cannot close V-65 or enable bit 17.

## Device regression

- Collector evidence: `build/device-evidence/provider-lsplant-v1/20260802-104458`.
- Logcat explicitly recorded `Provider native dispatcher installed as pass-through`.
- ExternalStorageProvider: `resolved=3 installed=3 backup=3 self_tested=3 errno=0`.
- MediaProvider: `resolved=2044 installed=2044 backup=2044 self_tested=2044 errno=0`.
- Both Provider records retained `action_total=2`, active admissions and
  `observed_capabilities=65536`; bit 17 remained clear.
- Enhanced collector passed without Provider fatal, null receiver or JNI local-reference fault.
- The observed dispatcher result is pass-through only; no real mapping claim is made.
