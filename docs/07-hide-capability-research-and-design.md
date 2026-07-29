# PathGuard Next `hide` 能力调研与候选设计

> 状态：Research / Proposed；尚未进入实现，不改变当前 `deny` 语义
>
> 文档版本：0.7
>
> 日期：2026-07-28
>
> 适用范围：Android 12+ 共享存储；Magisk Zygisk / KernelSU + ZygiskNext；目标应用无 Root 权限

## 1. 结论

2026-07-28 H0 已完成并选择决策门第 4 项：**当前设备与既定约束下保持 exact `hide` unsupported；内核补丁仍只作为所有较低侵入方案失败、且完整构建与回滚前提获批后的最后方案。** 当前产品仍只提供 `deny` 与 `redirect`。本文件冻结威胁模型、证据、候选顺序和重新准入门槛，不是 `hide` 实现承诺。

`hide` 必须是独立于现有 `deny` 的能力，不能作为 `deny` 的新名称、开关或隐式升级。

当前 `deny` 通过应用私有 mount namespace 中的空目录 bind mount 实现，保证目标目录内容不可访问，但目录挂载点仍可能被 `stat()` 观察到，访问通常表现为 `EACCES`，新增挂载也会出现在目标进程的 `/proc/self/mountinfo`。这些行为符合现有设计，不是缺陷。

本设计将 `hide` 的核心语义定义为：

```text
readdir(parent)       不返回目标名字
stat/open/opendir     对目标及后代返回 ENOENT
直接 syscall          不得绕过
MediaStore query      不返回目标及后代记录
/proc/self/mountinfo  不出现由 hide 新增的伪装挂载
```

调研结论如下：

1. **通用 stock kernel 上没有一个仅靠 bind mount、OverlayFS、应用进程 Hook 或权限位即可完整满足上述语义的方案。**
2. **OverlayFS：Rejected / Do not implement。** Whiteout 会虚拟化父目录、改变兄弟项写入语义并留下 mountinfo；它违反 mount stealth 和兄弟项语义不变两个硬约束。
3. **MediaProvider FUSE 私有接入：Rejected / H0.2 Kill。** AOSP 实现原生拥有请求 UID、lookup、readdir 和 `ENOENT` 语义，但目录在公开可定位的 `FileLookup` 前已提前返回；完整路径、请求 UID 与 cache 控制只在私有静态路径汇合。正确实现需要 inline patch/私有布局或全局关闭 dentry cache，命中既定 kill criteria。
4. **LKM-PathMask：Reference / Anti-pattern。** 它的 KMI 打包与诊断有参考价值；其 post-syscall 返回值改写、用户缓冲重写和可丢失 probe 不得用于 exact hide 生产数据面。
5. **SUSFS 2.2：Blocked。** namei/getdents 内联机制值得参考，但当前 ABI 没有 per-namespace/per-policy scope；在此条件满足前不得编写 PathGuard 生产 adapter。
6. 从纯机制看，满足 direct VFS exact hide 且不新增挂载的最可信方向仍是最小内核 VFS 内联后端；从交付角度看，当前 alioth 测试环境为非 GKI Linux 4.19.157，仓库也没有对应完整内核构建基线，因此它只能是**最后方案**，不能越过 stock-kernel spike、稳定 LKM 接口核验和上游 ABI 观察直接开工。
7. 内核 VFS 后端只解决目标应用自己的直接文件访问。MediaStore、Photo Picker、SAF、CloudMediaProvider 和已经代开的 FD 是独立访问面，必须分别声明 capability，不能用一句“hide 已启用”概括。
8. `hide` 后端不可用或运行时安装失败时，**不得静默退化成 `deny`**。两者的可观察结果不同，降级会违反用户意图。

因此当前不修改 `rules.toml` schema，不升级 `policy.bin`，不增加 Manager 开关，也不向用户暴露尚不能兑现的 `hide` 配置。H0 已严格按第 11 节顺序完成；内核补丁虽是剩余机制方向，但没有满足构建、回滚与维护准入，未启动。

## 2. `deny` 与 `hide` 的边界

| 观察行为 | 当前 `deny` | 目标 `hide` |
|---|---|---|
| `readdir(parent)` | 目标名字通常仍存在 | 目标名字不存在 |
| `stat(target)` | 挂载点可能存在 | `ENOENT` |
| `open/opendir(target)` | 通常 `EACCES` | `ENOENT` |
| 后代访问 | 被空目录/权限阻断 | `ENOENT` |
| 直接 syscall | VFS mount 生效 | VFS/filter 生效 |
| mountinfo | 可见 bind mount | hide 不新增 mount |
| MediaStore | 需要独立 query filter | 仍需要独立 query filter |
| SAF/Photo Picker | 不属于基础保证 | 仍需独立 capability |
| 设备覆盖 | 标准 Root + mount namespace | 取决于内核或 Provider 后端 |

规则层未来应使用两个明确动词：

```toml
deny = ["Pictures/Private"]
hide = ["Pictures/Secret"]
```

禁止设计以下形式：

```toml
deny_mode = "hide"
deny = [{ path = "Pictures/Secret", invisible = true }]
```

原因是 `deny` 与 `hide` 的执行后端、错误码、可观察性、设备要求和失败策略均不同。把它们塞入同一开关会制造隐式降级和无法解释的兼容差异。

## 3. 威胁模型和保证等级

### 3.1 受保护对象

首版只研究共享存储中的目录级规则，例如：

```text
/storage/emulated/0/Pictures/Nagram
/storage/emulated/0/DCIM/Screenshots
```

文件级、glob、扩展名、正则表达式和内容特征匹配不在首版范围。目录级能力与当前规则编译器的规范化、父子折叠和冲突检测模型一致。

### 3.2 攻击者能力

目标应用可以：

- 使用 Java `File`、NIO、libc 或原始 `syscall`；
- 使用绝对路径、相对路径、`dirfd`、storage alias 和符号链接；
- 调用 `getdents64`、`statx`、`faccessat2`、`openat2` 等入口；
- 查询 MediaStore；
- 读取 `/proc/self/mountinfo`、`mounts` 和 `mountstats`；
- 创建子进程或使用同一应用的多进程组件。

### 3.3 明确不保证

- Root 应用、内核代码执行、ptrace 高权限攻击者；
- `hide` 安装前已经持有的目录或文件 FD；
- 另一个系统 Provider 已经代开并通过 Binder 交给应用的 FD；
- 系统相册、Photo Picker、SAF、CloudMediaProvider 在对应 capability 未实现时的结果；
- 对主动写入探测者做到信息论意义上的“与从未存在完全不可区分”。

最后一项必须特别说明。若父目录可写，真正不存在的名字通常允许 `mkdir()`；隐藏一个真实目录后，为防止触达真实对象，首版应让针对隐藏名字的创建、删除、rename 和 link 操作统一失败。即使统一返回 `ENOENT`，恶意应用仍可能通过写入行为、时序或系统 Provider 侧信道推断异常。要完全模拟“名字从未存在且可重新创建”，需要给应用提供私有 shadow entry，问题会升级为完整可写隔离文件系统，不属于本能力。

因此首版产品措辞应是：

> 对目标应用的受支持直接文件访问，隐藏目录不可发现且不可访问。

不得宣称：

> 任何软件、任何系统服务、任何侧信道下都无法证明目录存在。

## 4. 平台事实

### 4.1 mount namespace 不能隐藏自己的挂载表

Linux mount namespace 隔离的是进程看到的挂载树；`/proc/<pid>/mountinfo` 展示的正是该 PID 所在 namespace 的挂载，包含 mount ID、父子关系、bind root、mount point、文件系统类型和 source。[S1][S2]

所以只要 `hide` 依赖新的 bind、tmpfs、OverlayFS 或 FUSE mount，目标应用就能从 mountinfo 观察到至少一个新挂载。再对 `/proc` 做字符串过滤只是增加第二个伪装层，原始 syscall、其他 proc 文件、mount ID 连续性和不同内核版本都会扩大维护面。

如果目标是“hide 本身不产生 mountinfo 痕迹”，最简单可靠的原则是：

```text
hide 后端不调用 mount()
```

