# PathGuard Hide 1.0 Lab package

This is an experimental KernelSU/Magisk-compatible package for the fixed
Redmi K90 Pro Max (`myron`) build:

```text
6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

It is deliberately not the production `module/` package. Installation checks
the architecture and exact `uname -r`, then leaves the kernel module unloaded.
`service.sh` never calls `insmod`, `INSTALL`, or `ENABLE`.

The package defaults to `shadow_mode=1` (`i_op` only). The other isolation
modes are explicit load arguments: `2` for `f_op`, `3` for dentry `d_op`, and
`0` for the complete shadow. The default can be overridden only after an
explicit unload/reload.

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
