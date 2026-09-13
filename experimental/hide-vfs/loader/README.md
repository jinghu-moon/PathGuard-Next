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
