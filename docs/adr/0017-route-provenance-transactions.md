# ADR-0017：冻结多源路由来源追踪与事务恢复

状态：Accepted

日期：2026-07-29

## 背景

多个 source selector 可以把不同逻辑路径重定向到同一个 target。前向写入只要使用
`collision=reject` 就可以保持确定性，但 target 到 source 的反向映射不再是函数：同一个 target
tail 可能来自 Pictures、Download 或其他 root。

当前 v5 `RestoreAbsolutePath` 在 backing prefix 相同的规则中按 visible path 字典序选择一个
canonical source。该行为没有文件级来源证据，会把真实歧义伪装成唯一结果，并使 Provider query、
MediaStore scan、realpath、rename 和 delete 可能指向错误 source。

format 6 已在 [ADR-0016](0016-policy-format-v6.md) 冻结 `reverse_mode=provenance`。本 ADR 冻结该
模式的所有权、身份、持久化、文件操作协调、崩溃恢复和歧义语义。目标不是承诺跨文件系统和
持久存储之间存在不存在的 ACID 原子性，而是保证任何故障都不会产生伪造的唯一来源。

## 核心不变量

1. 同一 `(storage_root_id, target_relative_path)` 在全部 scope 中同时最多有一个 committed owner；
2. committed record 只有在 target object identity 仍匹配时才能参与反向映射；
3. redirect create/rename 只有在 provenance commit durable 后才能向调用方返回成功；
4. 任何 adapter、query 或恢复逻辑都不能按规则顺序、路径字典序或“第一个匹配”猜 source；
5. store、identity 或 generation 不能证明唯一性时返回 `AmbiguousReverse`，保留真实 target 视图或
   省略虚拟别名；
6. recovery/GC 不因缺少 provenance 自动删除用户文件；不确定对象只能隔离为 unowned/ambiguous；
7. Pattern Engine 和 ActionEvaluator 不读写持久存储。route coordinator 是独立服务。

## 术语与身份

### Route scope

最低可信 scope 为：

```text
IdentityKey = (caller_uid, user_id)
RouteScope  = IdentityKey + optional TrustedAttribution + identity_epoch
```

`TrustedAttribution` 只有在当前 execution domain 能验证 package attribution 时才存在；不得从 policy
包名反推。shared UID 无可信 attribution 时，记录属于明确的 UID 共享 scope。`identity_epoch` 是
daemon 从当前 UID→package set、签名摘要和 user/profile 状态生成的稳定摘要，用于拒绝 UID 重用或
安装身份变化后的旧记录。无法重建相同 epoch 时，旧记录只能 ambiguous，不能自动转移所有权。

### Route semantic identity

持久记录使用 ADR-0016 的 `RuleId` 标识 canonical selector + action，不保存不稳定的 SelectorTable
row index。记录同时保存创建时 `content_generation`/`plan_generation` 供审计。

policy reload 后，daemon 只在当前 scope 中存在语义完全相同的 RuleId，且 target、preserve、
collision、reverse mode 未变时，将记录 rebind 到新 plan generation。RuleId 不存在或语义不一致时
记录进入 `stale_policy`，反向结果为 `AmbiguousReverse`。不因无关 package 的 generation 变化使
有效记录整体失效。

### Target object identity

裸路径、裸 inode 和会随正常写入变化的 ctime 都不是 durable identity。首版接受两级强身份：

1. `FILE_HANDLE`：稳定 volume/fs identity + handle type + 最多 128 bytes file handle；
2. `STATX_BTIME`：稳定 volume identity + inode + `statx` birth time，仅在 probe 证明 backing plane
   对该文件系统稳定提供 btime 时使用。

解析和验证必须在 secure backing directory FD 下进行，并记录 object type。当前 mount ID、dev 和
ctime 只作诊断或同 boot 快速拒绝，不能单独恢复 durable owner。两种强身份都不可用时，
`reverse_mode=provenance` 不准入；不得用 `(dev, ino, ctime)` 冒充跨重启强身份。

