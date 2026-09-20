# PathGuard KPM capability probe

这是固定设备实验路线的只读 KPM 探针。它复用 `refer/hide-refer/SukiSU_KernelPatch_patch`
的 KPM ABI，通过 `kallsyms_lookup_name()` 查询 namei/VFS/FUSE 候选符号是否存在。

探针明确不做以下事情：

- 不安装 inline hook、syscall hook、kprobe 或 tracepoint；
- 不调用任何解析出的内核函数；
- 不修改 inode、dentry、file operation 或返回值；
- 不创建、删除、重命名或访问测试文件。

因此 `found > 0` 或 KPM 加载成功只能证明 KernelPatch 的加载和符号解析入口可用，
不能证明 PathGuard 具备 hide 能力，也不能把产品状态改为 `active`。

## 构建

KPM 需要 Linux/Windows NDK 的 `aarch64-linux-android31-clang`。Windows 原生构建示例：

```powershell
make -C "experimental/hide-kpm/capability" `
  NDK_PATH="C:/Android/Sdk/ndk/28.0.13004108" `
  KPM_ROOT="D:/100_Projects/110_Daily/PathGuard-Next/refer/hide-refer/SukiSU_KernelPatch_patch"
```

输出为 `pathguard_kpm_capability_probe.kpm`。当前仓库中的 DDK clang 不是 NDK，不能
因为存在 `clang` 就假定它能提供 Android target sysroot。

## 设备验收（需要用户明确授权）

设备必须先通过 `collect_kernel_backend_capability.ps1` 的
`eligible_for_kpm_probe` 门禁。之后才允许人工执行：

```text
ksud kpm load /data/local/tmp/pathguard_kpm_capability_probe.kpm
ksud kpm ctl0 pathguard-kpm-cap-probe status
ksud kpm unload pathguard-kpm-cap-probe
```

验收只记录加载、`status` 输出和卸载后的内核日志/boot ID。任何异常都立即停止，
并保持 `Hide 1.0 = unsupported`。namei 行为探针和隐藏逻辑属于后续阶段，不能由本
探针替代。
