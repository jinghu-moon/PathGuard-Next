# ADR-0002：冻结 policy format v4 与 canonical IR

> 后续变更：ADR 0006 将格式升级到 v5，并使用 package entry 偏移 42 表示
> `provider_compat`。v4 的其余布局保持不变。

状态：Accepted

日期：2026-07-20

## 背景

schema 2 引入 mount rule、event rule、快照级 content generation 和逐包 plan generation。Host compiler 与 Zygisk reader 必须共享完全一致的字节契约，不能依赖 C++ 对象布局、宿主字节序或输入规则顺序。

## 决策

format v4 是唯一受支持格式。所有整数使用 little-endian，无隐式 padding。共享常量和纯函数位于 `core/include/pathguard/policy_format.h`。

哈希与校验参数冻结如下：

- package index：FNV-1a 32，offset basis `2166136261`，prime `16777619`。
- content generation 与 plan generation：FNV-1a 64，offset basis `14695981039346656037`，prime `1099511628211`。
- payload checksum：CRC-32/IEEE，反射多项式 `0xedb88320`，init `0xffffffff`，xorout `0xffffffff`。
- checksum 覆盖 `[header_size, file_size)`，即 Package、MountRule、EventRule 和 String 四张表的全部字节。

## Canonical IR 编码

canonical plan 依次编码：

```text
"PGPL4\0"
failure_mode:u8
media_compat:u8
allow_legacy_string_bind:u8
package:bytes
users:vector<bytes>
processes:vector<bytes>
mounts:vector<action:u8, depth:u16, flags:u16, visible:bytes, backing:bytes>
events:vector<action:u8, options:u32, source:bytes, target:bytes>
```

`bytes` 编码为 `length:u32 + raw UTF-8 bytes`，vector 编码为 `count:u32 + elements`。不写入字符串终止符。用户列表按数值排序，进程列表按字节排序；mount 按执行阶段、depth、visible、backing、flags 排序，event 按 action、source、target、options 排序。

plan generation 是上述 canonical plan 的 FNV-1a 64。

canonical content 依次编码：

```text
"PGIR4\0"
schema:u16
failure_mode:u8
allow_legacy_string_bind:u8
package_count:u32
按 package UTF-8 字节序排列的 (plan_size:u32 + canonical plan)
```

content generation 是 canonical content 的 FNV-1a 64。源码行号、注释、空白、规则输入顺序和 StringTable 物理 offset 不进入 canonical IR。

## 二进制布局

Header 固定 56 bytes：

| Offset | 字段 | 类型 |
|---:|---|---|
| 0 | magic | u32 |
| 4 | format | u16 |
| 6 | schema | u16 |
| 8 | file_size | u32 |
| 12 | payload_checksum | u32 |
| 16 | content_generation | u64 |
| 24 | package_count | u32 |
| 28 | mount_rule_count | u32 |
| 32 | event_rule_count | u32 |
| 36 | package_offset | u32 |
| 40 | mount_offset | u32 |
| 44 | event_offset | u32 |
| 48 | string_offset | u32 |
| 52 | header_flags | u32 |

`header_flags` 当前只定义 bit 0：`allow_legacy_string_bind`。未知 bit 必须拒绝；
该字段属于 v4 预留标志位的首次使用，不改变表布局。

Package entry 固定 48 bytes，MountRule 和 EventRule entry 均固定 16 bytes。具体字段 offset 由共享头定义并通过 golden vector 锁定。

## Golden vector

固定输入为 `org.localsend.localsend_app`、user `0`、process `*`、`media=hide_denied`，包含 `deny DCIM` 和 `deny Pictures/Nagram`。

- content generation、plan generation 和 payload checksum 由当前 canonical IR
  重新计算；向量中的 `allow_legacy_string_bind` 固定为 `false`。
- file size：190 bytes

完整二进制向量由 `tests/unit/binary_test.cpp` 断言。任何新增 header flag 必须
更新本 ADR 与 golden vector；未知 flag 不能静默接受。

## 后果

- schema 1 和 format v3 不再读取或生成。
- 64 位 generation 碰撞作为已接受的低概率风险，不增加第二套哈希。
- Zygisk 在索引查询前验证完整 payload checksum 和全量 package 排序；hash 命中后仍比较完整包名。