## 持久服务边界

route store 由 root daemon 单写。Provider、app-path 和 complete-VFS adapter 通过有界 IPC 调用
coordinator；它们不能各自维护数据库，也不能直接 append 同一文件。daemon unavailable、store
损坏或 IPC 超时时：

- mutation 尚未开始：该 provenance action fail-open，执行原始路径；
- mutation 已开始：按本 ADR 的补偿规则处理，不能再次盲目执行原操作；
- reverse query：返回 `AmbiguousReverse`，不能回退 v5 canonical source。

持久目录使用 daemon 预先打开并验证的 module-owned state root 下 `provenance/v1/`，目录 mode
`0700`、文件 mode `0600`。所有访问使用 dirfd、`O_NOFOLLOW|O_CLOEXEC` 和 secure resolver；路径
不能由规则或调用方指定。模块 disable 保留状态，显式 uninstall 才删除，并由卸载脚本记录结果。

## 存储格式 v1

首版使用自包含 snapshot + append-only WAL，不依赖 Android 私有 SQLite ABI，也不引入常驻第三方
数据库依赖：

```text
provenance/v1/CURRENT
provenance/v1/snapshot.<store_epoch>.bin
provenance/v1/journal.<store_epoch>.bin
```

所有整数 little-endian，所有字符串为 `u32 length + UTF-8 bytes`，不含 NUL。路径必须是已规范化
storage-root-relative path。reader 使用自身硬上限，不接受文件声明扩容。
所有 checksum 使用与 ADR-0016 相同的 CRC-32/IEEE 参数。CURRENT/snapshot/WAL header CRC 覆盖
各自完整 header，frame CRC 覆盖完整 frame，snapshot payload CRC 覆盖 header 后的全部 rows；
计算时 checksum 字段自身视为 0。

`CURRENT` 固定 32 bytes：magic bytes `PGPC` at 0、format u16=1 at 4、header size u16=32 at 6、
store epoch u64 at 8、manifest CRC-32 at 16、flags u32=0 at 20、zero reserved bytes[8] at 24。
CRC 计算时自身字段视为 0。文件名只由已验证的 epoch 生成，manifest 不接受任意路径。启动只
读取 CURRENT 指向的一对文件；同目录其他 epoch 是待 GC 文件，不能按 mtime 猜 active。

### Snapshot header

`snapshot.bin` header 固定 64 bytes：

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | magic | u32，bytes `PGPV` |
| 4 | format | u16，固定 1 |
| 6 | header_size | u16，固定 64 |
| 8 | file_size | u32 |
| 12 | record_count | u32 |
| 16 | last_sequence | u64 |
| 24 | store_epoch | u64 |
| 32 | payload_crc32 | u32 |
| 36 | header_crc32 | u32 |
| 40 | flags | u32，首版必须为 0 |
| 44 | reserved | bytes[20]，必须全 0 |

payload 是按 `(storage_root_id, target_relative_path, RouteScope canonical bytes)` 严格递增的
committed `RouteRecord`。每行编码为 `row_size:u32 + row_crc32:u32 + canonical payload`；重复 key、
重复 object owner、未知 enum/flag、非 canonical 顺序、trailing bytes 或超限均拒绝整个 snapshot。

### WAL frame

每个 journal 先有 32-byte file header：magic bytes `PGPW` at 0、format u16=1 at 4、header size
u16=32 at 6、store epoch u64 at 8、first sequence u64 at 16、header CRC-32 at 24、flags u32=0
at 28。CRC 计算时自身字段视为 0。其后是连续 frame，frame header 固定 48 bytes：