这不代表同一应用的 `redirect` 或现有 `deny` 挂载也会消失。若规则同时使用 mount 类能力，mountinfo 仍会看到那些挂载。

### 4.2 bind mount 无法删除父目录中的名字

把空目录 bind 到目标路径，只替换该 dentry 之后的解析结果。父目录仍包含原名字，因此：

```text
readdir("Pictures")  -> 仍可能返回 "Nagram"
stat("Pictures/Nagram") -> 观察到挂载点
```

这正是当前 `deny` 的设计边界。对目标本身叠加更多 mount flags、权限或只读 remount 都不会改变父目录的目录项列表。

### 4.3 OverlayFS whiteout 能隐藏名字，但代价不是常数

Linux OverlayFS 使用 whiteout 表示“upper 层删除了 lower 层同名对象”。whiteout 自身不出现在合并目录中，lower 的同名对象也被忽略，因此 `readdir` 和 lookup 语义符合 hide。[S3]

问题在于 whiteout 必须作用于**目标的父目录视图**：

```text
lower = 真实 Pictures
upper = 含 Nagram whiteout 的私有目录
merged -> 应用可见 Pictures
```

这会带来四个不可接受或待验证的变化：

1. merged 是新增挂载，mountinfo 可见；
2. 兄弟文件的写入、rename、chmod 等会触发 copy-up，真实共享存储与应用视图可能分叉；
3. upper/workdir 必须满足 xattr、`d_type`、同文件系统等 OverlayFS 要求；lower 虽可来自较多文件系统，但并非所有可挂载文件系统都可用；
4. Android 11+ 共享存储以 MediaProvider FUSE 为主要处理层，FUSE lower、SELinux、OEM 内核和 storage alias 组合必须逐机验证。[S3][S4]

因此 OverlayFS 不进入正式后端候选，也不再安排实现 spike。它已经违反“hide 不新增 mount”和“兄弟项保持真实写入语义”两个冻结约束，快速开发期应立即停止该路线。

### 4.4 Android 共享存储不是单一 VFS 视图

Android 11 起，MediaProvider 作为共享存储 FUSE handler，可以检查直接文件路径操作；MediaStore 同时维护媒体数据库。[S4][S5]

AOSP `FuseDaemon.cpp` 显示：

- FUSE 请求包含 `req->ctx.uid` 与 PID；
- lookup 拒绝时可以返回 `ENOENT`；
- readdir 会先按 UID 获取目录项，再跳过 lookup 返回 `ENOENT`、`EACCES` 等错误的项目；
- 为防止应用猜测 `Android/data` 和 `Android/obb` 中其他包是否存在，AOSP 会把相关 dentry timeout 设为 0；
- FUSE BPF 当前只用于 `Android/data` 与 `Android/obb` 的特定路径，不能假设它能过滤任意 Pictures/DCIM 规则。[S6]

这证明“按 UID 隐藏目录项并返回 ENOENT”与 Android 自身模型一致，也证明缓存失效是正确性的一部分。

但 MediaStore 查询、Photo Picker、SAF DocumentsProvider 与 CloudMediaProvider 仍是不同进程、数据库或远端数据源。改变目标应用自己的直接路径视图，不会自动改变这些系统 UI 和 Provider 的结果。[S7][S8]

## 5. 参考项目结论

### 5.1 LKM-PathMask v2.5.0

本地 `refer/hide-refer/LKM-PathMask-main` 与上游 commit `2b1b6dcde36f010c29af5ed28a144fb366b50435` 的 `pathmask.c` SHA-256 相同，因此以下结论针对当前 v2.5.0，而不是旧版 README 推断。[R1]

#### 5.1.1 实际数据模型和作用域

模块加载时用 `kern_path(..., LOOKUP_FOLLOW)` 把最多 16 个已存在目标解析为 `(s_dev, i_ino)`，同时保留原始绝对路径文本。目标不存在时直接跳过；rename、删除后重建或 FUSE inode 变化不会自动更新，只能 `rmmod + insmod` 重新解析。

作用域支持 `global`、`deny`、`allow`，但实现主键是 `current_uid/current_euid/current_fsuid` 与最多 1024 个 UID 的线性表。包名只在 `service.sh` 中被解析成 UID，因此：

- shared UID 的多个包无法分离；
- isolated UID 只能按固定 Android UID 区间整体纳入；
- 规则与应用 private mount namespace 无绑定关系；
- 每次命中 hook 都会做 UID 和目标数组线性扫描。

#### 5.1.2 实际拦截链

v2.5.0 同时注册三组 kretprobe：

1. `inode_permission` 与 `vfs_getattr`，按目标 inode 改返回值为 `-ENOENT`；
2. 7 个 `__arm64_sys_*` syscall stub 兜底：`newfstatat`、`statx`、`faccessat`、`faccessat2`、`readlinkat`、`openat`、`openat2`；默认关闭可被时序检测的 `faccessat`；
3. `__arm64_sys_getdents64` 返回后复制并压缩用户 dirent 缓冲，按 inode 号删除目标项。

第二组并不解析 VFS 路径。`sys_path_matches_target()` 明确要求参数首字节是 `/`，再做原始字符串前缀比较。因此相对 `dirfd`、不同 storage alias、`..`、符号链接入口和其他等价路径不由 syscall 兜底覆盖，只能寄希望于前两组可能已被 ThinLTO 内联掉的 VFS probe。

`openat/openat2` 更关键：真实 syscall 先执行完成，exit handler 看到成功 FD 后调用动态解析的 `close_fd()`，再把返回值改成 `-ENOENT`。关闭 FD 不能撤销 `O_CREAT`、`O_TRUNC`、设备打开或文件系统回调已经产生的副作用。模块也没有覆盖 `mkdirat`、`unlinkat`、`renameat2`、`linkat`、`symlinkat` 等 mutation 入口。

#### 5.1.3 目录过滤和并发边界

`getdents64` hook 每次在 entry handler 中按用户 buffer 大小执行 `kmalloc(..., GFP_KERNEL)`，上限 64 KiB；exit handler 再执行 `copy_from_user`、线性扫描、`memmove` 和 `copy_to_user`。存在以下确定边界：

- 只比较 `d_ino`，没有同时比较 superblock、parent 或 basename，可能误伤其他文件系统的同 inode 号；
- 返回长度超过已分配缓冲、分配失败、用户复制失败时直接跳过，源码日志明确写出 `directory may leak`；
- `getdents64.maxactive = 20`，其他 probe 为 40；Linux Kprobes 文档说明实例耗尽会跳过 entry handler 并增加 `nmissed`，该模块没有读取或报告 `nmissed`；[S9]
- Kprobes 文档要求 handler 不得 yield，但这里在 handler 中使用可能睡眠的 `GFP_KERNEL` 分配和可能触发用户页 fault 的 uaccess；这不适合作为可证明 fail-closed 的生产路径；[S9]
- 所有设备、所有相关 syscall 都先经过全局 probe，未命中 App 也承担入口开销；项目 changelog 已记录 `faccessat` trampoline 被检测器通过时序识别。

因此 PathMask 的价值是证明“外部 LKM + KMI 矩阵 + UID scope + WebUI 诊断”可交付，不是证明当前 hook 算法满足 exact hide。PathGuard 可借鉴其 KMI 打包、启动等待、包名到 UID 诊断和连续加载失败保护；不得复制其 post-syscall rewrite 数据面。

### 5.2 SUSFS 1.3.8 与 2.2

必须区分本地两代源码：

- `refer/hide-refer/susfs4ksu-master` 的 README 明确声明 `master` 永久停在 1.3.8；
- `refer/susfs4ksu` 和上游按内核版本维护的分支代表 2.x 设计。本次额外核验了 `gki-android14-6.1` commit `8eade9cd4aed3efddc9ff30b2e48d2d9667ad77d`，其头文件报告 SUSFS v2.2.0。[R2][R3]

旧 1.3.8 通过路径字符串、链表和多个 syscall/VFS patch 实现隐藏，不能代表当前能力。2.2 的重要变化是：

