# PathGuard Next 重定向子系统设计

> 状态：Draft / Phase R0 Ready
>
> 文档版本：0.2
>
> 日期：2026-07-20
>
> 适用范围：Android 12+、Magisk Zygisk / KernelSU + ZygiskNext

## 1. 结论

PathGuard Next 的同步重定向只使用 mount namespace 和 VFS。配置监控使用 inotify；文件事件监控和写后自动化使用 fanotify。三条路径必须分离：

```text
配置面：rules.ini -> inotify -> compile/validate -> policy.bin
同步数据面：policy.bin -> Zygisk -> companion -> bind mount -> VFS
异步事件面：fanotify -> bounded queue -> audit/export/move/delete
```

禁止使用 PLT Hook、JNI Hook 或 fanotify 写后搬运实现基础 `redirect`。Hook 只能作为用户显式启用的 provider 兼容后端，不得改变基础能力的成功状态。

本版审查结论：设计可以直接进入 Phase R0；进入 R1 挂载实现前，必须完成三项冻结：

- format v4 的 canonical IR、content/plan hash 和 payload checksum 定义；
- fanotify 能力位及 openat2/逐组件 FD walk 的 capability 与真机档位；
- 300ms、fail-open 和 namespace mutation lease 之间的事务 ADR。

在第三项 ADR 完成前，不把“300ms 绝对硬上限、超时始终 fail-open、放行后 namespace 不再变化”作为同时成立的产品承诺。

项目尚未发布，本设计不兼容当前 schema 1 和 policy format v3。实现时直接升级 schema 和二进制格式，删除只支持 deny 的 `DenyPlan`，不增加双格式 reader、迁移分支或废弃字段。

## 2. 目标与非目标

### 2.1 目标

1. 应用对源目录的读、写、列举、元数据查询、创建、删除和 rename 同步落到目标目录。
2. 普通文件访问不进入 PathGuard 用户态热路径。
3. 配置保存后立即编译，受影响应用在可控时间内收敛到新策略。
4. 支持选择性重定向、整盘隔离、真实目录放行、deny 和异步自动化。
5. 规则语法能够由用户直接阅读，所有歧义在编译期拒绝。
6. 每个应用的挂载是可取消、可回滚、可审计的事务。

### 2.2 非目标

- 不承诺对 Root 应用、内核代码执行或已持有文件描述符生效。
- 不隐藏 `/proc/self/mountinfo` 中的挂载痕迹。
- 不把 MediaStore、Photo Picker、SAF 或 CloudMediaProvider 结果自动等同于文件系统视图。
- 不在首版支持单文件、扩展名或 glob 的同步 mount 重定向；这些模式无法直接映射为稳定的目录挂载。
- 不承诺运行中进程无重启地删除或替换已有 mount。

## 3. 参考实现结论

### 3.1 老 PathGuard

老项目提供了丰富规则、PLT 路径改写、MediaStore Hook 和 fanotify 写后动作，但基础语义被拆成了两套实现：

- `->` 在应用进程内逐次改写 libc 路径，动态加载 ELF、直接 syscall 和未覆盖入口可能绕过。
- fanotify 在 `FAN_CLOSE_WRITE` 或 `FAN_MOVED_TO` 后移动文件，写入期间原路径仍然可见，失败后还需处理重复、覆盖和恢复。
- fanotify 的异步队列、`rename`/`copy_file_range`、重试和媒体扫描适合 export、move、trash，不适合同步 redirect。

保留其规则编译、事件队列、同文件系统 rename 和跨文件系统安全复制经验；删除其 PLT 重定向与“写后移动等于重定向”的设计。

### 3.2 Storage Redirect X

SRX 证明了以下方案在工程上有价值：

- 用 `/data/media/<user>` 作为真实内容源或目标源，再 bind 到应用可见的 `/storage/emulated/<user>`。
- 整体重定向存储根后恢复 `/Android` 和允许目录，再叠加更具体的路径映射。
- 显式处理 storage aliases、多用户、shared UID 和 MediaProvider 系统代写进程。

