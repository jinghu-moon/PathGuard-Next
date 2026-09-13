# Offline LKM adapter

This directory contains the non-privileged core of the PathGuard LKM loading
experiment. It accepts an ELF64/AArch64 relocatable module, a kernel symbol
snapshot, and a required vermagic string. It produces a new in-memory image
where:

- every named `SHN_UNDEF` symbol has a non-zero kernel address and is rewritten
  to `SHN_ABS`;
- the module has exactly one empty `__versions` section;
- `.modinfo` is relocated and contains the requested vermagic;
- the original module buffer remains unchanged.

Malformed ELF structures, missing or ambiguous symbols, non-empty
`__versions`, zero addresses, and invalid vermagic values are rejected before
any output is written.

`pathguard_lkm_offline_adapter` does not call `init_module`, read `/dev/kmsg`,
change `kptr_restrict`, or communicate with an Android device. Files named
`offline-adapted-do-not-load.ko` use DDK fixture addresses and must never be
loaded on a device.

## Restricted Android loader shell

`pathguard_lkm_loader` is the device-facing boundary for the next experiment.
The checked-in build is intentionally prepare-only:

```text
pathguard_lkm_loader --prepare-only \
  --module pathguard_probe.ko \
  --vermagic "<separately recorded device vermagic>"
```

It reads `/proc/kallsyms` without changing `kptr_restrict`, adapts a private
in-memory copy, prints only a report, and discards the adapted bytes before
exit. It never writes an adapted `.ko`. Zeroed kallsyms addresses, missing or
ambiguous symbols, malformed ELF data, and absent vermagic fail closed.

`--load` is parsed so automation cannot accidentally reinterpret it as another
option, but every checked-in build rejects it before reading the module. The
binary contains no `init_module` or `finit_module` call. Enabling a real load
path is a separate device experiment that requires explicit approval and its
own code review.

`KernelLogCursor` opens `/dev/kmsg` and falls back to `/kmsg`, consumes the
existing records, and can later return only records appended after that point.
This prevents a retry from accepting an unrelated historical vermagic error.
The prepare-only command does not use historical kmsg as evidence; it requires
an explicit vermagic value recorded for the target device.

The GitHub workflow builds the shell for Android arm64/API 26 with static
libc++, runs both loader test executables on the host, audits the ELF and shared
library dependencies, and rejects binaries that import module-loading entry
points or contain a `kptr_restrict` path.