1. 自 2.0 起不再依赖 kprobe/kretprobe，而是在已知内核源码和 KernelSU 流程中做内联修改；
2. `add_sus_path` 在 `address_space->flags` 设置 `AS_FLAGS_SUS_PATH`；若目标位于 FUSE，会同时标记 `fuse_inode` 与 backing inode；
3. namei/dcache/open 路径在命中标记 inode 时改走一个伪 qstr，并让原目标表现为 `ENOENT`；
4. getdents/compat_getdents 回调在写入用户 buffer 前通过 `(superblock, inode)` 查询标记并省略目录项；
5. `add_sus_path_loop` 不是 glob 或递归枚举，而是在每次 zygote App 被标记 umounted 后重新解析并标记同一路径，用于处理 inode 变化。

这套实现覆盖面和拦截时机明显优于 PathMask，也证明 exact hide 应进入 component lookup 与 dirent actor，而不是在 syscall 返回后修补结果。

但 SUSFS 2.2 仍不能直接成为 PathGuard adapter：

- 生效条件是 task 带 `TIF_PROC_UMOUNTED` 且 `current_uid() >= 10000`，不是请求中的 package、UID 列表或 mount namespace handle；
- `add_sus_path` ABI 只有路径和错误码，没有 per-app scope。同一个被标记 inode 会对整类 umounted App 生效，违反“仅 LocalSend 看不见、其他 App 正常可见”；
- FUSE 分支还要求调用者 UID 与目标 inode owner 不同。源码明确提示 `/sdcard` 路径对 MediaProvider 自身仍可见，因此 MediaStore/Photo Picker 仍需独立后端；
- BRENE 注释说明 SUSFS 会把命中 lookup 引向 `..5.u.S`；具有 `MANAGE_EXTERNAL_STORAGE` 的应用若能创建该名字，隐藏会失效。BRENE 只能在开机和 inotify 事件中删除它，不能把这一竞态变成 exact semantics；
- 它必须编译进对应内核，源码明确写出不需要也不提供 module exit；按内核版本维护 patch 仍是部署成本。

所以 SUSFS 当前状态是“最佳内核机制参考，不是可直接调用的 PathGuard 后端”。只有上游或 PathGuard 自己增加 versioned per-namespace policy ABI，并通过本文完整矩阵后，`susfs_adapter` 才能解除 blocked 状态。

### 5.3 susfs4ksu-module 与 BRENE

这两个项目都不是新的内核隐藏实现：

- `susfs4ksu-module` 安装 `ksu_susfs/sus_su`，在启动脚本中读取路径列表并调用 SUSFS reboot syscall ABI；
- BRENE v0.0.55 要求 SUSFS 2.2+，用 Shell/WebUI 生成 `custom_sus_path*.txt`，枚举 `/sdcard`、`/data/local/tmp` 等目录后调用同一 ABI；
- 两者都没有向 `add_sus_path` 传 package/UID/namespace，也没有改变 SUSFS 的作用域；
- BRENE 的 inotify 守护和 `..5.u.S` 清理反而展示了共享存储写入探测的持续运维成本。

可借鉴的是 capability probe、版本显示、持久化列表和用户诊断，不应把 WebUI 功能数量误判为后端能力。BRENE 为 AGPL-3.0，SUSFS 为 GPLv3；PathMask 内核文件为 GPL-2.0。本项目只提取事实和独立设计，不复制实现。[R4][R5]

### 5.4 NoMount

`refer/NoMount` 不调用 `mount()`，而是在 VFS 处理 getname、iterate_dir、permission、d_path、getattr、statfs 和 mmap 元数据，使用 static key、RCU 与 hash table 降低非命中开销。[R6]

它进一步证明：

- mountinfo 无痕的正确方向是避免 mount，而不是事后伪造 mountinfo；
- 目录视图和直接路径解析必须同时处理；
- 虚拟化越接近“替换文件”，需要伪装的一致性面越多。

NoMount 的目标是注入/重定向系统文件，不是按应用隐藏共享存储路径。其 GPLv3 代码只作为设计证据阅读，本项目不复制实现。

### 5.5 Storage Redirect X

`refer/Storage-redirection-X-Public-main` 的整盘隔离流程是：

```text
真实共享存储建立 anchor
应用可见 storage root bind 到隔离目录
恢复 Android
按 allowed_real_paths 恢复真实子目录
叠加 path mappings
```

这适合“默认隔离、显式放行”，也验证了 storage alias、多用户、`/data/media/<user>` backing 和系统代写兼容问题。[R7]

它不适合单个 `hide`：为了移除一个目录名而替换整个 storage/parent，再逐项恢复兄弟目录，会产生大量挂载、动态同步和 mountinfo 痕迹。该路线应留给未来 `isolation.allow`，不能偷塞进 `hide`。

### 5.6 AOSP MediaProvider

AOSP FUSE 是本次调研最有价值的用户态候选。它天然拥有请求 UID，并已经在 lookup/readdir/open/mkdir/rename 等入口执行按调用方的访问判定。[S6]

然而 PathGuard 作为 Root 模块没有稳定 API 去注册额外规则：

- `do_lookup`、`do_readdir_common` 等关键函数是 MediaProvider 内部 C++ 实现；
- 目录 lookup 在现有实现中可能早于 Java `onFileLookupForFuse()` 返回，不可只 Hook 一个 Java 方法；
- dentry/attr cache 需要同步失效，否则规则变更后可能继续命中旧缓存；
- MediaProvider 是 Mainline 模块，会独立于系统 OTA 更新；OEM 还可能修改进程名、库拆分和实现；
- FUSE passthrough/BPF 会改变部分路径的调用链。

H0.2 已在 Android 13 真机 APK、进程 maps、mount 拓扑和 AOSP 12-16 release 源码上完成核验。`FileLookup`、`GetDirectoryEntries` 虽在当前 ELF 中可动态定位，但前者不处理目录，后者只处理列举；真正覆盖 arbitrary directory lookup 的 `do_lookup`/`make_node_entry` 是静态内部实现。同一 FUSE device 又被 App namespace 共享，per-UID positive dentry 必须做内部零缓存/失效控制。因此该路线已经按 kill criteria 停止，不再实现版本偏移、签名扫描、inline hook 或全局 cache disable。证据见 `tests/device/hide/H0.2_MEDIAPROVIDER_FUSE_ALIOTH_20260728.md`。

### 5.7 Hook、seccomp、fanotify 和权限方案

以下方案不进入 exact hide 候选：

| 方案 | 排除原因 |
|---|---|
| Java/PLT Hook | 原始 syscall、静态链接、未覆盖库和新 API 可绕过；需要拦截入口持续增长 |
| Binder query Hook | 可过滤 MediaStore，但不处理直接 `getdents64/statx/openat2` |
| seccomp user notification | 需要解析用户指针、相对路径、dirfd 和 symlink；每次 syscall 进入用户态 |
| fanotify permission | 主要给出允许/拒绝，不能从父目录 readdir 中删除名字 |
| SELinux/DAC/`chmod 000` | 返回权限错误，名字和 inode 仍可观察 |
| 仅过滤 `/proc/self/mountinfo` | 只隐藏一个观测面，不改变目录项或路径 lookup |

现有 MediaStore query filter 可以成为 `hide.media_query` 的候选组成部分，但不能被称为文件系统 hide 后端。

## 6. 方案矩阵

| 后端 | 状态 | readdir 隐藏 | lookup `ENOENT` | mountinfo 无新增痕迹 | 原始 syscall | 兄弟项读写不变 | stock kernel | 维护成本 |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 当前 deny anchor | Stable，但不是 hide | 否 | 否 | 否 | 是 | 是 | 是 | 低 |
| OverlayFS + whiteout | Rejected | 是 | 是 | 否 | 是 | 否/待验证 | 不确定 | 中高 |
| tmpfs 父目录 + 逐项恢复 | Rejected | 是 | 是 | 否 | 是 | 需同步 | 是 | 高 |
| 应用 Java/PLT Hook | Rejected | 部分 | 部分 | 是 | 否 | 是 | 是 | 高 |
| PathMask v2.5.0 LKM | Reference / Anti-pattern | 条件性 | 条件性 | 是 | 部分 | 副作用不可撤销 | 否，需匹配 KMI | 高且 fail-open |
| SUSFS 2.2 `sus_path` | Blocked | 是 | 是 | 是 | 是 | 写入探测有边界 | 否，需补丁内核 | 中高，但非 per-app |
| MediaProvider FUSE 接入 | Experimental candidate | 是 | 是 | 是 | 是，限共享存储 FUSE | 是 | 是 | 很高且版本敏感 |
| 稳定 pre-VFS LKM 接口 | Conditional candidate | 待验证 | 待验证 | 是 | 待验证 | 待验证 | 取决于 KMI/签名 | 高 |
| PathGuard 最小内核内联后端 | Last resort | 是 | 是 | 是 | 是 | 是，mutation 统一拒绝 | 否，需补丁内核 | 最高且需长期维护 |

