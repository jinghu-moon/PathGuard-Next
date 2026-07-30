# ADR-0016：冻结 policy format 6 / schema 3

状态：Accepted

日期：2026-07-29

Supersedes：[ADR-0002](0002-policy-format-v4.md) 的 policy v5 格式与 canonical IR，以及
[ADR-0006](0006-saf-provider-virtualization.md)“格式影响”一节中的 `provider_compat` v5 编码。

## 背景

当前唯一可执行格式是 policy v5/schema 2。它只包含 Package、MountRule、EventRule 和 String
四张表，无法表达统一 Selector/Action、Glob v1、字符类、有界 `select.except`、execution
domain、capability requirement 和 operation mask。把这些语义追加到 v5 会保留两套规则模型，
也会使 Provider、app-path 和 event adapter 继续各自解析路径规则。

项目尚未发布，不需要运行时兼容旧格式。V-08 已接受 ADR-0015，因此 format 6 首版必须直接
包含 except refs，不能预留半成品字段后再升级。format 6 同时需要解决主设计草案中的一个结构
缺口：selector/except 使用 `PatternId`，但草案没有 PatternTable。没有显式 PatternTable 会迫使
base 与 except 复制 token range，破坏 PatternProgram 去重和引用校验。

## 决策摘要

- 源规则版本是 `format = 2`；编译产物是 policy `format = 6`、`schema = 3`；
- schema 3 表示统一 Selector/Action 语义模型。不得沿用 v5 的 schema 2；
- v6 是 v5 的一次性替代，不实现双 reader、尾部扩展或生产迁移器；
- 所有整数显式 little-endian，所有固定 row 逐字段编码，禁止 reinterpret C/C++ struct；
- Header 固定 128 bytes；九张固定宽度表按固定顺序连续排列，最后是 StringData；
- 增加全局 PatternTable。Selector 的 base 与 ExceptRef 都引用同一 PatternId；
- Pattern/Token/Class/String 可以跨 package 共享不可变内容，授权只能沿
  `Package -> Action -> Selector` 引用链确定；
- CRC-32 保护 payload，FNV-1a 64 保护 semantic canonical generation；
- reader 使用自身硬上限，不接受 policy 声明或协商 limits；
- 未知 enum/flag/operation/capability、非零 reserved、非 canonical range 和 trailing bytes 均拒绝。

## 公共常量

唯一生产定义必须位于 `core/include/pathguard/policy_format.h`，compiler、reader、daemon、
Zygisk/Provider、CLI 和 device probe 只包含该共享头，不复制数字。以下值冻结：

```text
magic                   = 0x424E4750  // bytes "PGNB"
policy format           = 6
policy schema           = 3
header size             = 128
invalid id              = 0xffffffff
operation mask version  = 1
```

哈希与 checksum 继续使用 ADR-0002 已验证参数：

- package hash：FNV-1a 32，offset basis `2166136261`，prime `16777619`；
- content/plan/rule generation：FNV-1a 64，offset basis `14695981039346656037`，
  prime `1099511628211`；
- payload checksum：CRC-32/IEEE，反射多项式 `0xedb88320`，init/xorout 均为
  `0xffffffff`；
- checksum 覆盖 `[header_size, file_size)`，Header 本身不参与。

## 文件布局

```text
Header[128]
PackageTable[package_count]                 row 64
ScopeRefTable[scope_ref_count]              row 8
SelectorTable[selector_count]               row 40
ActionTable[action_count]                   row 48
PatternTable[pattern_count]                 row 24
PatternTokenTable[token_count]              row 8
CharacterClassTable[class_count]            row 24
SelectorExceptRefTable[except_ref_count]     row 8
StringIndexTable[string_count]               row 8
StringData[string_bytes]
```

Header 和所有固定表 offset 必须 8-byte aligned。由于所有 row size 都是 8 的倍数，不插入
padding。空表的 offset 等于前一表末尾。`string_data_offset + string_bytes` 必须严格等于
`file_size`；不允许尾随字节。

