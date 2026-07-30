# ADR-0011：Pattern 快照使用原子指针与 hazard pointer 发布

状态：Accepted

日期：2026-07-29

## 背景

Pattern Engine 位于 Provider 和应用文件操作的同步热路径。policy 热更新必须满足：

- 读路径不取得全局 mutex，不在每次匹配时分配内存；
- 一个文件系统操作从匹配到动作执行只观察一个完整 generation；
- rename/link 等双路径操作不能一半读取旧表、一半读取新表；
- writer 可以发布新候选表，并在最后一个旧 reader 退出后安全回收旧表；
- Android/Zygote fork、线程退出和注册失败不能造成悬空引用或死锁。

单纯原子交换裸指针不能解决旧对象回收；读写锁破坏热路径不加锁目标；引用计数会在每次匹配
产生共享 cache line 写入；QSBR/epoch 需要可靠的 quiescent-state 协议，现有 Provider/app
线程生命周期没有这样的统一边界。

## 决策

采用单 writer、不可变 `MatcherSnapshot`、原子 active pointer 和 TLS hazard pointer：

1. loader 在私有内存中完成 policy 校验、索引构建和 action admission，构建完成前不可见。
2. active 只保存一个 `MatcherSnapshot*`。snapshot 内含不可变的 plan generation、索引、
   selector/action tables 和 capability requirements。
3. 进程初始化时分配固定容量、地址终生稳定的 hazard slot array。首版按 adapter profile
   冻结为：普通 app-path 进程 128 slots，Provider/SAF 系统代写进程 256 slots；未来 FUSE
   daemon profile 初值为 256，但只有 ADR-0010 的原型进入实现时才启用。每个参与匹配的线程
   通过 TLS 持有其中一个 slot。slot 注册/复用允许走带锁慢路径；注册完成后的每次读取不加锁、
   不分配。writer 始终扫描稳定 array，不扫描可能被线程退出释放的节点。
4. 首版所有 active/hazard 发布与扫描使用 sequentially consistent 原子操作。读者执行：

```text
p = active.load()
hazard.store(p)
if active.load() != p: retry
read immutable snapshot p
```

5. writer 构造新 snapshot 后执行原子 exchange，把旧 snapshot 放入 retire list。扫描所有
   hazard slots 后，只回收不再被任何 slot 引用的 retired snapshot。writer 不阻塞 reader。
6. 一次 `OperationContext` 持有一个 snapshot guard，所有 path operands、MatchSet 和
   ActionEvaluator 都使用该 guard；操作结束后才清空 hazard。
7. 不使用 128 位 tagged pointer。标准 hazard 获取协议在二次 active 校验成功后保护当前地址；
   generation 从已经受保护的不可变 snapshot 中读取。这样避免依赖 arm64 上未必 lock-free 的
   双字 CAS。
8. pthread TLS destructor 先清空 hazard，再释放 slot owner 供其他线程复用；slot 存储本身到
   进程退出才释放。初始化时注册 `pthread_atfork(nullptr, nullptr, ChildHandler)`。child
   handler 只给预先存在的 `volatile sig_atomic_t post_fork_dirty` 赋值，不加锁、不分配、不遍历
   slots。reader、writer 和 slot registration 的每个公共入口都先读取该标志；发现变化时，在
   正常执行上下文中先停用继承的 active snapshot/admission，清空 owner/hazard 和 TLS binding，
   回收子进程中已无 reader 引用的 retired snapshots，再用当前进程 identity/capability 重新构建
   snapshot。重建完成前动态动作 fail-open。该惰性重建是统一入口协议，不依赖每个调用方手工
   记得清理，也不在每次 Match 中增加 `getpid()` syscall。`pthread_atfork` 注册失败时 adapter
   不得 active。slot array 分配失败或已满时，当前操作 fail-open，返回
   `RuntimeUnavailable` 并把 adapter health 标记为 degraded。
9. retired snapshot 首版每进程上限为 8 个且合计不超过 8 MiB，两项先到者生效。writer 在
   exchange 前先尝试回收并检查剩余容量；容量不足时拒绝本次 reload、保留当前有效 snapshot。
   不得先发布再突破上限，不得阻塞文件操作线程等待宽限期，也不得提前释放旧对象。扩大容量
   必须结合下述真实设备 high-water mark 修订本 ADR，不能只改常量。

## 可观测性

