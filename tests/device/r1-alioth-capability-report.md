# R1 Alioth capability report

> 历史基线（2026-07-20，已过期）：本报告记录重构前 probe。2026-07-23 的性能记录
> 曾观察到 `proc_fd`，但尚未用 2026-07-24 的完整 root/filesystem/device/parent identity
> 验证路径重新采集。发布判断以 `r1-safety-validation.md` 新矩阵为准，不再引用本文件的
> bitset 作为当前 capability。

日期：2026-07-20

设备：Xiaomi alioth，Android 13，Linux 4.19.157，SELinux Enforcing，Magisk 30.6。

## Directory resolver

- `/storage/emulated/0/DCIM` 自动路径成功，capability 为 `component_fd_walk`。
- 强制逐组件 FD walk 成功。
- `/proc/self/fd` 在自动与强制 fallback 两条路径均以 `ENOTDIR` 拒绝，未跟随 magic-link。
- `openat2` 不可用，自动路径安全降级为逐组件 FD walk。

## FD-native mount

所有 mount 测试均在临时进程通过 `unshare(CLONE_NEWNS)` 建立的私有 mount namespace 中执行，测试后立即卸载。

| Source | Target | 结果 |
|---|---|---|
| 原始字符串 | 原始字符串 | 成功，作为控制组 |
| `/proc/self/fd/<n>` | 原始字符串 | `EINVAL` |
| 原始字符串 | `/proc/self/fd/<n>` | `EINVAL` |
| `/proc/self/fd/<n>` | `/proc/self/fd/<n>` | `EINVAL` |
| `open_tree` + `move_mount` empty-path FD | empty-path FD | `ENOSYS` |

最终 capability bitset 为 `2`，仅包含 `component_fd_walk`。本设备不得启用 redirect executor；必须保持编译门控并 fail-open，不能退化为字符串 mount。