reader 使用无符号 64 位临时值执行所有 `offset + count * row_size` 计算，验证不超过
`file_size` 后才缩窄为 32 位或构造 span。任何整数溢出、重叠、空洞、逆序 offset 都拒绝。

## Header：128 bytes

| Offset | 字段 | 类型 | 约束 |
| ---: | --- | --- | --- |
| 0 | magic | u32 | `0x424E4750` |
| 4 | format | u16 | `6` |
| 6 | schema | u16 | `3` |
| 8 | header_size | u32 | `128` |
| 12 | file_size | u32 | 等于实际文件长度 |
| 16 | payload_checksum | u32 | CRC-32/IEEE |
| 20 | header_flags | u32 | 仅 bit 0 已定义 |
| 24 | content_generation | u64 | canonical content FNV-1a 64 |
| 32 | package_count | u32 | 至少 1 |
| 36 | scope_ref_count | u32 | 已验证 ceiling |
| 40 | selector_count | u32 | 至少 1 |
| 44 | action_count | u32 | 至少 1 |
| 48 | pattern_count | u32 | 允许 0（仅 literal policy） |
| 52 | token_count | u32 | 与 Pattern ranges 一致 |
| 56 | class_count | u32 | 允许 0 |
| 60 | except_ref_count | u32 | 允许 0 |
| 64 | string_count | u32 | 至少 1，StringId 0 为空串 |
| 68 | string_bytes | u32 | StringData 精确长度 |
| 72 | package_offset | u32 | 必须为 128 |
| 76 | scope_ref_offset | u32 | canonical contiguous offset |
| 80 | selector_offset | u32 | canonical contiguous offset |
| 84 | action_offset | u32 | canonical contiguous offset |
| 88 | pattern_offset | u32 | canonical contiguous offset |
| 92 | token_offset | u32 | canonical contiguous offset |
| 96 | class_offset | u32 | canonical contiguous offset |
| 100 | except_ref_offset | u32 | canonical contiguous offset |
| 104 | string_index_offset | u32 | canonical contiguous offset |
| 108 | string_data_offset | u32 | canonical contiguous offset |
| 112 | failure_mode | u8 | 首版只允许 `0=open` |
| 113 | operation_mask_version | u8 | `1` |
| 114 | reserved | u16 | 必须为 0 |
| 116 | reserved | bytes[12] | 必须全 0 |

`header_flags` bit 0 为 `allow_legacy_mount`，表示用户允许 ADR-0005 的 legacy mount backend 参与
整 plan 预选择。它不是 observed capability，也不允许 strict 运行失败后重试 legacy。其他 bit
必须为零。

## PackageTable：64-byte row

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | package_hash | u32 |
| 4 | package_name | StringId/u32 |
| 8 | first_scope_ref | u32 |
| 12 | user_ref_count | u16 |
| 14 | process_ref_count | u16 |
| 16 | first_selector | u32 |
| 20 | selector_count | u32 |
| 24 | first_action | u32 |
| 28 | action_count | u32 |
| 32 | plan_generation | u64 |
| 40 | required_capabilities_union | u64 |
| 48 | required_operations_union | u64 |
| 56 | package_flags | u32 |
| 60 | reserved | u32，必须为 0 |

Package flags：bit 0=`all_users`，bit 1=`all_processes`，bit 2=`provider_enabled`。未知 bit
拒绝。wildcard 通过 flags 表达，不写入 `"*"` ScopeRef。`all_users` 与非零 user refs、
`all_processes` 与非零 process refs 不能同时出现。

Package rows 按 `(package_hash, package UTF-8 bytes)` 严格递增；hash 命中后仍比较完整包名。
每个 package 拥有连续且非空的 selector/action ranges。required unions 必须等于其 Action rows
对应字段逐位 OR 的结果；reader 复算并比较，union 只用于快速 status，不参与替代逐 action
admission。

## ScopeRefTable：8-byte row

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | kind | u8 |
| 1 | flags | u8，首版必须为 0 |
| 2 | reserved | u16，必须为 0 |
| 4 | value | u32 |

