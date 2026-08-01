# V-42～V-48 辅助执行域实施审计

- Change ID: `p6-auxiliary-audit-20260731`
- Tasks: `T-26～R-30`, `V-42～V-48`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Host: Windows x86_64, MSVC Release
- Device scope: V-42 单设备 fanotify baseline 为 `unsupported`；本报告不新增 event active 声明
- Evidence: `core/include/pathguard/effect_adapter.h`, `core/include/pathguard/export_worker.h`,
  `core/include/pathguard/complete_vfs_adapter.h`, `cli/src/main.cpp`,
  `tests/unit/auxiliary_adapters_test.cpp`, `tests/integration/cli_v6_test.cmake`,
  `tests/perf/pattern_benchmark.cpp`

## 审计结论

本批实现适合作为 Host 可验证的接口合同，但不满足生产执行域的完整验收条件。核心 matcher、
Redirect、Provider path-I/O 和 admission 没有依赖这些辅助 adapter；能力不足时不得切换执行域，
也不得把 adapter 存在解释为 capability active。

| 范围 | 当前实现 | 未完成条件 | 状态 |
| --- | --- | --- | --- |
| Observe | effect mask 正交分发；固定窗口限速；basename 脱敏；容量/锁竞争时只丢辅助事件；accepted/dropped/drained metrics | 设备 event source 与 V-43 验证；当前 runtime unsupported | complete（adapter contract） |
| Export | normalized event source/fake、FID/probed-stat key、有界 dedupe、完成项淘汰、失败项有界保留、4096 条恢复硬上限、CRC 原子 recovery file、crash replay、temp-copy/fsync/atomic-install/remove executor、overflow/rescan | 生产 fanotify capability probe、FID/DFID_NAME/pidfd/rename-target adapter及支持设备上的 V-43 | in_progress（production adapter implementation blocked） |
| CLI explain | format v6 静态 selector/action、except、domain、capability/operation union；JSON schema `pathguard.explain.v1` | runtime per-action admission、ambiguous reverse 和 inactive adapter DTO | in_progress |
| CLI status | V-44 golden；无 STL runtime DTO v2；共享 policy→admission builder；Provider/app-path 生产 writer；all generations、per-action、截断与 slot/retire/overflow/drop counters；CLI 稳定透传 | V-45 与真实文件结果的设备对比 | complete（contract/wiring） |
| CompleteVfs | fake backend、OperationPlan 翻译、统一 `AdmitAction`、generation/adapter/capability/operation gate | V-46 非 go，不实现真实 backend | adapter-only；T-29～V-47 blocked |
| Performance | matcher gate + Provider route、provenance、reload/retire、256 slot exhaustion、RSS相对门、machine profile与可配置并发 soak | V-48 30 分钟 Host归档及设备 profile/restart | complete（T-30 Host CI） |

V-42 的设备详情单独归档于 `V-42-event-before.md`。该设备没有 fanotify source，不能执行
overflow、跨文件系统或 daemon restart 队列测试；这些子场景保持 `not_observed`。

## 审计修复

CompleteVfs 原型原先只检查 capability bit 18 和 backend operation mask，会忽略 domain adapter state、
capability generation 与 plan generation。现改为统一调用 `AdmitAction`；stale generation、inactive /
unsupported adapter、缺 capability 或 operation 时均返回 unsupported，且不得调用 backend。

Export worker 的有效容量同时约束 queue 和 `tasks_/states_/failure_stages_` 全部持久状态，并收敛到
恢复格式的 4096 条硬上限；接纳新任务时只淘汰 complete，failed 保持显式 retry 语义，因此成功流
可以长期前进而失败流达到容量后有界拒绝。transfer executor 明确使用无异常、分阶段返回值合同，以兼容 Android
`-fno-exceptions`；copy/sync/rename 任一失败均进入可重试状态。队列满或 source overflow 都会
设置 rescan required 和 metrics，不得修改同步 Redirect 结果。幂等身份优先使用
`(fsid, file_handle, mount identity, generation, event kind/window)`；stat fallback 必须由调用方
显式证明 capability probe 已通过。versioned snapshot 验证 queued/failed/complete 保留，crash 时
running 只重放为 queued。durable store 采用 version/CRC/容量限制和 sync+atomic replace；filesystem
executor 采用目标目录临时文件 copy+fsync+no-replace install，move/trash 只在安装成功后删除源。
生产 fanotify adapter 因设备能力 unsupported 未接入。

Observe adapter 使用注入的单调时钟进行固定窗口限速；窗口锁竞争只丢弃 Observe，不阻塞主 I/O；
路径字段默认只保留 basename，避免把完整存储路径写入异步事件。下游 sink 失败和错误事件类型均有独立
metrics，当前仍未由 Provider/Zygisk runtime 接入生产事件 writer。

CLI 静态 explain 明确输出 `admission=not_evaluated`，避免把 policy 中的 required bits 误表述为
设备已观测能力。`status --json` 只组合权威状态文件，不从日志推断 hook active。

V-44 已冻结改造前文本/JSON golden。共享 `RuntimeStatusRecord` 升为 v2 POD，直接嵌入唯一生产
`ActionAdmission` 类型，避免 CLI 重算 observed/missing/reason；固定 16 项容量并显式报告
`action_total/actions_truncated`，不能静默截断。当前 Zygisk mount writer 输出 v2 aggregate 字段，
并由共享 builder 直接生成 action admission。Provider/app-path policy 发布后，Zygisk 原子写入逐 action
与 snapshot counters；CLI golden 覆盖这些字段的稳定 JSON 透传。实际状态与文件结果对比留在 V-45。

runtime benchmark 使用生产 `PolicyActionRouter::Route`、`RouteProvenanceStore` 与 snapshot domain，
补齐 Provider/provenance/reload/slot/RSS 门；短 soak 已进入 CTest，30 分钟 Host soak 归入 V-48。

## 验证结果

```text
Host Release build                         passed
Host Release CTest                        82/82 passed
Android NDK arm64-v8a                     passed
Android NDK armeabi-v7a                   passed
Zygisk APP_STL=none / ELF isolation       passed
Host/Android rules compiler parity        passed
```

Host CTest 覆盖 effect queue、Export event/retry/durable recovery/filesystem transfer、CompleteVfs stale
generation、Provider per-action status builder/CLI JSON、matcher与runtime性能门及短 soak。设备 fanotify、
CompleteVfs backend 和设备 profile 不执行；30 分钟 Host soak 已通过并归档。

## 差异分类

| 差异 | 分类 |
| --- | --- |
| CompleteVfs 使用统一 admission 并拒绝 stale generation | unexpected regression resolved |
| Observe/Export/CLI 的 Host 合同扩展 | planned extension |
| Export queue 满未置 rescan required | unexpected regression resolved |
| 核心 C1～C6 admission 和执行域选择 | unchanged |
| 生产 fanotify、CompleteVfs active、设备 profile | not_observed / blocked |

自动化范围内 `unexpected_regression=0`；第三部分整体仍未完成。