如果产品要求同时满足 exact hide、通用 stock kernel、无版本敏感 Hook、零 mount 痕迹和极低热路径成本，这组约束当前没有可行交集。必须放宽设备覆盖或放宽 hide 语义，不能用文案掩盖。

## 7. 候选架构：独立 visibility plan + 可替换后端

本节记录开始 H0 后仍然成立的职责边界，不代表已确定具体后端。现有编译器、daemon、Zygisk、`policy.bin` 和 Manager 在后端通过准入前不得预留未使用分支。

### 7.1 总体结构

```text
rules.toml
    -> 现有规则编译链
    -> Canonical Policy IR
    -> policy.bin + HideRequirements
    -> daemon capability admission
    -> Zygisk 在目标 mount namespace 就绪后注册 HidePlan
    -> admitted visibility backend
         - component lookup filter
         - directory-entry filter
         - mutation guard
    -> VFS/FUSE 继续完成正常文件操作

另一路：
    HidePlan -> MediaStore query filter
             -> 可选 Photo Picker / SAF capability
```

编译器只表达语义和 capability requirement，不理解内核 hook。Zygisk 只传递预编译、定长、有界的规则；内核不解析 TOML、JSON 或 `policy.bin`。

### 7.2 快速开发期的控制面重构

当前 `policy.bin` v5 把 `deny` 和 `redirect` 放在 mount rule table，Zygisk 也只把 action 0/1 转成 `MountOp`。`hide` 不是 mount 操作，不能为了复用现有 executor 把它追加成 `MountAction::kHide`。

H0 通过后允许直接升级格式并重构为两个执行计划：

```text
Canonical path policy
    |- MountPlan[]       deny / redirect
    `- VisibilityPlan[]  hide

MountBackend             只执行 mount plan
KernelHideBackend        只发布 visibility plan
ProviderVisibilityBackend 只处理 MediaStore/SAF 等 provider plan
```

规范化、作用域、父子折叠和冲突检测可以复用同一 canonical path rule；运行时 plan、capability、失败状态和性能统计必须分开。这样同时满足 DRY 与单一职责，避免 mount executor 出现与 mount 无关的 action 分支。

项目尚未发布，H1 不为旧实验版 `policy.bin` 增加双读兼容层；一次升级 compiler、daemon、Zygisk reader 和 golden vector即可。当前 H0 仍不改格式，避免先实现无法交付的 schema。

### 7.3 规则归属使用 mount namespace，不只使用 UID

共享 UID 会让两个包拥有相同 UID；MediaProvider 又可能代表调用方执行。仅以 `current_uid()` 作为直接 VFS hide 主键会把规则错误扩散到其他包或系统服务。

推荐在应用 private mount namespace 建立完成后，为该 namespace 绑定一个不可变 `HidePolicyHandle`：

```text
mount namespace -> HidePolicyHandle -> HiddenChild[]
```

同一应用的多进程若共享 namespace，自然共享规则；不同包即使 shared UID 相同，只要 namespace 不同，也不会互相污染。是否存在 OEM 让目标进程共享 namespace，必须由 admission probe 拒绝而不是猜测。

### 7.4 内核规则键不使用完整路径字符串

用户态在有权限的安装阶段完成规范化和解析，通过目录 FD 注册：

```cpp
struct HiddenChildKey {
    NamespaceCookie namespace_cookie;
    SuperblockIdentity parent_sb;
    uint64_t parent_ino;
    uint32_t parent_generation;
    NameHash basename_hash;
    uint16_t basename_length;
};
```

实际 ABI 不在 H0 前冻结。上述结构表达三个约束：

1. lookup/readdir 热路径不构造绝对路径；
2. storage alias 指向同一 parent inode 时自然命中同一规则；
3. hash 命中后仍比较完整 basename，不能只凭 hash 拒绝。

内核需要持有足够的 path/inode 引用，避免 dentry 回收后悬空。目录被删除、rename 或重建时如何更新身份是 H0 必测项；若无法无锁、无泄漏地维持正确性，首版应要求应用重启并重新注册，不增加后台 watcher。

### 7.5 lookup 与 readdir 必须是一条原子语义

只过滤 readdir 会被直接 `stat("known-name")` 绕过；只拒绝 lookup，目录列表仍会泄露名字。因此一个 backend capability 只有同时覆盖以下两类入口才可报告 ready：

```text
LookupFilter(parent identity, basename) -> -ENOENT
DirentFilter(parent identity, basename) -> omit
```

还必须验证创建、unlink、rmdir、rename、link、symlink、`O_CREAT` 和 `O_TMPFILE` 相关路径。所有触达隐藏 final component 的操作统一拒绝，不允许某个 mutation 入口把隐藏对象重新暴露。

具体内核 hook 点随 5.10/5.15/6.1/6.6 等版本变化，H0 必须按目标内核源码确认。文档不预先声称修改一个 `lookup_fast()` 或 `iterate_dir()` 就足够。

### 7.6 热路径约束

参考 NoMount 的 static key/RCU 思路，但自行实现最小能力：

- 系统没有任何 active hide policy 时，静态分支关闭；
- 当前 mount namespace 没有 policy 时，一次 unlikely branch 后返回；
- 命中 namespace 后按 parent identity 做 O(1) hash lookup；
- 读路径不得分配内存、睡眠、写日志或获取全局 mutex；
- policy 更新使用 copy-on-write + RCU，一次应用启动只发布一次；
- 不实现通用 VFS redirect、元数据伪装、mountinfo 伪造或运算符插件。

这使内核后端保持单一职责：

> 对已绑定 namespace 的若干 parent/name 对，拒绝 lookup 并过滤 dirent。

### 7.7 生命周期

首版只在进程启动时安装规则：

```text
policy 编译并通过 capability admission
    -> 应用下一次启动
    -> private mount namespace 已确认
    -> 注册完整 HidePlan
    -> 一次性 publish
    -> 应用代码开始执行
