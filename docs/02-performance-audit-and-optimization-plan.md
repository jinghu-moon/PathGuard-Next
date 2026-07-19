# PathGuard Next 性能审计与优化计划

> 状态：Review
>
> 文档版本：0.1
>
> 审计日期：2026-07-19
>
> 适用范围：Zygisk 启动路径、companion 挂载链路、MediaStore 兼容 Hook、daemon 与策略编译

## 1. 审计结论

本报告首先区分“功能必要成本”和“可优化成本”：

- `setns`、namespace 隔离和实际 mount syscall 是功能所需的内核操作，不能在没有测量的情况下通过省略校验换取延迟数字。
- `/proc` 固定间隔轮询、marker 文件、`fsync`、重复 Parcel 反序列化和全量包扫描属于可以通过设计减少的成本。
- 文件访问完成挂载后不再经过 PathGuard 用户态代码，这部分设计保持正确，不应重新引入每次访问的路径匹配或日志。

当前 P1/P2/P3 标签是基于代码结构的风险等级，不是已经测量出的结论。下一步必须先建立分阶段耗时数据，再确认哪一项实际主导 P95/P99。

按当前代码，最值得验证的假设依次是：

1. 每个应用 specialize 都执行全量 `policy.bin` 查找，包数量扩大后会成为所有应用的启动成本。
2. 命中策略的应用启动延迟主要由 readiness 轮询、companion 同步、marker/fsync 和 mount 组成，其中前几项是可优化成本。
3. MediaStore Hook 在高频 Binder query 场景中的固定 JNI/Parcel 成本可能高于 SQL 重写本身。
4. 配置解析和验证的 O(n²) 只在大规模配置更新时出现，不是文件访问热路径瓶颈。

## 2. 当前实现基线

### 2.1 Zygisk 策略查询

`LoadDenyPlan()` 每次调用都会执行 `openat`、`fstat`、`mmap` 和 `munmap`：

