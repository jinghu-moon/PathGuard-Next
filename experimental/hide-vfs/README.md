# PathGuard Hide 1.0 fixed-device prototype

This directory contains the native decision model and kernel prototype for one
admitted device build:

```text
device: myron
kernel release: 6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
scope: one target fsuid + one mount namespace + one real parent/basename
```

`pathguard_hide1.c` now contains the first data-plane prototype. `INSTALL`
validates the target process fsuid, pins its mount namespace through an
`nsproxy` reference, resolves and pins the parent path, and requires the full
12-operation mask. A successful install enters `INACTIVE`. `ENABLE` publishes
transactional inode/file operation shadows and a per-dentry `d_revalidate`
shadow; `DISABLE` restores only pointers still owned by this module and drains
the SRCU/RCU grace periods before releasing metadata.  Existing open
directory files keep the module-owned fops alive; `CLEAR` is rejected while
such files remain open, and a new `INSTALL` is rejected until they close.

The shadow wrappers cover lookup, atomic_open, iterate_shared, all nine parent
mutation callbacks, and d_revalidate. Target observers receive synthetic
negative lookup results, filtered directory entries, and `ENOENT` for governed
mutations. Other fsuid or mount-namespace observers call the saved callbacks.
The implementation is still a fixed-device prototype: it has one binding,
one basename, and no production admission.

The install caller must already be in the target mount namespace. This is an
intentional fail-closed constraint: `kern_path()` resolves in the caller's
namespace, so a cross-namespace call returns `EXDEV` instead of binding a
different parent with the same pathname. The binding explicitly holds both
the path and inode references until `CLEAR` or module unload.

The module is built against the same `android16-6.12` DDK family used by
SukiSU Ultra:

```sh
CC=clang make -C experimental/hide-vfs KDIR=/opt/ddk/android16-6.12
```

On the admitted device, SukiSU's loader resolves undefined symbols against the
live kernel and adapts vermagic before `init_module`. An exact myron
`Module.symvers` is therefore not a loading prerequisite for this route. The
device release allowlist, runtime symbol checks, CFI constraints, and device
regression requirements remain mandatory.

`hide1_control` supports `status`, `install`, `enable`, `disable`, and `clear`.
`DISABLE` preserves an inactive binding; `CLEAR` releases it. Failed replacement
installs are transactional and preserve the previous binding.

This prototype is not part of the production module. It must pass the complete
HideLab matrix, including warm positive dentries, concurrent access, lifecycle
teardown, and mutation/rename cases, before Hide 1.0 can leave `unsupported`.

## Experimental package

`package/` contains a separate KernelSU/Magisk-compatible lab package. The
package checks the fixed `myron` release at install time, does not load the
module during boot, and never runs `INSTALL` or `ENABLE` automatically. Build
it from the repository root with:

```powershell
./scripts/package-hide1-lab.ps1
```

The resulting ZIP is written under `dist/` (which is ignored by Git). Use the
package's `bin/hide1ctl` for manual, logged operations only. A successful
`load` or `INACTIVE` binding is not evidence that the VFS shadow is stable.