```

规则变化后标记目标应用 `pending_restart`，不做运行时增删和 cache 猜测。namespace 销毁时内核自动释放 policy handle；异常退出不得留下全局 UID 规则。

注册必须 all-or-nothing。任一目录解析、ABI、内核 capability 或 publish 失败时，不发布部分规则。

### 7.8 候选顺序与淘汰条件

候选按侵入性从低到高评估；只有前一项因证据触发 kill criteria，才进入后一项：

1. `mediaprovider_fuse`：只做隔离 spike。必须同时覆盖任意共享存储目录的 lookup、readdir、mutation 和原始请求 UID；依赖私有偏移、签名扫描、全局 cache 关闭或广域 Hook 时立即淘汰；
2. `pathguard_lkm`：仅当目标内核暴露可安全注册的 pre-lookup 与 dirent actor 接口，或可证明的等价机制时采用；需要 post-syscall 返回值改写、uaccess buffer 重写、固定 `maxactive` kretprobe 或符号偏移扫描时立即淘汰；
3. `susfs_adapter`：当前 blocked，只观察上游。只有 SUSFS ABI 提供 per-namespace/per-policy scope，且不会影响非目标 App 时才进入验证；
4. `pathguard_kernel_patch`：最后方案。在受支持设备内核源码中插入最小 lookup/dirent/mutation hook，仅当前三项均无法兑现冻结语义且项目接受设备白名单与长期内核维护成本时启动。

外部 LKM 仍需匹配 Android GKI KMI、vermagic、符号列表和模块签名策略。Android 文档只保证 KMI 稳定符号集合的兼容性，不保证任意内部 namei hook 点可供外部模块使用。[S10][S11] 因此“能编译一个 `.ko`”不能等同于“拥有稳定 exact hide ABI”。

即使内核补丁单设备原型通过，也必须先回答支持和发布成本：支持哪些 Android/内核版本、是否仅支持设备白名单、由谁维护 defconfig/toolchain、是否提供 boot image、如何处理 OTA 和失败回滚。没有可持续答案时，原型不得转为公开功能。当前 alioth / Linux 4.19.157 不具备仓库内可复现内核构建基线，因此在建立基线前不得进入最后方案。

### 7.9 失败策略

`hide` 是安全语义，不能 fail-open 后继续启动目标应用，也不能切换为 `deny`：

- 编译成功但设备无 backend：admission 拒绝发布；
- 应用启动时注册失败：拒绝激活该进程或按明确的 per-app failure policy 终止；
- MediaStore capability 缺失：若规则声明完整 hide，则 admission 失败；
- 旧策略仍有效时，daemon 保留旧 generation，不发布半能力新策略。

具体“阻止 specialize”机制需要先验证 Zygisk 生命周期，不能在未验证的回调中直接 `exit()`。H0 只需证明失败不会在无提示的情况下运行应用。

## 8. MediaProvider FUSE 候选 spike

虽然不作为默认推荐，H0 仍应对 MediaProvider FUSE 做一次有 kill criterion 的实验，因为它可能在不改内核的设备上提供最佳共享存储语义。

### 8.1 必须回答的问题

1. 目标 Android 12/13/14/15/16 和至少两种 OEM ROM 中，承载 FUSE 的进程、APEX 与 `.so` 分别是什么？
2. 是否存在可版本探测的稳定入口，同时覆盖 arbitrary directory 的 lookup 与 readdir？
3. 是否无需私有符号偏移、指令签名扫描和 inline patch？
4. 能否取得原始请求 UID，而不是 MediaProvider 自身 UID？
5. FUSE passthrough/BPF、dentry cache 和 attr cache 下是否仍能保证规则生效？
6. Provider 重启、Mainline 更新和多用户 mount 重建后如何自动恢复？
7. hook 未完整安装时能否严格 fail-open 且不使 MediaProvider 崩溃循环？
8. 每次 lookup/readdir 的额外开销是否低于预算？

### 8.2 Kill criteria

命中任一条件即停止该路线，不进入产品代码：

- 需要硬编码函数偏移、ROM fingerprint 白名单或反汇编签名扫描；
- 只能过滤 readdir，不能让直接 lookup 返回 `ENOENT`；
- 必须关闭整个共享存储的 dentry cache 才能正确；
- Provider 崩溃会导致存储服务不可用或重启循环；
- FUSE BPF/passthrough 路径可绕过；
- Mainline MediaProvider 小版本更新即可破坏 ABI；
- 需要把广域 PLT/ART Hook 常驻到所有应用。

如果 spike 失败，结论应是“stock-kernel exact hide unsupported”，而不是继续叠加 fallback Hook。

## 9. Provider 和选择器能力拆分

`hide` 的状态不能用一个布尔值表示。至少要区分：

```text
hide.direct_vfs       Java/libc/raw syscall 的路径视图
hide.media_query      目标应用发起的 MediaStore 查询
hide.media_open       content URI / MediaStore 代开 FD
hide.photo_picker     系统 Photo Picker 本地媒体视图
hide.cloud_media      CloudMediaProvider 远端媒体视图
hide.saf              DocumentsUI / DocumentsProvider 目录视图
hide.mount_stealth    hide 本身不新增可见挂载
hide.preexisting_fd   启用前已经持有的 FD
hide.provider_fd      Provider 或系统服务代开的 FD
```

未来首个可交付版本只在以下组合全部 ready 时才可宣称 `hide` 可用：

```text
hide.direct_vfs
hide.media_query
hide.mount_stealth
```

首版明确不包含：

```text
hide.photo_picker
hide.cloud_media
hide.saf
hide.preexisting_fd
hide.provider_fd
```

CLI/Manager 必须单独显示 Photo Picker、SAF 和 CloudMediaProvider 是否覆盖。未来若产品决定“hide 必须覆盖全部选择器”，应提高 admission requirement，而不是偷偷扩展基础后端。

设备能力必须按项展示，不能压成一个 `hide_enabled` 布尔值。部分能力通过时只能报告 unsupported，不得显示“已启用部分 hide”。诊断至少要能区分：后端缺失、内核或 MediaProvider 版本不支持、namespace 隔离不可确认、shared UID 风险、规则冲突、路径类型不符和 Provider 能力缺失。

产品文案只允许使用：

> 对目标应用的受支持直接文件访问，隐藏目录不可发现且不可访问。

禁止使用“完全不可见”“绝对隐藏”“系统级无痕”。还必须说明规则仅在目标应用重启后生效，已持有 FD、Provider 代开 FD、缩略图、媒体数据库和应用缓存可能继续暴露旧结果。

现有 BinderProxy MediaStore query filter 可复用于 `hide.media_query` 的早期验证，但需补测 projection 不含 `_data`、`RELATIVE_PATH`、Bundle query args、直接 content URI 和 OEM provider。它不能替代 Provider 自己代开 FD 的校验。

## 10. 规则与编译器候选设计

H0 通过前不实现下列 schema。本节只冻结方向，避免后端反向污染规则语言。

```toml
format = 1

