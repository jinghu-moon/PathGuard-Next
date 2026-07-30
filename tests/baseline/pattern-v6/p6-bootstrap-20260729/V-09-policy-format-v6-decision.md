# V-09 policy format 6 决策门

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-09` |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| After commit | N/A（架构决策，尚未修改生产 reader/writer） |
| Branch | `feature/pattern-redirect-v6` |
| Classification | `planned_break`（目标 rules format 2 / policy format 6/schema 3） |
| Decision | ADR-0016 `Accepted` |
| Reviewer conclusion | v6 一次性替代 v5；不保留双 reader；九表布局、引用、预算和拒绝语义已闭合 |

## Before 基线与计划内变化

- 当前可执行代码仍只生成和读取 rules format 1 / policy v5/schema 2；
- 当前 v5 使用 Package/MountRule/EventRule/String 四表，不能表达统一 Selector/Action、Glob、
  CharacterClass 或 `select.except`；
- V-03 已记录 C1～C5 当前真机基线；本任务只冻结目标格式并同步文档，不修改模块、规则、手机
  文件或运行时状态；
- 计划内破坏：实现阶段将由 format 2/v6 协调替换 format 1/v5，旧 rules/policy 被新组件稳定拒绝；
- 不允许变化：literal deny/redirect、LocalSend path-I/O、UID/user 隔离和 fail-open 核心结果。

## 冻结结果

| 项目 | 决策 |
| --- | --- |
| Source / binary | rules `format=2`；policy `format=6`、`schema=3` |
| 迁移 | v6 一次性替代 v5；无双 reader、尾部扩展或生产迁移器 |
| Header | 128 bytes，little-endian，CRC-32 payload，FNV-1a 64 canonical generations |
| 固定表 | Package、ScopeRef、Selector、Action、Pattern、PatternToken、CharacterClass、SelectorExceptRef、StringIndex |
| 字符串 | StringId 是 StringIndex row index；StringData 无 NUL、无空洞；ID 0 固定为空串 |
| Pattern | base/except 统一引用 PatternId；PatternProgram 全局 canonical 去重 |
| Except | ADR-0015 已 Accepted；v6 无“不含 except table”的条件变体 |
| Capability | 仅接受 bits 0～4、8～11、16～19；observed 状态不写入 policy |
| Operation | mask version 1，bits 0～22 已冻结，23～63 拒绝 |
| 安全预算 | reader 使用自身硬上限，不接受 policy 协商 limits |
| 拒绝 | v5、未知 schema/enum/flag/bit、非零 reserved、overflow、空洞、trailing bytes 均拒绝整份候选 |
| 发布 | compiler/reader/daemon/Zygisk/Provider/CLI/probe 必须在同一 change set 切换 |

ADR-0016 同时 supersede ADR-0002 的 v5 格式与 ADR-0006 的 `provider_compat` v5 编码。ADR-0013
不再悬空描述 operation 编号；ADR-0014/0015 已统一指向 rules format 2 与 policy schema 3。

## 严格复核修正

1. 增加 PatternTable，关闭 `PatternId` 原先没有实体表的引用缺口；
2. 将 `plan_generation` 的 canonical 输入补入 `failure_mode`，避免全局失败策略变化却复用旧 plan；
3. 不把未冻结的 `literal_score` 写入 policy；specificity 由 snapshot builder 从已验证 PatternProgram
   统一计算，持久格式只保存可精确复算的 depth 和 bucket hints；
4. 精确定义 first literal component、fixed extension 和 degenerate flag，reader 必须复算；
5. 修正 G6-GlobExcept 的 except patterns，使每项都与 base 存在有效交集；
6. 历史评审曾把所有 Provider action 固定为 capability mask `0x0000000000030000`；
   生产接入审计后按 ADR-0012 的正交能力模型修正为“前向 path-I/O 仅要求 bit 16，
   query/insert/reverse/provenance 才要求 bit 17”。原始 operation mask
   `0x00000000000ffeff`；
7. 修正 ADR-0015 中违反 TOML 1.0 的多行 inline table，示例现在可由目标 parser 直接消费。

## 布局算术与 canonical 检查

PowerShell 只读校验得到：

```text
header_contiguous=True
header_end=128
table_count=9
Package/ScopeRef/Selector/Action/Pattern/PatternToken/
CharacterClass/SelectorExceptRef/StringIndex row size 均为 8-byte aligned
provider_caps_mask_before=0x0000000000030000
provider_forward_caps_mask_after=0x0000000000010000
provider_ops_mask=0x00000000000ffeff
```

每张表的末字段均精确落在声明 row size；所有 table/range 要求连续覆盖。Pattern/Class/String 全局
去重且不得存在未引用 row。physical ordering 与 semantic canonical generation 分离，generation
不依赖 StringId、PatternId 或物理 offset。

## Golden 规则

- `G6-Literal`：LocalSend literal mount redirect；Pattern/Token/Class/Except counts 为 0，验证 target
  StringId、generation、CRC、offset 和空 requirements；
- `G6-GlobExcept`：Provider glob redirect + ASCII class + 两个有效 except；验证三条 PatternProgram、
  class bitmap、ExceptRef、capability/operation masks、provenance 和完整 bytes；
- expected hex 必须由独立 fixture 提供，测试不得调用 production encoder 生成 expected bytes；
- 当前任务只冻结 fixture 输入与断言规则；生产共享头、encoder/reader 和完整 hex 在 T-12/I-12
  红绿循环实现，不能把本 ADR 的 Accepted 状态解释为运行时代码已支持 v6。

## After 对比与回归

- 计划内变化：新增 ADR-0016，ADR-0002 标记 Superseded，主设计和 ADR-0013～0015 消除旧 schema、
  条件式 except 和未冻结 operation 编号；
- 当前生产行为：unchanged。本任务没有修改 `core/`、`rules/`、`daemon/`、`zygisk/`、`native/`、
  `module/config/` 或手机端状态；
- C1～C5：沿用 V-03/V-04 基线，无新观测差异；C6 尚未实现；
- Host Release CTest：`52/52` 通过，0 failed，总耗时 10.62 秒；
- `git diff --check`：通过；stale `Proposed`/schema 2/条件式 ADR-0015 扫描：0 命中；
- unexpected regression：0。

## 证据

| 路径 | SHA-256 |
| --- | --- |
| `docs/adr/0016-policy-format-v6.md`（含 V-10 ADR-0017 交叉引用同步） | `AA6E6DB790E3FEA53DE7EC0AE764F21E26065AAED2305968B91DA23564901FF5` |
| `docs/08-pattern-redirect-design.md`（含 V-10 provenance 决策同步） | `5C304BF479E8A4BFAB8D530D652CA59CE96B883C50532B14CCDCB76DECA3C52D` |
| `build/pattern-v6-v02-release/v09-ctest.xml` | `BFC31C34F42C3A204FF86DF3517F59B95B51ED296D5E9FE08C68CFD6E0E1E4BB` |

## 验收结论

- ADR-0016 状态为 Accepted，并明确 supersede 范围；
- Header、九表 row、ID/range、alignment、canonical encoding、hard limits 和 v5 拒绝语义已冻结；
- ADR-0015 结果已无条件进入 v6，PatternId 不再悬空；
- compiler/reader/CLI/device probe 的唯一常量归属和两个 golden 契约已明确；
- 当前核心行为回归通过，无非预期副作用；
- V-09 判定 `complete`。