| Offset | 字段 | 类型 |
| ---: | --- | --- |
| 0 | magic | u32，bytes `PGPR` |
| 4 | format | u16，固定 1 |
| 6 | header_size | u16，固定 48 |
| 8 | frame_size | u32 |
| 12 | frame_crc32 | u32；计算时本字段视为 0 |
| 16 | sequence | u64，严格递增 |
| 24 | transaction_id_hi | u64 |
| 32 | transaction_id_lo | u64 |
| 40 | event_type | u8 |
| 41 | operation_kind | u8 |
| 42 | flags | u16，首版必须为 0 |
| 44 | payload_size | u32 |

event type：1=`prepare`、2=`materialized`、3=`commit`、4=`abort`、5=`gc_tombstone`；operation
kind：1=`create`、2=`rename`、3=`delete`。每个 mutation 使用 `getrandom()` 生成的非零 128-bit
transaction ID。`commit` 携带完整最终 RouteRecord，不能只写一个依赖易失内存的“成功”标记。

WAL 每次状态转换写完整 frame 并 `fdatasync/fsync` 后才应答 adapter。恢复只允许丢弃一个
结构上延伸超过 EOF 的截断尾 frame；完整 frame 的 CRC 错误即使位于尾部也视为 corruption。
中间 frame 损坏、sequence 回退/跳跃、未知 transaction 或非法状态转换使 store 进入 `corrupt`，
所有 provenance action unsupported。不得跳过坏 frame 后继续猜测状态。

### RouteRecord

canonical record 至少包含：

```text
RouteScope
storage_root_id
target_relative_path
target_object_identity
object_type
logical_source_path
RuleId
created_content_generation
created_plan_generation
bound_plan_generation
commit_sequence
```

`logical_source_path` 是调用方看到的完整规范化相对路径，不是仅保存 basename；target path 与
source path 都不得包含 storage 根前缀。record 不保存 observed capability、文件内容、URI grant、
裸 FD、进程 PID 或物理 SelectorId。

首版 canonical payload 不使用可跳过 TLV。RouteScope 固定以 `caller_uid:i32`、`user_id:u32`、
`attribution_kind:u8`、zero reserved bytes[3]、`identity_epoch:u64` 开始，随后编码可信 attribution
canonical bytes 和当前 UID/package/signing binding canonical bytes；reader 比较完整 binding，不能
只相信 epoch hash。storage root/volume identity、source/target path 和 file handle 都使用有界
length-prefixed bytes。RuleId、三个 generation/sequence 均为 u64。FILE_HANDLE payload 编码
volume bytes、handle type i32 和 handle bytes；STATX_BTIME 编码 volume bytes、inode u64、birth
seconds i64 和 nanoseconds u32。任一 reserved、length、enum 或 canonical bytes 不合法即拒绝。

### Reader ceilings

| 限制 | 值 |
| --- | ---: |
| snapshot file | 32 MiB |
| WAL file | 64 MiB；达到 32 MiB 或 100000 frames 触发 compaction |
| committed records | 200000 |
| pending transactions | 1024 |
| frame/row payload | 16 KiB |
| file handle bytes | 128 |
| source/target relative path | 各 4096 bytes，单组件仍受 NAME_MAX |

超限使新 prepare 返回 `StoreLimitExceeded`；不能删除仍匹配对象的 committed record 来腾空间。

## 事务 API 与状态机

公共接口仅暴露：

```text
PrepareCreate(candidate) -> TransactionToken | error
Materialize(token, TargetObjectIdentity) -> ok | error
PrepareRename(old_record, new_candidate) -> TransactionToken | error
PrepareDelete(record) -> TransactionToken | error
Commit(token, final_record_or_tombstone) -> ok | error
Abort(token, reason) -> ok | error
ResolveReverse(scope, target_path, current_identity, current_plan) -> unique | ambiguous | none
```

Prepare/Materialize/Commit/Abort 对相同 transaction ID 幂等；不同 payload 重用同一 ID 是
`TransactionConflict`。reservation key 是全局 `(storage_root_id, target_relative_path)`，不按
RouteScope 分桶；与任一 scope 的 committed owner 或 pending transaction 冲突时返回
`EEXIST`/`RouteBusy`，不等待无界锁。RouteScope 是 owner 身份，不是允许多个物理 owner 的命名空间。

