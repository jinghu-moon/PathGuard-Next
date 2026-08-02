# T-42～R-42 Provider URI/document ID bounded extractor

## Scope

- Freeze the primary identifier kind and callback index for all pinned Provider methods.
- Extract content URIs and document IDs into a fixed-capacity UTF-8 value.
- Keep File reverse extraction, route binding, provenance I/O and Java result rewriting disabled.

## Method matrix

| Methods | Identifier | Callback index |
| --- | --- | --- |
| MediaProvider query/insert/openFile/update/delete | `android.net.Uri` | 1 |
| ExternalStorageProvider getFileForDocId | document ID String | 1 |
| MediaDocumentsProvider query/queryChildren/open/delete | document ID String | 1 |
| ExternalStorageProvider getDocIdForFile | pending File path extractor | none |

The callback array includes the instance receiver at index 0. Every declared identifier index is
therefore checked against the full callback argument count rather than the Java parameter count.

## Bounded text contract

- Output storage is a fixed 1024-byte array with at most 1023 bytes of payload.
- UTF-16 is converted to standard UTF-8, including valid surrogate pairs.
- Isolated surrogates, NUL, ASCII control characters and capacity overflow fail open.
- URI values must be `android.net.Uri` instances whose rendered value starts with `content://`.
- JNI lookup, type, invocation and string-region failures are cleared and fail open.

## Verification

- `pathguard_provider_lsplant_bridge_test`: passed identifier matrix and UTF vectors.
- MSVC Release CTest: `82/82` passed.
- NDK 29 LSPlant bridge: `arm64-v8a` and `armeabi-v7a` passed.
- Production integration guard: passed.
- `git diff --check`: passed.

## Remaining boundary

The extractor result is deliberately not connected to `ProviderRouteBindingV1` or
`ProviderMappingDecisionV1`. The next implementation step is the bounded File path extractor for
reverse mapping, followed by an immutable dispatch request seam. Capability bit 17 remains clear.

## Device result

`build/device-evidence/provider-lsplant-v1/20260802-113334` confirms `0.1.31-dev` loaded both
Provider method groups with complete backup/self-test masks and `provider_bridge_errno=0`. Both
actions remained active and `observed_capabilities=65536`; the collector produced no `logcat.txt`,
so this is a passthrough/no-regression result rather than proof that a real rewrite callback ran.

Device candidate: `dist/pathguard-next-v0.1.31-dev-universal.zip` (`versionCode=32`), SHA-256
`07f30bccd85f981fb42c905183ac76dc34388cd9a4e5888c4ae951b33d4fab4f`. The same hash was
verified at `/sdcard/Download/pathguard-next-v0.1.31-dev-universal.zip`.
