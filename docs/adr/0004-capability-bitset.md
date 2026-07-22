# ADR-0004：冻结重定向与 fanotify capability bitset

状态：Accepted

日期：2026-07-20

## 决策

capability 使用无符号 64 位 bitset。稳定 bit 位由 `core/include/pathguard/capabilities.h` 共享，不能按内核版本合并推断。

| Bit | 名称 | 含义 |
|---:|---|---|
| 0 | `openat2` | 支持所需 `openat2` syscall 与 resolve flags |
| 1 | `component_fd_walk` | 支持逐组件 `openat(O_NOFOLLOW)` fallback |
| 2 | `proc_fd_mount` | classic mount 可可靠消费 `/proc/self/fd/<n>` 固定 source 与 target；只支持其中一端时不设置 |
| 3 | `open_tree_move_mount` | `open_tree(AT_EMPTY_PATH)` 与 `move_mount(MOVE_MOUNT_*_EMPTY_PATH)` 可可靠消费固定 source/target FD |
| 4 | `string_bind_mount` | 一次性私有 namespace 中普通字符串 bind、mountinfo delta 身份验证与精确回滚完整可用；不代表用户已授权 legacy 后端 |
| 8 | `fanotify_fid` | 基础 FID report 可用 |
| 9 | `fanotify_dfid_name` | DFID + entry name report 可用 |
| 10 | `fanotify_pidfd` | PIDFD report flag 可用；单事件仍可能缺失 pidfd |
| 11 | `fanotify_rename_target` | 完整 rename target FID report 可用 |

## 约束

- `fanotify_dfid_name`、`fanotify_pidfd` 和 `fanotify_rename_target` 必须独立探测和上报。
- `uname` 版本只提供诊断信息，不直接设置 capability。
- mount capability 必须执行实际 attach、身份验证和回滚，不能只根据 syscall 存在或
  未返回 `ENOSYS` 设置。所需 flag 返回 `EINVAL`、`ENOSYS`、被 SELinux/权限策略拒绝，
  或 source/target/rollback 任一语义不成立时，对应 bit 不设置。
- probe 使用与实际 companion 相同的 UID、SELinux domain 和权限环境；结果绑定 boot
  identity、SELinux 环境与 topology generation。daemon 重启、存储重挂载或
  generation 变化后重新探测。
- 稳定 bitset 只描述 primitive。每次 probe 另外返回 backend action mask、
  mountinfo identity 支持和可选 `STATX_MNT_ID` 信息，不为这些运行时组合随意分配
  新稳定 bit。
- `proc_fd_mount` 与 `open_tree_move_mount` 均未设置时，默认标记 strict unsupported。
  只有 `string_bind_mount` 已独立探测成功、整个 `ProcessPlan` 的 action mask 被 legacy
  支持，且 policy 显式设置 `allow_legacy_string_bind` 时，才可在事务开始前选择
  ADR-0005 定义的 `legacy_namespace_bind`；不得逐规则或在 strict 运行时失败后降级。
- 运行时 probe 分别属于 R1 topology/backend resolver 和 R4 event reactor；本 ADR 只冻结协议位置。
