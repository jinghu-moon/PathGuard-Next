# V-07 官方 API 与内核语义约束

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-07` |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| After commit | N/A（实现前外部约束核验） |
| Branch | `feature/pattern-redirect-v6` |
| Access date | `2026-07-29` |
| Classification | `unchanged`（只归档官方约束） |
| Reviewer conclusion | 外部约束与设计边界一致；后续只能通过 runtime probe 准入设备相关能力 |

## 外部约束表

| 主题 | 官方结论 | PathGuard 约束 | 对应测试任务 |
| --- | --- | --- | --- |
| Android scoped storage | Android 11+ 使用 FUSE 让 MediaProvider 在用户态检查共享存储文件操作；MediaProvider 同时是可在 Android release 之外更新的 Mainline 模块 | Provider/FUSE 是动态 adapter，不把 Android 版本、ROM 名称或包名当作 ABI/capability 证明；必须在当前进程和当前模块版本做行为 probe | V-26～V-31、T-18～T-20、V-57 |
| MediaProvider | MediaProvider 通过 MediaStore API 提供索引元数据，并位于可更新的 APK-in-APEX 模块边界 | query/insert/scan/FD 必须作为一组独立一致性能力验证；找到类或符号只算 probe 输入，不能设置 bit 17 | V-28、T-19、I-19、V-29 |
| FUSE passthrough | Android 12 passthrough 由 daemon 在 open 或 create-and-open 时授权并绑定 lower file；后续 read/write 可直接进入 lower filesystem，直到文件关闭 | route、caller identity、generation 和 target FD 必须在 open/create 阶段固定；complete adapter 不能依赖每次 read/write 再进入 matcher | T-18、T-19、T-29、V-47、V-57 |
| DocumentsProvider | `documentId` 由 Provider 定义且对客户端是 opaque durable ID；`openDocument` 返回 `ParcelFileDescriptor`，create/query/rename/delete 是独立 Provider 操作 | 不从 document ID 猜物理路径；source document identity、可见 query、实际 FD 和物理 target 必须共享 route provenance 并逐操作验证 | V-28、T-19、I-19、V-29 |
| Linux `openat2` | `openat2` 始于 Linux 5.6；`RESOLVE_BENEATH` 限制路径留在 dirfd 后代，`RESOLVE_NO_SYMLINKS` 覆盖所有组件且隐含 NO_MAGICLINKS；可能返回 EAGAIN/EXDEV/ELOOP | 不按 kernel version string 判定；按 boot/topology generation 一次性实际 syscall/flag probe，缓存结果；不可用时进入明确的逐组件安全 fallback | V-36、T-23、I-23、V-37 |
| Linux fanotify | `FAN_CLOSE_WRITE` 只表示以写方式打开的文件已关闭；同对象/进程连续通知可合并；队列超限丢事件并产生 `FAN_Q_OVERFLOW` | fanotify 只能承载异步 Observe/Export；必须处理重复、乱序、合并、overflow 和 rescan，不能报告同步 redirect 成功 | V-42、T-27、I-27、V-43、V-59 |
| POSIX `fnmatch` | pathname 模式的 `*`、`?`、bracket expression 在 `FNM_PATHNAME` 下不匹配 `/`；Issue 8 另有可选大小写折叠语义 | Pattern Engine 采用受限、大小写敏感方言；普通 token 不跨组件，字符类不含 `/`；不引入 locale、隐式 dotfile 或 shell 扩展 | T-04～T-08、I-04～I-08 |
| POSIX `pthread_atfork` | child handler 在多线程 fork 后调用任何非 async-signal-safe 函数会产生未定义行为；handler 调用顺序也由注册顺序约束 | child handler 只写预分配的 `sig_atomic_t` dirty flag；清锁、分配、日志、遍历与 snapshot 重建延后到首个正常公共入口 | T-15、I-15、R-15、V-21 |
| Linux RCU requirements | RCU 的核心模型要求对象先完整初始化再发布，并在旧 reader 离开后才回收 | 只借鉴 publish/subscribe 与宽限期模型；用户态实现固定使用 ADR-0011 hazard pointer，不调用或模拟内核 RCU API | T-14、I-14、R-14、V-19 |

## 官方来源

以下页面均于 `2026-07-29` 访问：

1. Android Open Source Project，Scoped storage：
   <https://source.android.com/docs/core/storage/scoped>
2. Android Open Source Project，MediaProvider module：
   <https://source.android.com/docs/core/media/media-provider>
3. Android Open Source Project，FUSE passthrough：
   <https://source.android.com/docs/core/storage/fuse-passthrough>
4. Android Developers，DocumentsProvider：
   <https://developer.android.com/reference/android/provider/DocumentsProvider>
5. Android Developers，DocumentsContract.Document：
   <https://developer.android.com/reference/android/provider/DocumentsContract.Document>
6. Linux man-pages，`openat2(2)`：
   <https://man7.org/linux/man-pages/man2/openat2.2.html>
7. Linux man-pages，`fanotify(7)`：
   <https://man7.org/linux/man-pages/man7/fanotify.7.html>
8. The Open Group Base Specifications Issue 8，`fnmatch()`：
   <https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/functions/fnmatch.html>
9. The Open Group Base Specifications Issue 8，`pthread_atfork()`：
   <https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_atfork.html>
10. Linux kernel documentation，RCU requirements：
    <https://docs.kernel.org/RCU/Design/Requirements/Requirements.html>

## 必须冻结的实现判断

### MediaProvider ABI 不按 ROM 名称推断

Mainline 允许 MediaProvider 脱离 Android 大版本更新，同一 ROM/Android version 不能唯一确定其
内部类、字段和调用链。adapter admission 必须同时验证入口、原始 caller identity、所需 operation
mask 和端到端结果。符号命中、Hook commit 或品牌白名单均不能独立置位 bit 16/17/18。

### FUSE passthrough 的 route 生命周期

passthrough 的 lower file reference 在 open/create-and-open 成功后绑定，并持续到 close。一次打开
必须持有同一 snapshot generation 和 route identity；policy reload 不迁移已打开 FD。后续
read/write 未回到 daemon 是正常路径，不得被误判为 Hook 丢失。

### fanotify overflow 与异步语义

队列容量不是可靠交付保证。`FAN_Q_OVERFLOW` 意味着至少一个事件已经丢失，事件合并也意味着
通知条数不等于文件操作条数。因此 Export 需要幂等 key、恢复状态和 overflow rescan；任何事件
缺失只能降级异步任务状态，不能改变同步 Redirect/Denied 的结果。

### `openat2` capability probe

运行内核不保证与版本字符串或头文件声明一致。probe 使用实际 syscall 和所需 resolve flags，
区分 syscall 不可用、flag 不可用、EAGAIN 可重试以及 EXDEV/ELOOP 安全拒绝。探测结果按
boot/topology generation 缓存，不在每次路径操作试探；fallback 仍必须逐组件固定 dirfd identity，
不能退回字符串 `realpath` 安全判断。

### atfork child 限制

fork 后子进程只保留调用 fork 的线程，父进程其他线程可能把 mutex/hazard owner 状态留在继承
地址空间。child handler 不执行锁、allocator、logger、C++ 容器或 registry 遍历，只设置 dirty
标志。真正的 registry/snapshot 清理与重建发生在正常执行上下文，并在完成前让动态动作
fail-open/unsupported。

## 与当前项目事实的交叉验证

- V-03 中 LocalSend 的 Provider FD 写入与物理重定向成功，但 ModernMediaScanner 和 MediaStore
  query 失败，直接证明 FD 路由不能代表 bit 17 的 query/insert/scan 一致性；
- 当前测试设备 kernel 4.19.157 不能仅凭版本决定所有 backport capability，V-36/T-23 仍必须
  执行 syscall probe；
- 当前 runtime 使用常驻 Provider/Zygisk Hook，不得用“已安装 Hook”代替 capability admission；
- V-06 的 MaterialCleaner/NoMount 证据与官方能力分域一致，但参考项目不能覆盖官方约束。

## 验收结论

- scoped storage、MediaProvider、DocumentsProvider、FUSE passthrough、`openat2`、fanotify、
  `fnmatch`、`pthread_atfork` 和 RCU 均有官方 URL、访问日期和对应测试任务；
- 已明确禁止按 ROM/Android 名称推断 MediaProvider ABI；
- 已冻结 FUSE passthrough 的 open/create route 绑定语义；
- 已冻结 fanotify overflow/合并与异步边界；
- 已冻结 `openat2` 一次性运行时 probe 和安全 fallback；
- 已冻结 atfork child handler 的 async-signal-safe 限制；
- V-07 判定 `complete`。