不采用以下实现：

- 每个 specialize 进程扫描和解析全部 JSON 配置。
- marker 文件加固定 50ms 轮询的挂载结果同步。
- 缺少应用侧取消和部分挂载回滚的 mount 流程。
- 为普通重定向引入大范围 PLT、Java、Binder、CursorWindow 和 FUSE 内部 Hook。

### 3.3 reference repos

- AOSP 使用应用 mount namespace、`setns()` 和 bind mount 动态切换存储视图，说明该机制与 Android 平台模型一致。
- AOSP MediaProvider 表明 Android 11+ 的共享存储直接路径由 FUSE/MediaProvider 协作处理，provider 查询和 provider 代开 FD 是独立访问路径。
- `riru_storage_redirect` 的长期变更记录显示 Media Storage、shared UID、child zygote、OEM ROM 和应用交互会持续形成兼容维护面。
- `rvmm-zygisk-mount` 和官方 Zygisk sample 验证了 pre-specialize 连接 Root companion、companion `setns` 后挂载以及不需要 Hook 时卸载模块的基本模式。

## 4. 规则语言 schema 2

规则只使用显式动词。所有用户路径默认相对于当前用户的 `/storage/emulated/<user>`，绝对路径、`.`、`..`、NUL、符号链接穿越和未知选项一律拒绝。

### 4.1 最小选择性重定向

```ini
schema = 2
failure = open

[org.example.app]
users = *
processes = *

redirect DCIM/Example -> PathGuard/{package}/DCIM
deny Pictures/Private
```

语义：应用访问 `DCIM/Example/a.jpg` 时，VFS 实际访问 `PathGuard/org.example.app/DCIM/a.jpg`；其他应用仍看到真实 `DCIM/Example`。

### 4.2 整体隔离

```ini
[org.example.writer]
isolate -> Android/data/{package}/sdcard

allow Download/Public
allow Pictures/Shared
redirect DCIM/Camera -> Pictures/Camera
deny Pictures/Secret
```

`isolate` 将未命中更具体规则的共享存储访问落到应用沙箱。`allow` 仅允许出现在包含 `isolate` 的 section 中，表示恢复同路径的真实目录。

### 4.3 写后自动化

```ini
[org.example.camera]
observe DCIM/Camera
export DCIM/Camera -> Pictures/CameraBackup @mode=copy @media_scan=true
```

`observe` 只产生结构化事件。`export` 是异步动作，不改变应用当次文件访问的路径。后续可增加：

```text
@mode=copy        保留源文件
@mode=move        成功提交目标后删除源文件
@mode=trash       移入受控回收目录
@media_scan=true  成功后请求媒体扫描
```

### 4.4 路径占位符

首版只允许：

- `{user}`：Android user ID。
- `{package}`：完整包名。

不支持环境变量、命令替换、正则表达式或任意模板函数。

### 4.5 决策规则

1. 对输入路径执行组件级规范化。
2. 选择最长路径前缀规则。
3. 同一 source 只能有一个同步动作；相同长度冲突在编译期拒绝。
4. `deny`、`redirect` 和 `allow` 可以按父子路径嵌套，子路径规则优先。
5. 未命中规则时，有 `isolate` 则进入沙箱，否则保持真实路径。
6. `observe`/`export` 属于事件计划，不参与同步动作优先级。
7. `isolate` 的 `backing_path` 在占位符展开后视为隐含保留锚点；普通同步规则的 `visible_path` 与其相同、为其祖先或为其后代时，编译期拒绝。首版不为这类递归可见关系定义隐式语义。
8. 所有 `backing_path` 只相对于 FD 固定的 backend root 解析，不得经由应用可见的 `/storage` 路径反查。

## 5. 统一策略模型

删除 `DenyPlan` 和 `LoadDenyPlan()`，替换为：