[apps."org.example.app"]
users = [0]
hide = [
    "Pictures/Nagram",
    "DCIM/Screenshots",
]
```

编译规则：

1. `hide` 是目录字符串数组，不接受箭头或 inline table；
2. 复用当前 path normalization、绝对路径拒绝和组件边界比较；
3. 完全重复和父子包含按 `deny` 相同方式警告、折叠；
4. `hide` 与 `deny`、redirect source、redirect target 存在相等或祖先/后代关系时编译失败；
5. 同一规则不可同时请求 hide 与 Provider redirect；
6. `users = *` 在 Provider 侧归属尚未证明前不允许完整 hide；
7. 编译产物携带 capability requirements，不携带 backend 名称；
8. 后端选择属于设备 admission，不属于用户配置。

重新立项后还必须在编译期执行以下安全边界：

1. 拒绝 `/`、`/storage`、`/storage/emulated`、用户 storage root 与它们的 alias；
2. 首版拒绝隐藏 `Android`、`Android/data`、`Android/obb` 以及 `DCIM`、`Pictures` 等顶层共享目录，只允许经评审的子目录；
3. `DCIM/Camera` 等系统关键目录即使未来放开，也必须经过单独风险准入，不能只给警告后继续；
4. 每个应用最多 256 条规则，每个 basename 最多 255 bytes，总 policy 内存有固定上限；
5. basename 拒绝空值、NUL、`/`、`.`、`..` 和超长输入；
6. 编译器完成规范化和组件校验，内核只接收已经解析的 parent object 与 basename，不再解释路径字符串。

不增加：

- `backend = "susfs"`；
- `fallback = "deny"`；
- 通用 syscall 列表；
- 正则/glob；
- 动态运行时更新选项。

这些约束符合 KISS/YAGNI：用户声明想要的语义，设备只回答能否完整实现。

## 11. H0 验证计划与最后方案准入

H0 只产生事实和 kill decision，不产生用户功能。顺序按侵入性冻结：基线 probe -> stock-kernel FUSE spike -> 现有内核能力对照 -> 最小内核补丁。禁止因为内核补丁语义最完整就跳过前序阶段。

### 11.0 执行状态（2026-07-28）

H0 已在独立分支 `research/hide-h0` 完成，最终选择 H0.6 决策门第 4 项：

- 已冻结 versioned JSONL observation contract 和沙箱路径守卫；
- 已增加独立 arm64 NDK direct-VFS probe、ADB runner 和报告模板；
- probe 不链接 daemon、Zygisk 或规则编译器，不修改 `rules.toml`/`policy.bin`；
- 已在 Xiaomi alioth / Android 13 / kernel 4.19 / SELinux Enforcing 上完成首次
  ADB shell-domain 基线，82 条 observation 无 setup error，原始证据位于
  `build/device-evidence/hide-h0/20260728-003113/`，可审计结论见
  `tests/device/hide/H0.1_SHELL_BASELINE_ALIOTH_20260728.md`；
- shell runner 是 ADB shell UID/SELinux domain，只能验证探针、syscall 和设备拓扑基线；
- 该设备的 `openat2` 与 `faccessat2` 返回 `ENOSYS`，后续实现必须保留可审计 fallback；
- 已建立独立 `dev.pathguard.hideprobe` debug APK，命令行与 JNI 共用同一份 native
  probe 实现，并补充 Java `File`/NIO、MediaStore legacy/Bundle query 和 direct URI
  open；Host 49/49、APK JVM test 和 arm64 debug APK 构建已通过；
- app-domain runner 已安装并授予 `READ_MEDIA_IMAGES` 与 `MANAGE_EXTERNAL_STORAGE`；
  后续重新安装或变更权限仍需用户明确授权；
- 已在 alioth 的 `untrusted_app` 域完成普通媒体权限 app-domain 基线：canonical、
  `/sdcard`、`/storage/self/primary` 对目标均完整可见，MediaStore 分别返回 2/18 条并可
  direct URI open；完整结论见
  `tests/device/hide/H0.1_APP_BASELINE_ALIOTH_20260728.md`；
- all-files 配置不会绕过平台对 `/mnt/runtime/*`、`/mnt/user/*` 和 `/data/media/*`
  的 `EACCES`；canonical、`/sdcard`、`/storage/self/primary` 的可见结果不变；
- Photo Picker 与 SAF 单文件可 query/open Screenshots 图片；SAF Pictures/DCIM tree
  分别枚举到 `Nagram` 与 `Screenshots`；本机未配置 CloudMediaProvider，单项记为
  `Unsupported`；
- 完整结论见
  `tests/device/hide/H0.1_APP_ALL_FILES_AND_SELECTORS_ALIOTH_20260728.md`；
- H0.1 仅冻结“没有 backend 时完整可见”的 oracle，不代表任何 `hide.*` capability
  已通过。非目标 App 隔离必须在 H0.2 及后续 backend 实验中验证。
- H0.2 已核验 alioth Android 13 的 MediaProvider APK/ELF、FUSE mount 与 AOSP 12-16
  release 源码。目录 direct lookup 绕过 `FileLookup`，完整 hook 点属于静态私有实现；
  App namespace 共享同一 FUSE device，per-UID dentry cache 无稳定外部控制面；
- H0.2 命中私有 inline patch 与全局 cache 两项 kill criteria，未向 MediaProvider 注入
  代码。完整结论见
  `tests/device/hide/H0.2_MEDIAPROVIDER_FUSE_ALIOTH_20260728.md`。
- H0.3 确认 alioth 4.19 虽支持外部模块，但 `CONFIG_KPROBES=n`、`FANOTIFY=n`、
  `SECURITYFS=n`，且 `security_add_hooks`/lookup/dirent 内部符号未向模块导出；
- PathMask 没有 android13-4.19 KMI 构件且依赖 kprobe/kretprobe，本机黑盒实验记为
  `Unsupported`，未尝试加载不匹配 `.ko`。完整结论见
  `tests/device/hide/H0.3_LKM_INTERFACE_AUDIT_ALIOTH_20260728.md`。
- H0.4 确认本机没有 KernelSU/SUSFS backend；现有 `add_sus_path` ABI 也没有
  per-package/per-namespace policy scope，因此 adapter 保持 blocked；
- H0.5 缺少精确 kernel source、defconfig/toolchain、原版可复现构建和 boot 回滚
  基线，不满足最后方案准入，不启动内核补丁；
- H0.6 最终结论为 `hide unsupported; keep deny`。完整决策见
  `tests/device/hide/H0.4_SUSFS_AND_H0_DECISION_ALIOTH_20260728.md`。

### H0.1 建立基线探针

先写独立 probe，不接规则编译器：

- Java：`File.list()`、`exists()`、`isDirectory()`、`mkdirs()`、`delete()`、`renameTo()`、NIO `Files` 与 `DirectoryStream`；
- libc：`opendir/readdir`、`stat/lstat/fstatat`、`access/faccessat`、`open/openat`、`mkdir/rmdir/unlink/rename/link/symlink/readlink`；
- raw syscall：`getdents64`、`newfstatat/statx`、`faccessat2`、`openat/openat2`、`mkdirat/unlinkat/renameat2/linkat/symlinkat`；
- mutation：`mkdir`、`unlink/rmdir`、`renameat2`、`linkat`、`symlinkat`；
- alias：`/sdcard`、`/storage/self/primary`、`/storage/emulated/<user>`、`/mnt/runtime/default|read|write` 与仅在测试权限允许时使用的 `/data/media/<user>`；
- proc：`mountinfo`、`mounts`、`mountstats`、`stat`、`status`；
- MediaStore：`_data`、`RELATIVE_PATH`、`DISPLAY_NAME`、collection/Bundle query、projection 不含 `_data`、直接 URI query/open、缩略图、相册分组、recent 与 search；
- UI：应用内媒体选择器、系统 Photo Picker、SAF DocumentsUI、CloudMediaProvider。

每项记录返回值、`errno`、目录项、UID/PID、Android 版本、MediaProvider APEX 版本和内核版本。输出采用结构化 JSON，并同时记录设备型号、ROM fingerprint、CPU governor、文件系统、mount namespace identity、真实对象是否发生 inode/mtime/content 变化，供后续设备矩阵与回归比较。

### H0.2 MediaProvider FUSE 接入实验

**状态：Kill（2026-07-28）。** stock-kernel exact hide unsupported；本路线停止，不进入
产品实现。以下条目保留为当时的准入标准和后续审计依据。

优先验证无需修改内核的共享存储 FUSE 接入，严格执行第 8 节 kill criteria。实验代码必须独立于主模块，不进入日用设备默认启动路径；Provider 连续崩溃三次必须自动禁用并恢复原状态。

该阶段只有同时覆盖 lookup、readdir、mutation、原始请求 UID、FUSE passthrough/BPF 与 Mainline 更新恢复，才可进入候选。只能完成 MediaStore query filter 不等于通过 direct VFS hide。

### H0.3 LKM 接口审计与 PathMask 对照实验

**状态：Unsupported / Blocked（2026-07-28）。** 当前 alioth 4.19 没有稳定动态
pre-lookup/dirent 注册接口，PathMask KMI 与 kprobe 前提均不满足。以下矩阵保留给未来
具备完全匹配测试设备时使用。

先审计目标内核是否导出可安全注册的 pre-lookup、dirent actor 与 namespace 生命周期接口；没有稳定接口时记录 blocked，不得退回 kprobe 符号偏移扫描。随后在与预编译 `.ko` KMI 完全匹配的测试设备上，把 PathMask 当黑盒反例基线，不把它接入 PathGuard。至少执行：

- 绝对路径与 `openat(parent_fd, "child", ...)` 相对路径；
- `/sdcard`、`/storage/self/primary`、`/storage/emulated/<user>` alias；
- `O_CREAT`、`O_TRUNC` canary，确认返回 `ENOENT` 后真实文件未变化；
- `mkdirat/unlinkat/renameat2/linkat/symlinkat`；
- 单次返回超过 64 KiB 的目录、用户缓冲 fault 和内存压力；
- 超过 20 个并发 getdents、超过 40 个并发 stat/open，并读取每个 probe 的 `nmissed`；
- 两个 superblock 使用相同 inode number 时不得误过滤；
- shared UID 两包和非目标 App 可见性。

任一项泄露或产生副作用都记录为机制边界，不在 PathMask 上继续叠加补丁。该阶段目的是建立回归语料和量化 probe 开销。

### H0.4 SUSFS 2.2 对照与上游 ABI 观察

**状态：Unsupported / Blocked（2026-07-28）。** 本机没有 SUSFS/KernelSU；已审计 ABI
仍无 per-package/per-namespace policy scope，不编写生产 adapter。以下矩阵保留给未来
已有受支持 SUSFS 内核的测试设备。

仅在已经使用受支持 SUSFS 内核的测试设备执行：

- 确认 LocalSend 和非目标 App 是否都被 `sus_path` 影响；
- 验证 FUSE 目标、目录重建与 `add_sus_path_loop` 重标记时序；
- 授予测试 App `MANAGE_EXTERNAL_STORAGE` 后尝试创建 `..5.u.S`；
- 验证 MediaProvider、MediaStore、Photo Picker 与直接路径的差异；
- 记录 App Profile/KernelSU umount 状态对 `TIF_PROC_UMOUNTED` 的影响。

当前预期是“直接隐藏机制通过、per-app isolation 不通过”。除非上游 ABI 已发生可核验变化，否则不编写生产 adapter。

### H0.5 PathGuard 最小内核补丁，最后方案

**状态：Blocked / Not started（2026-07-28）。** 当前缺少精确 kernel source、可复现
原版构建与已验证 boot 回滚镜像，且设备白名单/OTA 维护范围未获产品批准。

只有 H0.2 已按 kill criteria 淘汰、H0.3 不存在安全的 pre-VFS LKM 接口、H0.4 仍无 per-namespace/per-policy ABI，并且项目已接受设备白名单、OTA 维护和 boot 回滚成本，才能开始本阶段。开始前还必须具备目标 ROM 对应的内核源码 commit、defconfig、clang/toolchain、可复现原版构建和已验证回滚镜像。

在单一已知内核上实现最小原型：

```text
固定 namespace
固定 parent directory FD
固定一个 basename
lookup -> ENOENT
readdir -> omit
```

原型不得包含动态配置、Provider、重定向、mountinfo 过滤或通用路径改写。先证明 lookup 与 readdir，再加入 mutation guard；单设备行为通过后仍不能直接进入产品，必须补齐支持矩阵和发布维护决策。

OverlayFS 不再安排实现 spike：whiteout 必须挂载父目录视图、mountinfo 可见且兄弟写入可能 copy-up，已经违反三个冻结约束。快速开发期直接停止该路线，避免为已知不满足目标的方案投入代码。

### H0.6 决策门

**当前决策：第 4 项。** `mediaprovider_fuse` 已 Kill，`lkm_vfs` 与 `susfs_adapter`
不可用，`kernel_patch_vfs` 未满足最后方案准入；保持 `hide` unsupported，继续只提供
`deny` 与 `redirect`。

H0 完成后只能得到以下四种结论之一：

1. `mediaprovider_fuse` 通过全部 kill criteria：可作为共享存储限定实验后端，不再修改内核；
2. `lkm_vfs` 独立通过完全相同的行为、并发和性能矩阵：可作为 KMI 限定交付形态，不再开发内联补丁；
3. 前述路线均失败，但 `kernel_patch_vfs` 通过且维护方案获批：进入 H1 ABI/安全设计；
4. 上述路线均未通过：保持 `hide` unsupported，继续只提供 `deny`。

禁止把“部分通过”包装成 hide 发布。

## 12. TDD 与验证矩阵

### 12.1 行为测试

对每条隐藏目录执行以下断言：

| 操作 | 期望 |
|---|---|
| 枚举父目录 | 不含目标 basename |
| `stat/lstat/statx` | `-1/ENOENT` |
| `open/openat/openat2` | `-1/ENOENT` |
| `opendir` | `-1/ENOENT` |
| 访问已知后代 | `-1/ENOENT` |
| 相对 `dirfd` 访问 | `-1/ENOENT` |
| 符号链接别名访问 | 不可绕过 |
| 大小写变体 | 与目标文件系统语义一致 |
| 同名兄弟、相似前缀 | 不误伤 |
| 非目标应用 | 完整可见、可访问 |
| 目标应用其他目录 | 读写语义不变 |

以下用例专门防止参考实现中的已知缺口回归：

- `openat(dirfd, relative)`、`openat2` resolve flags、`chdir` 后相对路径；
- 路径中 `.`、`..`、重复 `/`、符号链接和所有 storage alias；
- `O_CREAT|O_EXCL`、`O_TRUNC` 失败后真实存储 byte-for-byte 不变；
- 隐藏目录 rename/delete/recreate 后，旧 inode 不误伤、新 inode 不泄露；
- 大目录每轮 getdents buffer 边界为 4 KiB、32 KiB、64 KiB、128 KiB；
- inode number 相同但 superblock/parent 不同的目录项不误伤；
- 1/20/21/40/41/128 并发线程持续 lookup/readdir，不允许抽样漏拦；
- 可写父目录下创建隐藏 basename 和 `..5.u.S` 等实现内部名字，不能触达真实对象或解除隐藏。

### 12.2 身份测试

- 多用户同包；
- shared UID 不同包；
- 主进程与 `:remote` 子进程；
- isolated process；
- child zygote/WebView renderer；
- Provider 代表应用执行；
- namespace 重建、应用 force-stop 和重启。

### 12.3 缓存与竞态测试

- hide 安装前后有/无 positive dentry cache；
- negative dentry cache；
- 目录 handle 已打开后规则变化；
- 目录被 rename、删除、重建；
- Provider/Mainline 模块重启；
- 并发 lookup/readdir 与 namespace 销毁；
- policy generation 切换时无 use-after-free、部分发布或旧规则泄漏。

首版若选择“仅启动时发布”，运行中规则变化应明确保持旧视图直到应用重启，而不是尝试在线修复所有缓存。

### 12.4 Provider 测试

- MediaStore query 不返回目标及后代；
- projection 不含 `_data` 时仍不泄露；
- 直接已知 content URI 的 query/open；
- 缩略图、相册分组、recent、搜索；
- Photo Picker local、CloudMediaProvider；
- SAF queryChildDocuments、search、recent、openDocument；
- Provider 无法归属调用方时按文档状态处理，不猜 package。

### 12.5 安全测试

- 非特权进程不能创建、替换、枚举或解除内核 policy；
- namespace cookie/PID 复用不能误绑定；
- 传入 FD 必须是目录并来自预期 storage root；
- basename 拒绝 `/`、NUL、`.`、`..` 和超长输入；
- 计数、长度和内存预算有硬上限；
- ABI 版本不匹配 fail closed；
- fuzz 注册消息、规则删除和 namespace 销毁顺序。
- fuzz 相对 `dirfd`、rename/link 双路径参数和异常 dirent，禁止任何 post-success 副作用被伪装为失败。

### 12.6 性能预算

在无规则、其他应用有规则、当前 namespace 有规则三种状态分别测量：

```text
stat/open non-match
readdir 100 / 1,000 / 10,000 / 100,000 entries
应用冷启动
MediaStore query 100 / 1,000 rows
```

建议 H0 门槛：

- 无 active hide policy：VFS microbenchmark P95 回归不超过 1%；
- 其他 namespace 有规则、当前 namespace 无规则：P95 回归不超过 2%；
- 当前 namespace 有最多 256 条规则的 non-match：P95 回归不超过 3%；
- 热路径零动态分配、零全局 mutex、零日志；
- 不因 hide 关闭整个共享存储 dentry cache。
- 不允许固定容量 probe instance 耗尽后 fail-open；若实现存在 `nmissed` 概念，任意非零即测试失败。

百分比需要同时报告绝对纳秒值和至少 30 轮置信区间，避免微基准噪声产生假结论。

### 12.7 真实应用与设备矩阵

Probe 通过只是准入，不替代真实应用验证。至少覆盖：

- LocalSend、Telegram/Nagram、系统相册、Files by Google、MiXplorer 或同类文件管理器；
- 应用内文件选择、分享、保存、媒体扫描、缩略图、下载、备份和多进程访问；
- 主用户、副用户、工作资料、克隆/双开应用；
- shared UID 两包、isolated process、WebView renderer 和 child zygote；
- 至少一个 AOSP/Pixel 系 ROM 与一个 OEM ROM，并记录 device、fingerprint、Android、内核、Root 方案和 MediaProvider APEX 版本。

任何目标应用泄露、非目标应用误伤或同一规则在 storage alias 下结果不一致，均判定 backend 未通过，不以应用兼容例外掩盖。

## 13. 分阶段实施建议

| Phase | 目标 | 产物 | 退出条件 |
|---|---|---|---|
| H0 | 可行性与 kill decision | 通用 probe、FUSE spike、LKM/SUSFS 对照；必要时最后执行内核补丁原型 | 至少一个后端完整通过或明确 unsupported |
| H1 | 选定后端 ABI/生命周期 | versioned ABI、身份与并发设计；内核后端另含 KMI/patch 发布策略 | namespace 绑定和销毁压力测试通过 |
| H2 | 最小 direct hide | lookup + readdir + mutation | raw syscall/alias 矩阵通过 |
| H3 | 编译器接入 | `hide` AST/IR/requirements | admission 与冲突 golden 通过 |
| H4 | MediaStore | query/open capability | LocalSend 等真实应用矩阵通过 |
| H5 | 选择器兼容 | Photo Picker/SAF 独立能力 | 每个 Provider 单独报告状态 |
| H6 | 性能与发布 | benchmark、fuzz、回滚 | 预算、真机矩阵、文档全部通过 |

H0 之前不做 schema 和二进制格式工作；H2 之前不接 Manager UI；H4 之前不在用户文案中称为完整 hide。若 H0 在 FUSE 或 LKM 路线已完整通过，立即停止，不再开发内核补丁；只有第 11 节最后方案准入全部满足时，H0 才包含内核原型。

## 14. 需要形成的新 ADR

H0 后若继续，应新增 ADR，而不是直接改写现有 ADR-008：

- `ADR-0007 hide semantics and no-degrade policy`；
- `ADR-0008 hide backend selection and kernel-patch-last policy`；
- `ADR-0009 provider visibility capability split`。

当前 ADR-008“不隐藏 PathGuard mountinfo”继续对 `deny`/`redirect` 成立。未来 VFS hide 不产生 mount，因此不是对旧 ADR 的偷偷修改；若要隐藏其他已有挂载，则属于另一项 root-stealth 能力，不在本文范围。

## 15. 最终建议

1. 保留当前 `deny`，继续把它定位为稳定、广覆盖、低成本的访问阻断能力。
2. 不采用 OverlayFS、父目录 tmpfs 恢复、应用 syscall Hook 或 PathMask 式 post-syscall kretprobe 作为正式 hide。
3. H0 已按“统一 probe -> stock-kernel MediaProvider FUSE -> 稳定 LKM 接口 -> SUSFS ABI”顺序完成，没有跳过较低侵入候选。
4. 修改内核是最后方案：只有 FUSE、稳定 LKM 接口和现有上游 ABI 均被证据淘汰，且目标内核可复现构建、可回滚、维护范围获批时，才实现最小 VFS 内联补丁。
5. 快速开发期允许在 H1 一次性升级 `policy.bin`，但必须将 `MountPlan` 与 `VisibilityPlan` 分开，不能让 hide 污染现有 mount executor。
6. SUSFS 2.2 当前不提供 per-app ABI，BRENE 不改变这一事实；不实现会误伤其他 App 的 adapter。
7. 若用户要求 stock kernel 普遍可用，则接受“目前无法提供 exact hide”的结论，不用 `deny` 冒充。
8. MediaStore、Photo Picker 和 SAF 保持独立能力与状态，不重新塞回核心 VFS 数据面。

该设计的核心取舍是：

```text
deny 追求覆盖面和稳定性；
hide 追求不可发现语义，并接受更严格的设备能力门槛。
```

## 16. 资料来源

### 16.1 权威资料

| 编号 | 资料 | 用途 |
|---|---|---|
| S1 | [Linux mount_namespaces(7)](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html) | mount namespace 与 `/proc/<pid>/mount*` 视图关系 |
| S2 | [Linux proc_pid_mountinfo(5)](https://man7.org/linux/man-pages/man5/proc_pid_mountinfo.5.html) | mountinfo 字段、bind root、mount point 和父子关系 |
| S3 | [Linux OverlayFS documentation](https://docs.kernel.org/filesystems/overlayfs.html) | whiteout、opaque、copy-up、upper/lower 要求和 readdir 缓存 |
| S4 | [AOSP Scoped storage](https://source.android.com/docs/core/storage/scoped) | Android 11+ MediaProvider FUSE 架构 |
| S5 | [AOSP FUSE passthrough](https://source.android.com/docs/core/storage/fuse-passthrough) | open 后 passthrough 与 FUSE daemon 边界 |
| S6 | [AOSP MediaProvider FuseDaemon.cpp](https://android.googlesource.com/platform/packages/providers/MediaProvider/+/refs/heads/main/jni/FuseDaemon.cpp) | UID、lookup、readdir、ENOENT、缓存和 FUSE BPF 实现 |
| S7 | [Android Photo Picker](https://developer.android.com/training/data-storage/shared/photopicker) | 系统选择器与应用直接路径访问边界 |
| S8 | [Android DocumentsProvider](https://developer.android.com/guide/topics/providers/document-provider) | SAF Provider 独立进程与文档模型 |
| S9 | [Linux Kprobes documentation](https://docs.kernel.org/trace/kprobes.html) | kretprobe `maxactive/nmissed`、handler 并发与不可 yield 约束 |
| S10 | [Android Loadable kernel modules](https://source.android.com/docs/core/architecture/kernel/loadable-kernel-modules) | Android LKM 构建、签名与加载约束 |
| S11 | [Android GKI versioning](https://source.android.com/docs/core/architecture/kernel/gki-versioning) | KMI 稳定范围、KMI generation 与符号兼容边界 |
| S12 | [Linux openat2(2)](https://man7.org/linux/man-pages/man2/openat2.2.html) | `open_how`、resolve flags、`ENOSYS/E2BIG/EAGAIN` 与相对 `dirfd` 测试 |
| S13 | [Linux getdents(2)](https://man7.org/linux/man-pages/man2/getdents.2.html) | raw `getdents64` 目录项布局、返回值和错误语义 |

### 16.2 本地参考项目

| 编号 | 路径 | 读取结论 |
|---|---|---|
| R1 | `refer/hide-refer/LKM-PathMask-main/kernel/pathmask.c`、`service.sh`、`update/changelog.md`；[upstream v2.5.0 commit](https://github.com/Andrea-lyz/LKM-PathMask/commit/2b1b6dcde36f010c29af5ed28a144fb366b50435) | kretprobe、绝对路径匹配、getdents 重写、UID scope、KMI 交付与诊断 |
| R2 | `refer/hide-refer/susfs4ksu-master` | 冻结的 SUSFS 1.3.8 旧设计，仅用于版本对照 |
| R3 | `refer/susfs4ksu/kernel_patches/fs/susfs.c`；[upstream gki-android14-6.1 commit](https://gitlab.com/simonpunk/susfs4ksu/-/commit/8eade9cd4aed3efddc9ff30b2e48d2d9667ad77d) | SUSFS 2.2 FUSE inode、namei/getdents、task scope 与 `sus_path_loop` |
| R4 | `refer/hide-refer/susfs4ksu-module-1.5.2/boot-completed.sh` | SUSFS ABI 编排、路径重标记和启动期运维职责 |
| R5 | `refer/hide-refer/BRENE-main/boot-completed.sh`、`utils.sh`、`webroot` | SUSFS 2.2 前端、`..5.u.S` 写入边界与非 per-app 事实 |
| R6 | `refer/NoMount/kernel/src/nomount.c`、`nomount.h` | 无 mount 的 VFS 虚拟化、static key/RCU/hash 结构 |
| R7 | `refer/Storage-redirection-X-Public-main/srx_core/src/mount/apply.rs` | 整盘隔离、真实目录恢复和路径映射顺序 |
| R8 | `refer/AOSP-MediaProvider/jni/FuseDaemon.cpp`、`MediaProviderWrapper.cpp` | Android FUSE 的真实调用链与缓存处理 |
| R9 | `refer/riru_storage_redirect` | 历史二进制样本；无源码，不作为实现级证据 |

参考项目只用于理解机制与维护成本。PathMask、SUSFS、BRENE、NoMount 等 copyleft 项目不复制代码；正式实现以 Linux/AOSP 接口、独立设计和本项目测试为准。

### 16.3 审阅输入

`refer/审核报告.md` 用于检查产品边界、候选顺序、诊断、规则准入与测试遗漏。本文吸收了其能力清单、明确状态标签、真实应用/设备矩阵、资源上限和内核维护风险；没有把“首选内核补丁”原样采纳，而是依据当前产品决策将其调整为最后方案。审核报告属于设计评审意见，不替代第 16.1 节权威资料和第 16.2 节源码证据。
