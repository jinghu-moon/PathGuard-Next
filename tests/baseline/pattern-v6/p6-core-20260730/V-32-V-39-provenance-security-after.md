# V-32～V-39 provenance、安全与故障隔离对比

- Change ID: `p6-core-20260730`
- Before commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`
- After commit: `working_tree`

## 实现与自动验证

- route store 按 `(storage_root_id, target_relative_path)` 跨 UID/scope 全局唯一预留；不使用旧字典序
  canonical source fallback。
- create/rename/delete 使用 prepare/materialized/commit/abort；rename 原子迁移 owner，delete
  提交 tombstone；失败 create/rename 仅在 strong identity 匹配时补偿。
- WAL frame 有版本、sequence、CRC 和 16 KiB payload 上限；截断/CRC/状态机异常拒绝恢复。
- 启动 replay 后将全部遗留 pending 追加 durable ABORT；不扫描或删除物理中间对象，后者保持
  unowned/ambiguous，新的 reservation 不会被永久阻塞。
- reverse 只返回 unique/none/ambiguous；identity 或 plan generation 不匹配为 ambiguous。
- openat2 能力按实际 errno 缓存；ENOSYS/EINVAL/权限拒绝才选择逐组件
  `openat(O_NOFOLLOW)`，EAGAIN/EXDEV/ELOOP 不误判为永久 unsupported。
- NoMatch 不记录；deny/collision/ambiguous 与 fail-open 原因使用稳定 errno/status，诊断按
  `(scope, reason, rule)` 限速。

专项测试：`pathguard_route_provenance_test`、`pathguard_failure_policy_test`、
`pathguard_provider_route_context_test`、`pathguard_snapshot_publisher_test` 全部通过。
全量 Host Release `75/75` 通过，Android 双 ABI、Zygisk `APP_STL=none`、ELF isolation 与
Host/Android rules compiler parity 通过。

## 对比结论

| 差异 | 分类 |
| --- | --- |
| 删除 v5 canonical reverse 猜测 | planned_break |
| 多源不同名允许、同 target key 拒绝 | planned extension |
| identity/generation 不可证明时 ambiguous | planned_break（安全收紧） |
| Provider/daemon 重启、真实 fsync/strong identity probe | not_observed |
| symlink/TOCTOU 真机矩阵、Linux 4.19/5.6+ | not_observed |

自动化范围内没有 partial owner、错误唯一映射或错误 active。设备层故障注入未执行，状态保持
`not_observed`。
