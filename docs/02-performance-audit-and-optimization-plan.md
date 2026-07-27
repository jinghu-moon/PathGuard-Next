# PathGuard Next 性能审计与优化计划

> 状态：Phase 2 已完成；Phase 3 已由 ADR-0001 决定暂不实施；R1 companion mount 事务分段优化已完成（见 6.5）；跨设备采样待完成
>
> 文档版本：0.3
>
> 审计日期：2026-07-19（0.2 增补 2026-07-23 R1 mount 事务优化；0.3 增补 2026-07-26 规则编译器 D0）
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

`LoadProcessPlan()` 每次调用都会执行 `openat`、`fstat`、`mmap` 和 `munmap`，随后使用 format v5 的 package hash 排序索引和二分查找：

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

Hook 位于 `BinderProxy.transactNative`，只在显式 `file_picker = true`、存在 deny 且 mount
成功后安装。Binder descriptor 对 provider binder 做一次性缓存；provider query 先解析
AttributionSource 和 Uri，确认 authority 后才解析 projection、Bundle 和两个 Binder。

deny SQL、参数文本和无原始参数时复用的 JNI `String[]` 已在 Hook 安装时预计算。命中且原
query 自带 selection/args 时仍需创建合并后的 String[]、Bundle、Parcel 并重新序列化请求；
这是保持原查询语义所需的分配，不能缓存跨 query 的用户参数。

这是高频媒体查询场景的重点观测路径。之前“图片选择界面全部显示加载失败”属于逻辑错误，已修复；当前设备的纯 Hook 计时与 UI 回归已通过，但该 Hook 仍需要比 mount 路径更高密度的跨 ROM 回归。

### 2.4 daemon 与策略编译

