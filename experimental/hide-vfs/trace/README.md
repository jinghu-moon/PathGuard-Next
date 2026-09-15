# PathGuard VFS trace probe

这是进入真实 VFS prototype 前的只观测探针。它仅在以下入口注册 kprobe：

```text
lookup_one_len
vfs_create
vfs_mkdir
vfs_unlink
vfs_rmdir
vfs_rename
```

每个处理器只递增一个 `atomic64_t` 计数器并返回 `0`。它不读取或修改
`pt_regs`，不修改返回值、dentry、inode 或 file operations，也不安装隐藏规则。
模块退出时逆序注销已注册的 probe；任一入口注册失败，整个模块加载失败并清理已注册项。

模块通过 `/dev/pathguard_vfs_trace_probe` 的只读 ioctl 导出计数、`nmissed`、注册状态和
内核 release。这个探针只能证明当前设备是否允许这些入口被 kprobe 观测，不能证明能够
安全改变 VFS 语义，更不能证明 Hide 已实现或通过 HideLab。

使用与 capability probe 相同的 android16-6.12 DDK 构建：

```sh
make -C experimental/hide-vfs/trace \
  KDIR=/path/to/android16-6.12 \
  KSRC=/path/to/android16-6.12-source
```

设备加载、卸载和回归必须在静态审查、本地 ELF/KMI 检查通过后单独授权；不得把未经验证
的 VFS prototype 或离线适配模块混入本实验。
