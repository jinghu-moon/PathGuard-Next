# V-42 Event/Export 设备改造前基线

- Change ID: `p6-event-before-20260731`
- Task: `V-42`
- Observed commit: `1fcb35b`
- Device: Xiaomi M2012K11AC, Android 13 / API 33
- Kernel: `4.19.157-perf-g9607d8651312`, arm64
- Root framework: Magisk `30.6:MAGISK:R` (`30600`)
- SELinux: Enforcing
- Module: `0.1.17-dev`
- Rules SHA-256: `dea93c4b7a3fff4853f552f76ec581ebc3aa674bb70ffdd0452b00d6fe869419`
- Policy SHA-256: `e2b2e771b4daccedceb49b2e6064f9c833b44ae942a69832352cc3baf6aef9af`
- Zygisk arm64 SHA-256: `b6eb7c903ece8c127a74dce6b85840ce0e1b9791c315a6e453927d6bdd05ea0e`

## 步骤与实际结果

1. 只读检查设备 `/proc/config.gz`：

```text
# CONFIG_FANOTIFY is not set
```

2. 检查 fanotify sysctl 和 trace event：

```text
/proc/sys/fs/fanotify: No such file or directory
/sys/kernel/debug/tracing/events/fanotify: No such file or directory
```

3. 检查活动规则和 daemon 状态。规则只有 deny/redirect，没有 `observe_rules` 或
   `export_rules`；`rules-status.txt` 报告 policy active、`capability_generation=1`，但当前状态
   schema 不包含 per-domain bit 8～11。这是 T-28/R-28 尚未完成的可观测性缺口，不能从
   `status=active` 推导 event domain active。

4. 检查三个 LocalSend 进程 `.status`。它们均报告 backend 2 的核心 redirect transaction
   complete；文件中没有 event adapter 或 fanotify active 字段。

## Capability 判定

| Capability | 本设备结论 | 依据 |
| --- | --- | --- |
| bit 8 `fanotify_fid` | unsupported | kernel 未编译 `CONFIG_FANOTIFY` |
| bit 9 `fanotify_dfid_name` | unsupported | 基础 fanotify 不存在 |
| bit 10 `fanotify_pidfd` | unsupported | 基础 fanotify 不存在 |
| bit 11 `fanotify_rename_target` | unsupported | 基础 fanotify 不存在 |

由于 `fanotify_init` 能力在内核中不存在，本设备无法构造 event queue；close-write/rename、
overflow、跨文件系统 transfer 和 daemon restart recovery 均为 `not_observed`，不是通过。

## 结论

| 场景 | 分类 | 结论 |
| --- | --- | --- |
| kernel fanotify source | unchanged | unsupported |
| bit 8～11 admission | unchanged | 必须保持 unsupported，不得 fallback 到同步 redirect 域 |
| event queue/overflow/跨 FS/restart | not_observed | source 不可构造，需另一台支持 fanotify 的设备 |
| C2/C3 核心 redirect | unchanged | 当前 active 状态只属于核心 backend，不代表 event active |

V-42 的本设备 before 基线已完成。T-27～R-27 和 V-43 仍未完成；后续实现必须在支持
fanotify 的设备上独立探测 bit 8～11，不能按 Android/OEM 名称推断。