当前 daemon 使用 inotify + `poll(-1)`，无配置变化时不会周期唤醒：[daemon/src/main.cpp:121](../daemon/src/main.cpp#L121)。这是正确方向。

配置事件发生后会等待 150ms，再次完整读取配置，并重新解析、验证和编码：[daemon/src/main.cpp:75](../daemon/src/main.cpp#L75)。该成本只发生在控制面，不属于运行时文件访问热路径。

解析阶段使用 `unordered_set` 检测重复 package 和重复 rule。验证阶段使用 redirect/deny 来源集合，并按路径祖先查找冲突，避免规则数量上的双重循环。64 条 redirect 规则 Host P95 约 105us，仍属于控制面成本。

## 3. 优化范围治理

### 3.1 MediaStore Hook 必须显式启用

架构文档将 MediaStore 兼容定位为可选 compat 后端。当前实现已将 Hook 安装门槛收敛为显式 `file_picker = true`，并且只有 mount 成功且应用存在 deny 规则时才在 `postAppSpecialize` 安装，解决了原实现的两个问题：

- 不需要 MediaStore 兼容的应用不再承担 JNI Hook、Binder 拦截和常驻模块成本。
- “是否安装 Hook”已经成为策略层显式、可诊断的产品选择。

配置项保持显式：

```toml
[apps."org.example.app"]
file_picker = true
deny = ["Pictures/Private"]
```

默认值为 `false`。当前实现只过滤目标应用进程直接发起的 MediaStore query；系统
Photo Picker、SAF 和 CloudMediaProvider 是独立的跨进程数据面，不得由该能力隐式
宣称覆盖。Hook 初始化失败时保持 strict deny anchor，不把媒体索引过滤假报为成功。

### 3.2 模块常驻按 Hook 与否分别计算

纯 deny/redirect 只依赖内核 mount，理论上不需要 `.so` 常驻。只有 Hook 函数仍被 Binder 调用时，模块才必须保持加载。

模块卸载不得削弱诊断能力。`status`、`explain` 和挂载状态查询不能依赖目标进程内的 Zygisk `.so` 常驻、JNI 全局引用或模块内存状态：`explain` 应从策略快照生成，运行状态应由 daemon/companion 按 PID、进程 start time 和 generation 记录，并可通过目标进程的 `mountinfo` 进行复核。当前实现尚未建立完整的 applied-state 记录链路，这应作为后续状态模型的一项明确约束，而不是假定已经实现。

当前 `postAppSpecialize` 在 mount 成功后按需安装 Hook，随后仅在 Hook 需要常驻时保留模块；因此指标应拆成：

- 无 Hook 的匹配应用：模块卸载后的额外 RSS/PSS。
- 有 Hook 的匹配应用：Hook 安装后的常驻 RSS/PSS。

daemon/CLI 保持 C++ 可执行文件；规则编译器 D0 已冻结 C++20 + toml++ v3.4.0，完整编译链只静态链接到 daemon 和离线 compile/validate CLI。编译延迟、峰值内存与 publish/fsync 单独报告，不能混入 Zygisk 指标。Zygisk 已拆为独立 `APP_STL=none` 目标，移除 C++ STL/atomic 依赖并使用 C 类型和编译器原子内建；当前最终构建 arm64 Zygisk 库为 71,280B，armeabi-v7a 为 57,628B，ELF 不再依赖 libc++，并必须继续保持不链接 Rust runtime、TOML parser 或规则诊断。

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
- policy format v5、共享字段定义、固定 golden vector 和 hash 排序索引。
- readiness 退避、MediaStore descriptor 缓存/Uri 早退出、validator 来源集合。
- Zygisk `APP_STL=none` 双 ABI 构建。

Phase 1 的确定性代码改造已经完成。Phase 2 已完成 readiness 退避、MediaStore
历史快路径实验、validator 冲突检测和 Zygisk `APP_STL=none`。当前 R1 将
MediaStore compat 编译门控为不可执行；剩余阻塞项是补强后 mount identity 路径的
Alioth 回归、worker owner-death/rollback failure 注入，以及跨设备 P95/P99 采样。

2026-07-20 在 Xiaomi `alioth`（arm64-v8a、Magisk 30.6）上完成新通信链路回归。直接继承 companion socket 的实现曾在 `postAppSpecialize` 返回 `EACCES`，并被观测到“应用侧 fail-open、helper 之后仍完成 mount”的竞态；该实现已删除。当前 `memfd` 共享页 + futex 版本正常路径为 `result_received=1`、`result=0`、`committed=1`。20 次冷启动中 readiness P50/P95 为 4.1/8.1ms，mount P50/P95 为 0.31/0.55ms，companion total P50/P95 为 6.27/10.18ms，20/20 Hook 在 mount 成功后安装且无 fail-open。

当前 mutation lease 协议已完成两组 450ms 真机延迟注入。lease 前延迟共 6 次，应用等待 300.069-300.287ms，helper 均返回 `ECANCELED`，`mount_total_us=0`、`committed=0`，无 mount、无 Hook。第一条 mount 后延迟中，应用等待 452.931ms，helper 回滚耗时 63us，`committed=0`，无残留 mount、无 Hook。此前累计 `hook_cpu_us` 包含原始 Binder transact 等待，不能用来证明 Hook 自身性能；计时边界已改为在调用原始 transact 前结束。

修正计时边界后的 LocalSend 图片选择回归中，512 次 query 包含 483 次重写、29 次非媒体 query、0 fallback，纯 Hook 累计约 60.9ms，平均约 119us/query。缩略图网格、连续滚动和预览均通过；该结果仅代表当前设备和 2 条 deny path，仍需在 1/8 条路径及其他 ROM 上补样本。

### 6.5 R1 companion mount 事务分段优化（2026-07-23, alioth/4.19, backend=proc_fd）

R1 双后端 executor 上线后，在 `alioth`（Linux 4.19.157、Magisk 30.6、Zygisk）实测单规则
companion mount 事务约 246ms，其中 mount syscall 本身仅约 40 微秒。通过 `clock_gettime`
分段计时逐层定位并优化，单规则 mount_total 从约 132ms 降到约 1.5ms，companion total
从约 246ms 降到约 4ms。2026-07-24 已补回 strict mountinfo 的 root、filesystem、
major:minor 和 parent identity 比对；下述 4ms 是补强前样本，必须在新验证路径上重新采样，
不能直接作为补强后的最终预算。

分段计时（stat / mountinfo read / mountinfo parse 三段独立埋点）推翻了"瓶颈是 mountinfo
冷读"的假设：mountinfo 全表 read 仅约 0.6ms，真正成本是 parse（约 67ms）与
`stat(target)` 的 FUSE getattr（约 45ms）。四项改动：

| 改动 | 内容 | 单规则收益 |
|---|---|---:|
| probe 缓存 | capability probe 结果按 boot_id + SELinux enforce + SELinux policy identity + policy_flags + topology generation 缓存于 companion 父进程，fork 子进程经 COW 继承。目标 namespace 在执行前重新捕获 topology 并比较 generation。 | probe 约 100ms → 0（旧样本缓存命中约 1µs；新 key 待复测） |
| strict 免 before_scan | strict 后端删除 mount 前的 mountinfo 全表扫描。该扫描仅为 legacy 的 before/after delta 服务（ADR-0005 legacy 条目）；strict（ADR-0005 strict 条目）只要求 mount 后校验身份。 | before_scan 约 65ms → 0 |
| mountinfo whole-file read + 手写 parse | 一次性 read 整个 `/proc/self/mountinfo` 到内存缓冲，再以手写指针扫描逐行解析；仅对 mountpoint 命中的行提取完整字段，替换对约 1600 行每行 `sscanf("%4095s")` 的开销。 | parse 约 67ms → 约 0.15ms（约 450x） |
| strict verify 免 FUSE stat | strict verify 从 target 挂载点的 mountinfo 行完成身份校验（mount ID + `root` + filesystem），不再对 target 路径做 `stat()`。见 ADR-0005 strict 条目更新。实测 mountinfo 行的 `root` 字段即固定 source 的规范路径，直接满足身份验证。 | verify 约 45ms → 约 0.8ms |

已验证的负面假设（避免重复尝试）：

- `statx(AT_STATX_DONT_SYNC)` 不能让 FUSE 跳过 daemon getattr 往返（实测 42ms vs `stat` 44ms）。
- 不能用 `fstat(target.fd)` 替代 target 路径 stat：`target.fd` 是 mount 前 pin 的 O_PATH fd，
  指向被覆盖前的目录，用它验证会拿到 mount 前的 inode，语义错误。

多规则（16 条 redirect）旧样本的 mount_total 约 25.6ms、单条 max 约 1.6ms，随规则数线性
增长，每条 verify 约 0.8ms。该样本形成时内核生成 mountinfo 的成本尚未稳定暴露；2026-07-26
后续测量确认每次完整读取约 66-75ms，因此“暂不实施 group verify”的旧结论已由 6.7 取代。

legacy 后端**不适用**上述 strict verify 简化：它仍保留 mount 前后的 mountinfo delta 校验和
`stat` + `SameObject` 身份比对（ADR-0005 legacy 条目），因为字符串 bind 无固定 FD 保证。

### 6.6 namespace 级 mountinfo snapshot（2026-07-26）

2026-07-25 在同一台 alioth 上重新采样发现，约 1608 行 mountinfo 的内核 seq_file
生成/读取稳定需要约 66-75ms；6.5 中 0.6ms 的旧样本不能代表补强身份验证后的当前路径。
单规则事务此前会在 companion 候选 topology、目标 topology、source pin、propagation、
target pin 和 verify 中重复生成整表，mount syscall 仍只有约 50us。

当前实现改为有界、绑定 mount namespace dev/inode 的 `MountInfoSnapshot`：

- 原始文本上限 2MiB、条目上限 4096，匿名映射存储，不进入线程栈；
- companion 候选 topology 的 visible/source 查询共享一次 snapshot；
- worker 变更前的一次 snapshot 同时服务 topology、source pin、propagation 和首条 target pin；
- mount 后立即写入未知 mount ID 的 journal，再捕获 post-snapshot，完成 mount ID、parent、
  root、filesystem、device 验证并回填 journal；该 snapshot 成为下一条嵌套规则的当前视图；
- 回滚前统一捕获一次 snapshot 并确认全部 journal 对象仍为顶层 mount，逆序卸载后再统一
  捕获一次验证全部 mount ID 消失；身份不明仍进入 `namespace_tainted`。

单规则正常 worker 路径由约 6 次整表读取降为 2 次；companion 候选 topology 由 2 次降为
1 次。新增 `mi_snapshots`、`mi_read_us`、`mi_parse_us` 和 `topology_candidate` 遥测。预计收益
必须通过生产配置真机 P50/P95/P99 重新确认，文档不预先宣称达到 300ms。

2026-07-26 已在 alioth、Android 13、Linux 4.19.157、SELinux Enforcing、Magisk 30.6
上使用生产配置完成 50 次 LocalSend force-stop/冷启动矩阵。策略为单条
`Download/localsend-source -> Download/localsend-redirect`，后端为 strict `proc_fd`。
所有样本均为 `committed=1`、`result=0`、`mi_snapshots=2`，probe cache 全部命中；未出现取消、
回滚、namespace taint 或残留 mount。应用 namespace 中的 bind root 为
`/0/Download/localsend-redirect`，redirect marker 可从 visible source 路径读取，而宿主 namespace
中的 source 目录保持为空。

| 指标（微秒） | P50 | P95 | P99 / max |
|---|---:|---:|---:|
| candidate topology snapshot total | 7,624 | 14,787 | 15,240 |
| worker 两次 mountinfo read 合计 | 150,646 | 187,599 | 215,679 |
| worker 两次 mountinfo parse 合计 | 969 | 1,549 | 1,779 |
| mount transaction | 70,740 | 82,429 | 102,982 |
| companion total | 172,284 | 215,900 | 237,772 |
| 应用侧 Zygisk wait total | 152,796 | 199,176 | 218,472 |
| raw mount syscall | 31 | 44 | 52 |

该结果验证了事务级 snapshot 在当前设备上将正常 worker 路径固定为两次整表生成，并使单规则
事务在 50/50 样本中于取消预算前提交。当前主要成本仍是内核生成/读取 mountinfo，而非解析、
FD pin 或 mount syscall。证据保存在 `build/device-evidence/snapshot-prod-50-20260726-120101/`；
该目录是本地测试产物，不作为跨 ROM 结论。

### 6.7 扁平 MountOp 批处理与真实操作选后端（2026-07-27）

format 1 已在编译期拒绝 redirect 父子包含和 deny/redirect 包含，并折叠嵌套 deny，因此当前
visible target 互不嵌套。实现据此完成以下改造：

- worker 用初始 snapshot 固定全部唯一 source 和全部 target；多个 deny 规则只固定一次
  deny anchor；
- 取得 mutation lease 后连续执行全部 MountOp，每次成功 syscall 立即写入未知 mount ID 的
  固定容量 journal；
- 循环结束只捕获一次最终 snapshot，用同一对 initial/final snapshot 验证全部 mount；只有
  完整身份确认的 mount 才按 operation index 回填 journal。正常 worker 路径固定为两次
  snapshot，与规则数无关；
- 失败路径复用最终 snapshot 做 rollback identity 验证，最终 snapshot 不可用时才为回滚重试；
  无法确认身份、journal 溢出、rollback 失败和 owner death 仍进入 `namespace_tainted`；
- 标准已隔离 namespace 删除隔离 capability probe、probe cache 和专用 500ms 等待。首条
  真实 MountOp 依次尝试 `open_tree`、`proc_fd`，只有明确未发生 mutation 的失败才允许继续；
  首个成功 backend 锁定整个事务。若 `/storage` 必须先改为 private，则在 lease 前保留一次
  不缓存的隔离 probe，避免不可逆 propagation 后才发现 backend 不可用。legacy 仍只允许
  显式授权的 redirect-only disposable namespace，deny 不降级；
- worker 的两次连续 policy 读取合并为 lease 前一次 PID start time、policy、topology 联合复核；
- MediaStore deny SQL、参数文本和 JNI `String[]` 改为 Hook 安装时生成一次。无原始 query
  selection/args 时直接复用 global reference，有原参数时只构建合并结果。

2026-07-27 已在同一台 alioth 上完成 20 次生产构建冷启动矩阵，规则为 2 deny + 1 redirect。
20/20 均为 `committed=1`、`result=0`、`mi_snapshots=2`，60/60 MountOp 成功；首条
`open_tree` 均以 `ENOSYS` 无 mutation 失败并安全锁定 `proc_fd`。未出现取消、回滚、taint
或 query fallback。修正 companion 行末 `total_us` 提取后的结果如下：

| 指标（微秒） | P50 | P95 | P99 / max |
|---|---:|---:|---:|
| candidate topology snapshot total | 8,463 | 13,107 | 14,305 |
| worker 两次 mountinfo read 合计 | 156,704 | 165,576 | 173,262 |
| 全量 source/target pin | 1,494 | 1,977 | 2,706 |
| mount transaction | 72,577 | 77,125 | 79,523 |
| companion total | 180,173 | 192,390 | 197,587 |
| 应用侧 Zygisk wait total | 160,609 | 170,519 | 173,731 |

功能回归确认宿主 source 为空时，应用 namespace 的 visible source 精确呈现 backing 中两个
文件；两个 deny target 均指向同一 deny anchor，应用 UID 列举返回 `EACCES`。强制重新进入
LocalSend 媒体选择器后累计 `query_media=786`、`query_rewrite=786`、`query_fallback=0`，
deny 目录中的最新媒体未出现在选择器中。该三规则样本不能与 2026-07-26 单规则样本作同配置
加速结论，但 P95 尾延迟仍低于旧单规则样本。

### 6.8 strict 成功路径延迟 mountinfo 身份读取（2026-07-27）

6.7 之后的主要成本仍是 worker 第二次 post-mount snapshot。strict `open_tree`/`proc_fd`
后端的 source 与 target 都由已复核的 FD 消费；单次 mount syscall 返回成功时，内核已原子
完成该 MountOp。成功提交不需要 mount ID，mount ID 只用于失败后的精确回滚。因此实现调整为：

- 初始 snapshot 保留，继续统一承担目标 namespace topology、传播属性、全部 source/target
  mount identity 与 lease 前复核；
- strict 全部 syscall 成功且共享状态成功从 `applying` 提交到 `complete` 时，不捕获
  post-mount snapshot，日志记为 `verification=syscall`，正常 worker 固定为 1 次 snapshot；
- apply、取消或共享状态提交任一步失败且 journal 非空时，立即捕获 fresh snapshot，验证并
  回填全部 mount ID 后才允许回滚；捕获/身份确认失败仍 taint 并终止 namespace；
- legacy string 后端不享受该优化，成功路径仍执行 before/final delta、root、filesystem、
  device、parent 和 `stat + SameObject` 校验。

纯决策测试覆盖 strict success、legacy success、strict failure 和无 mutation 四种组合；Host
48/48、Android arm64/arm32 生产构建及 mount-delay、worker-crash、rollback-failure 三个注入
档位编译已通过。

同日使用与 6.7 完全相同的 2 deny + 1 redirect 配置完成 20 次生产构建冷启动 A/B 复测。
20/20 均为 `mi_snapshots=1`、`committed=1`、`result=0`，60/60 MountOp 均记录
`verification=syscall`；未出现 rollback、taint 或残留 mount：

| 指标（微秒） | 6.7 P50 | 6.8 P50 | 降幅 | 6.7 P95 | 6.8 P95 | 降幅 |
|---|---:|---:|---:|---:|---:|---:|
| worker apply | 165,181 | 89,013 | 46.1% | 171,768 | 101,772 | 40.8% |
| mount transaction | 72,577 | 84 | 99.9% | 77,125 | 110 | 99.9% |
| worker mountinfo read | 156,704 | 82,099 | 47.6% | 165,576 | 94,197 | 43.1% |
| companion total | 180,173 | 105,180 | 41.6% | 192,390 | 120,365 | 37.4% |
| 应用侧 Zygisk wait | 160,609 | 86,553 | 46.1% | 170,519 | 104,163 | 38.9% |

candidate topology P50/P95 为 8,573/17,320us，source/target pin 为 1,457/1,772us，说明收益
来自精确删除第二次 worker mountinfo 生成，而不是把成本转移到其他阶段。单轮功能门禁再次确认
两个 deny 返回 `EACCES`、redirect visible source 呈现 backing 内容、MediaStore 412/412 media
query 被改写且 0 fallback。force-stop 后应用 PID 消失，Zygote、system_server 和 daemon 均无
目标挂载残留。证据位于 `build/device-evidence/deferred-verify-20260727/`。

工程注意：修改 Zygisk `.so` 后必须全清 `native/obj` 与 `native/libs` 再重编，否则增量构建
可能因某个编译单元失败而静默复用陈旧 `.o`，导致产物 md5 不变、改动不生效。部署后需重启
且 zygote 换库存在延迟，应以新增日志字符串（如 `mount backend unavailable`、
`mount_pin_loop`）门控确认新库生效
再采样。冷启动首次挂载有抖动（mount_total 可能回到 130ms 量级），需采多次取稳态。

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
- 优化 MediaStore 最小 Parcel 解析、descriptor 缓存和 deny filter 安装时预计算。（代码与
  Host/NDK 验证完成，新增缓存路径待真机 UI/性能回归）
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