`kind=0` 表示 user，value 是 Android user ID；`kind=1` 表示 process，value 是非空 StringId。
每个 package range 中先放 user（数值严格递增），再放 process（按字符串 bytes 严格递增）。
Package 的 `first_scope_ref/user_ref_count/process_ref_count` ranges 必须连续分割整个 ScopeRefTable，
不允许未归属、重复或跨 package 引用。

## SelectorTable：40-byte row

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | root | StringId/u32 |
| 4 | base_pattern | PatternId/u32 |
| 8 | first_except | u32 |
| 12 | first_action | u32 |
| 16 | except_count | u16 |
| 18 | action_count | u16 |
| 20 | depth | u16 |
| 22 | reserved | u16，必须为 0 |
| 24 | match_kind | u8 |
| 25 | object_type | u8 |
| 26 | reserved | u16，必须为 0 |
| 28 | first_literal_component | StringId/u32 |
| 32 | fixed_extension | StringId/u32 |
| 36 | reserved | u32，必须为 0 |

`match_kind`：0=`literal_prefix`，1=`glob`。`object_type`：0=`any`，1=`file`，2=`directory`。
literal selector 的 `base_pattern=invalid_id`、`except_count=0`，两个 bucket hint 也必须为
`invalid_id`；glob selector 必须引用有效 PatternId，且两个 hint 必须等于该 Pattern row 的值。
无 first literal/fixed extension 时使用 `invalid_id`，不能使用空 StringId 冒充 absent。

`depth` 是规范化 root 的组件数；两个 bucket hint 是编译期缓存，不是可信输入。reader 从
root/Pattern 重算并要求完全相等。specificity 由 snapshot builder 从 root 与 PatternProgram
统一计算，不写入 policy，避免把匹配排序算法重复固化为可伪造缓存。Selector 在 package range 内按
`(root bytes, match_kind, object_type, base pattern canonical bytes, sorted except patterns)`
严格递增并去重。

每个 selector 的 Action range 必须位于所属 package 的 Action range 内；这些 ranges 按 selector
顺序连续分割 package Action range。Selector 本身不携带 package/UID 授权。

## ActionTable：48-byte row

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | selector_id | u32 |
| 4 | target | StringId/u32 |
| 8 | rule_id | u64 |
| 16 | required_capabilities | u64 |
| 24 | required_operations | u64 |
| 32 | priority | i32 |
| 36 | options | u32 |
| 40 | action_kind | u8 |
| 41 | execution_domain | u8 |
| 42 | preserve_mode | u8 |
| 43 | collision_mode | u8 |
| 44 | reverse_mode | u8 |
| 45 | action_flags | u8，首版必须为 0 |
| 46 | reserved | u16，必须为 0 |

枚举值：

| 类型 | 数值 |
| --- | --- |
| action | 0 deny；1 redirect；2 observe；3 export |
| domain | 0 mount；1 app_path；2 provider；3 complete_vfs；4 event |
| preserve | 0 not_applicable；1 relative |
| collision | 0 not_applicable；1 reject |
| reverse | 0 none；1 static_unique；2 provenance |

`reverse=provenance` 的持久所有权与事务语义由
[ADR-0017](0017-route-provenance-transactions.md) 定义；该枚举本身不表示 store 已准入。

deny/observe 的 target 必须为 `invalid_id`；redirect/export 必须引用非空 target。redirect/export
首版只允许 preserve=relative、collision=reject；deny/observe 两字段必须 not_applicable。
observe options 必须为 0。export options bits 0～1 为 transfer mode（0 copy、1 move、2 trash），
bit 2 为 media scan；其他 bit 拒绝。deny/redirect options 必须为 0。

合法 domain/action/matcher 组合：

- mount：仅 literal_prefix + deny/redirect；
- app_path/provider/complete_vfs：literal_prefix 或 glob + deny/redirect；
- event：literal_prefix 或 glob + observe/export。

不得在 reader 中把非法组合降级到另一个 domain。mount domain 的 strict/legacy primitive 选择仍由
ADR-0005 在该 domain 内按整 plan probe 处理；它不是跨 execution domain fallback。

