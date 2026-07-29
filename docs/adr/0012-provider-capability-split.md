# ADR-0012：Provider 意图与三个动态路径准入能力分离

状态：Accepted

日期：2026-07-29

## 背景

旧 `file_picker = true`/`provider = virtualize` 同时混合了用户意图、Binder 调用方身份、
Provider query/insert 路径映射和 FUSE 覆盖状态。三类能力由不同组件提供，也会独立失效：

- Binder identity 可能因 `clearCallingIdentity()`、OEM 实现或 Hook 缺失而不可用；
- query/insert/create 的 URI、projection、真实 FD 和反向扫描可能只完成其中一部分；
- FUSE 请求身份和完整操作矩阵依赖 Mainline/OEM/内核能力，与 Provider Java/Binder Hook
  是否成功不是同一事实。

用一个 bool 表示上述状态会把 partial hook 误报为 active，也无法解释 LocalSend/SAF 失败发生
在哪个能力面。

## 与 ADR-0004 的关系

本 ADR 追加冻结 [ADR-0004](0004-capability-bitset.md) 的 64 位稳定 capability 协议，不修改
既有 bit 0～4 和 8～11 的含义。bit 5～7、12～15 继续保留，不得因本 ADR 顺手复用。

## 决策

配置和运行时能力彻底分离：

- `provider = { enabled = true }` 只表示用户允许 Provider adapter 参与，不是 capability；
- 删除 `file_picker` 作为运行时能力或 active 判据；
- 首版冻结三个相互独立的稳定 admission bit：

| Bit | 名称 | 置位条件 | 不代表 |
|---:|---|---|---|
| 16 | `provider_caller_uid` | 能在整个受支持 Provider 操作期间取得并验证原始 Binder caller UID/user，identity clear 区间也有成对跟踪或安全拒绝 | query/insert 已映射、FUSE 可用 |
| 17 | `provider_query_insert_mapping` | query projection、insert/create、返回 URI/FD、底层 path I/O 与必要反向映射通过同一虚拟路径语义的完整测试 | complete syscall 覆盖、FUSE 可用 |
| 18 | `fuse_complete_path` | FUSE 请求身份以及 lookup/open/create/rename/unlink/readdir/reply 的基线矩阵全部通过 capability probe | 仅找到符号、安装 Hook 或 Provider 映射成功 |

未来 C++ 常量名称固定为 `kCapabilityProviderCallerUid`、
`kCapabilityProviderQueryInsertMapping` 和 `kCapabilityFuseCompletePath`。本 ADR 只冻结协议；具体
常量、probe 和 reader 校验在 P1/P3/P4 对应实现阶段加入。

`fuse_complete_path` 位于 FUSE backend capability domain，不属于 Provider 子系统；它仍作为第三个
独立准入位列在同一动态路径状态矩阵中。命名禁止使用 `fuse_hooked`，因为“Hook 已安装”不是
用户可依赖的语义能力。

三个稳定 bit 描述准入基线。probe 另外返回 action mask 和诊断 substatus，例如
`path_read/write/query/insert/create/rename/delete/reverse_scan/readdir`。substatus 用于精确诊断和
动作矩阵，不随意占用新的稳定 bit；任一 composite bit 的必需 substatus 缺失时，该 bit 不置位。

## 动作准入矩阵

| 动作范围 | 必需稳定能力 | 额外要求 |
|---|---|---|
| Provider/SAF glob redirect | `provider_caller_uid` + `provider_query_insert_mapping` | 对应 read/write/query/insert/create/rename/delete/reverse action mask 完整；`reverse_mode=provenance` 时 ADR-0017 store/coordinator 必须 healthy |
| `enforcement = "provider"` glob deny | `provider_caller_uid` + `provider_query_insert_mapping` | deny 操作矩阵完整；缺项时整个 deny rule inactive |
| `enforcement = "complete"` glob deny/redirect | `fuse_complete_path` 或未来等价 VFS complete capability | FUSE/VFS action mask 覆盖规则所需全部操作 |
| 仅 app path Hook 的 redirect | `app_path_adapter`（bit 19）+ `execution_domain = app_path` | 按 ADR-0013 同时校验进程级 adapter state 与 required/observed operation mask，不冒充 Provider 或 complete |

Provider redirect 不要求 `fuse_complete_path`；FUSE complete backend 也不要求 Binder
`provider_caller_uid`，因为其身份来自 FUSE request context。能力是正交组合，不是三级 fallback。
app-path 的准入由 [ADR-0013](0013-app-path-api-capability.md) 单独冻结：bit 19 只表示 adapter
semantic baseline，具体路径操作仍由 operation mask 准入。

## 状态模型

每条 action 的状态至少报告：

```text
intent: enabled | disabled
admission: active | inactive | unsupported
required_bits
observed_bits
missing_bits
action_mask
probe_reason / errno
capability_generation
plan_generation
```

`unsupported` 表示当前设备/进程无法提供要求；`inactive` 表示意图关闭、规则不适用或尚未准入；
两者不能合并。Hook 已提交但 composite capability 未通过时，模块保持驻留并全量透传，不设置
active，也不能仅因 GOT/符号命中设置 bit。

## 否决方案

### 保留单一 `file_picker` bool

否决。它无法表达身份、Provider 映射和 FUSE 三个独立故障域，容易产生错误 active 状态。

### 把三个 bit 当作全部 Provider 功能的逐级 fallback

否决。Provider 和 FUSE 是正交后端。失败后是否可使用另一后端由 action requirements 和
admission 决定，不能在一次操作中静默降级。

### 以 Hook/符号存在设置 FUSE capability

否决。符号命中只能作为 probe 输入；只有完整请求身份和操作矩阵验证成功才能设置
`fuse_complete_path`。

## 兼容与迁移

- format 1 的 `file_picker = true` 由迁移工具转换为 format 2 的
  `provider = { enabled = true }`，不能预设任何 observed bit；
- `policy.bin` 只保存 action required bits，不保存某台设备的 observed bits；
- observed bits 只存在于带 generation 的 runtime capability snapshot；
- reader 遇到未知 required bit 必须拒绝新 policy，并继续使用上一份有效 snapshot。

## 后果

- LocalSend/SAF 故障可以明确区分 caller identity、Provider mapping 和 FUSE complete path；
- provider scope 能在没有 FUSE 的设备上独立工作；
- complete enforcement 不会因部分 Provider/libc Hook 被错误标为 active；
- capability 协议保持小而稳定，细粒度操作差异由 action mask/substatus 承担。

## 依据

- [ADR-0006：SAF 系统代写进程虚拟化](0006-saf-provider-virtualization.md)
- [ADR-0013：app-path adapter 准入](0013-app-path-api-capability.md)
- [ADR-0017：route provenance 事务](0017-route-provenance-transactions.md)
- [Pattern redirect design §7.3～§7.5](../08-pattern-redirect-design.md)
- [AOSP scoped storage/FUSE](https://source.android.com/docs/core/storage/scoped)
- [AOSP MediaProvider module](https://source.android.com/docs/core/media/media-provider)
