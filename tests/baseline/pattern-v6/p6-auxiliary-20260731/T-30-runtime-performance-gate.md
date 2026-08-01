# T-30～R-30 Runtime 性能与退化 Host 门

- Change ID: `p6-runtime-gate-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Host: Windows x86_64 / MSVC Release
- Status: `complete`（设备 profile 属于 V-48）

## 覆盖矩阵

`pathguard_pattern_benchmark` 保留 matcher 专项，`pathguard_runtime_benchmark` 增加生产接口矩阵：

| 场景 | 性能/资源断言 |
| --- | --- |
| Provider scope miss / route match | policy v6 `PolicyActionRouter::Route`；P50/P95/P99/max；热路径 allocation=0；match 相对 scope-miss 门 |
| Provenance prepare/abort | 独立 store/journal reference；延迟和分配基线 |
| Provenance prepare/materialize/commit | 相对两阶段 reference 的延迟与三阶段 allocation 门 |
| Snapshot acquire/reload | 发布相对 acquire 门；每次 reload 最多 caller 的一个 snapshot allocation；retire reject=0 |
| 256 slot exhaustion | 全部 slot 由独立线程持有；失败 acquire 相对可用 acquire 的容量比例门；high-water/counter 精确断言；allocation=0 |
| RSS | 同进程 warm baseline + policy/blob 与当前 RSS 比例噪声预算；10,000 次 route/reload 后必须低于门限 |
| reload/match soak | 4 reader + 1 writer；真实 `MatcherSnapshot`；可配置秒数；slot/reload reject 必须为 0 |

所有 JSONL 记录包含 schema、platform、compiler、architecture、hardware threads、P50/P95/P99/max、
allocation、relative budget、RSS 和 RSS budget。阈值只与同次、同进程 reference 比较，不跨不同
verification mode 使用固定纳秒结论。

## 参考运行

```text
provider_scope_miss                    P99 100ns, allocations 0
provider_route_match                   P99 300ns, allocations 0
provenance_prepare_abort               P99 1000ns
provenance_prepare_materialize_commit  P99 1800ns
snapshot_acquire                       P99 100ns, allocations 0
snapshot_reload                        P99 200ns, allocations 1000/1000
snapshot_slot_exhaustion               P99 1300ns, allocations 0
runtime soak smoke (2s)                35,994,575 matches / 430,430 reloads
slot high-water                        4
retired high-water                     3
reload rejected                        0
result                                 passed
```

R-30 审计未发现超门或持续资源增长，因此依据 YAGNI 不增加新的热路径算法；“无需优化”是
证据驱动的重构结论，不以无依据改动换取微基准数字。

## V-48 边界

30 分钟 Host soak（1800s）已通过：29,887,370,378 matches / 402,536,774 reloads，RSS
11,460,608 → 11,681,792 bytes，slot high-water 4、retired high-water 5、reload rejected 0，stderr 为空。
设备 Provider restart、冷/热批次和设备 RSS profile 仍需真机，
不计入 T-30 Host CI 完成状态。
