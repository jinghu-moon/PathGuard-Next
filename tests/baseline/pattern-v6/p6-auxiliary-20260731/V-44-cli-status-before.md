# V-44 CLI/status/explain 改造前基线

- Change ID: `p6-status-before-20260801`
- Before commit: `1fcb35b` + 本分支既有 CLI v6 working tree
- Scope: Host Release fixture
- Status: `complete`

## Golden

- 人类可读 status：`V-44-status-before.txt`
- 机器可读 status：`V-44-status-before.json`
- 静态 policy explain：`V-44-explain-before.json`

三条命令正常场景 exit code 均为 `0`；缺少 module/policy/package 参数为 `2`；状态文件或
policy 不存在/损坏为 `1`。静态 explain 只显示 intent 与 required union，并明确
`admission=not_evaluated`，没有从日志或设备名称推断 active。

## Planned breaks

| 范围 | before | planned after |
| --- | --- | --- |
| runtime DTO | `version=1`，mount 结果字段 | `pathguard.runtime_status.v2`，增加 all generations、per-action admission 容器和 counters |
| daemon rules JSON | 无独立 schema/version | `pathguard.rules_status.v1` |
| CLI aggregate | `pathguard.status.v1` | 保持 outer v1，嵌入有版本的 rules/runtime DTO |
| explain | `pathguard.explain.v1`，静态 admission 未评估 | schema 保持；只有读取权威 runtime DTO 时才输出 observed/missing/reason |

未知 key 在 status 聚合时保持并透传；形似整数但带前导零的值保持 JSON string。format/policy
未知版本由 decoder 拒绝，不由 CLI 猜测兼容。