```cpp
enum class MountAction : uint8_t {
    Deny,
    Redirect,
    Restore,
    IsolateRoot,
};

struct LogicalMountRule {
    MountAction action;
    StringId visible_path;
    StringId backing_path;
    uint16_t depth;
    uint16_t flags;
};

struct EventRule {
    EventAction action;
    StringId source_path;
    StringId target_path;
    uint32_t options;
};

struct ProcessPlan {
    uint64_t snapshot_generation;
    uint64_t plan_generation;
    FailureMode failure_mode;
    Span<LogicalMountRule> mounts;
    Span<EventRule> events;
    MediaCompat media_compat;
};
```

字段使用 `visible_path` 和 `backing_path`，禁止继续使用含义容易颠倒的 `source`/`target` 表示 mount syscall 参数。

## 6. policy.bin format v4

format v4 是唯一受支持格式，至少包含：

```text
Header
  magic, format=4, schema=2
  file_size
  content_generation (64-bit canonical IR content hash)
  payload_checksum
  package_count, mount_rule_count, event_rule_count
  table offsets

PackageTable     按 package_hash + package name 排序，包含 plan_generation
MountRuleTable   固定宽度、按 parent-first 应用顺序存储
EventRuleTable   固定宽度
StringTable      去重 UTF-8 字符串
```

要求：

- `content_generation` 来自整个规范化 IR 的内容哈希，不再固定为 `1`。
- `content_generation` 表示整个快照；每个 `ProcessPlan` 另带仅覆盖该包有效计划的 `plan_generation`。无关包变化时，运行中进程可以用相同的 `plan_generation` 证明其挂载语义仍然有效。
- R0 必须在共享格式头和 golden vectors 中冻结 content hash、plan hash、payload checksum 的算法、seed、输入字节序和 canonical IR 编码；这些参数未确定前不得把 format v4 标记为完成。
- 64 位内容哈希碰撞属于已知且接受的低概率风险，不维护第二套哈希或定期强制全量比对，也不声称能够检测 `content_generation`/`plan_generation` 碰撞。
- checksum 覆盖整个 payload，Zygisk reader 必须校验，不能只由 Host decoder 校验。
- 未知 action、越界 offset、未终止字符串、无序 package index 和错误 checksum 全部 fail-open 并记录损坏状态。
- `package_hash` 只用于索引；命中 hash 后必须逐项比较完整 package name，不能把 hash 相等视为包名相等。
- 编译器和 Zygisk reader 共享纯 C 格式头与 golden vectors。

## 7. 挂载计划生成

### 7.1 路径域

规则存储逻辑相对路径。运行时展开为：

```text
visible root: /storage/emulated/<user>
backend root: /data/media/<user>
```

`redirect A -> B` 生成的核心操作是：

```text
bind backend(B) -> visible(A)
```

不是把 mount 目标改成 `/data/media`。`/data/media` 是 backing source；应用实际解析的是其 namespace 中的 `/storage` 视图。

### 7.2 storage topology probe

daemon 在启动和存储重挂载后构建只读 topology snapshot：

- 解析 mountinfo，而不是硬编码假定所有 ROM 相同。
- 识别 `/storage/emulated/<user>`、`/mnt/user`、`/mnt/runtime` 和厂商别名。
- 对符号链接别名先解析是否最终落到同一 mount；同一 vfsmount 不重复挂载。
- topology 不满足安全前提时标记 `unsupported`，不回退到 Hook。

### 7.3 预检

companion 在 `setns` 前后分别校验：

- PID、PID start time、UID、完整 package name、snapshot generation 和 plan generation。
- backing 路径位于 `/data/media/<user>` 允许根内。
- visible 路径位于已探测 storage root 内。
- 每个组件拒绝 symlink；优先使用 Linux 5.6 引入的 `openat2(RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS)`，不支持或被 OEM 禁用时使用逐组件 `openat(O_NOFOLLOW | O_DIRECTORY)`。
- `openat2` 和 fallback 都必须返回并持有 `O_PATH`/目录 FD，source 与 target 的最终 mount 必须消费这些已固定对象；禁止“字符串预检成功后，再用原始字符串调用 mount”的 check-use 分离。classic mount 若通过 `/proc/self/fd/<n>` 消费固定 FD，必须有独立真机兼容测试。
- source/target 类型一致，目标目录已由 daemon 以正确 owner、group、mode 和 SELinux label 创建。