- [zygisk/src/module_entry.cpp:141](../zygisk/src/module_entry.cpp#L141)
- [zygisk/src/module_entry.cpp:151](../zygisk/src/module_entry.cpp#L151)
- [zygisk/src/module_entry.cpp:210](../zygisk/src/module_entry.cpp#L210)

包策略随后从第一个 package entry 开始顺序扫描：

- [zygisk/src/module_entry.cpp:178](../zygisk/src/module_entry.cpp#L178)

当前 package entry 没有 `package_hash`，也没有排序索引。复杂度为：

```text
未命中策略：O(package_count)
命中策略：O(package_count + rule_count)
```

这与架构基线要求的 hash、排序和二分查找不一致：[docs/00-architecture-design.md:529](00-architecture-design.md#L529)、[docs/00-architecture-design.md:546](00-architecture-design.md#L546)。当前配置只有一个应用策略时，绝对耗时预计很小；问题在于该成本由所有 Zygisk 应用启动共同承担，并且会随 package 数量线性增长。

### 2.2 Companion 与挂载同步

匹配应用在 `preAppSpecialize` 中连接 companion 并发送计划：[zygisk/src/module_entry.cpp:493](../zygisk/src/module_entry.cpp#L493)。companion 先以 10ms 粒度轮询目标进程的 UID 和 SELinux context，理论上限为 5 秒：[zygisk/src/module_entry.cpp:290](../zygisk/src/module_entry.cpp#L290)。

随后链路包含：

- `socketpair`
- `fork`
- `setns`
- 每条规则一次 `lstat` 和 mount
- marker 写入
- `fsync`、`fchown`、`fchmod`

相关位置：[zygisk/src/module_entry.cpp:359](../zygisk/src/module_entry.cpp#L359)、[zygisk/src/module_entry.cpp:577](../zygisk/src/module_entry.cpp#L577)、[zygisk/src/module_entry.cpp:314](../zygisk/src/module_entry.cpp#L314)。

`postAppSpecialize` 又以 25ms 间隔轮询 marker，最长等待 5 秒：[zygisk/src/module_entry.cpp:395](../zygisk/src/module_entry.cpp#L395)。

其中 `setns` 和 mount 是功能必要成本；固定间隔 `/proc` 读取、marker 磁盘往返、`fsync` 和重复轮询是可优化成本。5 秒是理论上限，不代表真机平均值，必须通过分阶段计时确认。

### 2.3 MediaStore Binder Hook

Hook 位于 `BinderProxy.transactNative`，目前只在 transaction code 不是 query 时快速返回：[zygisk/src/media_query_hook.cpp:216](../zygisk/src/media_query_hook.cpp#L216)。对于 code 1，它会先执行接口描述符查询，然后完整反序列化 AttributionSource、Uri、projection、Bundle 和两个 Binder，最后才检查是否是 MediaProvider：[zygisk/src/media_query_hook.cpp:222](../zygisk/src/media_query_hook.cpp#L222)、[zygisk/src/media_query_hook.cpp#L237](../zygisk/src/media_query_hook.cpp#L237)。

命中媒体查询后还会创建新的 String[]、Bundle、Parcel 并重新序列化请求：[zygisk/src/media_query_hook.cpp:119](../zygisk/src/media_query_hook.cpp#L119)、[zygisk/src/media_query_hook.cpp:275](../zygisk/src/media_query_hook.cpp#L275)。

这是高频媒体查询场景的潜在 P1 瓶颈。之前“图片选择界面全部显示加载失败”属于逻辑错误，已修复；但该 Hook 仍需要比 mount 路径更高密度的回归和性能测试。

### 2.4 daemon 与策略编译

当前 daemon 使用 inotify + `poll(-1)`，无配置变化时不会周期唤醒：[daemon/src/main.cpp:121](../daemon/src/main.cpp#L121)。这是正确方向。

配置事件发生后会等待 150ms，再次完整读取配置，并重新解析、验证和编码：[daemon/src/main.cpp:75](../daemon/src/main.cpp#L75)。该成本只发生在控制面，不属于运行时文件访问热路径。

解析阶段使用 `std::any_of` 检测重复 package 和重复 rule：[core/src/policy.cpp:114](../core/src/policy.cpp#L114)、[core/src/policy.cpp:143](../core/src/policy.cpp#L143)。验证阶段对规则进行双重循环：[core/src/validation.cpp:34](../core/src/validation.cpp#L34)。因此大规模配置存在 O(n²) 风险，但当前配置规模下优先级低于启动和 Binder 路径。

## 3. 优化范围治理

### 3.1 MediaStore Hook 必须显式启用

架构文档将 MediaStore 兼容定位为可选 compat 后端。当前实现只要应用命中 deny 规则，就会尝试安装 Hook：[zygisk/src/module_entry.cpp:483](../zygisk/src/module_entry.cpp#L483)。这会造成两个问题：

- 不需要 MediaStore 兼容的应用也承担 JNI Hook、Binder 拦截和常驻模块成本。
- “是否安装 Hook”没有成为策略层可见、可诊断的产品选择。

建议增加显式配置项，例如：

```ini
media_compat = off
media_compat = query_filter
```

默认值应为 `off`。只有用户明确选择 `query_filter` 时才安装 Hook；纯 deny/redirect 应用只执行 mount，挂载完成后卸载模块。该字段需要纳入 schema、`policy.bin` 和状态输出，不能通过隐式规则推断。

### 3.2 模块常驻按 Hook 与否分别计算

纯 deny/redirect 只依赖内核 mount，理论上不需要 `.so` 常驻。只有 Hook 函数仍被 Binder 调用时，模块才必须保持加载。

当前 `postAppSpecialize` 仅在 Hook 未安装时卸载：[zygisk/src/module_entry.cpp:520](../zygisk/src/module_entry.cpp#L520)。因此后续指标应拆成：

- 无 Hook 的匹配应用：模块卸载后的额外 RSS/PSS。
- 有 Hook 的匹配应用：Hook 安装后的常驻 RSS/PSS。

此外，Android 构建使用静态 C++ 运行库：[native/Application.mk:3](../native/Application.mk#L3)。`libpathguard_zygisk.so` 当前约 262 KiB，包含大量重定位项。可以单独评估 `APP_STL := none`，但必须先确认 Zygisk 源码不依赖 C++ 标准库 ABI，并进行多 ROM 回归。

## 4. 确定性改动与待验证改动

### 4.1 可以纳入下一批的改动

这些改动的收益方向明确，不依赖某个设备的具体耗时分布：

1. 将 package entry 改为带 hash 的排序索引，并使用二分查找。
2. 将 companion mount 结果直接通过保留的 socket/FD 返回，去掉 marker、`fsync` 和 post 阶段文件轮询。
3. 将 companion 等待失败的应用侧硬上限收紧到数百毫秒量级，在上限内 fail-open 并记录 `failed`/`stale` 状态。
4. 让超时请求具备取消或终止 helper 的协议，避免应用已经放行后 companion 仍在后台改变 namespace。
5. 增加显式 `media_compat` 配置，纯 mount 应用不安装 Hook，挂载完成后卸载模块。
6. 重复 package/rule 检测使用 `unordered_set`，这是局部、低风险的 O(n) 改动。

应用侧 fail-open 后不应静默异步修改正在运行进程的 namespace；若没有完整的 live apply 事务模型，应将该次标记为失败，等待下次重启或由控制面显式重试。

### 4.2 必须先测量再调参的改动

以下改动不能凭经验调整：

- readiness 轮询从固定 10ms 改成什么退避曲线。
- marker/socket 等待超时的最终数值。
- 每应用最大规则数和 mount 批处理方式。
- MediaStore Hook 是否值得保留，以及哪种 Parcel 快路径收益最大。

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
T_marker_or_result  结果同步
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

## 7. 执行顺序

### Phase 0：建立证据

- 增加低频分段计时和聚合计数。
- 建立 package/rule 规模基准。
- 建立 MediaStore 正确性与性能回归集。
- 在至少一台低端、一台中端设备上采样。

### Phase 1：确定性收益

- package hash + 排序 + 二分查找。
- companion 直接结果传递，移除 marker/fsync。
- 收紧应用侧等待上限并实现超时取消。
- `media_compat` 显式开关。
- 纯 mount 应用卸载模块。
- parser 中重复项使用哈希集合。

### Phase 2：针对测量结果优化

- 调整 readiness 退避曲线。
- 优化 MediaStore 最小 Parcel 解析和 descriptor 缓存。
- 评估 `APP_STL := none`。
- 优化规则冲突检测的数据结构。

### Phase 3：独立 ADR

- 论证 Zygote 继承 mmap。
- 论证快照刷新指针和 fork-safe 生命周期。
- 只有在实测显示 mmap 固定成本占主要比例时才实施。

## 8. 验收标准

优化完成后必须同时满足：

- 未命中应用的策略查询不再随 package 数量线性增长。
- companion 超时不会让应用无条件等待 5 秒。
- fail-open 后不会有未取消的后台 helper 继续修改运行中 namespace。
- 默认 deny/redirect 不安装 MediaStore Hook。
- MediaStore compat 场景非媒体 query 能够快速回退，且图片选择、预览、缩略图和普通文件查询均通过回归测试。
- 删除 marker/fsync 后，挂载结果仍具备明确成功、失败和超时状态。
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
