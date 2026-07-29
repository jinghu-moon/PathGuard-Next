# Hide H0 backend report

## Identity

| Field | Value |
|---|---|
| Date | |
| Backend and version | |
| Device / model | |
| ROM fingerprint | |
| Android / API | |
| Kernel | |
| Root solution | |
| SELinux | |
| MediaProvider APEX | |
| Execution UID / domain | |
| Mount namespace | |

## Contract

- [ ] `hide.direct_vfs`: lookup and directory enumeration agree.
- [ ] Mutation failures leave inode, mtime, size, and content unchanged.
- [ ] Relative `dirfd`, storage aliases, symlinks, and raw syscalls cannot bypass.
- [ ] Non-target apps remain unaffected.
- [ ] `hide.media_query` passes independently.
- [ ] `hide.mount_stealth` adds no mount table entry.
- [ ] Unsupported surfaces and pre-existing/provider-opened FDs are reported.

## Evidence

| Test | Expected | Actual | errno | Side effect | Evidence file |
|---|---|---|---:|---:|---|
| | | | | | |

## Performance

Record absolute latency, P50/P95/P99, at least 30 runs, CPU governor, file
system, and confidence interval for no-policy, other-namespace, non-match, and
match states.

## Decision

Choose exactly one:

- [ ] Pass: backend satisfies the full minimum capability set.
- [ ] Conditional: research continues; capability is not publishable.
- [ ] Kill: a documented kill criterion was reached.
- [ ] Unsupported: no candidate satisfies the frozen semantics.

Partial results must not be published as `hide` and must not fall back to
`deny`.
