# ADR-0003：超时、fail-open 与 namespace mutation lease

状态：Accepted / Device Validated

日期：2026-07-20

## 背景

经典 bind mount 事务包含多次 namespace mutation。应用等待硬上限、超时 fail-open 和返回后 namespace 不再变化三项无法在 helper 已开始挂载时同时保证。

## 决策

采用设计文档第 8 节的契约 2：保持 fail-open；300ms 仅作为取得 namespace mutation lease 之前的取消上限，不是 helper 进入 `applying` 后的绝对等待上限。

状态机至少包含：

```text
pending -> applying -> complete
pending -> cancel_requested -> cancelled
applying -> cancel_requested -> rollback_complete
applying -> rollback_complete
```

约束如下：

1. helper 必须在第一条正向 mount 前以 CAS 将 `pending` 改为 `applying`，该 CAS 是 mutation lease 的唯一获取点。
2. 应用在 300ms 到期时仅能把 `pending` CAS 为 `cancel_requested`。成功后可以立即 fail-open；helper 不得再进入 `applying`。
3. 超时时若状态已是 `applying`，应用请求取消并等待 `rollback_complete`，完成后才允许目标进程继续。
4. helper 每条 mount 前和提交前检查取消；所有成功 mount 进入栈，失败或取消时逆序 `umount2(MNT_DETACH)`。
5. `complete` 发布后不接受取消。持有 lease 后的失败不会发布中间 `failed` 终态，而是在回滚完成后直接发布 `rollback_complete`；`failed` 只表示尚未取得 lease 的预检失败。
6. 内部 readiness 5 秒上限不计入 300ms 承诺；等待期间仍可由 `pending -> cancel_requested` 中止。

## 产品语义

对外指标必须分别报告：

- pre-mutation cancel latency，目标不超过 300ms；
- applying transaction latency；
- rollback latency。

不得再把 300ms 描述为所有命中应用启动的绝对硬上限。fail-open 仅在 namespace 从未变更或回滚完成后成立。

## 进入 R1 的门槛

Host 状态转换、CAS 竞争测试和 Zygisk/NDK 编译已完成。真机已覆盖取得 lease 前的取消和第一条 mount 后的取消回滚，可以进入 R1；PID start time 联合校验仍属于 R1 preflight 的实现门槛。

## 真机证据

2026-07-20 在 Xiaomi `alioth`、Android 13、Magisk 30.6 上完成延迟注入：

| 场景 | 样本 | 应用等待 | helper 结果 | namespace 结果 |
|---|---:|---:|---|---|
| lease 前延迟 450ms | 6 | 300.069-300.287ms | `ECANCELED`、`propagation_us=0`、`mount_total_us=0`、`committed=0` | 6/6 无 PathGuard mount，6/6 未安装 Hook |
| 第一条 mount 后延迟 450ms | 1 | 452.931ms | `ECANCELED`、`rollback_us=63`、`committed=0` | 无 PathGuard mount，未安装 Hook |

lease 前档位证明应用成功执行 `pending -> cancel_requested` 后，迟到 helper 无法进入 `applying`。第一条 mount 后档位证明应用不会在 300ms 时提前放行，而是等待逆序回滚完成。
