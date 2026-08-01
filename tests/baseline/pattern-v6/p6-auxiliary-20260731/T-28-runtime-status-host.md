# T-28～R-28 Runtime status Host 合同

- Change ID: `p6-runtime-status-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Status: `complete`（V-45 设备对比独立 pending）

## Red / green

`pathguard_runtime_status_test` 的 red 阶段确认 v1 DTO 缺少 per-action admission、all generations、
截断语义和资源计数。green 阶段将唯一生产 `ActionAdmission` 直接嵌入无 STL
`RuntimeActionStatus`，不在 CLI 复制 observed/missing/reason 计算。

v2 冻结字段包括：

- action kind/mask/domain、intent、rule/selector/conflict ID；
- required/observed/missing capability 与 operation；
- admission state/reason、probe errno、plan/capability generation；
- content/snapshot/plan/capability/topology generations；
- hazard slot、retire limit/high-water、event overflow、diagnostic drop counters；
- 固定 16 项 action 容量，以及 `action_total/actions_truncated` 显式截断。

daemon `rules-status.json` 增加 `pathguard.rules_status.v1`；共享无 STL
`AppendPackageRuntimeActions` 直接从 policy v6 action 和唯一 `AdmitAction` 构建 per-action DTO。
Zygisk Provider/app-path policy 发布后原子写入 `pathguard.runtime_status.v2`，包含 action kind/domain、
rule/selector、required/observed/missing capability/operation、admission reason、generation 和 snapshot
counters。CLI outer `pathguard.status.v1` 保持稳定并透传这些固定 key；静态 policy explain 继续使用
`admission=not_evaluated`。

```text
pathguard_runtime_status_test       passed
pathguard_rules_control_test        passed
pathguard_cli_v6_integration        passed
Host Release CTest                 82/82 passed
Android NDK arm64-v8a              passed
Android NDK armeabi-v7a            passed
Zygisk APP_STL=none / ELF guard    passed
```

## V-45 边界

T-28～R-28 的模型、生产 writer 和 CLI 透传已完成。active/inactive/unsupported/collision/
ambiguous/overflow 与真实文件结果的一致性仍必须在 V-45 真机重放；未执行前不得把单纯安装成功
解释为设备全能力 active。
