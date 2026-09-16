# PathGuard VFS operation-table preflight

该探针只解析一个绝对父目录并报告其 inode/file/dentry operation 是否存在，不写入
`i_op`、`i_fop` 或 `d_op`，不安装规则，也不改变行为。ABI v2 额外报告本次启动内的
mount、superblock、inode、dentry 和 operation-table 地址身份；这些地址只用于 root 侧
诊断，不能作为跨启动的稳定 ABI。它用于在真实 prototype 安装前确认目标目录的
operation table 完整性和 storage topology。

```text
lookup atomic_open readdir create mkdir mknod symlink unlink rmdir link rename revalidate
```

`status_reader` 对 `/dev/pathguard_vfs_preflight` 执行一次 scan 和 status ioctl。设备加载、
路径扫描和卸载需要单独授权；该探针不能替代 HideLab，也不能证明 Hide 已实现。
