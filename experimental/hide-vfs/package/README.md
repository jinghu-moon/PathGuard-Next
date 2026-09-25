# PathGuard Hide 1.0 Lab package

This is an experimental KernelSU/Magisk-compatible package for the fixed
Redmi K90 Pro Max (`myron`) build:

```text
6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

It is deliberately not the production `module/` package. Installation checks
the architecture and exact `uname -r`. At boot, `service.sh` compares device,
architecture, fingerprint, kernel release and the packaged module SHA-256
against `config/hide1_device_profile.json`. On an exact match it uses the
SukiSU `ksud insmod` loader with `shadow_mode=0`, then starts `pathguardd`.
Any mismatch or load failure leaves Hide inactive and is recorded under
`run/service.log`.

The daemon independently checks the profile against the current boot ID,
kernel release, module hash, boot-state record and live LKM device before it
can reconcile Hide rules. It then watches `config/rules.toml`; a valid matching
rule is installed and enabled automatically, while removal or invalid input
causes no partial update: deleting the Hide rule revokes it, while an invalid
rules file leaves the last known-good snapshot active and reports the parse
error. The fixed-device profile is
an experimental device allowlist, not proof of HideLab regression or a
production admission. `product_state` remains `unsupported`.

The boot service loads `shadow_mode=0` to match the full regression profile.
Manual `hide1ctl load` defaults to `shadow_mode=1` (`i_op` only). The
FUSE-aware paths remain explicit experiments: `2` for `f_op`,
`3` for dentry `d_op`, `4` for the read-only FUSE backend (lookup,
atomic_open, readdir and dentry revalidation), and `0` for the complete shadow.
Mode `4` never installs mutation wrappers. The default can be overridden only
after an explicit unload/reload. A successful load is not a hide result.

Mode `4` only bridges directory file objects opened after `enable`. Directory
FDs opened before `enable` keep the original filesystem `f_op`; activation now
fails with `EBUSY` if the target already owns a governed parent/hidden-directory
FD. HideLab must close those descriptors and retry rather than classify an
unsafe activation as a pass.

The control status output includes diagnostic counters in the form
`operation=total/hidden` and `dentry_install=total/success/failure`. These
counters are observational only and do not change the hide policy.
ABI v7 prints `symlink_probe=registered/calls/target/fd/hidden_fd`,
`vfs_symlink_probe=registered/calls/target/valid/hidden_parent/child_parent/negative_child/shadow_iop`,
and return classifications for `may_create` and `security_inode_symlink`.
The inode-security return bridge changes `-EACCES` to `-ENOENT` only for an
active rule whose target identity, namespace, generation, hidden parent and
negative child all match. `inode_security_bridge_enoent` reports actual
rewrites. This narrow bridge is still experimental and is not a Hide 1.0 pass.

The lab `hide1ctl load` explicitly enables `diagnostic_probes=1` and
`symlink_errno_bridge=1`. The core module does not require these kernel-specific
probes; production integration must keep them disabled and treat the strict
symlink bridge as an unproven capability until a native namei backend exists.

After reboot, inspect the automatic boot path from a root shell:

```sh
/data/adb/modules/pathguard_hide1_lab/bin/hide1ctl status
cat /data/adb/modules/pathguard_hide1_lab/run/boot-state
cat /data/adb/modules/pathguard_hide1_lab/run/service.log
cat /data/adb/modules/pathguard_hide1_lab/run/daemon.log
```

`boot-state` is regenerated each boot and is not itself admission. A prior
boot's HideLab evidence is never reused. The profile and live-device checks
only enable this fixed-device lab path; they do not alter the product state
from `unsupported` or replace the required full active regression.

`install` and all subsequent control operations must use the target process
mount namespace. The wrapper accepts the target PID and uses `nsenter`:

```sh
.../hide1ctl install 10549 <target-pid> <generation> \
  /storage/emulated/0/Pictures pathguard_hide1_probe
.../hide1ctl enable <target-pid> <generation>
```

Mode 0 additionally installs an independent i_op shadow on the governed
hidden directory inode. This is intended to close mutations performed through
an already-open hidden-directory FD; it does not yet prove strict symlink
errno semantics when FUSE/namei rejects the request before `i_op->symlink`.

The current shadow implementation has rebooted the device during two previous
`ENABLE` attempts. Treat `enable` as a crash experiment, keep recovery access,
and do not classify a successful module load as Hide 1.0 approval. Always run
`disable`, `clear`, and `unload` after an experiment when the device remains
online. The package writes boot and control logs under its `run/` directory.
