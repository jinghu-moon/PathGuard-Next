# PathGuard VFS call-path coverage probe

该探针只观测 namei/FUSE 调用链，不修改任何 VFS 行为。它尝试独立注册以下入口：

```text
lookup_fast / lookup_slow / path_openat / open_last_lookups
fuse_lookup / fuse_atomic_open / filename_lookup / link_path_walk
iterate_dir / fuse_readdir / fuse_filldir / fuse_dentry_revalidate
do_filp_open / vfs_getattr / vfs_statx
```

每个 handler 只递增 `atomic64_t`，模块通过 `/dev/pathguard_vfs_coverage_probe` 的只读
ioctl 返回命中数、`nmissed`、注册状态、注册错误、必需标记和内核 release。必需入口是
`path_openat`、`iterate_dir`、`do_filp_open`；可选入口未导出时只标记为 unavailable，模块
仍然加载并报告 `partial` 或 `unsupported`。只有必需入口全部注册且受控操作产生预期命中，
才可进入下一阶段；加载成功本身不是覆盖通过证据。

```sh
make -C experimental/hide-vfs/coverage \
  KDIR=/path/to/android16-6.12 \
  KSRC=/path/to/android16-6.12-source
```
