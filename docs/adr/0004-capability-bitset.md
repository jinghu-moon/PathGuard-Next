# ADR-0004：冻结重定向与 fanotify capability bitset

状态：Accepted

日期：2026-07-20

## 决策

capability 使用无符号 64 位 bitset。稳定 bit 位由 `core/include/pathguard/capabilities.h` 共享，不能按内核版本合并推断。

| Bit | 名称 | 含义 |
|---:|---|---|
| 0 | `openat2` | 支持所需 `openat2` syscall 与 resolve flags |
| 1 | `component_fd_walk` | 支持逐组件 `openat(O_NOFOLLOW)` fallback |
| 2 | `proc_fd_mount` | classic mount 可可靠消费 `/proc/self/fd/<n>` 固定对象 |
| 3 | `open_tree_move_mount` | `open_tree(AT_EMPTY_PATH)` 与 `move_mount(MOVE_MOUNT_*_EMPTY_PATH)` 可可靠消费固定 source/target FD |
| 8 | `fanotify_fid` | 基础 FID report 可用 |
| 9 | `fanotify_dfid_name` | DFID + entry name report 可用 |
| 10 | `fanotify_pidfd` | PIDFD report flag 可用；单事件仍可能缺失 pidfd |
| 11 | `fanotify_rename_target` | 完整 rename target FID report 可用 |

## 约束

- `fanotify_dfid_name`、`fanotify_pidfd` 和 `fanotify_rename_target` 必须独立探测和上报。
- `uname` 版本只提供诊断信息，不直接设置 capability。
- syscall 存在但所需 flag 返回 `EINVAL`、`ENOSYS` 或被策略拒绝时，对应 bit 不设置。
- `proc_fd_mount` 与 `open_tree_move_mount` 均未设置时，同步 redirect 必须标记为 unsupported 并 fail-open；禁止退化为“固定 FD 预检后使用原字符串 mount”。
- 运行时 probe 分别属于 R1 topology/backend resolver 和 R4 event reactor；本 ADR 只冻结协议位置。
