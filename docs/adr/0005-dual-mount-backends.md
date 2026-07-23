# ADR-0005：严格与兼容挂载双后端

状态：Accepted

日期：2026-07-20

## 背景

Alioth Android 13 / Linux 4.19.157 真机证明：

- 逐组件 FD walk 可安全解析目录；
- classic bind mount 可以消费普通路径字符串；
- classic mount 不能消费 `/proc/self/fd/<n>`；
- `open_tree()` / `move_mount()` 不可用。

MaterialCleaner 与 Storage Redirect X 同时证明，旧内核可以在应用 mount
namespace 中通过普通字符串 bind 实现真实 VFS 重定向。它不能提供 FD 驱动
mount 的 check-use 一致性，但不应与“完全不支持重定向”等同。

## 决策

规则编译、挂载顺序、namespace mutation lease、取消、提交和回滚只保留一套。
运行时根据独立 capability probe 选择挂载后端，不根据 `uname` 推断：

```text
open_tree_move_mount -> strict_fd/open_tree
proc_fd_mount         -> strict_fd/classic
string_bind_mount     -> legacy_namespace_bind（必须显式授权）
otherwise             -> unsupported
```

该顺序是候选优先级，不是逐条规则的降级链。后端选择以整个 `ProcessPlan` 为单位，
输入至少包含计划要求的 action mask、目标 namespace topology、capability snapshot
和用户授权；被选后端必须支持该事务中的全部同步动作。选择在取得 namespace
mutation lease 前完成，同一事务不得混用 strict 与 legacy。

strict 后端被选中后，运行时 mount、验证或回滚失败必须按统一事务协议结束；即使
用户允许 legacy，也不得在同一次启动中从 strict 失败自动重试字符串 bind。只有
probe 明确证明 strict 不可用，且 legacy capability、action mask 与用户授权同时满足
时，才可在事务开始前选择 legacy。

### strict_fd

- source 与 target 由 `openat2()` 或逐组件 FD walk 固定。
- 最终正向 mount 必须消费固定 FD。
- `open_tree_move_mount` 优先于 `proc_fd_mount`。
- `proc_fd_mount` 必须同时证明 source FD 和 target FD 都被最终 mount 消费；只支持
  `/proc/self/fd/<source>` 不足以构成 strict capability。
- source 可以在事务预检时固定；target 必须在父级 mount 完成后的当前 VFS 视图中
  just-in-time 解析，禁止在第一条 mount 前一次性打开全部子 target。
- mount 后仍校验 mountinfo 与 source/target 身份。该校验从 target 挂载点的
  mountinfo 行完成：确认存在非零 mount ID、`root` 字段等于固定 source 在其文件系统内
  的规范路径、filesystem 与设备符合计划。strict 的最终 mount 已消费固定 source FD，
  source 身份由内核构造保证，因此**不需要**对 target 路径做一次额外的
  `stat()`/`statx()` 来比对 dev/ino——在 FUSE 存储上该路径 `stat` 会触发一次
  与 daemon 的 getattr 往返（实测约 45ms），而它证明的身份已被"消费固定 FD + mountinfo
  行"覆盖。strict 也不做 before/after 行数 delta（见 legacy）。

### legacy_namespace_bind

- 默认关闭，只能由全局 `allow_legacy_string_bind = true` 显式开启。
- capability probe 必须在一次性私有 namespace 中证明普通字符串 bind、验证和
  回滚完整成功。
- 用户配置路径先规范化为内部相对路径；mount syscall 不得消费配置原文。
- source/target 在 mount 前仍走安全 FD resolver，并记录设备、inode 与 mount ID。
- mount 前立即复验字符串路径与已固定对象一致，mount 后解析 mountinfo 并复验。
- 每次 mount 前后比较 mountinfo delta；必须恰好出现一个预期 mount，且 mountpoint、
  parent mount ID、root、filesystem 与 major:minor 符合计划，不允许出现额外 mount。
- 每个成功 mount 记录 canonical target、新 mount ID 和操作前后身份，失败时逆序
  回滚。旧内核无法提供 `STATX_MNT_ID` 时，mount ID 来自 mountinfo；该 statx 字段
  是可选增强，不能成为 legacy 的硬依赖。
- 回滚前必须确认记录的 mount 仍是 canonical target 的最顶层 mount；无法确认时
  禁止盲目 `umount2(path)`。
- 回滚对象无法确认时，失败作用域是 mount namespace，不是单个 PID。helper 必须
  关闭其持有的 namespace/mount FD，并使执行 `setns` 的 worker 返回原 namespace
  或退出，再终止该 namespace 的全部成员；不得让任何成员带部分挂载继续运行。
- legacy 首版只允许进入成员关系可确认、应用代码尚未执行的 disposable app
  namespace。system process、共享 namespace 或成员关系不明确时保持 unsupported。
- 产品状态的 backend/security 维度必须报告 `legacy_string`/`legacy_toctou`，不得与
  strict backend 或 `fd_pinned` 合并。

字符串 mount 的 TOCTOU 风险只能缩小，不能消除。文档和 UI 不得把
`legacy_namespace_bind` 描述为 FD-safe。

## 首版范围

- `strict_fd` 只开放已通过对应 action matrix 的同步动作；R1 首个共同基线为目录
  `redirect`。
- `legacy_namespace_bind` 首版只开放选择性目录 `redirect`。
- legacy `deny` 必须在 deny anchor 方案真机验证后单独解锁。
- legacy `isolate + allow` 必须在 source plane、嵌套 target 和全量回滚矩阵完成后
  单独解锁。
