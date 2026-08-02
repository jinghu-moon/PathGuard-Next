# T-40～R-40 Provider Java dispatch specification

## Scope

- Freeze the dispatch role, minimum operation mask, dynamic-argument requirement and Java result
  kind for all 11 version-pinned Provider methods.
- Connect native `nativeDispatch` to the frozen method-ID table.
- Keep the dispatcher pass-through; no Android object extraction or provenance I/O is enabled.

## Contract

- ExternalStorageProvider methods are classified as forward document path and reverse document ID.
- MediaProvider methods are classified as query, insert, open, update and delete.
- MediaDocumentsProvider methods are classified as query, open and delete.
- Open mode, update ContentValues and delete target type remain dynamic argument decisions.
- Result kinds are frozen as File, document ID, Cursor, Uri, ParcelFileDescriptor, count or void.
- Unknown method IDs and null argument arrays are pass-through.

## Verification

- `ValidateProviderJavaDispatchSpecsV1()` is a compile-time complete-table validator.
- `pathguard_provider_lsplant_bridge_test` passed the role/mask/result/dynamic matrix.
- NDK 29 LSPlant bridge built for `arm64-v8a` and `armeabi-v7a`.
- Production integration guard passed.
- Capability bit 17 and runtime status behavior were not changed.

## Remaining boundary

The next step is operation-specific extraction of URI/document ID/mode/ContentValues into a bounded
native request. Until that extractor can produce a validated `ProviderRouteBindingV1`, dispatch
continues to return pass-through and V-65 remains pending.
