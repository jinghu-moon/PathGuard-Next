# Strict Deny Anchor Verification

> Date: 2026-07-27
>
> Device: Xiaomi Mi 10 / Alioth, Android 13, Linux 4.19
>
> Result: strict proc-fd backend passed

## Delivered path

PathGuard Rules TOML `deny` now flows through the existing single pipeline:

```text
deny string array
  -> normalized/conflict-checked Canonical Policy
  -> policy format v5 action 0 with empty backing path
  -> Zygisk ProcessPlan
  -> root-owned empty deny anchor
  -> FD-pinned bind mount
  -> mountinfo identity verification
  -> shared mutation journal and exact rollback
```

The binary layout and format version are unchanged. Redirect remains action 1;
deny uses the action 0 value already frozen by schema 2/format v5.

## TDD and device evidence

- compiler: deny normalization, redundant-child removal, conflict validation,
  Canonical Policy retention, action-mask admission and round-trip decoding;
- tools: lint, deterministic plan add/remove and `explain --path` deny match;
- Host/Android parity: mixed two-deny plus one-redirect policy bytes,
  diagnostics and requirements are identical;
- Android control plane: daemon publishes a mixed policy and the mmap reader
  observes two action-0 entries with empty backing paths plus one redirect;
- Alioth deny anchor: strict proc-fd backend (`backend=2`) rejects unprivileged
  read, directory listing and create, then verifies exact mount-ID rollback and
  reveals the original target content again.

## Security boundary

The companion accepts the anchor only when it is an empty directory owned by
root, mode `0000`, reachable through the pinned module directory, and the path
and FD resolve to the same inode. The target is resolved just in time and both
source and target are pinned before mutation. Legacy string-bind deny remains
unsupported. This feature protects direct filesystem access; MediaStore,
Photo Picker, CloudMediaProvider and general SAF deny filtering remain separate
compatibility work.
