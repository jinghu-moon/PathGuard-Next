# V-59～V-62 最终 Host 故障、性能与追踪审计

- Change ID: `p6-final-host-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Host: Windows x86_64 / MSVC Release + Clang UBSan
- Status: `complete`（Host/离线部分）

## V-59 Host 故障矩阵

Release 与 UBSan CTest 均通过以下自动化故障组：

| 故障组 | 自动化证据 | 结果 |
| --- | --- | --- |
| mount pre/post mutation、rollback | `pathguard_mount_transaction_test` | 无 partial commit，逆序 rollback |
| policy corruption/reload/publish failure | `pathguard_policy_v6_test`、`pathguard_rules_control_test` | 旧 generation 保持或 inactive，不误报 active |
| topology/generation stale | rules pipeline/control、action admission/router tests | stale/unsupported 不执行 backend |
| Provider Hook inactive/lifecycle | Provider lifecycle/route context tests | committed hook 透明透传，无错误 unload |
| provenance crash/journal failure/corruption | route provenance、Export worker tests | committed/absent/ambiguous，损坏恢复可观测 |
| event overflow | auxiliary adapter、Export worker tests | 丢 Observe/Export 并计数，不改变核心 I/O |
| snapshot slot/retire exhaustion | snapshot publisher/domain tests、runtime benchmark | 有界拒绝、counter 精确、无泄漏 |

设备 namespace owner death、真实 Provider crash loop、真实 fanotify overflow 仍属于 V-57～V-59
设备部分，保持 `not_observed`。

## V-60 Host 性能与 soak

- Release matcher/runtime benchmark 全部满足同次 reference 的相对阈值；
- 2 秒 CI soak 与 30 分钟 Host soak 均通过；
- 30 分钟结果：29,887,370,378 matches、402,536,774 reloads、RSS +221,184 bytes、
  slot high-water 4、retired high-water 5、reload rejected 0；
- 不跨 verification mode 比较绝对时间，不以 fail-open 次数换取表面性能。

设备 50 次冷/热操作、Provider 接收批次和设备 RSS profile 仍需真机。

## V-61/V-62 离线审计

本报告对应的机器可验证入口为 `V-59-V-62-final-host-audit.json`，包含冻结 schema 要求的
rules/policy/module hashes、scenario、steps、before/after actual、classification、evidence paths 和
reviewer conclusion，并已通过 `tests/baseline/validate_comparison_report.cmake`。

- planned breaks：format 1、policy v5、旧 mapper、`provider_compat`/`file_picker`、旧 status DTO；
- replacements：format 2、PolicyV6、Selector/Action、provenance、versioned runtime status；
- `unexpected_regression=0`（Host、NDK/ABI 与已观察单设备证据范围）；
- C1～C6、Glob v1、五 execution domains、稳定 capability bits 和 DecisionReason 均有自动测试、
  设备场景或明确 `unsupported/not_observed`；
- CompleteVfs 决策为 `adapter-only`，T-29～R-29/V-47 的阻断符合 V-46 前置条件，不是遗漏实现。
- T-27～R-27 的 Host worker/store/executor 合同已完成，但生产 fanotify capability probe 与
  FID/DFID_NAME/pidfd/rename-target adapter 仍是实现阻断，未归并为 V-43 的纯设备验证。

最终跨设备 V-61/V-62 结论依赖 V-57/V-58 及 V-59/V-60 设备报告；V-63 因此保持 blocked，不能
仅凭 Host 结果判定整体 go。