namespace mutation 后、provenance COMMIT 前必须 fsync 受影响的 backing parent directory；rename
跨目录时两边都要同步。该要求只保证名称/owner 协调，不擅自改变应用对文件内容 fsync 的语义。
backing parent fsync 无法通过 self-test 时 provenance mutation 不准入。

### Create

顺序固定：

```text
durable PREPARE（预留 target，保存完整 source candidate）
  -> filesystem create with no-replace
  -> 从已打开 FD 取得强 object identity
  -> durable MATERIALIZED
  -> durable COMMIT
  -> 向调用方返回成功/FD
```

create 失败时 durable ABORT。PREPARE 之前的 store/IPC 失败允许 fail-open 到原路径。文件创建后、
COMMIT 前失败时，coordinator 只在 path 和 object identity 同时匹配时补偿 unlink；补偿成功后 abort，
再由 adapter 根据原操作是否仍可安全重放决定 fail-open。补偿失败时返回 `EIO`/结构化
`ProvenanceCommitFailed`，保留对象为 unowned/ambiguous，绝不返回 redirect 成功。

普通 open existing 不创建 owner：只有已验证 committed record 或 static-unique route 可以反向
打开。target 已存在但没有匹配 owner 时，create 仍按 collision reject，不能“收养”外部文件。

### Rename

rename 使用一个 transaction 同时锁定 old/new route key：

```text
durable PREPARE_RENAME（old record/identity + new candidate/reservation）
  -> filesystem rename no-replace
  -> 验证 new path 的 object identity
  -> durable MATERIALIZED
  -> durable COMMIT（原子替换 old record 为 new record）
```

同一 selector 内 rename 更新 source/target relative path；跨 selector rename 按同一个 snapshot 对
两个 operand 决策，并把 owner 变更为 destination RuleId。移出 redirect domain 时 commit tombstone；
移入时建立新 owner。首版对涉及 provenance 的 `RENAME_EXCHANGE`、覆盖式 rename 和 hard link 返回
unsupported，不把一个 inode 静默绑定多个 source。

提交失败时仅在 identity 匹配时尝试 rename 回 old path。补偿失败进入 ambiguous/degraded，不能
谎报原路径仍存在。

### Delete

delete 顺序固定：

```text
durable PREPARE_DELETE（record + expected identity）
  -> filesystem unlink/rmdir
  -> durable COMMIT tombstone
  -> 返回 filesystem 结果
```

unlink/rmdir 失败则 abort 并保留 committed owner。文件系统删除成功而 tombstone commit 失败时，
不能恢复已经删除的对象；返回实际 syscall 结果，同时将 adapter/store 标记 degraded。recovery
看到 expected path 缺失后完成 tombstone。打开中的 FD 继续使用 open/create 时固定的 route handle，
但该 pathname 不再产生反向别名。

## 崩溃恢复

daemon 启动时先验证 snapshot，再严格 replay WAL；恢复完成前 provenance action 不准入。

| Pending 状态 | 文件系统事实 | 恢复结果 |
| --- | --- | --- |
| create prepared，target 不存在 | 无 mutation | abort |
| create prepared，target 存在但无 materialized identity | 无法证明 owner | 保留文件为 unowned/ambiguous，abort reservation |
| create materialized，identity 匹配 | mutation 已完成 | commit record |
| create materialized，target 缺失 | mutation 未保留 | abort |
| rename prepared，old identity 仍在 old、new 不存在 | rename 未发生 | abort，保留 old owner |
| rename prepared，old 缺失、expected old identity 只在 new | rename 已发生 | commit new owner |
| rename materialized，identity 只在 new | rename 已发生 | commit new owner |
| delete prepared，old identity 仍存在 | delete 未发生 | abort，保留 owner |
| delete prepared，path 缺失 | delete 已发生 | commit tombstone |
| 任一状态出现两边实体、identity mismatch 或无法安全解析 | 事实不唯一 | quarantine + `AmbiguousReverse` |