### 7.4 应用顺序

mount 必须 parent-first，使更具体的子路径覆盖父路径：

1. `mount(nullptr, "/storage", ..., MS_REC | MS_PRIVATE, ...)`。
2. 建立真实 storage anchor，避免根隔离后丢失真实内容源。
3. 应用 `isolate root`。
4. 恢复 Android 平台必要子树和用户 `allow` 路径。
5. 应用显式 `redirect`。
6. 应用 `deny` tmpfs。
7. 提交共享状态。

默认使用普通 bind；只有确定必须复制子挂载的根隔离操作才使用 recursive bind。

## 8. 挂载事务与故障模型

沿用并泛化当前 `memfd + futex + CAS` 协议，状态至少包含：

```text
pending -> complete | failed
pending -> cancel_requested -> cancelled
pending -> applying -> complete
applying -> cancel_requested | failed
cancel_requested | failed -> rollback_complete
```

helper 每个阶段检查取消：readiness 前后、`setns` 前、每条 mount 前、提交前。每次成功 mount 都写入栈；任何校验、mount、超时或 CAS 失败都按逆序 `umount2(MNT_DETACH)`。

`pending` 状态超时时，应用可以 CAS 为 `cancel_requested` 并安全 fail-open，因为 helper 尚未取得 namespace mutation lease。helper 必须在第一条 mount 前 CAS 为 `applying`。

经典多步 bind mount 下，以下三项不能同时作为绝对保证：应用同步等待硬上限 300ms、超时后 fail-open、应用放行后 namespace 不再发生正向 mount 或回滚。helper 已进入 `applying` 后，应用若立即返回，可能在回滚完成前短暂观察到部分挂载。

R0 必须用独立 ADR 在进入 R1 前选择并验证以下契约之一：

1. 300ms 为绝对硬上限；超时时若已进入 `applying`，终止目标进程并请求回滚。
2. 保持 fail-open；已进入 `applying` 时等待 `rollback_complete`，承认 300ms 只是开始挂载前的取消上限而非绝对等待上限。
3. 先在 detached mount tree 或独立 namespace 完成构造，再用经过设备矩阵验证的原子提交机制切换。

在 ADR 完成前，不得同时宣称“300ms 绝对硬上限”和“超时始终 fail-open”。内部 5 秒 readiness 上限只能被取消，不能直接变成应用同步等待预算。

失败模式：

- `failure = open`：默认。仅在 namespace 尚未变更或回滚完成后继续启动，状态为 `failed`，不安装 provider Hook；`applying` 中的超时行为由上述事务 ADR 决定。
- `failure = closed`：挂载失败时终止目标进程。只有实现并完成真机测试后才允许编译该值；在此之前编译器直接拒绝。

## 9. 实时配置监控

`pathguardd` 是唯一配置 owner：

1. inotify 监听配置父目录，处理 `IN_CLOSE_WRITE`、`IN_MOVED_TO`、`IN_CREATE`、`IN_DELETE` 和 queue overflow。
2. 100-200ms debounce 后进行稳定双读。
3. 原始字节哈希相同则跳过 parse。
4. parse、normalize、validate、compile 全部成功后生成 content generation 和逐包 plan generation。
5. content generation 相同则不发布。
6. 临时文件完整写入并校验后，通过同目录 rename 原子发布。
7. 计算 old/new IR diff，得到受影响 package 集合。

### 9.1 运行中应用收敛

“实时”定义为配置变化立即被发现和编译，不等于对已有 namespace 无事务地强行修改。

- 新进程立即读取新 snapshot generation；运行中进程以 plan generation 判断本包语义是否实际变化。
- 新增且与旧 mount 不冲突的操作可以在后续实现受控 live-add。
- 删除、修改目标、改变 isolate 或父级规则必须 force-stop 受影响普通应用，使下一次启动从空白 namespace 应用完整计划。
- system process、MediaProvider 和 shared UID 组不自动停止；返回 `pending_restart` 并要求显式操作或重启。