Action rows 在 package 内按
`(selector_id, execution_domain, action_kind, priority descending, target bytes, options,
required_capabilities, required_operations, rule_id)` 严格递增。相同 semantic action 去重。

## PatternTable：24-byte row

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | first_token | u32 |
| 4 | token_count | u16 |
| 6 | component_count | u16 |
| 8 | first_literal_component | StringId/u32 |
| 12 | fixed_extension | StringId/u32 |
| 16 | pattern_flags | u16 |
| 18 | reserved | u16，必须为 0 |
| 20 | reserved | u32，必须为 0 |

pattern flag bit 0=`degenerate`，其他 bit 拒绝。首版精确定义为：无法提取
`first_literal_component` 且无法提取 `fixed_extension` 时置位；否则必须清零。所有缓存字段由
reader 从 token stream 重算。`first_literal_component` 是第一个完全由非空 literal tokens 组成的
完整 pattern 组件；`fixed_extension` 是末组件最后一个 literal `.` 之后、直到组件结束都为
literal 且非空的字节串，不含 `.`。不存在时使用 `invalid_id`。
Pattern rows 按 canonical token stream 严格递增并全局去重；Pattern 不携带 package ownership。
Pattern token ranges 按 Pattern 顺序连续分割整个 PatternTokenTable，不允许未引用 token。

## PatternTokenTable：8-byte row

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | token_kind | u8 |
| 1 | token_flags | u8，首版必须为 0 |
| 2 | reserved | u16，必须为 0 |
| 4 | operand | u32 |

token kind：0=`literal`、1=`star_component`、2=`one_component_char`、
3=`globstar_component`、4=`char_class`、5=`component_separator`。

literal operand 是非空 StringId；char_class operand 是 ClassId；其余 operand 必须为 0。
token stream 必须满足 ADR-0014 的组件级语法，不能仅逐 token 验证 enum。

## CharacterClassTable：24-byte row

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | bitmap_low | u64 |
| 8 | bitmap_high | u64 |
| 16 | class_flags | u32 |
| 20 | reserved | u32，必须为 0 |

class flag bit 0=`negated`，其他 bit 拒绝。bitmap 不得为空，不得包含 `/`；ASCII 之外无法编码。
Class rows 按 `(flags, bitmap_low, bitmap_high)` 严格递增并全局去重。所有 class 必须至少被一个
CHAR_CLASS token 引用。

## SelectorExceptRefTable：8-byte row

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | pattern_id | u32 |
| 4 | reserved | u32，必须为 0 |

每个 Selector 的 except range 按 Pattern canonical bytes 严格递增、去重，不能引用 base pattern。
所有 ranges 按 Selector 顺序连续分割整个 ExceptRefTable。Reader 执行 ADR-0015 的 8/selector、
256/package 预算及空集/冗余/冲突验证；ExceptRef 不注册为顶层候选。

## StringIndexTable 与 StringData

StringIndex row 固定 8 bytes：

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | data_offset | u32，相对 StringData |
| 4 | byte_length | u32 |

StringId 是 StringIndex row index，不是裸字节 offset。StringId 0 必须是空串 `(offset=0,length=0)`。
其余字符串必须是无 NUL 的有效 UTF-8，按 raw UTF-8 bytes 严格递增、全局去重。StringData 按
StringId 顺序直接拼接 bytes，不写 NUL，不留空洞；每个 index 的 offset 必须等于前一字符串末尾。
除 StringId 0 外，所有字符串必须被至少一个 row/token 引用。

## Operation mask v1

Action `required_operations` 与 runtime `observed_operations` 使用同一 u64 编号：

| Bit | 名称 | Bit | 名称 |
| ---: | --- | ---: | --- |
| 0 | lookup/stat | 12 | readlink |
| 1 | access | 13 | metadata mutation |
| 2 | open read | 14 | truncate |
| 3 | open write | 15 | watch |
| 4 | create | 16 | provider query |
| 5 | directory iterate | 17 | provider insert |
| 6 | mkdir | 18 | media scan |
| 7 | rename | 19 | reverse mapping |
| 8 | hard link | 20 | close-write event |
| 9 | unlink | 21 | move event |
| 10 | rmdir | 22 | delete event |
| 11 | canonical path | 23～63 | reserved |