committed record 在每次反向使用前验证 scope、identity epoch、RuleId rebind、target path 和 object
identity。任一不匹配都不能返回 source。恢复不会用“文件存在”替代 object identity，也不会因
pending 超时直接删除文件。

## Snapshot compaction 与 GC

compaction 由单 writer 串行化：先拒绝/短暂阻塞新 prepare 并完成已有 transaction，再冻结一个
committed view 和 `last_sequence` → 写新 epoch canonical
snapshot 与空 WAL → fsync 两个文件和目录 → 写并 fsync temporary CURRENT → atomic rename 替换
CURRENT → 再 fsync 目录。CURRENT 切换前旧 epoch 始终 active；切换后新 snapshot/WAL 已完整
durable。旧 epoch 只能在 manifest 切换和再次目录 fsync 成功后异步删除。任一步失败保留旧 CURRENT，
不能按文件 mtime 或“编号最大”选择另一套 epoch。

GC 规则：

- committed record 不按年龄淘汰；
- target 缺失或 strong identity 不匹配时写 durable tombstone，再从 active map 移除；
- `stale_policy`/`stale_identity` 先进入 quarantine；默认保留 30 天供诊断，之后只删除 metadata；
- unowned physical file 永不自动删除；
- abandoned pending transaction 只按恢复矩阵处理，不靠 wall-clock 猜测；
- WAL tombstone 在进入 durable snapshot 后才能通过 compaction 移除；
- time 只用于诊断/保留期，不参与 owner 正确性判断，避免时钟回拨改变语义。

## Reverse resolution 与 Provider/FUSE

`ResolveReverse` 只有以下结果：

- `unique`：一个 committed record 同时通过 scope、epoch、RuleId/current plan、path 和 object identity；
- `none`：静态可证明无 route、target 不属于任何 candidate target，或唯一 committed owner 属于
  另一个不共享的 RouteScope；不得向当前 caller 泄露其 logical source；
- `ambiguous`：缺 record、多 record、pending/quarantine、identity/generation/store 损坏或无法验证。

Provider query/document ID/open、MediaStore insert/scan 和 complete-VFS d_path/readdir 必须消费同一
结果。`ambiguous` 不等于 deny：对外保留真实 target path 或省略虚拟 source alias，并返回
`DecisionReason::AmbiguousReverse` 诊断；不能选择一个 source。document ID 是 Provider opaque ID，
只作为 adapter 输入/输出，不作为 provenance 主键。

ADR-0012 bit 17 只有在 query/insert/path I/O 和本动作要求的 provenance prepare/commit/recovery
全部通过时才能置位相应 reverse operation substatus。app-path 的纯前向动作可以使用
`reverse_mode=none`，不因 store 不可用冒充 Provider 完整语义。

policy 中所有 `reverse_mode=provenance` action 的 required operation mask 必须包含 ADR-0016
operation bit 19=`reverse mapping`。runtime 只有在 store recovery 完成、strong identity probe 与
coordinator self-test 通过时才把该 bit 放入对应 domain 的 observed operations。

## 故障与诊断

稳定 reason 至少包括：

```text
AmbiguousReverse
ProvenanceUnavailable
ProvenanceCorrupt
ProvenanceCommitFailed
RouteBusy
RouteIdentityMismatch
RoutePolicyStale
StoreLimitExceeded
```

审计记录 transaction ID、operation、RuleId、scope hash、target/source hash、sequence、store epoch、
filesystem errno 和补偿结果；普通 reverse unique/no-route 不做同步日志。路径明文只进入 root-only
限量 debug，不写公共 logcat。

## 否决方案

### 继续使用 canonical visible source

否决。规则顺序或字典序不是文件来源证据，会把多对一关系伪装成可逆函数。

### 只以 target path 为 key

否决。删除后重建、inode 重用或外部替换会继承旧 owner。path 必须与强 object identity 同时验证。

