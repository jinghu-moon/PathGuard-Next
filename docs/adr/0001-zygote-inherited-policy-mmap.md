# ADR-0001：暂不采用 Zygote 继承 policy mmap

状态：Accepted / Deferred

日期：2026-07-20

## 背景

Zygisk 模块运行在 Zygote fork 出来的进程中。理论上可以在模块首次加载时打开并 mmap `policy.bin`，让后续应用进程继承映射，减少每次 specialize 的 `openat`、`fstat`、`mmap` 和 `munmap`。

当前实现对每个命中或未命中进程独立读取 policy 快照，并使用 format v3 的 package hash、排序索引和二分查找。配置 daemon 通过临时文件和原子 rename 发布新快照。

## 约束

1. Zygote 中不能增加 inotify 线程、定时线程或持锁后台任务。fork 时其他线程的锁状态会被子进程继承，可能造成永久死锁。
2. 原子 rename 不会更新已经继承的旧 inode 映射。旧映射无法从自身内容得知新快照已经发布。
3. 应用 specialize 之后必须能安全发现快照变化，并在旧映射仍被使用时保持生命周期有效。
4. 失败回退必须保持当前策略快照可读，不能因为缓存失效导致应用启动崩溃或误用未校验数据。

## 实测依据

2026-07-20 在 Xiaomi `alioth`、arm64-v8a 上采集 20 次 LocalSend 冷启动：

| 阶段 | 观测值 |
|---|---:|
| `policy_open_us + policy_lookup_us + policy_unmap_us` | 约 20-40 us |
| readiness P50 / P95 | 4.1 ms / 8.1 ms（退避后） |
| mount_total P50 / P95 | 0.31 ms / 0.55 ms |
| companion total P50 / P95 | 6.27 ms / 10.18 ms |

当前 mmap 固定成本不是启动 P95 的主导项。引入共享映射、generation 指针和 fork-safe 刷新协议会增加明显的正确性复杂度，却不能在当前设备预算中提供可验证的用户收益。

## 决策

暂不实施 Zygote 继承 policy mmap。保持每次 specialize 对当前 `policy.bin` 执行受校验的 `openat`、映射、hash 二分查找和释放流程。

这不是永久否决。当前 Phase 2 的 readiness 退避和挂载事务先完成；policy 查询路径保持简单、独立、可观测。

## 重开条件

满足以下任一条件时重新测量并评估：

- 至少两台设备的真实 P95 显示 policy open/map/unmap 占命中应用总启动时间的 10% 以上；
- package 数量达到 10000 以上，且设备实测 `policy_open` 固定成本明显高于 lookup 和 mount；
- Zygote 或 Android Runtime 提供明确的、无后台线程的快照刷新生命周期钩子；
- 能用独立 generation/inode 指针完成刷新，并通过旧映射、rename、进程 fork、daemon 重启和异常回退测试。

在重开前不得以“减少几个 syscall”的微基准结果替代真实设备 P95/P99 数据。
