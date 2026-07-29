# V-05 当前代码与目标设计差距

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-05` |
| Baseline commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| Target | `docs/08-pattern-redirect-design.md` v0.8 |
| Classification | `unchanged`（只读盘点） |
| Reviewer conclusion | 所有替换、复用和删除边界均有 owner 与任务 ID |

## 总体结论

当前实现是稳定的 format 1/policy v5 literal-prefix 系统，不是 Pattern v6 的不完整实现。
因此不能在旧 `RedirectRule`/`PathRule` 上追加 glob 分支；必须按设计建立统一
Selector/Action/Decision，再一次性切换 format 2/v6。现有 mount 事务、Binder caller UID、
Provider Hook 生命周期和 daemon 原子发布是应复用的执行基础。

## 模块差距表

| Owner | 当前事实 | 目标状态 | 动作 | 任务 |
| --- | --- | --- | --- | --- |
| `rules/include/.../document.h` | `RedirectRule` 只有 source/target；app 有 `file_picker`、deny、redirect | format 2 内嵌 selector/action 值对象，provider intent 独立 | 替换 public document schema，不兼容旧字段 | V-12→T-03→I-03→R-03→V-13 |
| `rules/src/compiler.cpp` | 只允许 `enabled/users/processes/file_picker/deny/redirect`；arrow desugar 后仍是 literal | 解析 root/glob/type、priority、preserve、collision、enforcement/provider intent | 保留 TOML/source diagnostics，重写 schema decoder | T-03、T-04～T-07 |
| `rules/src/semantic.cpp` | `file_picker` 映射成 `ProviderCompat::kVirtualize`；deny/redirect 物化为 mount rows | canonical SelectorTable/ActionTable；domain/admission 分离 | 删除 bool→compat 隐式语义；复用 path normalization/error model | T-08～T-13 |
| `core/include/pathguard/policy.h` | `AppPolicy` 直接拥有 `LogicalMountRule`/`EventRule` | backend-neutral Selector/Action/PatternPlan/Decision | 新建统一 IR；Mount/Event 成为物化 plan，不再是 canonical policy | T-03、T-10、R-16 |
| `core/include/pathguard/policy_format.h` | v5/schema 2；56-byte header；Package/Mount/Event/String | v6 tables、硬预算、canonical encoding，可选 ExceptRefTable | 由新 format 6 ADR 冻结后一次性替换 | V-09、V-14→T-12→I-12→R-12→V-15 |
| `core/src/binary.cpp` | encoder/reader 校验 v5 offsets/counts/CRC/generations | validated v6 view + owned builder；明确拒绝 v5 | 复用 bounds/CRC/generation 测试思路，不保留双 reader | T-12、R-12、R-31 |
| `core/include/pathguard/capabilities.h` | stable bits 0～4、8～11 | 加入 ADR 冻结的 bits 16～19 + operation mask | 同步共享头/probe/status/reader；禁止日志替代位 | V-16→T-13→I-13→R-13→V-17 |
| `core/include/pathguard/runtime_status.h` | mount transaction 导向的 enforcement/backend/transaction/security/reason | 统一 Decision/Admission diagnostics + all generations/counters | 保留 mount safety 状态，扩展而非字符串拼接 | R-10、T-24、T-28 |
| `daemon/src/rules_control.cpp` | 编译 v5、环境 requirements、单 publisher、失败保留旧 generation | 私有构建 v6 snapshot/index/admission 后原子发布 | 复用单 writer、candidate sequence、fsync/rename；替换 blob/requirements | I-12、I-13、I-14 |
| `zygisk/src/module_entry.cpp` | header-only v5 reader、package lookup、MountPlan、严格事务 | v6 reader + admitted domain plans + MatcherSnapshot guard | 保留 specialize/companion/lease/rollback；替换 policy decode/plan source | I-12、I-14～I-17 |
| `native/directory_resolver.cpp` | openat2 + component walk，目录型 resolver | capability-cached SecurePathResolver，支持动态 target operands | 复用 FD walk/openat2 实现；统一 probe 与 target API | V-36→T-23→I-23→R-23→V-37 |
| `native/mount_executor.cpp` | strict/legacy selection、apply/verify/rollback | 只消费由统一 IR 物化的 MountPlan | 保留实现和安全不变量，不让 executor 理解 glob | V-22→T-16→I-16→R-16→V-23 |
| `zygisk/.../provider_path_mapper.h` | fixed `PathRule{caller_uid,user,visible,backing}`，prefix forward/reverse | OperationContext→PatternEngine→ActionEvaluator→OperationPlan | 替换 matcher；禁止 canonical reverse 猜测 | T-17～T-22、R-32 |
| `zygisk/.../provider_caller_uid.hpp` | Binder clear/restore、FUSE request、raw UID 选择纯函数 | provider caller UID capability producer | 复用并纳入 bit 16 probe；package attribution 不可信时保持 UID scope | T-13、T-18、R-18 |
| `zygisk/src/provider_redirect_hook.cpp` | libc path rewrite、常驻镜像 Hook、直接日志 | Provider composite adapter、独立 capabilities/query-insert/provenance | 保留 Hook allowlist/驻留透传；替换 path/action decision | V-26～V-31、T-18～T-20 |
| `zygisk/src/media_query_filter.cpp` | deny query filter，非统一 action/effect | query/insert mapping 与统一 Provider route context | 不把现有 deny filter 当 bit 17；按 P3 重构 | V-28→T-19→I-19→R-19→V-29 |
| `cli/src/main.cpp` | compile/validate/lint/plan/explain/status 读取旧 policy/mount fields | v6 Decision/Admission/status/explain schema | 保留命令职责；替换 DTO、golden 和输出字段 | V-44→T-28→I-28→R-28→V-45 |
| `protocol/schema/control-protocol.md` | 控制平面文档，没有 v6 capability/Decision DTO | versioned control/status contract | 与 shared headers、CLI golden 同步 | I-12、R-28、R-33 |
| `tests/unit` | 52 项中的 literal/mount/provider/rules/v5 baseline | Pattern v1、v6、snapshot、provenance、adapter composite | 保留 before tests；按红绿重构增量替换 | T-03～T-30 |
| `tests/golden/policy-v5` | v5 golden 是当前执行契约 | v6 golden + v5 rejection fixture | v5 在切换前保留，切换后只作为 rejection/baseline | T-12、R-31 |

## 应复用的正确性基础

以下实现不是“旧设计包袱”，应通过 adapter/依赖倒置继续复用：

- rules SourceBuffer、TOML adapter、diagnostic location 与资源 limits；
- daemon 单 writer、候选校验、旧 generation 保留、原子 publish；
- `ContentGeneration`/`PlanGeneration` 的稳定性思想；
- package→UID/user scope 解析与真实 Binder caller UID；
- strict mount backend、FD pin、mutation lease、verify、rollback、namespace taint；
- Provider 常驻镜像 allowlist、commit 后驻留透传；
- Release Host/NDK/ELF isolation 与设备故障注入工具。

## 必须删除或替换的语义

- format 1 与 v5 production reader/writer；
- `file_picker`/`provider_compat` 单 bool 准入；
- canonical policy 直接等同 MountTable/EventTable；
- Provider `PathRule` 自有 prefix matcher；
- target→canonical visible source 的无 provenance 反向猜测；
- 通过“Hook installed”或日志文本推断 stable capability；
- CLI/status 对旧 mount/event rows 的直接展示契约。

删除只在等价 v6 路径通过 before/after 后执行，对应 R-31/R-32；不得在红测之前清理安全网。

## 当前测试保护与空白

当前 52 项保护 rules parse/semantic/v5 binary、mount transaction/backend、Provider prefix/caller/
lifecycle、Media query deny、control plane、hot reload 和 release boundary。尚无以下实现：

- Pattern token/character class/globstar/brace matcher；
- canonical SelectorTable/ActionTable/CandidateIndex/ActionEvaluator；
- capability bits 16～19 和 operation masks 的共享协议；
- hazard-pointer MatcherSnapshot；
- route provenance；
- Provider query/insert/scan 复合一致性；
- v6 encoder/reader/golden。

这些空白分别由 T-03～T-24 建立红测，不能用现有 prefix tests 宣称覆盖。

## 验收结论

- rules、core、daemon、zygisk、native、CLI、protocol、tests 均已盘点；
- 每项明确标为复用、替换或删除，并绑定后续任务；
- 当前代码事实与设计 2.1 的描述一致；
- 未发现可直接扩展旧 `PathRule` 而不破坏统一能力边界的安全捷径；
- V-05 判定 `complete`。
