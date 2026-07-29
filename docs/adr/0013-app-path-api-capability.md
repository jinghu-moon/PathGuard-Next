# ADR-0013：冻结 app-path adapter 准入能力与操作掩码

状态：Accepted

日期：2026-07-29

## 背景

[ADR-0012](0012-provider-capability-split.md) 的动作准入矩阵引用了 `app_path_api`
capability/action mask，却没有分配稳定 bit 或指定其他正式协议。这使最先进入 P2 的 app-path
glob redirect 仍需在实现时临时决定如何准入。

app-path adapter 在目标应用进程内拦截已覆盖的 Java/libc 路径入口。它不经过 Binder，也不
等同于 Provider/FUSE。其可用性包含两个正交事实：

1. 当前进程中的 adapter 是否已安全安装并通过基线语义自检；
2. 该规则所需的每个路径操作是否都在 observed operation mask 中。

只定义一个 bit 会掩盖 open/rename/readdir 等操作覆盖差异；只定义 operation mask 又无法表达
adapter 初始化、Hook 事务或生命周期整体失败。因此需要“稳定 adapter admission bit + versioned
operation mask”两层模型。

## 与 ADR-0004、ADR-0012 的关系

本 ADR 追加冻结 [ADR-0004](0004-capability-bitset.md) 的 64 位协议，在
[ADR-0012](0012-provider-capability-split.md) 已使用 bit 16～18 后分配 bit 19。bit 0～4、
8～11、16～18 的含义不变，bit 5～7 和 12～15 继续保留。

ADR-0004 原先所称“primitive”由 ADR-0012/0013 收紧为：稳定 bit 可以描述可独立探测的底层
primitive，也可以描述具有明确失败边界的 adapter semantic baseline；具体路径操作覆盖始终由
backend operation mask 表达，不能继续为每个 libc API 分配稳定 bit。

## 决策

### 1. 稳定能力位

| Bit | 名称 | 置位条件 | 不代表 |
|---:|---|---|---|
| 19 | `app_path_adapter` | 当前目标进程的 app-path adapter 已完成受控 Hook 事务、生命周期校验和基线语义 self-test，并可安全调用统一 Pattern Engine/Action Evaluator | Provider/SAF、FUSE、静态链接、直接 syscall，或 operation mask 中未设置的路径操作可用 |

未来 C++ 常量名称固定为 `kCapabilityAppPathAdapter`。禁止使用容易被理解为“全部 API 可用”的
`kCapabilityAppPathApi`。bit 19 按进程身份观测，不能作为设备级全局事实缓存。

### 2. Policy requirement

每条由 app-path 执行的动态动作在 Action IR/`policy.bin` 中编码：

```text
execution_domain = app_path
required_capabilities includes app_path_adapter
required_operations = bitmask
```

`execution_domain` 是动作的执行位置，不是 fallback 优先级。`required_operations` 由动作种类
和规则语义在编译期计算，reader 必须拒绝未知 operation bit。

首版 operation mask 至少分别表达：

- open/create；
- stat/access；
- directory open/iteration；
- mkdir；
- rename/link；
- unlink/rmdir；
- canonical/readlink；
- metadata mutation；
- truncate；
- watch。

具体固定数值随 policy format 6 的共享格式头和 golden vectors 一次冻结，不能由每个 adapter
自行编号。

### 3. Runtime observation

每个目标进程独立发布：

```text
observed_capabilities includes app_path_adapter
adapter_state = active | inactive | unsupported | degraded
observed_operations = bitmask
probe_reason / errno
capability_generation
process_identity = pid + process_start_time
```

仅命中符号、注册 Hook 或提交 GOT/PLT 修改，不足以设置 bit 19。adapter 必须完成安装事务、
驻留生命周期校验并通过最小 self-test；各操作是否可用由 observed mask 单独表达。不同应用
进程不得共享 observed state。

### 4. Admission

app-path 动作仅在以下条件同时成立时 active：

```text
execution_domain == app_path
app_path_adapter in observed_capabilities
adapter_state == active
required_operations subset_of observed_operations
plan_generation and capability_generation are current
```

bit 19 缺失表示 adapter 整体不可准入；缺少 required operation 表示该动作不可准入。两者都
返回 `unsupported`，但分别报告 missing capability 和 missing operation mask。不得因为 open
可用就推断 rename/readdir 可用，也不得静默切换到 Provider、FUSE 或 mount。

### 5. 产品语义边界

- app-path 只承诺当前目标进程内、observed mask 覆盖的入口；
- 不承诺静态链接、直接 syscall、未 Hook native library 或系统代写进程；
- app-path glob redirect 可以独立 active，但不得标记为 Provider 或 `complete`；
- app-path adapter 不能作为 `enforcement = "provider"` 或 `"complete"` glob deny 的依据；
- 现有字面量目录 redirect 继续由 mount backend 执行，不因本 ADR 改用 app-path。

## 否决方案

### 只分配 bit 19，不使用 operation mask

否决。这会重新制造一个 bool 掩盖多个故障域的问题，无法安全判断依赖 rename/readdir 的规则。

### 不分配稳定 bit，只使用 operation mask

否决。operation mask 不能表达 adapter 初始化、Hook 事务、模块驻留或生命周期整体失败，与
ADR-0012 已采用的“语义基线 + action mask”模型也不一致。

### 为每个 libc 操作分配稳定 capability bit

否决。稳定 bitset 会随 API 数量持续膨胀。操作集合属于 versioned operation mask。

### 不记录执行域，仅靠可用能力选择后端

否决。这会形成隐式 fallback。同一操作不能因为某个后端碰巧可用就改变规则的保障范围。

## 兼容与迁移

- format 1 字面量 redirect 保持 mount 语义，不生成 app-path requirement；
- format 2 glob 动作只有显式编译到 `execution_domain = app_path` 时才要求 bit 19；
- `policy.bin` 保存 required capability 和 operation mask，不保存设备/进程 observed 状态；
- observed state 只存在于按进程身份和 generation 绑定的 runtime capability snapshot；
- reader 遇到未知 execution domain、required capability 或 operation bit 时拒绝新 policy，并继续
  使用上一份有效 snapshot。

## 验证门槛

- adapter 初始化/Hook 事务/self-test 任一失败时 bit 19 不得设置；
- 对每个 operation bit 分别注入 Hook 缺失，只拒绝依赖该 operation 的动作；
- 同设备两个应用具有不同 observed mask 时不得串用状态；
- Hook 已提交但 self-test 失败时模块保持驻留透传，adapter 不得 active；
- direct syscall/static linking 用例不得被误报为已覆盖；
- status/explain 同时显示 execution domain、required/observed capabilities 和 operation masks。

## 后果

- ADR-0012 的 app-path 悬空引用被关闭，bit 19 获得明确但有限的语义；
- app-path 整体失败和单项操作缺失可被区分，不会重新制造“大而全 API 可用”的假设；
- P1 必须在 policy format 6 中冻结共享 operation mask，P2 实现进程级 probe 和 admission；
- 后续新增路径 API 通常只扩展 versioned operation mask，不再消耗 primitive bit。

## 依据

- [ADR-0004：capability bitset](0004-capability-bitset.md)
- [ADR-0012：Provider 能力拆分](0012-provider-capability-split.md)
- [Pattern redirect design §5、§7.2](../08-pattern-redirect-design.md)
