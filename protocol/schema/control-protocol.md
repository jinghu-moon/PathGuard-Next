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