未知 operation bit 拒绝 policy。Policy 只保存 required 状态，observed 状态不得写入文件。
新增未占用 bit 可以由后续 ADR 冻结；改变既有 bit 含义必须升级 operation mask version 和 policy
format。

## Capability 编码

required capabilities 仅允许 ADR-0004/0012/0013 已冻结的 bits 0～4、8～11、16～19。
5～7、12～15 及 20～63 当前均视为未知，policy 中置位即拒绝。required capability 是 action
必须满足的 semantic baseline；mount backend 的 primitive OR 选择由 ADR-0005 的整 plan resolver
处理，不通过伪造 required bit fallback 表达。

## Canonical IR 与 generation

generation 不哈希物理 offset、StringId、PatternId 或 table padding，而哈希 semantic canonical
stream。基础编码为 little-endian scalar；`bytes` 为 `u32 length + raw UTF-8`；vector 为
`u32 count + elements`。

Canonical plan：

```text
"PGPL6\0"
schema:u16 = 3
header semantic flags:u32
failure_mode:u8
package:bytes
package flags:u32
users:sorted vector<u32>
processes:sorted vector<bytes>
selectors:sorted vector<
  root:bytes, match_kind:u8, object_type:u8,
  base canonical pattern or literal sentinel,
  sorted unique except canonical patterns,
  actions:sorted vector<rule_id/action/domain/priority/target/preserve/
                        collision/reverse/options/required caps/required ops>
>
```

Canonical content：

```text
"PGIR6\0"
schema:u16 = 3
header semantic flags:u32
failure_mode:u8
package_count:u32
按 package UTF-8 bytes 排序的 (plan_size:u32 + canonical plan)
```

plan/content generation 分别对上述 bytes 使用 FNV-1a 64。注释、空白、TOML 顺序、brace 原文、
source line、物理 table ID/offset 和 observed device capability 不进入 generation。

RuleId 使用 FNV-1a 64 哈希：

```text
"PGRL6\0" + package bytes + selector canonical bytes + action canonical bytes
```

action canonical bytes 不包含 RuleId 自身。semantic action 完全相同则编译期去重；不同 canonical
bytes 得到相同 RuleId 时编译失败 `RuleIdCollision`，不能依赖 64-bit 碰撞继续排序。

## Canonical 物理编码

encoder 只产生一种合法物理编码，reader 同时验证语义和 canonical form：

- Package、ScopeRef、Selector、Action、Pattern、Class、ExceptRef、String 均按上述顺序严格排序；
- package/selector/action/scope/except/token ranges 连续覆盖对应表；
- Pattern/Class/String 全局去重且没有未引用条目；
- depth/hint/flag/union/generation 字段全部复算相等；
- 不接受重复 row、等价但不同 ID 排列、空洞或多余字符串。

这使相同 semantic policy 产生字节完全相同的 `policy.bin`，便于 golden、缓存和故障诊断。

## Reader 硬上限

Header count 是不可信输入，不是协商配额。v6 reader 固定以下 ceiling，并继续执行主设计的
per-package/bucket/token 预算；任一先到即拒绝：

| 限制 | 值 |
| --- | ---: |
| file_size | 2 MiB |
| package_count | 1024 |
| scope_ref_count | 32768 |
| selector_count | 16384 |
| action_count | 32768 |
| pattern_count | 32768 |
| token_count | 65536 |
| class_count | 16384 |
| except_ref_count | 32768 |
| string_count | 32768 |
| string_bytes | 1 MiB |
| user refs/package | 32 |
| process refs/package | 64 |
| selector/package | 256 |
| action/package | 512 |
| pattern tokens/pattern | 64 |
| pattern tokens/package（base + except） | 4096 |
| except/selector | 8 |
| except refs/package | 256 |
| degenerate pattern/root | 16 |
| degenerate pattern/package | 32 |
| candidate/bucket | 64 |

