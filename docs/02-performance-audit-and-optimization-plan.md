# PathGuard Next 性能审计与优化计划

> 状态：Phase 2 已完成；Phase 3 已由 ADR-0001 决定暂不实施；跨设备采样待完成
>
> 文档版本：0.1
>
> 审计日期：2026-07-19
>
> 适用范围：Zygisk 启动路径、companion 挂载链路、MediaStore 兼容 Hook、daemon 与策略编译

## 1. 审计结论

本报告首先区分“功能必要成本”和“可优化成本”：

- `setns`、namespace 隔离和实际 mount syscall 是功能所需的内核操作，不能在没有测量的情况下通过省略校验换取延迟数字。
- `/proc` 固定间隔轮询、重复 Parcel 反序列化和全量包扫描属于可以通过设计减少的成本；首轮发现的 marker/`fsync` 路径已经删除。
- 文件访问完成挂载后不再经过 PathGuard 用户态代码，这部分设计保持正确，不应重新引入每次访问的路径匹配或日志。

P1/P2/P3 标签最初来自代码结构审计，不是测量结论。当前已经建立 Host 基准、真机分段计时和 MediaStore 聚合计数；现阶段判断以第 6 节实测为准，尚未覆盖的 P99 和多 ROM 数据不得由单设备结果外推。

本轮围绕四个主要假设完成了实现和首轮验证：

1. package 线性扫描已由 hash 排序索引和二分查找替代；10000 package 的 Host lookup P95 约 0.6 微秒，当前剩余成本主要是每次 specialize 的映射 syscall。
2. 命中策略应用的启动延迟主要来自 readiness 等待；共享结果页同步和 mount 成本较低，marker/`fsync` 已不再参与。
3. MediaStore Hook 已通过最小 Parcel 解析和 descriptor 缓存缩短快路径；当前设备 512 次 query 的纯 Hook 平均约 119 微秒且无 fallback。
4. parser/validator 的 O(n²) 风险已通过哈希集合、来源集合和路径祖先查找消除，并由大规模 Host 基准覆盖。

## 2. 当前实现基线

### 2.1 Zygisk 策略查询

`LoadDenyPlan()` 每次调用都会执行 `openat`、`fstat`、`mmap` 和 `munmap`，随后使用 format v3 的 package hash 排序索引和二分查找：

