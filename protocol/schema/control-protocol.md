# Control Protocol

This directory reserves the versioned wire contract shared by `pathguardd`,
`pathguardctl`, and the Manager App.

The first implementation uses length-prefixed local messages. Payloads must be
bounded, versioned, and independent from the user-facing `rules.toml` syntax.

## Runtime status v1

R1 companion publishes an atomic per-PID status record under
`run/status/<pid>.status`. It is a transitional daemon-readable record; target
process memory is never the source of truth. Required fields are:

```text
version, pid, uid, process_start_time, process
enforcement, backend, transaction, security, reason, error
snapshot_generation, plan_generation, topology_generation
```

`pathguardctl status <module-dir> [pid]` reads these records and
`pathguardctl explain <policy.bin> <package>` explains snapshot semantics. A future
UDS transport may wrap the same fields without changing their meaning.

## Manager rules save v1

The daemon control layer owns Manager validation and save semantics. A save
request contains exactly:

```text
expected_source_digest
replacement_rules_toml
```

The response keeps source and deployment identities distinct:

```text
saved, error_code, message
source_digest, candidate_sequence, active_content_generation
deployment_epoch, capability_generation, topology_generation, status
```

The daemon performs the operation in this order:

1. securely load `rules.toml` and compare `expected_source_digest`;
2. run the shared rules compiler and device admission on the replacement;
3. load the source again and repeat the digest comparison;
4. durably and atomically replace `rules.toml`;
5. invoke the existing reconciler, which remains the only `policy.bin` writer.

A stale digest returns `PG-SOURCE-CONFLICT`. Invalid syntax/semantics returns
the shared compiler diagnostic code. Unsupported device requirements return
`PG-ADMISSION-UNSUPPORTED`. Rejected requests do not replace the source or
change the active content generation/deployment epoch. The Manager must not
parse rules independently and must not write `policy.bin`.