### 使用 `(dev, inode, ctime)` 作为 durable identity

否决。ctime 会因正常写入和 metadata mutation 改变，也不能稳定防止跨重启 inode 重用。只能作为
诊断或同 boot 快速拒绝。

### 每个 adapter 自建来源表

否决。Provider、app-path 和 complete VFS 会产生不同 owner、恢复顺序和冲突结果。daemon 必须是
单 writer，adapter 只做 IPC/client 和文件操作翻译。

### 只在 close-write 后异步记录

否决。调用方可能在记录 durable 前已经收到成功，崩溃后留下“成功文件无来源”的窗口；
fanotify/FileObserver 也可能丢事件。异步事件只可用于审计/GC 提示。

### store 失败后仍向 target 写入

否决。这样会制造无法安全反向展示的新文件。mutation 前失败应 fail-open 到原路径；mutation 后
失败必须补偿或显式报错/ambiguous。

### 自动删除恢复时的 unowned 文件

否决。无法证明文件由失败 transaction 创建时，删除可能造成用户数据丢失。正确选择是保留真实
target 对象并拒绝伪造 source。

## 验证门槛

- 两个 source 指向同一 target 的不同名成功、同名 `EEXIST` 和唯一反向来源；
- prepare/materialized/commit/abort 幂等、payload 冲突和 1024 pending/limits；
- create/rename/delete 每个 frame、filesystem syscall、fsync、IPC reply 前后的 crash injection；
- truncated tail 可恢复；中间 CRC/sequence/enum 损坏使 store unsupported；
- daemon、Provider、MediaProvider 重启和设备 reboot 后 committed owner 保持；
- policy 无关变更可按 RuleId rebind；RuleId/target/scope/identity epoch 变化进入 stale/ambiguous；
- file handle 与 statx btime identity probe、外部替换、inode 重用、symlink 和跨 storage root；
- shared UID 无 attribution、可信 attribution、UID 重用和多 user/work profile；
- rename old/new、跨 selector、移入/移出、补偿失败；exchange/overwrite/hard link 稳定 unsupported；
- delete 打开文件、rmdir、commit 失败和 recovery tombstone；
- snapshot compaction 的 file/rename/directory fsync 故障与旧 epoch 回退；
- store corrupt/unavailable/limit 时不写 target、不猜 source，static-unique route 不受影响；
- 代码搜索确认 v5 `RestoreAbsolutePath` canonical fallback 不再进入生产路径。

## 后果

- C4 的多源前向与反向语义闭合，不再依赖规则声明顺序；
- Provider/FUSE 获得同一个可恢复 route owner 服务，Pattern Engine 保持纯函数；
- 每次 provenance mutation 增加同步 WAL 和 IPC 成本，这是“成功返回后来源可恢复”的必要代价；
- 跨文件系统与持久 store 仍不存在真正原子提交，极端 crash 可以留下 unowned 文件，但只能导致
  显式 ambiguous，不能导致错误唯一映射或自动数据删除；
- strong object identity 不可用的设备会把 provenance mode 标为 unsupported，而不是降低安全语义。

## 依据

- [Pattern redirect design §6.3、§7.4～§7.6](../08-pattern-redirect-design.md)
- [ADR-0012：Provider capability](0012-provider-capability-split.md)
- [ADR-0016：policy format 6](0016-policy-format-v6.md)
- [Android DocumentsProvider](https://developer.android.com/reference/android/provider/DocumentsProvider)
- [Linux open(2)](https://man7.org/linux/man-pages/man2/open.2.html)
- [Linux rename(2)](https://man7.org/linux/man-pages/man2/rename.2.html)
- [Linux fsync(2)](https://man7.org/linux/man-pages/man2/fsync.2.html)
- [Linux name_to_handle_at(2)](https://man7.org/linux/man-pages/man2/open_by_handle_at.2.html)
- [Linux statx(2)](https://man7.org/linux/man-pages/man2/statx.2.html)
