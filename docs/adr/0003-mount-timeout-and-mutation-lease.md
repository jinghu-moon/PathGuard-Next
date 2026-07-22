# ADR-0003：超时、fail-open 与 namespace mutation lease

状态：Accepted / Propagation Taint Pending Device Validation

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
applying -> cancel_requested -> namespace_tainted
applying -> rollback_complete
applying -> namespace_tainted
```

约束如下：

1. helper 必须在第一项 namespace mutation 前以 CAS 将 `pending` 改为 `applying`，该 CAS 是 mutation lease 的唯一获取点。`MS_PRIVATE`、source anchor 和正向 bind 都属于 namespace mutation，不能放在 lease 之前。
2. 应用在 300ms 到期时仅能把 `pending` CAS 为 `cancel_requested`。成功后可以立即 fail-open；helper 不得再进入 `applying`。
3. 超时时若状态已是 `applying`，应用请求取消并等待 `rollback_complete` 或 `namespace_tainted`。只有前者允许目标进程继续；后者必须终止 namespace 成员。
4. helper 每项 namespace mutation 前和提交前检查取消；所有可逆 mount 进入栈，失败或取消时逆序 `umount2(MNT_DETACH)`。
5. `complete` 发布后不接受取消。持有 lease 后的失败不会发布中间 `failed` 终态；只有可逆 mount 已全部验证回滚且传播状态未发生不可恢复变化时，才发布 `rollback_complete`。`failed` 只表示尚未取得 lease 的预检失败。
6. 内部 readiness 5 秒上限不计入 300ms 承诺；等待期间仍可由 `pending -> cancel_requested` 中止。

## 传播状态与 namespace taint

`mount(nullptr, "/storage", ..., MS_REC | MS_PRIVATE, ...)` 会改变 mount propagation peer group。将一个 shared mount 改为 private 后，不能通过简单的 `MS_SHARED` 恢复并重新加入原 peer group，因此该操作不能伪装成普通的可逆栈项。

helper 在取得 mutation lease 前必须解析目标 namespace 的 mountinfo，并判断 `/storage` 是否已经满足隔离要求：

- 已经是满足要求的 private/slave 状态时，不重复修改传播属性。
- 必须执行传播变更时，在 journal 中记录 `propagation_changed = true`，并将其视为不可逆 mutation。
- `propagation_changed = true` 后如果事务失败或取消，即使全部 PathGuard mount 已卸载，也不得发布 `rollback_complete` 或继续 fail-open；必须发布 `namespace_tainted`，关闭 helper 持有的 namespace/mount FD，并使执行 `setns` 的 helper worker 返回原 namespace 或直接退出，再终止该 namespace 的全部目标进程，使其从 zygote 获得新的 namespace 后重新启动。
- 无法确认 namespace 成员或无法确认全部成员已终止时，状态保持不可恢复错误，不得让任一成员继续运行。

产品中的 `rollback_complete` 只表示“PathGuard 可逆 mount 已清除，且没有不可恢复的传播变更”；不得仅根据 mount 栈为空推导 namespace 已恢复原状。

## 产品语义

对外指标必须分别报告：

- pre-mutation cancel latency，目标不超过 300ms；
- applying transaction latency；
- rollback latency；
- namespace taint count 与被终止的 namespace member 数量。

不得再把 300ms 描述为所有命中应用启动的绝对硬上限。fail-open 仅在 namespace 从未变更，或可逆 mutation 已回滚且传播状态未被不可恢复地修改后成立。

## 进入 R1 的门槛

Host 状态转换、CAS 竞争测试和 Zygisk/NDK 编译已完成。真机已覆盖取得 lease 前的取消和第一条 mount 后的取消回滚，可以进入 R1 的无传播变更路径；PID start time 联合校验仍属于 R1 preflight 的实现门槛。执行会改变 `/storage` propagation 的路径还必须补充 `namespace_tainted` 状态转换、namespace member 终止和重新启动真机测试，未通过前不得宣称该失败路径可安全 fail-open。

## 真机证据

2026-07-20 在 Xiaomi `alioth`、Android 13、Magisk 30.6 上完成延迟注入：

| 场景 | 样本 | 应用等待 | helper 结果 | namespace 结果 |
|---|---:|---:|---|---|
| lease 前延迟 450ms | 6 | 300.069-300.287ms | `ECANCELED`、`propagation_us=0`、`mount_total_us=0`、`committed=0` | 6/6 无 PathGuard mount，6/6 未安装 Hook |
| 第一条 mount 后延迟 450ms | 1 | 452.931ms | `ECANCELED`、`rollback_us=63`、`committed=0` | 无 PathGuard mount，未安装 Hook |

lease 前档位证明应用成功执行 `pending -> cancel_requested` 后，迟到 helper 无法进入 `applying`。第一条 mount 后档位证明应用不会在 300ms 时提前放行，而是等待逆序回滚完成。
