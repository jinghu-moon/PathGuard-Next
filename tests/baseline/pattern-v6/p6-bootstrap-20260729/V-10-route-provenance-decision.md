# V-10 route provenance 决策门

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-10` |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| After commit | N/A（架构决策，尚未修改生产 route mapper） |
| Branch | `feature/pattern-redirect-v6` |
| Classification | `planned_break`（删除 v5 canonical visible source fallback） |
| Decision | ADR-0017 `Accepted` |
| Reviewer conclusion | 多源反向只接受 committed provenance + strong identity；无法证明唯一时 `AmbiguousReverse` |

## Before 基线

- 当前 `zygisk/src/provider_path_mapper.cpp::RestoreAbsolutePath` 对 backing prefix 做最长匹配；多个
  visible source 共享相同 backing 时，再按 visible path 字典序选一个 source；
- `tests/unit/provider_path_mapper_test.cpp` 当前把 Pictures 和 Download 映射到同一 target 后，断言
  reverse 固定返回 `Download/localsend-source`；该结果是实现选择，不是文件来源证据；
- V-03 已证明两个 source 可把不同文件前向写入同一 target，且同名目录的第二次创建失败；
- V-03 没有把现有 canonical reverse 宣称为正确，C4 reverse 仍是明确缺口；
- 本任务只改 ADR/设计/TDD 文字，不修改 mapper、模块或手机状态，C1～C5 当前行为 unchanged。

## 冻结结果

| 项目 | 决策 |
| --- | --- |
| Owner key | 全局 `(storage_root_id,target_relative_path)` 只能有一个 owner；RouteScope 是 owner 身份 |
| RouteScope | `(caller_uid,user_id)` + 可选可信 attribution + 完整 identity binding/epoch |
| Semantic ID | 持久化 RuleId，不保存物理 SelectorId；reload 只对语义相同 RuleId rebind |
| Object ID | FILE_HANDLE 优先；fallback 仅 stable volume + inode + statx btime |
| 持久服务 | root daemon 单写；Provider/app-path/complete-VFS 走有界 IPC |
| Create | durable prepare → no-replace create → materialized identity → durable commit → success/FD |
| Rename | 同 transaction 锁 old/new，no-replace rename，commit 原子替换 owner |
| Delete | durable prepare → unlink/rmdir → durable tombstone |
| Store | epoch snapshot + append-only WAL + atomic CURRENT manifest |
| Recovery | 恢复为 committed、absent 或 unowned/ambiguous；不猜 source、不自动删文件 |
| GC | committed 不按年龄淘汰；缺失/mismatch 写 tombstone；quarantine 只清 metadata |
| Admission | provenance action 要求 operation bit 19；store/identity/coordinator self-test 必须通过 |

## 存储格式验证

ADR-0017 冻结：

```text
CURRENT header       32 bytes, contiguous, 8-byte aligned
snapshot header      64 bytes, contiguous, 8-byte aligned
WAL file header      32 bytes, contiguous, 8-byte aligned
WAL frame header     48 bytes, contiguous, 8-byte aligned
```

CURRENT 只保存 `store_epoch`，文件名由验证后的 epoch 构造；新 snapshot/WAL 都 fsync 后才 atomic
replace CURRENT。崩溃前旧 epoch 始终 active，切换后新 epoch 已完整 durable，不按 mtime 或最大编号
猜 active 文件。

所有 checksum 使用 CRC-32/IEEE。snapshot/WAL/frame 有固定 header、zero reserved、严格 sequence、
canonical rows 和 reader ceilings。仅结构上延伸超过 EOF 的尾 frame 可丢弃；完整 frame CRC 错误
即使在尾部也使 store corrupt/unsupported。

## 崩溃与故障语义

- prepare 前 store/IPC 失败：mutation 未开始，动作 fail-open 到原路径；
- create 后 commit 失败：仅在 target path + strong identity 同时匹配时补偿 unlink；补偿失败保留
  unowned 文件并返回错误，不谎报 redirect 成功；
- rename 后 commit 失败：仅按 identity 补偿 rename；失败后 ambiguous/degraded；
- delete 后 tombstone 失败：不能恢复已删除对象；返回实际 syscall 结果，recovery 依据 path 缺失
  完成 tombstone；
- prepared create 在 crash 后发现未知 target：不“收养”、不删除，保留真实 target 并 ambiguous；
- committed record 每次 reverse 前复核 scope/binding、RuleId、path 和 object identity；任一不匹配
  都不得返回 source。

这不是跨文件系统与 journal 的虚假 ACID 承诺。允许的最坏结果是 preserved unowned object +
`AmbiguousReverse`，不允许错误唯一映射或自动数据删除。

## 计划内破坏与同步

- v6 删除 `RestoreAbsolutePath` 的字典序 canonical fallback；旧 unit expected 在 V-32/T-21/T-22
  前后对比中改为 provenance unique 或 `AmbiguousReverse`；
- ADR-0012 补充 provenance 对 Provider bit 17/reverse substatus 的准入条件；
- ADR-0016 的 `reverse=provenance` 明确只表示 policy intent，不表示 store 已 active；
- 主设计 6.3/P3 改为引用 ADR-0017，删除 `(dev,ino,ctime)` durable fallback；
- TDD I-21 改为 file handle/statx btime 与 CURRENT epoch，不再指导实现裸 device/inode key。

## After 对比与回归

- 当前生产行为：unchanged；尚未删除 mapper fallback，也未引入 WAL/IPC；
- C1～C3/C5：沿用 V-03/V-04，无新差异；
- C4 前向/collision：unchanged；reverse 的目标行为已冻结但尚未实现；
- Host Release CTest：`52/52` 通过，0 failed，总耗时 10.55 秒；
- storage header arithmetic、local links、stale provenance terms 和 `git diff --check` 全部通过；
- unexpected regression：0。

## 证据

| 路径 | SHA-256 |
| --- | --- |
| `docs/adr/0017-route-provenance-transactions.md` | `3633F19367407B77E6F0BF1E080797686DB2E5F49BD739938035A925C0626835` |
| `docs/08-pattern-redirect-design.md` | `5C304BF479E8A4BFAB8D530D652CA59CE96B883C50532B14CCDCB76DECA3C52D` |
| `docs/09-pattern-redirect-tdd-task-list.md` | `E04BE3982EDCF4D2147169E04A19B4D9B74BD4B23C2F1FAE2344C12218F3ED58` |
| `build/pattern-v6-v02-release/v10-ctest.xml` | `4804CAE79C328222795FBA596738A2BAE9339B2B4F7FBAFE9B800EBF6510843A` |

## 验收结论

- ADR-0017 状态为 Accepted；
- prepare/materialized/commit/abort、identity、generation/rebind、store format、rename/delete、
  crash recovery、compaction 和 GC 均有确定语义；
- `AmbiguousReverse` 与 fail-open/errno 边界已冻结；
- v5 canonical source fallback 明确为计划内删除，不能作为恢复路径；
- P3/T-21/T-22 不再依赖未定义行为；
- 当前核心行为回归通过，无非预期副作用；
- V-10 判定 `complete`。