运行时 matcher 的 4096 transition budget 不来自 Header，也不写入 policy。扩大 ceiling 必须同步
修改 ADR、唯一共享 constants、compiler、reader、golden、fuzz 与性能证据；不能由策略请求。

## Golden vectors

T-12 必须维护独立于 production encoder 的完整 hex fixtures，并让 compiler、core reader、
Zygisk/Provider bootstrap reader、CLI 和 device probe 共同消费。至少冻结两组：

### G6-Literal

- package `org.localsend.localsend_app`，user 0，all processes；
- `allow_legacy_mount=true`；
- literal mount redirect：
  `Download/localsend-source -> Download/localsend-redirect`；
- action priority=0，required capabilities/operations 均为 0；mount backend OR 选择不编码成 action
  requirement；
- 断言 Pattern/Token/Class/Except counts 全为 0，base_pattern sentinel、target StringId、Package
  unions、plan/content generation、CRC、全部 offsets 和 StringId canonical order。

### G6-GlobExcept

- package `org.pathguard.glob_golden`，user 0，all processes，provider enabled；
- root `Pictures`，glob `**/IMG_[0-9]?.jpg`，type=file；
- except `private/**` 与 `**/thumbnail-*/**`，两者都与 base 存在有效交集；
- provider redirect 到 `Download/images`，preserve relative、collision reject、reverse provenance；
- canonical action 的 required capabilities 由动作实际保障范围决定：Provider 前向 path-I/O
  redirect 固定要求 bit 16；只有 query/insert/reverse 视图或 `reverse=provenance` 才额外要求
  bit 17。禁止仅因 Provider 进程名或 PLT Hook 成功就添加 bit 17；
  required operations 固定为 bits 0～7、9～19（mask `0x00000000000ffeff`）；bit 8 hard link
  不属于首版 Provider composite contract；
- 断言 PatternTable 三条 canonical program、token kinds、ASCII class bitmap、ExceptRef order、
  bit 16/17 capability requirements、operation mask v1 的精确 required bits、Action/Selector ranges、
  generations、CRC 和完整 bytes。

每个 fixture 还生成逐字段 manifest；测试不能调用 production encoder生成 expected bytes。

## 拒绝与发布语义

reader 必须至少稳定区分：`UnsupportedPolicyFormat`、`UnsupportedPolicySchema`、
`InvalidHeader`、`ChecksumMismatch`、`LimitExceeded`、`IntegerOverflow`、`NonCanonicalLayout`、
`InvalidReference`、`UnknownEnum`、`UnknownFlags`、`UnknownCapability`、`UnknownOperation`、
`GenerationMismatch` 和 `SemanticConflict`。

遇到 v5、truncated、overflow、overlap、unknown、nonzero reserved、invalid UTF-8、CRC/generation
错误或 trailing bytes 时，拒绝整份候选 policy，保留上一份有效 v6 snapshot。首次启动没有有效
v6 时 fail-open 并报告 unsupported/invalid；不得部分读取、就地修复、猜测字段或回退 v5 reader。

compiler、daemon publisher、所有 runtime reader、CLI/status、fixtures、默认规则和 device probe
必须在同一个协调 change set 切换；任何组件未同步时不得发布模块。

## 后果

- 统一 Selector/Action 与有界 except 获得唯一、完整且可验证的二进制契约；
- PatternTable 消除 base/except token range 重复，并使跨 package 只读 PatternProgram 去重安全；
- schema 3 明确标识语义模型破坏，不与 v5 schema 2 混淆；
- 严格 canonical reader 增加加载期开销，但加载不在文件操作热路径；
- format 6 不为未来 action、NOT token、limits negotiation 或兼容 reader 预留隐式语义。

## 依据

- [Pattern redirect design §4、§5、§8](../08-pattern-redirect-design.md)
- [ADR-0012：Provider capability](0012-provider-capability-split.md)
- [ADR-0013：app-path capability 与 operation mask](0013-app-path-api-capability.md)
- [ADR-0014：Glob v1](0014-glob-language-boundary.md)
- [ADR-0015：有界 selector 反选](0015-bounded-selector-negation.md)
