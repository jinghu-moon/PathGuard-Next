# V-67 Provider delete target feasibility boundary

## Finding

The pinned MediaProvider delete method is:

```text
delete(Uri, Bundle) -> int
```

Neither the public callback arguments nor the current Provider contract probe exposes a trusted
file/directory target kind. The current device/ABI therefore cannot satisfy the target fact required
to choose `kOperationUnlink` versus `kOperationRmdir`.

## Decision

- Mark delete target classification `not_observed/unsupported` for this device and pinned profile.
- Keep the dispatch request incomplete for `kDeleteTarget`.
- Do not infer type from URI last segment, MIME, display name, or delete return count.
- Do not add a Java/hidden-ABI dependency solely to manufacture the missing fact.

## Evidence

- `core/include/pathguard/provider_lsplant_bridge.h` keeps `kDeleteTarget` dynamic and unresolved.
- `build/device-evidence/provider-lsplant-v1/20260802-120913` shows no regression in the installed
  `0.1.34-dev` bridge; both Provider groups remain active with `provider_bridge_errno=0` and bit 17
  clear.
- The public contract probe verifies delete behavior but cannot establish target kind.

This boundary is a deliberate fail-open result. A future Provider profile may close it only when a
version-pinned, independently verified target-type source is available.