开发期优先保证确定性，不实现同时维护两代 mount tree 的复杂兼容层。未来只有在 `open_tree()`、`move_mount()` 等新 mount API 的设备覆盖和原子替换收益经过 ADR 论证后，才考虑无重启 rebase。

`open_tree()`/`move_mount()` 基础 API 从 Linux 5.2 提供；`MOVE_MOUNT_BENEATH` 从 Linux 6.5 提供。版本号只用于规划，实际仍按 syscall/flag 独立探测。`MOVE_MOUNT_BENEATH` 表示把 mount 插入现有顶层 mount 之下，不自动等价于“原子替换整棵挂载树”；ADR 必须先定义 staged tree 的构造、单点提交、旧树处置、回滚和已打开 FD 语义。

## 10. 实时文件事件监控

fanotify 是可选事件面，不参与 `redirect` 正确性：

- mark `/data/media` 对应 mount，按独立能力位请求 FID、name、pidfd 和完整 rename 信息。
- capability bitset 至少拆分为 `fanotify_fid`、`fanotify_dfid_name`、`fanotify_pidfd` 和 `fanotify_rename_target`；不得合并为单一“fanotify 增强”位。
- 主线版本门槛：基础 `FAN_REPORT_FID` 和 create/delete/moved 目录项事件为 Linux 5.1；`FAN_REPORT_DFID_NAME` 为 5.9；`FAN_REPORT_PIDFD` 为 5.15，并回溯到 5.10.220；完整 `FAN_RENAME`/`FAN_REPORT_TARGET_FID` 为 5.17，并回溯到 5.15.154 和 5.10.220。Android common kernel 与 OEM kernel 可能独立回溯，运行时探测结果优先于 `uname` 版本。
- 基础事件集：close-write、moved-to、create、delete、rename。没有 `FAN_REPORT_DFID_NAME` 时，目录项名称不保证能通过 `readlink(/proc/self/fd/<n>)` 等价恢复；无法可靠恢复的事件类型必须禁用或降级为不带路径的审计，不得执行路径相关破坏性动作。
- 没有完整 rename target 信息时，可将 moved-from/moved-to 作为两个独立事件处理，但不得声称具备原子 rename 归因。支持 `FAN_REPORT_PIDFD` 也不代表每个事件都一定返回有效 pidfd，归因可靠性必须逐事件判断。
- 单个 epoll reactor 同时处理 fanotify、inotify、控制 UDS、signalfd 和 worker 完成通知。
- 文件复制、媒体扫描等阻塞工作进入有界 worker queue；队列满时丢弃低优先级 observe，不能阻塞 mount 或配置编译。
- 事件幂等键至少包含 plan generation、fsid/file handle、事件类型和时间窗口。
- `FAN_MOVED_TO` 无可靠 PID/package 时，只执行全局规则；不得猜测包名后执行破坏性动作。

安全提交：同文件系统优先 `renameat2`；跨文件系统使用临时目标、`copy_file_range`/read-write fallback、权限与大小校验、可选 fsync、原子 rename，最后才删除源文件。

## 11. MediaStore、Photo Picker 与 SAF

文件系统 redirect 只保证目标进程直接路径访问。以下能力独立声明：

```ini
media = off
media = hide_denied
```

首版保留 `hide_denied`，只用于 deny 查询过滤。redirect 不自动改写 MediaStore 行、Photo Picker 选择结果或 provider 返回的 FD。

未来若实现 `media = virtualize`，必须单独设计：

- 调用方身份与 shared UID。
- query、openFile、insert、update、delete、rename 和 CursorWindow。
- MediaProvider 主线版本与 OEM 差异。
- provider 崩溃时的完整 fail-open。
- 每种 transaction 的性能与 UI 回归。

该字段开始进入 schema 时必须沿用 `failure = closed` 的编译期锁存模式：在实现、能力探测和真机矩阵全部解锁前，编译器即使识别该值也必须拒绝生成可执行计划。