- provider、MediaStore、SAF 与 Photo Picker 兼容性不由任一 mount 后端隐式声明。

`deny` 在计划层编译为“将固定的 deny anchor bind 到 target”，不作为后端独有的
mount primitive。deny anchor 只承诺禁止遍历、列举和修改目录内容；bind mount
本身不保证 `stat(deny_mountpoint)` 失败，因此产品不得宣称隐藏挂载点元数据。

## Source plane

`/data/media/<user>` 保留为规范存储身份与 Root 控制面。实际 bind source 由
topology snapshot 选择，优先使用已验证的真实 storage/FUSE source plane，例如
`/mnt/user/<user>/emulated/<user>`。未证明访问控制和文件语义一致时，不得把
`/data/media/<user>` 直接作为所有 ROM 的唯一 bind source。

topology 使用两阶段验证：daemon namespace 只发现候选 source plane；companion 在
目标 app namespace 中重新验证 mount ID、parent ID、root、filesystem、major:minor
以及它与 `/storage/emulated/<user>` 的对应关系。最终 source identity 与
`topology_generation` 一同进入事务，存储重挂载或 generation 改变后不得继续使用
旧 probe 结果。

## 传播与失败语义

- 进入目标 namespace 后先检查 `/storage` propagation；不满足隔离要求时必须成功
  设置 `MS_REC | MS_PRIVATE`，旧实现的“记录错误后继续”不再允许。
- 两个后端共享 ADR-0003 的 mutation lease。取得 lease 后失败必须先完成可逆
  mutation 回滚；只有未产生 namespace taint 时才允许目标进程继续。
- 预检失败且 namespace 从未变更时保持 fail-open。
- 回滚验证失败属于不可恢复的 namespace 状态，必须终止该 namespace 的全部成员。

传播属性不是普通可逆 mount。helper 必须在 lease 前通过 mountinfo 判断 `/storage`
是否已经满足隔离要求；若取得 lease 后执行 `MS_PRIVATE` 并改变了原 propagation，
后续失败即使卸载了全部 PathGuard mount，也必须按 ADR-0003 发布
`namespace_tainted` 并终止该 namespace 的全部成员，不能报告普通
`rollback_complete`。

## Backend 接口边界

后端只负责把准备好的 source 挂到当前步骤的 target，以及验证和回滚该次 mutation；
规则编译、父子覆盖、动作顺序、lease、取消和事务状态不进入后端：

```cpp
class MountBackend {
public:
    virtual Result<AppliedMount, MountError>
    Apply(const PreparedMountStep& step) = 0;

    virtual Result<void, MountError>
    Verify(const AppliedMount& mount) = 0;

    virtual Result<void, MountError>
    Rollback(const AppliedMount& mount) = 0;
};
```

公共模型必须区分 `PinnedIdentity` 与 `CanonicalLocator`：前者表示 resolver 固定并
持有的对象，后者只用于 legacy 由规范相对路径生成的内部字符串。不得用同一个
`ResolvedSource`/`ResolvedTarget` 类型暗示 legacy 的最终 mount 也消费了固定 FD。
所有错误返回结构化 `MountError`，至少包含阶段、backend、operation ID、errno、
预期/实际 mount identity 和是否导致 namespace taint；禁止只返回裸 `int`。

## Capability 协议

能力协议至少拆分为：

```text
resolver:
  openat2
  component_fd_walk

attach:
  open_tree_move_mount
  proc_fd_mount
  string_bind_mount

verification:
  mountinfo_identity
  statx_mnt_id_optional

actions:
  redirect
  deny_anchor
  isolate_root
  restore
```

`open_tree_move_mount` probe 必须覆盖实际使用的 empty-path flags、detached mount
创建、target attach、验证和回滚；不能只根据 syscall 返回非 `ENOSYS` 判定。
`proc_fd_mount` 必须完成 source/target 双 FD 的实际 bind。

`string_bind_mount` 只表示在一次性私有 namespace 中，使用与实际 companion 相同的
UID、SELinux domain 和权限环境完成字符串 bind、mountinfo delta 验证和精确回滚；
不表示用户已经允许 legacy，也不表示所有 action 已解锁。capability、action mask
与用户授权必须同时满足。

probe 结果至少绑定 boot identity、SELinux enforcing/policy 环境和
`topology_generation`。daemon 重启、存储重挂载或 topology generation 改变时必须
重新探测。生产配置只保留 `allow_legacy_string_bind = false`；强制具体 backend
仅作为测试/debug override，不进入规则语义。

## 产品状态

状态按正交维度报告，不使用单一扁平枚举混合安全等级和事务结果：

```text
enforcement: inactive | active | pending_restart | failed
backend: none | strict_open_tree | strict_procfd | legacy_string
transaction: complete | failed_preflight | rollback_complete | namespace_tainted
security: fd_pinned | legacy_toctou
reason: capability_missing | legacy_not_authorized | unsupported_action | ...
```

`unsupported` 是选择结果，`failure = open` 是该结果的处理策略；两者不得视为同一
状态。所有状态同时报告 snapshot/plan/topology generation。

## 后果

- strict capability 完整的设备自动获得严格 FD 后端，不需要用户选择具体 syscall。
- Alioth 等仅具备 legacy capability 的设备可以在明确风险提示后使用真实 VFS
  选择性 redirect。
- 同一份规则在两个后端具有相同目录语义，但产品必须暴露不同安全等级。
- legacy 后端需要额外的 mountinfo 身份验证、PID start time 复验和异常终止路径。
