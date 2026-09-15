# PathGuard VFS call-path coverage probe

该探针只观测 namei/FUSE 调用链，不修改任何 VFS 行为。它尝试注册：

```text
path_openat
fuse_atomic_open
iterate_dir
fuse_readdir
fuse_dentry_revalidate
do_filp_open
```

每个 handler 只递增 `atomic64_t`，模块通过 `/dev/pathguard_vfs_coverage_probe` 的只读
ioctl 返回命中数、`nmissed`、注册状态和内核 release。任一入口无法注册，整个模块加载失败
并清理已注册项。该模块用于确认设备上的真实调用路径，不能作为 Hide 后端或 HideLab 通过证据。

```sh
make -C experimental/hide-vfs/coverage \
  KDIR=/path/to/android16-6.12 \
  KSRC=/path/to/android16-6.12-source
```