在上述设计完成前，不复制 SRX 的广域 Hook 栈。

## 12. 性能设计

### 12.1 快路径原则

- Zygisk 不解析 INI/JSON，不扫描配置目录，不启动线程。
- 未命中包只做 open/mmap、hash 二分查找并请求 dlclose。
- mount 完成后普通文件访问只有 VFS/FUSE 固有成本。
- 未启用 media compat 的匹配应用也卸载 Zygisk `.so`。
- observe/export 关闭时不启动 fanotify worker。

### 12.2 初始预算

| 指标 | 目标 |
|---|---:|
| 未配置应用额外启动耗时 P95 | `< 0.5ms` |
| policy lookup P95 | `< 0.1ms` |
| 16 条 mount plan P95 | `< 10ms`，按设备分档复核 |
| 应用同步等待目标 | `300ms`；绝对上限与 `applying` 超时语义由 R0 事务 ADR 冻结 |
| 1000 package 编译 P95 | `< 10ms` Host 基线 |
| daemon 空闲 CPU | 事件驱动，长时间窗口接近 0 |
| daemon 稳态 RSS | `< 3MiB`，启用 event worker 单独统计 |

每项报告 P50/P95/P99/max/sample count。不得用算法复杂度或桌面 Host 数据替代真机结果。

## 13. 验证矩阵

### 13.1 Host

- schema 2 parser golden tests。
- 所有冲突、环、父子覆盖、非法占位符和 symlink 路径测试；显式覆盖 isolate backing_path 与普通规则 visible_path 的相同、祖先和后代关系。
- format v4 损坏、checksum、bounds fuzz；`package_hash` 碰撞必须验证完整包名比较。`content_generation`/`plan_generation` 碰撞是已接受风险，不列为可检测能力。
- package 1/10/100/1000/10000，mount rules 0/1/4/16/32/64 基准。
- old/new snapshot generation diff 和逐包 plan generation 稳定性测试；无关包变化不得使该包进入 `pending_restart`。

### 13.2 真机文件语义

- open/stat/opendir/read/write/create/unlink/rename/renameat2。
- Java File、Kotlin、NDK、直接 syscall 和运行时加载 native library。
- 父 redirect + 子 allow/redirect/deny。
- 应用子进程、remote process、isolated process、shared UID 和多用户。
- `/sdcard`、`/storage/self/primary`、`/mnt/user`、`/mnt/runtime` 及 OEM aliases。
- FUSE passthrough 开/关设备、低端/中端设备和至少两个 ROM 系列。
- 独立必测“内核或 backport 不提供 openat2，走逐组件 FD walk”档位；设备性能档位不能替代内核能力档位。
- fanotify 至少覆盖：仅基础 FID、DFID_NAME 无 pidfd、pidfd 无完整 rename target、完整 rename target 四种 capability 组合。无法获得真实设备时用可控内核/测试桩补齐 Host 协议测试，但不得替代至少一台降级真机。

### 13.3 事务

- 第 N 条 mount 失败后前 N-1 条全部回滚。
- `pending` 超时并成功取消后，迟到 helper 不得取得 mutation lease 或产生 mount。
- helper 已进入 `applying` 时，分别验证事务 ADR 选定的终止、等待回滚或 staged commit 契约，不得用 `pending` 测试替代。
- PID 复用、进程退出、snapshot/plan generation 变化和 daemon 重启。
- 配置删除/修改后 force-stop，再启动只存在新 plan generation mounts。

### 13.4 事件面

- close-write、rename into watched tree、大文件跨文件系统复制。
- queue overflow、重复事件、daemon crash/restart 和磁盘空间不足。
- 无可靠 package attribution 时不执行 package-specific destructive action。

## 14. 实施阶段

### Phase R0：破坏性模型重构

- schema 2、format v4、content generation。
- 冻结 content/plan hash、payload checksum 和 canonical IR 编码。
- capability bitset 为 `FAN_REPORT_DFID_NAME`、`FAN_REPORT_PIDFD` 和完整 rename target 保留独立稳定位。
- 完成 300ms、fail-open 与 namespace mutation lease 的事务 ADR；ADR 未完成不得进入 R1 mount executor。
- `DenyPlan` -> `ProcessPlan`。
- 编译器只接受真正可执行的 failure mode 和 action。

