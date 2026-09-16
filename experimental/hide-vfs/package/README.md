# PathGuard Hide 1.0 Lab package

This is an experimental KernelSU/Magisk-compatible package for the fixed
Redmi K90 Pro Max (`myron`) build:

```text
6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

It is deliberately not the production `module/` package. Installation checks
the architecture and exact `uname -r`, then leaves the kernel module unloaded.
`service.sh` never calls `insmod`, `INSTALL`, or `ENABLE`.

The package defaults to `shadow_mode=1` (`i_op` only), matching the last stable
lab package. The FUSE-aware paths remain explicit experiments: `2` for `f_op`,
`3` for dentry `d_op`, `4` for the read-only FUSE backend (lookup,
atomic_open, readdir and dentry revalidation), and `0` for the complete shadow.
Mode `4` never installs mutation wrappers. The default can be overridden only
after an explicit unload/reload. A successful load is not a hide result.

Mode `4` only bridges directory file objects opened after `enable`. Directory
FDs opened before `enable` keep the original filesystem `f_op`; HideLab must
test that case separately and classify any visible governed basename as `LEAK`.

The control status output includes diagnostic counters in the form
`operation=total/hidden` and `dentry_install=total/success/failure`. These
counters are observational only and do not change the hide policy.

After reboot, use a root shell:

```sh
/data/adb/modules/pathguard_hide1_lab/bin/hide1ctl load
/data/adb/modules/pathguard_hide1_lab/bin/hide1ctl status
```

`install` and all subsequent control operations must use the target process
mount namespace. The wrapper accepts the target PID and uses `nsenter`:

```sh
.../hide1ctl install 10549 <target-pid> <generation> \
  /storage/emulated/0/Pictures pathguard_hide1_probe
.../hide1ctl enable <target-pid> <generation>
```

The current shadow implementation has rebooted the device during two previous
`ENABLE` attempts. Treat `enable` as a crash experiment, keep recovery access,
and do not classify a successful module load as Hide 1.0 approval. Always run
`disable`, `clear`, and `unload` after an experiment when the device remains
online. The package writes boot and control logs under its `run/` directory.