- [zygisk/src/module_entry.cpp:141](../zygisk/src/module_entry.cpp#L141)
- [zygisk/src/module_entry.cpp:151](../zygisk/src/module_entry.cpp#L151)
- [zygisk/src/module_entry.cpp:210](../zygisk/src/module_entry.cpp#L210)

包策略不再从第一个 package entry 开始顺序扫描：

- [zygisk/src/module_entry.cpp:178](../zygisk/src/module_entry.cpp#L178)

当前 package entry 带有 `package_hash` 和排序索引。复杂度为：

```text
未命中或命中策略：O(log package_count + rule_count)
```

hash 只用于索引，等值区间内仍校验完整 package name，避免碰撞误命中。大规模 Host 基准已确认 lookup 本身不是瓶颈，剩余固定成本是每次映射的 syscall。

### 2.2 Companion 与挂载同步

匹配应用在 `preAppSpecialize` 中连接 companion 并发送计划。companion 使用 1、2、4、8、10ms 退避轮询目标进程的 UID 和 SELinux context，理论上限仍为 5 秒。

随后链路包含：

- `socketpair`
- `fork`
- `setns`
- 每条规则一次 `lstat` 和 mount
- 通过 `SCM_RIGHTS` 把 `memfd` 共享结果页传给 companion
- 通过共享状态字和 futex 返回固定大小的 mount 结果

相关位置：[zygisk/src/module_entry.cpp:359](../zygisk/src/module_entry.cpp#L359)、[zygisk/src/module_entry.cpp:577](../zygisk/src/module_entry.cpp#L577)、[zygisk/src/module_entry.cpp:314](../zygisk/src/module_entry.cpp#L314)。

`postAppSpecialize` 在共享状态字上执行 futex 等待；300ms 只约束取得 mutation lease 前的取消。超时通过原子 CAS 把状态从 `pending` 改为 `cancel_requested` 后立即 fail-open；若 helper 已进入 `applying`，应用请求取消并等待 `rollback_complete`。helper 在 readiness、取得 lease、每条规则前和最终提交前检查取消；挂载成功只有在 CAS 提交 `complete` 后才保留，否则回滚完成后才发布结果。marker、`fsync`、继承到应用域的跨 SELinux socket 和 25ms 文件轮询路径均已删除。

其中 `setns` 和 mount 是功能必要成本；readiness 退避参数已由真机 P50/P95 数据验证。300ms 是 pre-mutation cancel 预算，不是 applying 事务的绝对等待上限。

### 2.3 MediaStore Binder Hook

Hook 位于 `BinderProxy.transactNative`，只在显式 `media=hide_denied` 且 mount 成功后安装。Binder descriptor 对 provider binder 做一次性缓存；provider query 先解析 AttributionSource 和 Uri，确认 authority 后才解析 projection、Bundle 和两个 Binder。

命中媒体查询后还会创建新的 String[]、Bundle、Parcel 并重新序列化请求：[zygisk/src/media_query_hook.cpp:119](../zygisk/src/media_query_hook.cpp#L119)、[zygisk/src/media_query_hook.cpp:275](../zygisk/src/media_query_hook.cpp#L275)。

这是高频媒体查询场景的重点观测路径。之前“图片选择界面全部显示加载失败”属于逻辑错误，已修复；当前设备的纯 Hook 计时与 UI 回归已通过，但该 Hook 仍需要比 mount 路径更高密度的跨 ROM 回归。

### 2.4 daemon 与策略编译

当前 daemon 使用 inotify + `poll(-1)`，无配置变化时不会周期唤醒：[daemon/src/main.cpp:121](../daemon/src/main.cpp#L121)。这是正确方向。

配置事件发生后会等待 150ms，再次完整读取配置，并重新解析、验证和编码：[daemon/src/main.cpp:75](../daemon/src/main.cpp#L75)。该成本只发生在控制面，不属于运行时文件访问热路径。

解析阶段使用 `unordered_set` 检测重复 package 和重复 rule。验证阶段使用 redirect/deny 来源集合，并按路径祖先查找冲突，避免规则数量上的双重循环。64 条 redirect 规则 Host P95 约 105us，仍属于控制面成本。

## 3. 优化范围治理

### 3.1 MediaStore Hook 必须显式启用

架构文档将 MediaStore 兼容定位为可选 compat 后端。当前实现已将 Hook 安装门槛收敛为显式 `media=hide_denied`，并且只有 mount 成功后才在 `postAppSpecialize` 安装，解决了原实现的两个问题：

- 不需要 MediaStore 兼容的应用不再承担 JNI Hook、Binder 拦截和常驻模块成本。
- “是否安装 Hook”已经成为策略层显式、可诊断的产品选择。

配置项保持显式：

```ini
media = off
media = hide_denied
```

默认值为 `off`。只有用户明确选择 `hide_denied` 且 mount 成功时才安装 Hook；纯 deny/redirect 应用只执行 mount，挂载完成后卸载模块。mount 失败时不安装 Hook，保证 `failure = open` 不残留媒体查询过滤。该字段已经纳入 schema 2 和 `policy.bin` format v4。

### 3.2 模块常驻按 Hook 与否分别计算

纯 deny/redirect 只依赖内核 mount，理论上不需要 `.so` 常驻。只有 Hook 函数仍被 Binder 调用时，模块才必须保持加载。

模块卸载不得削弱诊断能力。`status`、`explain` 和挂载状态查询不能依赖目标进程内的 Zygisk `.so` 常驻、JNI 全局引用或模块内存状态：`explain` 应从策略快照生成，运行状态应由 daemon/companion 按 PID、进程 start time 和 generation 记录，并可通过目标进程的 `mountinfo` 进行复核。当前实现尚未建立完整的 applied-state 记录链路，这应作为后续状态模型的一项明确约束，而不是假定已经实现。

当前 `postAppSpecialize` 在 mount 成功后按需安装 Hook，随后仅在 Hook 需要常驻时保留模块；因此指标应拆成：

- 无 Hook 的匹配应用：模块卸载后的额外 RSS/PSS。
- 有 Hook 的匹配应用：Hook 安装后的常驻 RSS/PSS。

daemon/CLI 继续使用静态 C++ 运行库；Zygisk 已拆为独立 `APP_STL=none` 目标，移除 C++ STL/atomic 依赖并使用 C 类型和编译器原子内建。当前最终构建 arm64 Zygisk 库为 71,280B，armeabi-v7a 为 57,628B，ELF 不再依赖 libc++。

## 4. 确定性改动与待验证改动

### 4.1 可以纳入下一批的改动

这些改动的收益方向明确，不依赖某个设备的具体耗时分布：

1. 将 package entry 改为带 hash 的排序索引，并使用二分查找。hash 命中后必须在等值区间内校验完整 package name；hash 仅用于索引，不能作为策略身份。（已完成）
2. 将 companion mount 结果通过 `memfd` 共享结果页和 futex 返回，去掉 marker、`fsync`、跨 SELinux socket 和 post 阶段文件轮询。（已完成）
3. 将 companion 等待失败的应用侧 pre-mutation cancel 预算收紧到数百毫秒量级，并区分 `failed`、`cancelled` 与 `rollback_complete`。（已完成 Host/NDK 状态机和真机验证）
4. 让超时请求具备 mutation lease、取消和回滚协议，避免应用已经放行后 companion 仍在后台改变 namespace。（已完成状态 CAS、helper lease、Host 竞争测试及 pending/applying 真机延迟验证）
5. 增加显式 `media_compat` 配置，纯 mount 应用不安装 Hook，挂载完成后卸载模块。（已完成）
6. 重复 package/rule 检测使用 `unordered_set`，validator 冲突检测使用来源集合和祖先查找。（已完成）

应用侧 fail-open 后不应静默异步修改正在运行进程的 namespace；若没有完整的 live apply 事务模型，应将该次标记为失败，等待下次重启或由控制面显式重试。

取消协议必须覆盖应用侧已经 fail-open 的场景：应用放行后，迟到的 helper 不能继续对该进程执行 `setns` 或 mount。

### 4.2 必须先测量再调参的改动

以下改动不能凭经验调整：

- readiness 轮询已改为 1、2、4、8、10ms 退避；需继续观察多设备尾延迟。
- socket 等待超时的最终数值。
- 每应用最大规则数和 mount 批处理方式。
- MediaStore descriptor 缓存和 Uri 早退出已完成；修正计时边界后的纯 Hook 计时和 UI 回归已通过，仍需补充其他 ROM 和 1/8 条 deny path 样本。

“平均更快”不等于“P99 更好”。所有参数都应以 `T_total`、P95 和 P99 为准。

### 4.3 Zygote 继承 mmap：独立 ADR，不与当前批次混改

Zygisk 模块加载在 Zygote 中，理论上可以在首次加载时打开并 mmap `policy.bin`，让 fork 子进程继承映射，从而减少每个应用的 `openat/fstat/mmap/munmap` 固定成本。这是有潜在收益的候选方向，但存在三个约束：

1. 不能在 Zygote 中新增 inotify 线程或带锁的常驻后台线程，否则会引入 fork-safety 风险。
2. 原子 rename 后，继承的旧 inode 不会自动变成新快照；从旧映射自身读取 generation，无法发现新文件已经替换。
3. 因此必须另有安全的“当前快照指针”或轻量外部版本源，例如受控的 inode/stat 检查、专用 generation 文件或经过论证的 Zygote 生命周期刷新点。

该方案应单独形成 ADR，先完成 fork 安全、配置刷新、旧映射生命周期和异常回退设计，再决定是否实施。当前批次先实现正确的 hash+二分查找，不把两种风险混在一起。

## 5. MediaStore 快路径设计约束

不能通过扫描 Parcel 原始字节来猜测 authority。`Uri` 位于结构化 Parcel 数据中，可靠判断仍需要最小合法解析。

建议的快路径顺序：

1. transaction code 不是 query，直接调用原始函数。
2. 缓存或快速确认 Binder interface descriptor，不重复执行不必要的 JNI 查询。
3. 只解析到 `Uri` 所需的最小字段。
4. `Uri.authority` 不是 `media` 时，立即恢复 position 并调用原始函数，不再解析 projection、Bundle 和 Binder 参数。
5. 只有 MediaProvider 查询才继续解析完整参数并修改 Bundle。

同时必须覆盖：

- 无 query args。
- 非 media authority。
- 非标准 projection。
- 空 Uri、异常 Parcel 和解析失败回退。
- 原始查询结果不被错误过滤。
- 大量连续查询时的 JNI 局部引用和 Parcel 回收。

## 6. 性能测量方案

### 6.1 Zygisk/companion 分段计时

使用 `clock_gettime(CLOCK_MONOTONIC, ...)`，只在采样或超过阈值时输出日志，避免日志本身污染结果。至少记录：

```text
T_policy_open       openat + fstat + mmap
T_policy_lookup     package 查找和规则提取
T_hook_install      JNI 初始化和 Hook 注册
T_companion_connect connectCompanion + bootstrap 写入
T_process_ready     readiness 等待
T_setns             进入目标 namespace
T_mount_one         每条 lstat + mount
T_result            共享状态页 + futex 结果同步
T_total             对应用启动的总影响
```

按照架构基线分别测试 package 数量 `1/10/100/1000/10000`，单应用规则数 `0/1/4/16/32/64`，报告 P50、P95、P99、最大值和样本数。

### 6.2 MediaStore 测量

不记录每次查询的完整 SQL 或路径，只记录聚合计数：

```text
query_total
query_non_media
query_media
query_rewrite
query_fallback
hook_cpu_ns_total
```

分别测试非媒体 Binder query、MediaProvider query、无 deny path、1/2/8 条 deny path以及图片选择器连续滚动场景。

### 6.3 daemon 测量

记录：

- inotify 到开始读取的延迟。
- 防抖等待时间。
- 原始配置读取时间。
- parse、validate、encode 各自耗时。
- policy 比较和 publish 时间。
- daemon RSS/PSS、线程数和空闲唤醒次数。

当前设备已观察到的旧版 daemon（PID 2045）RSS 约 2.6 MiB、约每秒一次 voluntary context switch，只能作为旧版 1 秒轮询基线，不能代替最新 inotify 构建的测量。

### 6.4 Phase 0 首轮结果

2026-07-19 在 Windows Release Host 上运行 `pathguard_policy_benchmark`，并完成 Host 单元测试、Android arm64 构建和热更新回归。以下数据用于确认趋势，不作为 Android 真机最终预算：

| 场景 | 首轮 parse P95 | 优化后 parse P95 | 优化后 total P95 | 说明 |
|---|---:|---:|---:|---|
| 1000 package、每包 1 条规则 | 约 2.3 ms | 约 0.9 ms | 约 1.8 ms | 重复 package 检测由线性扫描改为哈希集合 |
| 10000 package、每包 1 条规则 | 约 154 ms | 约 9.4 ms | 约 20.5 ms | 仍需关注字符串表编码和整体编译成本 |

新增的 package index 查找器在 Host 基准中，10000 package 的 P95 约 0.6 微秒；该数字只覆盖已映射内存中的二分查找，不包含应用启动时的 `openat/fstat/mmap/munmap`，不能直接等同于 Android `T_lookup`。

这一轮结果确认两点：

1. parser 的 O(n²) 重复检测是实际可见的控制面瓶颈，使用 `unordered_set` 后在 10000 package 场景显著下降。
2. package hash + 排序 + 二分查找的纯查找成本已经很低，下一轮应单独测量 mmap 固定成本，而不是继续微优化比较循环。

Phase 0 已完成的实现项：

- Zygisk/companion 分段耗时日志。
- MediaStore 聚合 query 计数与累计 Hook CPU 时间。
- daemon parse、validate、encode、compare、publish 分段日志。
- Host 策略规模基准和 package collision 查找测试。
- policy format v4、共享字段定义和 hash 排序索引。
- readiness 退避、MediaStore descriptor 缓存/Uri 早退出、validator 来源集合。
- Zygisk `APP_STL=none` 双 ABI 构建。

Phase 1 的确定性代码改造已经完成。Phase 2 已完成 readiness 退避、mount 成功后安装 Hook、MediaStore 快路径与纯 Hook 计时、validator 冲突检测和 Zygisk `APP_STL=none`。当前实现阶段没有遗留阻塞项，剩余工作是跨设备及不同 deny path 数量的扩展采样。

2026-07-20 在 Xiaomi `alioth`（arm64-v8a、Magisk 30.6）上完成新通信链路回归。直接继承 companion socket 的实现曾在 `postAppSpecialize` 返回 `EACCES`，并被观测到“应用侧 fail-open、helper 之后仍完成 mount”的竞态；该实现已删除。当前 `memfd` 共享页 + futex 版本正常路径为 `result_received=1`、`result=0`、`committed=1`。20 次冷启动中 readiness P50/P95 为 4.1/8.1ms，mount P50/P95 为 0.31/0.55ms，companion total P50/P95 为 6.27/10.18ms，20/20 Hook 在 mount 成功后安装且无 fail-open。

当前 mutation lease 协议已完成两组 450ms 真机延迟注入。lease 前延迟共 6 次，应用等待 300.069-300.287ms，helper 均返回 `ECANCELED`，`mount_total_us=0`、`committed=0`，无 mount、无 Hook。第一条 mount 后延迟中，应用等待 452.931ms，helper 回滚耗时 63us，`committed=0`，无残留 mount、无 Hook。此前累计 `hook_cpu_us` 包含原始 Binder transact 等待，不能用来证明 Hook 自身性能；计时边界已改为在调用原始 transact 前结束。

修正计时边界后的 LocalSend 图片选择回归中，512 次 query 包含 483 次重写、29 次非媒体 query、0 fallback，纯 Hook 累计约 60.9ms，平均约 119us/query。缩略图网格、连续滚动和预览均通过；该结果仅代表当前设备和 2 条 deny path，仍需在 1/8 条路径及其他 ROM 上补样本。

## 7. 执行顺序

### Phase 0：建立证据

- 增加低频分段计时和聚合计数。
- 建立 package/rule 规模基准。
- 建立 MediaStore 正确性与性能回归集。
- 在至少一台低端、一台中端设备上采样。

### Phase 1：确定性收益

- package hash + 排序 + 二分查找。（已完成）
- companion 通过共享结果页直接传递结果，移除 marker/fsync。（已完成）
- 收紧应用侧等待上限并实现超时取消。（已完成首版）
- `media_compat` 显式开关。（已完成）
- 纯 mount 应用卸载模块。（已完成）
- parser 中重复项使用哈希集合。（已完成）

### Phase 2：针对测量结果优化

- 调整 readiness 退避曲线。（已完成，1/2/4/8/10ms）
- 优化 MediaStore 最小 Parcel 解析和 descriptor 缓存。（已完成，纯 Hook 计时和 UI 回归通过）
- 评估 `APP_STL := none`。（已完成并采用，双 ABI 构建和 arm64 真机加载通过）
- 优化规则冲突检测的数据结构。（已完成并增加 redirect-heavy 基准）

### Phase 3：独立 ADR

- 论证 Zygote 继承 mmap。（已完成 ADR-0001）
- 论证快照刷新指针和 fork-safe 生命周期。（已完成 ADR-0001）
- 实测显示 mmap 固定成本不是主要比例，当前决策为暂不实施。

## 8. 验收标准

优化完成后必须同时满足：

- 未命中应用的策略查询不再随 package 数量线性增长。
- hash 命中后在等值区间内校验完整 package name；构造 hash 碰撞测试时不得拿错策略。
- companion pre-mutation 超时不会让应用无条件等待 5 秒，300ms 不适用于已进入 `applying` 的回滚等待。
- fail-open 后不会有未取得 lease 的后台 helper 修改运行中 namespace；已取得 lease 的 helper 必须完成回滚后才放行。
- 人为注入超过 pre-mutation cancel 预算的 companion 延迟，分别验证 `pending` 取消和 `applying` 回滚；检查必须结合 PID start time，避免 PID 复用造成误判。（pending/applying 真机档位已通过；PID start time 联合校验在 R1 preflight 实现）
- 默认 deny/redirect 不安装 MediaStore Hook。
- MediaStore compat 场景非媒体 query 能够快速回退，且图片选择、预览、缩略图和普通文件查询均通过回归测试。
- 删除 marker/fsync 后，共享状态协议仍具备明确成功、失败、取消和超时状态。
- parser/validator 在 1000 条规则基准上达到文档目标，或记录设备级预算调整依据。
- 所有结论都附带 P50/P95/P99 数据，不以理论复杂度替代设备测量。

## 9. 当前不应优先优化的项目

以下项目当前不是主要瓶颈：

- inotify 空闲等待。
- 几 KB 配置的 `policy.bin` 写入。
- FNV/hash 本身。
- 挂载完成后的普通文件访问路径。
- 为未来规模提前引入复杂异步运行时或大型序列化依赖。

这些项目只有在基准数据明确显示其占用主要预算时才进入优化范围。