### Phase R1：选择性 redirect

- topology probe、backend path resolver、通用 mount executor。
- openat2 与逐组件 FD walk 两条路径都必须以固定 FD 驱动最终 mount，并纳入独立真机档位。
- deny + redirect 混合事务、取消和回滚。
- 直接文件系统真机矩阵。

### Phase R2：isolate + allow

- real storage anchor、storage root redirect、Android restore、nested overrides。
- 多用户、shared UID 和 alias 验收。

### Phase R3：实时收敛与状态

- snapshot/plan generation diff、affected package、普通应用 force-stop/restart。
- `status`/`explain`/`probe` 和 applied plan generation；无关包变化后不得误报 `pending_restart`。

### Phase R4：observe/export

- fanotify capability probe、epoll reactor、有界 worker、幂等任务。
- copy/move/trash/media scan。

### Phase R5：provider compat

- 保留并强化 deny query filter。
- redirect provider virtualization 必须先通过独立 ADR。

## 15. 采用与拒绝清单

| 来源 | 采用 | 拒绝 |
|---|---|---|
| 老 PathGuard | 规则编译、fanotify 自动化、bounded queue、安全 copy/move | PLT 基础重定向、写后移动冒充同步 redirect |
| SRX | `/data/media` backing、root isolation、allow restore、alias/shared UID 思路 | 每进程 JSON、marker 轮询、广域 Hook、无回滚 mount |
| AOSP | app namespace、setns、bind mount、FUSE/provider 分层 | 假定所有 OEM topology 相同 |
| Zygisk sample | pre 阶段 companion、无需 Hook 时 dlclose | 在 post 阶段尝试 Root 文件访问 |

## 16. 参考资料

### 16.1 本地代码

- 老 PathGuard：`D:/100_Projects/110_Daily/PathGuard/native/folder_manager.cpp`
- 老 PathGuard fanotify：`D:/100_Projects/110_Daily/PathGuard/native/folder_manager_daemon.cpp`
- SRX mount：`refer/Storage-redirection-X-Public-main/srx_core/src/mount/`
- SRX lifecycle：`refer/Storage-redirection-X-Public-main/srx_core/src/lifecycle/`
- AOSP MediaProvider：`refer/pathguard-reference-repos/AOSP-MediaProvider/`
- Riru Storage Redirect：`refer/pathguard-reference-repos/riru_storage_redirect/`
- Zygisk sample：`refer/pathguard-reference-repos/zygisk-module-sample-master/`

### 16.2 官方网页

- AOSP Storage：https://source.android.com/docs/core/storage
- AOSP Scoped storage：https://source.android.com/docs/core/storage/scoped
- AOSP FUSE passthrough：https://source.android.com/docs/core/storage/fuse-passthrough
- AOSP vold VolumeManager：https://android.googlesource.com/platform/system/vold/+/refs/heads/main/VolumeManager.cpp
- Linux mount namespaces：https://man7.org/linux/man-pages/man7/mount_namespaces.7.html
- Linux mount(2)：https://man7.org/linux/man-pages/man2/mount.2.html
- Linux openat2(2)：https://man7.org/linux/man-pages/man2/openat2.2.html
- Linux move_mount(2)：https://man7.org/linux/man-pages/man2/move_mount.2.html
- Linux inotify(7)：https://man7.org/linux/man-pages/man7/inotify.7.html
- Linux fanotify(7)：https://man7.org/linux/man-pages/man7/fanotify.7.html
- Linux fanotify_init(2)：https://man7.org/linux/man-pages/man2/fanotify_init.2.html
- Christian Brauner，Mounting into mount namespaces：https://brauner.io/2023/02/28/mounting-into-mount-namespaces.html
- 官方 Zygisk API：https://github.com/topjohnwu/zygisk-module-sample/blob/master/module/jni/zygisk.hpp