P1 起按进程和 adapter profile 维护无分配、饱和递增的计数器/高水位：

- `hazard_slot_acquire_fail_total`；
- `hazard_slots_in_use_high_watermark`；
- `snapshot_reload_rejected_retire_limit_total`；
- `retired_snapshot_count_high_watermark`；
- `retired_snapshot_bytes_high_watermark`；
- `post_fork_registry_rebuild_total`。

status/explain 输出当前值、adapter profile、slot capacity、retire limits 和 generation。日志只在
首次失败及限速窗口内记录，不能在文件热路径逐次打印。槽位失败或 retire 拒绝计数非零必须
进入真机验收报告；否则无法判断固定容量是否造成隐性 fail-open。

首版选择顺序一致原子是正确性优先的 KISS 决策。只有基准证明原子屏障成为显著瓶颈，并有
C++ 内存模型证明、弱内存压力测试和独立 ADR 时，才允许放宽 memory order。

## 否决方案

### `shared_mutex`/读写锁

否决。Provider 文件操作会在 writer 或异常 reader 下排队，无法兑现热路径不加锁，也放大
策略 reload 对用户操作的尾延迟。

### 原子 `shared_ptr`/每次引用计数

否决首版采用。它实现简单，但每次读取都修改共享引用计数，且当前 Android NDK/C++ 标准及
lock-free 性质不能作为跨设备保证。可作为将来有数据支持的替代方案重新评估。

### epoch/QSBR

否决首版采用。它需要所有 reader 可靠报告 quiescent state，Provider 动态线程、长操作和
Zygote fork 会增加回收停滞与生命周期复杂度。

### 永不回收旧 snapshot

否决。虽然读路径最简单，但配置反复更新会形成无界内存增长。

## 正确性不变量

- snapshot 发布前完全初始化，发布后不可变；
- 未成功发布的新 policy 不影响当前 snapshot；
- reader 只在 hazard 二次校验成功后解引用 snapshot；
- snapshot 被任何 hazard 引用时不得回收；
- MatchSet 不得脱离产生它的 snapshot guard 使用；
- capability snapshot 改变时生成新的 admission generation，不原地修改 MatcherSnapshot；
- fork 后父进程的 hazard registry 不被子进程当作有效状态。

## 验证门槛

- TSAN host 测试覆盖并发 Match/reload、线程注册/退出和 retire 扫描；
- 弱内存压力测试覆盖 hazard publish 与二次 active 校验；
- rename/link 在并发 reload 下始终使用同一 generation；
- Zygote fork 模拟测试证明子进程 registry 重建；
- reader 长时间持有旧 snapshot 时 writer 不阻塞 reader、不提前回收，且 retire 上限生效；
- app-path 128 slots、Provider 256 slots 的边界和耗尽故障注入均产生准确计数；
- atfork child handler 不调用锁、allocator、日志或其他非 async-signal-safe 路径，首次正常入口
  只执行一次幂等重建；
- `pthread_atfork` 注册失败时 adapter 保持 unsupported，不能退回调用方手工清理；
- 空 plan 和无 pattern 热路径零分配、无 mutex。

## 后果

- P1 必须实现 snapshot 生命周期和发布协议，P2 只能消费该接口，不能自建缓存。
- writer 和线程首次注册路径更复杂，但复杂性集中在一个组件，不泄漏到 matcher/action。
- 读路径固定为少量原子操作；性能基线必须在 P0/P1 建立。

## 实施记录

`feature/pattern-redirect-v6` 的 Zygisk `APP_STL=none` 路径使用专用固定容量实现，但必须与本 ADR
共享完全相同的不变量。`pathguard_policy_snapshot_domain_test` 直接编译该生产头，覆盖 slot 耗尽、
retire 保护、oversize 拒绝以及 fork 后先发布再读取时的 stale TLS binding 重注册；不得仅以 Host
`snapshot_publisher.h` 的测试替代生产实现证据。

## 依据

- [Pattern redirect design §5.5](../08-pattern-redirect-design.md)
- [Linux RCU publish/subscribe requirements](https://docs.kernel.org/RCU/Design/Requirements/Requirements.html)
- [POSIX pthread_atfork](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_atfork.html)

Linux RCU 文档只提供“初始化后发布、旧 reader 退出后回收”的模型依据；具体 userspace
hazard-pointer 正确性由本 ADR 的不变量和测试门槛负责，不能直接套用内核 RCU API。
