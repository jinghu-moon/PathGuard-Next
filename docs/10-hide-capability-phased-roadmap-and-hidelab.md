# PathGuard Next `hide` 能力收缩、分阶段路线与 HideLab

> 状态：Research / Proposed；尚未进入生产实现
>
> 文档版本：1.0
>
> 日期：2026-09-01
>
> 关联文档：`docs/07-hide-capability-research-and-design.md`
>
> 适用范围：Android 共享存储；目标应用无 Root 权限；按应用选择性隐藏

## 1. 文档目的

本文整理 `hide` 能力调研期间形成的事实、源码审计结果和产品决策，并把它们收敛为一条可以验证、可以停止、不会用近似实现冒充真隐藏的实施路线。

本文不是聊天逐句转录，也不是实现承诺。它重点回答以下问题：

1. PathGuard 所说的 `hide` 到底保证什么；
2. 软件通过哪些路径发现和访问文件，是否能逐层针对性隐藏；
3. 当前 Android 16 真机具备哪些内核和存储条件；
4. bind mount、OverlayFS、PathMask/NoOpt、SUSFS、MediaProvider FUSE、Kasumi 和 NoMount 是否满足要求；
5. 在“完整语义不能降级”的前提下，如何收缩能力并分阶段交付；
6. 如何先建设自动化攻击测试“矛” HideLab，再开发隐藏后端“盾”。

本文继承 `docs/07-hide-capability-research-and-design.md` 已冻结的 exact hide 语义。若两份文档对核心语义存在冲突，以 `07` 的冻结要求为准；本文负责补充后续设备调研、Kasumi/NoMount 审计和阶段化路线。

## 2. 执行摘要

当前结论是：

1. **通用 stock Android 内核上，仍没有已验证且可直接交付的 per-app exact hide 方案。**
2. **“针对软件获取文件的手段逐层隐藏”在工程上成立，但访问面不止一个。** 直接 VFS、MediaStore、ContentResolver、SAF、Photo Picker、CloudMediaProvider 和已有 FD 必须分别治理。
3. **只拦截常用 API 不等于隐藏。** Java API 最终可能进入 libc/VFS，但应用也可以使用 JNI、raw syscall、相对 `dirfd`、storage alias 或系统 Provider 绕过上层 Hook。
4. **SUSFS 的 namei/getdents 时机最接近真隐藏，但现有 ABI 缺少 PathGuard 所需的逐规则 UID/namespace scope。**
5. **Kasumi API 17 的 dirhijack 机制比 syscall 返回后改写更接近目标，但原版仍不满足 PathGuard 契约。** 主要风险是 FUSE `atomic_open` 绕过、mutation 未封闭、hide 规则缺少逐规则作用域、控制面丢弃安装错误，以及对非稳定 GKI 内部符号和共享 inode operation 替换的依赖。
6. **NoMount v20 的 whiteout 已在正确层面处理 lookup/readdir，且规则可指定 UID，但它同样不满足 PathGuard 契约。** 它没有包装 `atomic_open` 或 mutation；规则没有 namespace scope；同路径多 UID 在 parent child index 中会互相覆盖；hook/child 分配失败可静默成功。
7. **最可信的 direct hide 方向是最小化 PathGuard VFS 后端。** 它只能被声明为特定 KMI/OEM/ROM 的设备能力，不能先宣称通用支持。
8. **版本阶段扩大的是访问面和设备覆盖，不是降低隐藏质量。** Hide 1.0 在声明的 direct VFS 范围内也必须完整覆盖 lookup、`atomic_open`、readdir、mutation 和缓存一致性；不能先发布 syscall Hook 近似版。
9. **必须先完成 HideLab。** 没有能主动使用 Java、JNI、raw syscall、Provider、alias、缓存次序和 mutation 攻击后端的测试软件，就无法证明“不可发现且不可访问”。

因此，截至本文日期，产品状态仍应是：

```text
deny       supported
redirect   supported
hide       unsupported
```

只有某个后端通过本文定义的 capability 准入和 HideLab 矩阵后，才能对对应设备、对应访问面报告 `active`。

## 3. 调研方法与证据等级

本轮结论来自四类证据：

| 等级 | 证据 | 可以证明 | 不能单独证明 |
|---|---|---|---|
| E1 | 已连接设备只读采集 | 当前设备、ROM、内核、Root、挂载与 inode 拓扑 | 其他 ROM 或未来 OTA 一定相同 |
| E2 | 本地源码逐行审计 | 指定源码版本的控制流、数据模型和明确缺口 | 未执行路径在真机上的全部竞态结果 |
| E3 | Linux/AOSP 官方资料 | VFS、Kprobe、GKI KMI、FUSE 等平台契约 | OEM 私有修改和特定模块一定兼容 |
| E4 | 上游项目资料与网页调研 | 项目定位、支持范围、已公开风险 | PathGuard exact hide 已经通过 |

本文使用以下措辞区分结论强度：

- **已确认**：有 E1、E2 或权威 E3 的直接证据；
- **高风险推论**：源码调用链显示存在绕过或破坏可能，但仍须 HideLab 真机复现；
- **设计要求**：PathGuard 后端必须满足，尚不代表已有实现；
- **不支持**：缺少必要机制或没有通过准入，不能对用户承诺。

## 4. 对话与决策演进

本轮讨论形成的决策不是一步得出的，演进如下：

### 4.1 从现实场景开始

最初问题可以具体化为：

```text
只让 LocalSend 看不到 Pictures/Nagram，
其他应用和系统仍然正常看到并使用该目录。
```

这排除了全局 chmod、全局改名、删除目录或对所有 App 生效的方案。规则必须具备调用方身份和隔离范围。

### 4.2 区分 deny 与 hide

现有 bind mount/权限手段可以阻止访问，但父目录仍可能列出 `Nagram`，目标也可能表现为 `EACCES` 或一个空挂载点。这是 `deny`，不是 `hide`。

讨论因此冻结：`hide` 是独立能力，不能成为 `deny` 的显示名称，也不能在失败时自动退化为 `deny`。

### 4.3 从“有没有现成真隐藏”转向完整访问面

应用获取文件不只有 `java.io.File`。它可以直接调用 VFS，也可以让 MediaProvider、DocumentsProvider 或系统选择器代查、代开文件。因此，单点 Hook 只能覆盖某一访问面。

“针对性隐藏”仍然可行，但必须明确：针对的是哪个观察者、哪个路径平面、哪个 Provider 和哪组操作。

### 4.4 接受设备限定，而不是语义降级

通用 stock kernel 没有稳定的目录项过滤扩展接口。讨论接受 `hide` 可能只在受支持 KMI/ROM 上启用，但不接受在所有设备上提供一个语义残缺的同名功能。

### 4.5 引入分阶段 hide

阶段化方案可行，但版本的含义必须是扩大覆盖面：

```text
Hide 1.0  完整 direct VFS
Hide 2.0  direct VFS + MediaStore
Hide 3.0  再增加 SAF/DocumentsProvider
Hide 4.0  再增加 Photo Picker/CloudMediaProvider
```

Hide 1.0 不能省略 direct VFS 内部的关键操作，再把漏洞留给 2.0 修复。否则版本号只是在包装 approximate hide。

### 4.6 先造“矛”，再造“盾”

最终形成共识：测试 hide 必须有一个像攻击软件一样枚举、猜测、直接 syscall、跨 alias、使用 Provider、改变缓存顺序并尝试 mutation 的软件；同时还要有不受策略的对照 App 和 Root Oracle。

这套自动化系统命名为 **HideLab**。它不是附属测试，而是后端进入实现和发布的前置条件。

## 5. exact hide 冻结要求

### 5.1 核心观察语义

`hide` 对目标应用的核心语义为：

```text
readdir(parent)       不返回目标 basename
stat/open/opendir     对目标及后代返回 ENOENT
直接 syscall          不得绕过
MediaStore query      在声明 mediastore capability 时不返回相关记录
mountinfo             hide 本身不产生新挂载
```

“目标及后代返回 `ENOENT`”包括已知完整路径访问。应用即使已经知道 `Pictures/Nagram/secret.jpg` 的名字，也不能通过跳过父目录枚举直接打开。

### 5.2 调用方隔离

同一真实目录必须满足：

| 观察者 | 期望 |
|---|---|
| 目标 App，例如 LocalSend | 目标名称不存在，直接访问 `ENOENT` |
| 非目标普通 App | 目录完整可见、可读写 |
| Root Oracle | 能观察真实对象并校验未损坏 |
| 系统 Provider | 取决于对应 Provider capability，不隐式宣称已隐藏 |

不能使用“所有 KernelSU umounted App”或“所有 UID >= 10000”代替逐规则调用方隔离。

### 5.3 mutation 契约

隐藏不能只处理读取。对隐藏名称及其后代，以下 mutation 必须统一封闭：

```text
create / mkdir / mknod / symlink / link
unlink / rmdir
rename source / rename destination
open(O_CREAT / O_TRUNC / O_TMPFILE 等相关组合)
```

首版建议统一返回 `ENOENT`。重点不是错误码美观，而是操作不能先成功触达真实对象，再在 syscall 返回时伪装失败。

例如，后置 Hook 把成功的 `open(O_TRUNC)` 改写成 `ENOENT`，真实文件已经被截断，这属于破坏性失败，绝不能计为 hide。

### 5.4 兄弟项语义不变

隐藏 `Pictures/Nagram` 不应改变 `Pictures/Screenshots` 或其他兄弟项的正常创建、写入、rename、权限和 inode 语义。

这项要求排除了以父目录 OverlayFS 视图实现单项隐藏的正式路线，因为 copy-up、upper/workdir 和合并目录可能改变所有兄弟项行为。

### 5.5 不允许静默降级

以下行为均禁止：

```text
hide 后端失败 -> 自动改用 deny
direct_vfs 可用、mediastore 不可用 -> 仍把整条规则报告 active
部分 operation 包装失败 -> 保留已经安装的另一部分
规则容量耗尽 -> 静默跳过新规则
probe instance 耗尽 -> fail-open 但仍报告正常
```

请求的 capability 不能完整满足时，规则必须拒绝激活，并给出稳定、可诊断的状态。

## 6. 现实例子：只向 LocalSend 隐藏 Nagram

### 6.1 规则意图

目标应用：

```text
package = org.localsend.localsend_app
path    = Pictures/Nagram
```

用户意图不是“LocalSend 打不开 Nagram”，而是“LocalSend 的受支持观察面中不存在 Nagram”。

### 6.2 正确结果

对 LocalSend：

```text
list("Pictures")                         不含 Nagram
stat("Pictures/Nagram")                 ENOENT
open("Pictures/Nagram/known.jpg")       ENOENT
openat(pictures_fd, "Nagram", ...)      ENOENT
raw getdents64(pictures_fd)              不含 Nagram
MediaStore query                         仅在 mediastore active 时不含相关行
```

对 Control App：

```text
list/stat/open                           正常成功
```

对 Root Oracle：

```text
真实目录存在
canary 内容、inode、结构未被 Target Probe 的 mutation 改变
```

### 6.3 为什么一个文件管理器截图不构成证明

文件管理器可能：

- 只使用 Java `File.list()`；
- 使用缓存结果；
- 通过 MediaStore 而不是 VFS 列出媒体；
- 自己过滤隐藏项；
- 没有尝试已知后代、alias 或 raw syscall；
- 没有执行任何 mutation。

因此“界面上没看见”只能作为体验验证，不能作为 exact hide 的安全证据。

## 7. 已连接设备与存储拓扑

### 7.1 本轮设备配置

本轮只读采集到的目标设备为：

| 项目 | 值 |
|---|---|
| ADB serial | `f3ba305a` |
| 厂商/型号 | Xiaomi Redmi `25102RKBEC` |
| device | `myron` |
| SoC | Qualcomm `SM8850` |
| Android | Android 16 / API 36 |
| ROM | `OS3.0.23.0.WPMCNXM` |
| 安全补丁 | `2026-01-01` |
| Kernel | `6.12.23-android16` 系列 |
| KMI | `android16-6.12` |
| Verified Boot | `green` |
| vbmeta | locked |
| SELinux | Enforcing |
| Root | KernelSU/SukiSU Ultra，`ksud 4.1.3`，内核模块形态 |

仓库 2026-08-01 的既有设备证据记录过同一 `myron` 设备的 KernelSU `4.1.2` 和完整内核 build string。版本变化说明 Root framework 也必须进入 capability fingerprint，不能只按机型缓存结果。

### 7.2 内核配置能力

已观察到的相关配置包括：

```text
CONFIG_KPROBES=y
CONFIG_KRETPROBES=y
CONFIG_FTRACE=y
CONFIG_BPF=y
CONFIG_BPF_LSM=y
CONFIG_MODULES=y
CONFIG_MODVERSIONS=y
CONFIG_OVERLAY_FS=y
```

这些开关说明设备具备进行内核实验的基础条件，但不等于存在稳定的目录 lookup/readdir 过滤 ABI。Android GKI KMI 只承诺白名单内的稳定接口；VFS 内部 operation table、未导出符号和 OEM 私有布局不能因为“模块能加载”就被视为兼容。

### 7.3 SUSFS/KPM 状态

只读 capability probe 结果为：

```text
ksud susfs status   -> false
ksud susfs version  -> unsupported
ksud kpm            -> ENOTTY
```

结论：当前设备虽然是 KernelSU 内核模块形态，但没有可供 PathGuard 直接调用的 SUSFS/KPM hide backend。不能把 KernelSU、SUSFS 和 KPM 当作同一个能力。

### 7.4 LocalSend 配置

本轮观察到：

```text
package   = org.localsend.localsend_app
appId/UID = 10358
version   = 1.17.0
```

UID 会随卸载重装、多用户或包状态变化，生产规则不能把一次采集到的 `10358` 永久写死。包名到 UID 的绑定必须在 admission/launch 时重新验证。

### 7.5 storage alias 与 inode 拓扑

`Pictures` 的前台共享存储 alias：

```text
/sdcard/Pictures
/storage/emulated/0/Pictures
/mnt/user/0/emulated/0/Pictures
```

本轮观察到它们的 `device:inode` 均为：

```text
1048605:15771
```

而 pass-through/backing 平面：

```text
/mnt/pass_through/0/emulated/0/Pictures
```

观察到：

```text
65079:15771
```

这意味着：

1. 前台 FUSE alias 共享同一个父 inode identity，适合以 `(superblock, parent inode, basename)` 统一识别；
2. pass-through/F2FS 是不同 superblock，即使 inode number 恰好相同，也不是同一个 VFS identity；
3. 只在前台 FUSE inode 安装规则，不能自动宣称 backing 平面也隐藏；
4. 只比较 inode number 会跨 superblock 误伤；
5. `Nagram` 也呈现 FUSE 与 backing 不同 device、可能相同 inode number 的结构，测试必须同时覆盖两类平面。

是否需要治理 pass-through 取决于目标 App 的 mount namespace 是否能访问它。该平面应建模为独立 capability 或明确不可达前置条件，不能依赖路径字符串偶然不公开。

## 8. 软件发现和访问文件的路径面

“软件获取文件需要哪些手段”不能只按编程语言枚举，应按最终执行主体和数据平面划分。

### 8.1 目标进程直接 VFS

常见上层入口：

```text
java.io.File
java.nio.file.Files
Kotlin/Flutter/React Native 的文件 API
libc opendir/readdir/stat/open/access
JNI 自定义 native 库
```

可直接使用的低层入口包括：

```text
getdents64
statx / newfstatat / faccessat2 / readlinkat
openat / openat2 / O_PATH
mkdirat / unlinkat / renameat2 / linkat / symlinkat
```

因此，Hook Java 或 libc 导出函数不能形成强保证。应用可以绕过它们直接发 syscall；真正的 direct hide 必须在共享的 VFS lookup、directory actor 和 mutation 路径上成立。

### 8.2 路径解析变体

同一个对象可能通过以下方式到达：

```text
绝对路径
相对 cwd
相对 dirfd
符号链接
`.` / `..` / 重复 `/`
storage alias
已打开父目录 FD
```

按原始路径字符串做前缀匹配会漏掉相对路径、alias 和规范化变体。规则主键应尽量基于已解析父目录 identity 与单个 basename，而不是在每次 syscall 后重新猜路径文本。

### 8.3 MediaStore 与 ContentResolver

应用可以查询媒体数据库获得 `_id`、相册、缩略图和 content URI，再通过 ContentResolver 请求 Provider 代开 FD。

direct VFS hide 不会自动删除数据库记录。即使 `_data` 不在 projection 中，文件名、相册、缩略图或已知 URI 也可能泄露存在性。因此 `mediastore` 必须是独立 capability，并覆盖 query 与 open，而不是只过滤某一列。

### 8.4 SAF / ExternalStorageProvider

Storage Access Framework 通过 DocumentsProvider 枚举、搜索和打开文档。执行 VFS 操作的可能是 Provider 进程，目标 App 只通过 Binder 接收结果或 FD。

若只按 `current_uid()` 过滤目标 App 的进程，Provider 代办路径不会命中。需要可靠恢复 Binder caller identity，并在 Provider 的 query/open 两条链上保持一致。

### 8.5 Photo Picker 与 CloudMediaProvider

Photo Picker 有自己的查询、recent、搜索、local/cloud 数据源和 URI 授权模型。它不是 MediaStore query 的简单别名。

因此选择器隐藏必须单独声明；没有 `photo_picker` capability 时，产品不能暗示图片在系统选择器中不可见。

### 8.6 已有 FD 与跨进程传递

如果 hide 激活前目标进程已经持有文件或目录 FD，或者其他进程通过 Binder/Unix socket 传入 FD，路径 lookup 已经结束。VFS 名称隐藏不能撤销这个对象引用。

首版明确不保证回收已有 FD。规则激活策略应优先绑定应用冷启动，并把“已有 FD”写入非保证范围。

### 8.7 能否逐层针对性隐藏

可以，但必须把问题表达为能力组合：

```text
direct_vfs
mediastore
saf
photo_picker
cloud_media
backing_view
```

每一层都要有：

- 可识别的真实调用方；
- 完整的列举、lookup/open 和 mutation/query 契约；
- 缓存失效方式；
- 独立测试与运行时状态；
- 不可用时的拒绝策略。

这比寻找一个“万能 Hook”更复杂，但边界清晰、可验证，也符合单一职责原则。

## 9. 候选方案调研结果

### 9.1 总览

| 方案 | readdir 隐名 | lookup `ENOENT` | raw syscall | per-app | 无新增 mount | mutation 完整 | 结论 |
|---|---:|---:|---:|---:|---:|---:|---|
| bind mount / 权限 | 否 | 否 | 阻断但非隐藏 | 可按 namespace | 否 | 不适用 | `deny`，不是 hide |
| OverlayFS whiteout | 是 | 是 | 是 | 可按 namespace | 否 | 改变父目录写语义 | Rejected |
| Java/libc/Zygisk Hook | 部分 | 部分 | 否 | 是 | 是 | 否 | 不可作 exact backend |
| PathMask/NoOpt 类返回后改写 | 部分 | 部分 | 入口依赖 probe | UID 粗粒度 | 是 | 否 | Reference / Anti-pattern |
| MediaProvider 私有 FUSE接入 | 机制上可行 | 机制上可行 | 仅共享存储 FUSE面 | 有请求 UID | 是 | 理论可覆盖 | 公开稳定接入不存在 |
| SUSFS 2.2 | 是 | 是 | VFS 内联 | 不满足逐规则 scope | 是 | 机制较完整 | Blocked |
| Kasumi API 17 | 设计目标是 | 设计目标是 | VFS 层为主 | 全局 VIEW/SPOOF scope | 是 | 不完整 | 仅作机制参考 |
| NoMount v20 | 是 | 设计目标是 | VFS 层为主 | 单 UID，但无 namespace | 是 | 否 | Reference / Rejected |
| 最小 PathGuard VFS 后端 | 设计要求是 | 设计要求是 | 设计要求是 | 设计要求是 | 是 | 设计要求是 | 最可信方向，尚未实现 |

### 9.2 bind mount

bind 一个空目录或不可访问目录到目标路径，可以让后代内容不可访问，但不会删除父目录中的 basename：

```text
readdir("Pictures")       -> 仍可返回 Nagram
stat("Pictures/Nagram")  -> 仍可观察挂载点或权限结果
```

同时新增 mount 会出现在目标 namespace 的 `/proc/self/mountinfo`。因此它继续服务于稳定的 `deny`，不进入 hide 后端。

### 9.3 OverlayFS whiteout

whiteout 能在合并视图中同时影响 readdir 和 lookup，单看读取语义接近 hide。但它必须替换目标父目录视图，带来：

- 新 mount 与 mountinfo 痕迹；
- upper/workdir 生命周期；
- 兄弟项 copy-up；
- rename、chmod、xattr 和 inode 语义变化；
- FUSE lower、SELinux、OEM OverlayFS 组合风险。

它已经违反冻结约束，不再安排实现 spike。

### 9.4 应用层或 Zygisk syscall Hook

Hook `File.list()`、libc `readdir()` 或少数 syscall wrapper 适合做兼容实验，不适合安全语义：

- native 库可以直接 syscall；
- 静态链接、内联和替代 libc 路径可绕过；
- Provider 在另一个进程执行；
- mutation 的副作用可能发生在返回值改写之前；
- 需要持续追逐 API 和 ABI。

因此不能把“针对常用软件 API”定义为 Hide 1.0。

### 9.5 PathMask / NoOpt 类后置改写

这类实现通常通过 kprobe/kretprobe：

- 在 `getdents64` 返回后改写用户 dirent buffer；
- 在 `stat/open` 返回后修改 errno；
- 用固定数量 probe instance 承载并发；
- 用路径字符串或 inode number 判断目标。

主要问题：

- probe 容量耗尽、分配/uaccess 失败可能直接泄漏；
- `nmissed` 不是可接受的偶发误差；
- 相对 `dirfd` 和 alias 可能绕过字符串匹配；
- 成功后的 `O_TRUNC/O_CREAT` 无法撤销；
- 只按 inode number 会跨 superblock 误判。

其 KMI 打包、启动诊断和 UID 映射可以参考，数据面不能用于 exact hide。

### 9.6 SUSFS 2.2

SUSFS 将隐藏判断放入 namei/dcache/open 和 getdents actor，时机正确，且能同时标记 FUSE inode 与 backing inode。它证明 direct-VFS 真隐藏应在“名字解析前”和“目录项写入用户缓冲前”完成。

阻塞点不是机制强度，而是控制作用域：

- `add_sus_path` 以路径为主，没有每条规则的 package/UID/namespace handle；
- 生效依赖 KernelSU 的 task 分类，例如 `TIF_PROC_UMOUNTED`；
- 同一 inode 标记会影响一类 App；
- 不能表达不同 App 拥有不同 hide 集合；
- MediaProvider 本身的可见性仍需独立处理。

因此 SUSFS 是最佳机制参考之一，但现有 ABI 不能直接成为 PathGuard adapter。

### 9.7 MediaProvider 私有 FUSE 接入

AOSP MediaProvider FUSE 天然持有请求 UID，并在 lookup、readdir、open 和 mutation 附近工作，机制上很适合共享存储。

但 PathGuard 没有稳定公开的规则注册 API。关键逻辑位于 Mainline MediaProvider 的私有 C++ 实现中，路径拼接、目录快速路径和 dentry cache 控制分散；inline patch 私有布局会随 APEX/OEM 更新变化。

结论：可继续作为长期平台合作或稳定 adapter 研究方向，不作为当前可交付后端。

### 9.8 NoMount v20

NoMount 是 KernelSU/APatch 的 VFS 路径注入/whiteout 框架。它不创建 mount，而是替换目标父目录的 `i_op.lookup` 与 `i_fop.iterate_shared`，whiteout 在 lookup 中制造 negative dentry、在 readdir actor 中省略 basename。规则中的 `target_uid` 可限制到单个调用 UID，因此它比 SUSFS 的全类 App scope 更接近“只向 LocalSend 隐藏 Nagram”。

但是它不是 PathGuard exact hide 的直接候选：

- 仅替换 `.lookup` 和 `.iterate_shared`，继承真实 `.atomic_open`，因此 FUSE cold-cache open 路径存在与 Kasumi 同类的绕过风险；
- 没有 `create/mkdir/unlink/rename/link/symlink` 等 mutation wrapper，无法确保失败前没有触达真实对象；
- 规则只有 `target_uid`，没有 package/user 验证、mount namespace cookie 或 policy generation；
- 同一 parent/basename 的 child index 只容纳一个 rule pointer，多个 UID 的同路径规则会相互覆盖；
- hook 或 child-array 内存分配失败不会传回 `add_rule`，可能出现控制面成功、数据面未安装；
- 通过控制进程的 `kern_path()` 解析并绑定 parent inode，对 Android 共享存储 alias、独立 namespace 与 pass-through/backing 平面没有完整保证；
- 直接替换 `super_block.s_op/s_xattr`、inode `i_op/i_fop` 和 dentry `d_op`，属于 VFS 内部实现，不是稳定 GKI KMI。

NoMount 的无 mount、parent/basename 目录项过滤、RCU/seqcount 快路径和 observer-aware dentry revalidate 值得作为设计证据；原版不能整体引入，也不能作为 Hide 1.0 数据面。

### 9.9 最小 PathGuard VFS 后端

从机制上，最可信方向仍是一个职责单一的 VFS 后端：

- 只处理按观察者隐藏单个 parent/basename；
- 在 lookup、`atomic_open`、readdir 和 mutation 入口统一决策；
- 不 mount；
- 不做 Provider query；
- 使用不可变 policy generation 和事务安装；
- 仅在已验证 KMI/ROM 上报告可用。

它不是“通用 LKM”承诺。若必须解析未导出符号或替换内部 operation table，就要按内核 build fingerprint 白名单，并接受 OTA 后重新准入。

## 10. VFS whiteout 参考源码审计

### 10.1 审计对象

本地源码：

```text
refer/Kasumi-main
```

该目录不含独立 `.git` 元数据，因此不能从本地快照可靠恢复 Kasumi 上游 commit。本文不使用 PathGuard 父仓库提交号冒充上游版本，审计对象以本地文件内容、协议版本和下述源码位置共同定位。

协议版本：

```c
#define KSM_PROTOCOL_VERSION 17
```

位置：`refer/Kasumi-main/src/include/kasumi_uapi.h:25`。

### 10.2 值得借鉴的机制

Kasumi 的 `dirhijack` 比 syscall 返回后改写更接近 exact hide：

1. `kasumi_dh_lookup()` 对隐藏观察者制造 negative dentry，不调用真实 lookup，目标表现为 `ENOENT`；
2. `kasumi_dh_proxy_actor()` 在原始目录项写给调用者前省略匹配 basename；
3. 自定义 `d_revalidate` 根据 observer scope 使同一 dentry 对不同观察者重新解析；
4. 使用 RCU/SRCU、shadow operation 和 policy replace 处理并发与视图切换；
5. 能从 KernelSU provider 判断 VIEW/SPOOF 观察者。

关键位置：

```text
src/core/kasumi_dirhijack.c:276   negative dentry
src/core/kasumi_dirhijack.c:396   readdir actor
src/core/kasumi_dirhijack.c:539   observer-aware d_revalidate
src/core/kasumi_dirhijack.c:986   inode_operations clone
```

这些设计证明：lookup、readdir 和 observer-aware cache 是正确问题域。

### 10.3 高风险缺口一：FUSE `atomic_open`

Kasumi 安装 shadow inode operations 时执行：

```c
im->fake_iop = *orig_iop;
im->fake_iop.lookup = kasumi_dh_lookup;
```

它只替换 `.lookup`，其余 operation，包括真实文件系统的 `.atomic_open`，从原表继承。

本机 `/proc/kallsyms` 已确认存在：

```text
fuse_atomic_open
fuse_dir_inode_operations
```

Linux 打开最后路径分量时，在满足条件的 dcache miss 路径可以调用目录 inode 的 `.atomic_open`。FUSE 的 `fuse_atomic_open` 可以自行完成 lookup/open，而不经过被替换的普通 `.lookup`。

由此得到高风险推论：

```text
cold dcache -> open/openat hidden child
    可能进入继承的 fuse_atomic_open
    可能绕过 kasumi_dh_lookup

stat hidden child -> 先制造 synthetic negative
    后续 open 可能得到 ENOENT
```

这意味着结果可能依赖访问顺序和 dcache 状态。`opendir` 使用的打开路径也必须纳入测试，但是否在该内核上命中同一 `atomic_open` 分支，应由 HideLab 追踪/结果确认，不能仅凭源码推导宣称已复现。

无论实际复现结果如何，PathGuard 后端都必须显式包装或拒绝带 `.atomic_open` 且无法安全治理的 operation table。只替换 `.lookup` 不满足准入。

### 10.4 明确缺口二：mutation 未封闭

由于 Kasumi 克隆整张原始 inode operation table 后只替换 lookup，以下 operation 仍指向真实文件系统：

```text
create / mkdir / mknod / symlink / link
unlink / rmdir
rename
```

此外 `.atomic_open` 还可能承载 `O_CREAT`。风险包括：

- rename destination 指向隐藏 basename 时覆盖真实对象；
- `O_TRUNC` 触及真实文件；
- 创建隐藏名称返回 `EEXIST`，泄露真实对象存在；
- unlink/rmdir/rename source 通过已有 dentry 触达真实对象；
- 返回结果无法统一为 `ENOENT`。

这是相对 PathGuard mutation contract 的明确不完整，不需要等到 UI 测试才成立。

### 10.5 明确缺口三：hide 规则缺少逐规则作用域

`kasumi_hide_entry` 只保存（`refer/Kasumi-main/src/internal/kasumi_types.h:65`）：

```c
char *path;
u32 path_hash;
```

`kasumi_dh_child` 保存名称、来源、flags 和 `hide`，没有 policy handle、target UID 或 namespace cookie。

KernelSU provider 的判断是全局观察者分类（`refer/Kasumi-main/src/policy/kasumi_path_policy.c:724`）：

```c
scope = provider(uid) ? KASUMI_POLICY_SCOPE_SPOOF
                      : KASUMI_POLICY_SCOPE_VIEW;
```

因此原版不能自然表达：

```text
LocalSend        隐藏 Pictures/Nagram
另一个 App       隐藏 Pictures/Other
Control App      两者都可见
```

PathGuard 需要的是“规则 -> 目标身份/namespace -> hidden child”的映射，而不是所有 VIEW observer 共用一份 hide 集合。

### 10.6 明确缺口四：控制面可能静默成功

Kasumi ioctl 添加 hide 后，在 dirhijack 启用时调用（`refer/Kasumi-main/src/control/kasumi_ioctl.c:1439`）：

```c
(void)kasumi_dirhijack_hide(src);
```

返回值被丢弃。若 dirhijack 安装、内存分配或 child 注册失败，控制器仍可能只看到外层规则添加成功，而 lookup 轴没有完整安装。

这违反 PathGuard 的事务准入和 no-degrade 要求。控制面必须能回答：

- 所有必需 operation 是否已经包装；
- 所有 hidden child 是否发布到同一 generation；
- 缓存是否完成必要失效；
- 任一步失败是否整体回滚；
- 当前状态是 active、unsupported 还是 failed。

### 10.7 维护与可信计算基风险

Kasumi 还依赖：

- 未导出符号的运行时解析；
- 通过 kprobe 获取 kallsyms 等辅助能力；
- 替换共享 inode 的 `i_op`、`f_op`、`d_op`；
- ftrace/kretprobe fallback；
- 针对内核版本差异的兼容分支。

这些能力不属于 Android GKI 稳定 KMI。错误恢复、模块卸载、并发 inode 生命周期和 OEM 修改都会扩大可信计算基。

### 10.8 Kasumi 结论

```text
机制：值得参考
原版：不能直接作为 PathGuard exact hide 后端
引入策略：不整体移植，只提取独立设计事实
验证重点：atomic_open、mutation、observer scope、事务失败
```

不整体引入也符合 YAGNI：Kasumi 同时承担注入、合并、重定向、root spoof 等更大职责，PathGuard Hide 1.0 只需要最小 hidden-child 数据面。

### 10.9 NoMount v20 本地源码审计

#### 10.9.1 审计对象与定位

本地源码：

```text
refer/hide-refer/nomount-master
```

该目录没有独立 `.git` 元数据；审计对象以本地快照、`NOMOUNT_VERSION "20"`、模块声明 `v2.0.0` 和源码位置共同定位。许可证为 GPL-3.0。

NoMount 的主产品目标是无 mount 的系统文件注入/重定向，以及让“排除 UID”看见原始系统视图；whiteout 是该框架的一个能力，不是为 Android 共享存储 per-app exact hide 设计的独立后端。

#### 10.9.2 可借鉴的机制

NoMount 在正确的 VFS 层处理目录可见性：

```text
parent i_op.lookup          -> nomount_hijacked_lookup
parent i_fop.iterate_shared -> nomount_hijacked_iterate_dir
dentry d_revalidate         -> 按当前 UID 重验视图
whiteout                    -> negative dentry + readdir omit
```

对应位置：

```text
kernel/src/nomount.c:303   lookup
kernel/src/nomount.c:327   iterate_shared
kernel/src/nomount.c:682   d_revalidate
kernel/src/nomount.c:832   operation-table hijack
```

规则数据带有 `target_uid`，匹配条件是 `target_uid == 0 || target_uid == current_uid().val`。这证明“per-UID whiteout”可以放在 lookup/readdir 共同边界，而无需新建 mount。另有一个全局 UID bypass table：命中的 UID 对全部 NoMount 规则回退到真实文件系统。这适合其 root-detection 排除用途，但与 PathGuard 的正向 target-policy 模型不同。

#### 10.9.3 明确缺口一：`atomic_open` 与 mutation 未治理

`nomount_hijack_dir_ops()` 复制原 inode/file operation table 后，只改写：

```c
fake_iop.lookup = nomount_hijacked_lookup;
fake_fop.iterate_shared = nomount_hijacked_iterate_dir;
```

源码没有 `.atomic_open`、`create`、`mkdir`、`mknod`、`symlink`、`link`、`unlink`、`rmdir` 或 `rename` 的 wrapper。

因此存在两个不可接受的 direct hide 缺口：

1. FUSE 目录在 cold dcache 的 `open/openat` 可能通过继承的 `.atomic_open` 自行 lookup/open，绕过只替换的 `.lookup`；
2. 对 hidden basename 的 `O_CREAT/O_TRUNC`、rename、unlink 等不受前置 guard 保护，可能泄露 `EEXIST`、成功修改真实对象，或发生失败后副作用。

这不是“后续优化项”，而是 Hide 1.0 无法准入的硬条件。HideLab 必须用 cold `openat/openat2`、`stat -> open` 顺序和完整 mutation matrix 在目标 FUSE/KMI 上复现或证伪绕过；在此之前不能把 NoMount whiteout 视为 exact hide。

#### 10.9.4 明确缺口二：规则作用域与多 UID 冲突

`struct nomount_rule` 有 `target_uid`，但没有 namespace、package、user 或 policy generation。规则由控制进程的 `kern_path()` 解析父目录，再直接替换该 inode operation table；它不是以目标应用 namespace/已验证目录 FD 安装。

这会带来两层问题：

- 同一 UID 的所有进程统一命中，无法区分 shared UID 的不同 package，也不处理 isolated UID；
- Android 共享存储 FUSE alias 和 pass-through/backing 可处于不同 superblock/namespace 平面，控制进程解析到的 parent inode 不能证明等同于目标 App 的全部可达视图。

更具体地说，RB tree 的 key 允许相同 virtual path 使用不同 `target_uid`，但 parent child-array 的 `nomount_bsearch_child()` 仅按 basename 找到一个 pointer；`__nomount_inject_child_locked()` 对同名项直接覆盖该 pointer。第二条同路径 UID 规则会使第一条从数据面失效，删除第二条也不会自动恢复第一条。

因此 NoMount 不能正确表达：

```text
LocalSend  隐藏 Pictures/Nagram
App B      也隐藏 Pictures/Nagram
Control    仍看到 Pictures/Nagram
```

#### 10.9.5 明确缺口三：安装非事务且可静默 fail-open

`nomount_hijack_dir_ops()` 的返回类型为 `void`。其 `kzalloc` 失败、inode 缺少可替换 operation 或 file operation 不可替换时，调用方不会收到错误。`__nomount_inject_child_locked()` 同样在 child-array `kmalloc` 失败时直接返回；`nomount_generate_virtual_topology()` 随后仍可能返回成功，`__nomount_add_rule()` 再把规则放入全局 RB tree 并记录“Successfully added”。

规则替换也不是事务式：旧 rule 先从 tree/parent child index 摘除，再尝试生成新拓扑；后续失败时旧规则不会回滚。批量 payload 中每条规则依次安装，`payload->status` 会被最后一次结果覆盖，之前成功的规则也不会因后续失败撤销。

这违反 PathGuard 的核心要求：完整 operation 验证、完整 child 发布、cache 处理和 generation 发布必须作为一个事务完成；任何失败都不能报告 active。

#### 10.9.6 维护、缓存与可信计算基风险

NoMount 除目标 parent inode 外，还会修改整个 superblock 的：

```text
s_op
s_xattr
```

并直接写入 inode `i_op/i_fop` 与 dentry `d_op`。whiteout dentry 的 `d_op` 会被替换为仅含 NoMount `d_revalidate` 的表，同时清除原文件系统 dentry operation flags，而不是链式调用原实现。这扩大了与 FUSE、其他 LSM/FS、cache revalidate 和模块卸载的兼容风险。

项目使用 `VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver` 与 `ANDROID_GKI_VFS_EXPORT_ONLY` namespace import。它说明模块需要内部 VFS 能力，不构成稳定 KMI 承诺；“预编译 LKM 可以加载”也不等于本机 operation layout、生命周期和 OTA 后仍正确。

#### 10.9.7 NoMount 结论

```text
机制：无 mount 的 VFS whiteout 参考
UID 规则：可表达单 UID，但不构成完整 per-app scope
原版：不满足 PathGuard exact hide
引入策略：不整体移植，不作为 Hide 1.0 后端
验证价值：作为 HideLab 的 atomic_open、mutation、scope、安装失败对照对象
```

## 11. 当前可行性结论

### 11.1 “真隐藏”仍然存在，但必须限定声明域

若“真隐藏”指对所有软件、所有系统服务、Root、已有 FD 和任意侧信道都与对象从未存在完全等价，则当前项目不应承诺。

若定义为：

> 对指定非 Root 应用，在已声明并处于 active 的访问面中，目标目录不可枚举，已知路径及后代返回 `ENOENT`，mutation 不触及真实对象，其他应用语义不变。

则该能力在技术上有方案方向，但需要设备限定的 VFS 后端和分层 Provider adapter。

### 11.2 当前设备不是“开箱即用”

`myron` 具备 kprobe、ftrace、BPF、module 和 Android 16 GKI 基础，适合作为研发目标机；但：

- 没有 SUSFS backend；
- KPM ioctl 不可用；
- 没有标准 LKM dirent/lookup filter ABI；
- Kasumi 原版没有通过 PathGuard 契约；
- NoMount 原版没有通过 PathGuard 契约；
- 还没有 HideLab 对目标内核完成矩阵验证。

所以当前能力状态仍是 `unsupported`，不能因为设备配置丰富就直接标为 supported。

### 11.3 建议方向

短期：先完成 HideLab，以及 Kasumi/NoMount 对照实验，不改生产 schema。

中期：实现一个固定设备、固定父目录、固定 basename、固定 target UID/namespace 的最小 VFS prototype；仅验证 lookup、`atomic_open`、readdir、mutation 和 cache。

长期：若 prototype 通过，再设计 versioned ABI、KMI 白名单和 Provider capabilities。若无法保证 operation 完整性或 OTA 维护成本不可接受，则保持 hide unsupported。

## 12. hide 能力收缩原则

能力收缩不是把“隐藏”改成“打不开”，而是缩小承诺范围。

### 12.1 可以收缩的维度

- 设备：只支持经过验证的 KMI、内核 build fingerprint 或 ROM；
- 对象：首版只支持共享存储目录，不支持文件、glob、regex；
- 调用方：只支持非 Root 普通应用主 UID；
- 生命周期：首版只在应用冷启动前安装，不承诺在线无缝变更；
- 访问面：用 capability 明确 direct VFS、MediaStore、SAF、Picker；
- 规则数量：设置有诊断的硬上限；
- alias：只声明已经解析和验证的 storage roots；
- 已有 FD：明确不回收。

### 12.2 不可收缩的核心语义

在任何声明为 active 的访问面内，以下项目不能省略：

- 目录枚举不返回 basename；
- 已知目标和后代不能直接访问；
- raw syscall 不绕过；
- mutation 不触达真实对象；
- 非目标 App 不受影响；
- 安装失败不部分发布；
- 状态不谎报；
- hide 不新增 mount。

### 12.3 产品措辞

允许的措辞：

> 已对 LocalSend 启用 Direct hide；MediaStore、SAF 和 Photo Picker 当前不受保护。

不允许的措辞：

> Nagram 已完全隐身。

运行时必须把边界显示为机器可读 capability 状态，而不是藏在说明文档中。

## 13. 分阶段 Hide 路线

### 13.1 版本定义

| 版本 | 能力 | 保证 | 不包含 |
|---|---|---|---|
| Hide 0 / Lab | HideLab 与后端对照 | 能发现泄漏、误伤和破坏性失败 | 不向用户提供 hide |
| Hide 1.0 Direct | `direct_vfs` | lookup、`atomic_open`、readdir、后代和 mutation 完整 | MediaStore、SAF、Picker、已有 FD |
| Hide 1.5 Multi-KMI | `direct_vfs` 扩展设备 | 语义不变，扩大 KMI/OEM 白名单 | 不增加 Provider 面 |
| Hide 2.0 Media | `direct_vfs + mediastore` | query、已知 URI、ContentResolver open 不泄露 | SAF、Picker/Cloud |
| Hide 3.0 Documents | 增加 `saf` | DocumentsProvider 枚举、搜索、open 一致 | Photo Picker/Cloud |
| Hide 4.0 Picker | 增加 `photo_picker/cloud_media` | 系统选择器声明范围内一致 | Root、已有 FD、未支持 Provider |

### 13.2 为什么 Hide 1.0 不能是 syscall Hook 试用版

Hide 1.0 虽然只覆盖 direct VFS，但 direct VFS 本身是一个完整一致性域。少一个入口就会出现同一 App 内：

```text
File.list() 看不到
raw getdents64 看得到

stat 返回 ENOENT
open(O_TRUNC) 却截断真实文件

热缓存返回 ENOENT
冷缓存 atomic_open 成功
```

这不是“1.0 功能少”，而是“1.0 保证不成立”。实验版本可以内部存在，但不得以 hide 产品能力发布。

### 13.3 阶段退出条件

每个阶段只有两种正常退出：

```text
PASS         对应 capability 进入受支持矩阵
UNSUPPORTED  保持未支持并记录证据
```

不能以“常用 App 看起来可用”“大部分 case 通过”或“失败概率很低”作为发布结论。

## 14. Hide 1.0 最小 VFS 后端设计

### 14.1 单一职责

Hide 1.0 内核后端只负责 direct VFS 的 hidden child 决策。它不负责：

- MediaStore 数据库过滤；
- SAF/Picker Hook；
- redirect 或 deny mount；
- mountinfo 伪装；
- Root 环境隐藏；
- glob/regex 编译；
- UI 和配置持久化。

### 14.2 最小数据模型

概念模型：

```text
HiddenChild {
    parent_superblock
    parent_inode
    basename
    immutable_policy
}

Policy {
    target_uid
    target_mount_namespace
    generation
}
```

实现中还需要用户 ID、namespace 生命周期 cookie、长度上限和引用管理，但不应提前加入文件内容、通配符或 Provider 数据。

选择 `(superblock, parent inode, basename)` 的原因：

- 对 alias 后的同一 FUSE parent 可统一匹配；
- 不依赖易变的完整路径字符串；
- 不会仅因 inode number 相同而跨 superblock 误伤；
- readdir 天然拥有 parent 与 child basename；
- lookup/mutation 也在 parent/name 边界决策。

### 14.3 必须统一治理的 operation

```text
lookup
atomic_open
iterate_shared / iterate
d_revalidate

create / mkdir / mknod / symlink / link
unlink / rmdir
rename source / destination
```

还必须验证特定内核中 `tmpfile`、whiteout rename flags、`O_PATH` 和 io_uring 等是否进入已覆盖路径。无法证明的 operation 应使该 operation table admission 失败，而不是默认继承。

### 14.4 observer 决策

热路径决策至少绑定：

```text
current task identity
verified target UID/user
target mount namespace generation/cookie
active immutable policy generation
```

只检查 UID 不足以覆盖 shared UID、isolated process 和 Provider 代办。Hide 1.0 可以收缩为“不支持 shared UID/isolated/Provider”，但 admission 必须检测并报告，不得误认为普通 per-package 隔离。

### 14.5 缓存一致性

同一 dentry 可能被目标 App 和非目标 App 观察。后端必须处理：

- hide 前已有 positive dentry；
- hide 后的 synthetic negative dentry；
- 非目标观察者重新看到真实 entry；
- disable 后目标观察者恢复；
- parent rename/delete/recreate；
- inode 回收与 namespace 销毁。

可以采用 observer-aware `d_revalidate` 或隔离视图，但不能让一个观察者生成的 negative dentry 永久污染所有观察者。

Hide 1.0 建议仅在应用冷启动前发布策略，以减少在线变更面；HideLab 仍要测试 enable/disable，确保控制面不会破坏全局 cache。

### 14.6 事务安装

建议安装流程：

```text
解析全部目标到受信 storage root
-> 验证 parent identity 与 basename
-> 验证所有必需 operation 可安全包装
-> 分配完整 immutable generation
-> 安装 shadow/observer 设施
-> 处理已有 dentry/cache
-> 原子发布 generation
```

任一步失败：

```text
不发布新 generation
撤销本次安装的所有 shadow
保持上一 generation 不变
返回稳定错误和 capability reason
```

不允许控制面先写入“规则存在”，再忽略数据面安装返回值。

### 14.7 卸载和故障策略

模块退出、daemon 崩溃、namespace 销毁和 OTA ABI 不匹配必须分别定义。建议：

- ABI/KMI 不匹配：后端不加载，规则 `unsupported`；
- 规则编译失败：旧 generation 保持 active；
- 后端部分安装失败：整体回滚；
- daemon 消失：已发布 immutable policy 可继续，或明确 fail-closed 停止应用启动；
- 无法安全恢复原 operation table：设备不进入支持矩阵。

## 15. capability、admission 与状态模型

### 15.1 配置表达意图，不写死版本号

未来配置应声明所需能力，而不是 `hide_version = 2`：

```toml
[[visibility]]
package = "org.localsend.localsend_app"
path = "Pictures/Nagram"
capabilities = ["direct_vfs", "mediastore"]
```

版本属于发布路线；capability 才是机器可判定的契约。

### 15.2 admission 输入

```text
package -> 当前 user/UID/package attribution
storage path -> canonical root + parent identity + basename
requested capabilities
kernel release + build fingerprint + KMI generation
root framework/runtime mode
backend protocol version
Provider/APEX version
operation table feature probe
规则/内存容量
```

### 15.3 运行时状态

示例：

```text
rule=localsend:nagram
generation=42

direct_vfs    active
mediastore    active
saf           unsupported(reason=no_adapter)
photo_picker  unsupported(reason=no_adapter)
```

若规则请求 `direct_vfs + mediastore`，但 `mediastore` 缺失，则整条规则状态应是：

```text
rejected(reason=required_capability_missing:mediastore)
```

而不是偷偷启用 direct 部分。

### 15.4 状态枚举建议

```text
unsupported  设备/版本没有实现
inactive     能力存在但规则未启用
admitting    正在事务验证，尚未发布
active       全部请求能力已发布
failed       运行时故障，带稳定 reason
rejected     规则或环境不满足准入
```

状态必须能被 CLI、Manager 和 HideLab 读取，且与真实数据面结果交叉验证。

## 16. HideLab：自动化测试“矛”

### 16.1 目标

HideLab 的任务不是证明某个 UI 看起来正常，而是主动寻找：

```text
LEAK              目标 App 发现或访问隐藏对象
OVERBLOCK         Control App 或兄弟项被误伤
SEMANTIC_DRIFT    errno、alias、缓存次序结果不一致
DESTRUCTIVE_FAIL  返回失败但真实对象已被修改
STATE_LIE         runtime 报 active，但数据面未完整安装
CRASH/HANG        内核、Provider 或 App 崩溃/卡死
```

### 16.2 三观察者架构

```text
                    Host Orchestrator
                           |
          +----------------+----------------+
          |                |                |
  Target Probe App  Control Probe App   Root Oracle
  受 hide policy     不受 hide policy     真实/backing视图
  主动尝试绕过       验证正常可见性       校验无破坏
```

任何 hide case 都不能只有 Target Probe。否则“目标看不到”可能是权限、挂载损坏或整个存储不可用造成的。

### 16.3 建议组件

```text
tests/device/hide/
  app-probe-target/       独立 package/UID
  app-probe-control/      独立 package/UID
  native/                 共享 JNI/raw syscall probe
  fixtures/               fixture manifest/schema
  orchestrator/           host/device runner
  schemas/                JSONL 与 summary schema
```

实际实现时优先复用现有：

```text
tests/device/hide/app-probe
native/libs/arm64-v8a/pathguard_hide_vfs_probe
run_hide_h0_baseline.ps1
run_hide_h0_app_probe.ps1
collect_hide_h0_app_evidence.ps1
```

不应重写已有 Java/NIO/JNI、MediaStore、Picker、SAF 和 native probe；应把它们扩展成双 App + Root Oracle 的统一协议。

### 16.4 一次性 fixture

所有 mutation 只能针对随机、一次性测试目录：

```text
Pictures/PathGuardHideLab/<run-id>/
  hidden/
    canary.txt
    child/
      nested.txt
  visible-sibling/
    writable.txt
  rename-source/
  same-prefix-hidden-x/
```

禁止对真实 `Pictures/Nagram` 执行 create/truncate/rename/unlink 测试。真实目录只可用于只读观察案例。

`<run-id>` 必须不可预测且全局唯一，使新 basename 自然形成 cold-dentry case；默认不使用全局 `drop_caches`，避免改变整机状态和掩盖真实生命周期问题。

### 16.5 Root Oracle 基线

启用规则前记录：

```text
storage alias 与 backing path
mount namespace 与 mountinfo hash
每个对象的 superblock/device/inode
目录树结构
文件 size/mode/mtime
canary SHA-256
兄弟项写入能力
```

每组 mutation 后重新校验：

- hidden canary 内容和长度未变；
- hidden 目录未被删除、移动或覆盖；
- 没有意外新建隐藏 basename；
- visible sibling 仍可正常写入；
- Control App 仍能看到和打开真实对象；
- hide 没有新增 mountinfo 记录。

### 16.6 Target Probe 的攻击面

Target Probe 必须同时包含：

- Java `File`；
- Java NIO；
- JNI/libc；
- raw syscall；
- MediaStore/ContentResolver；
- SAF；
- Photo Picker；
- 多线程和多进程；
- 已知完整路径与枚举发现；
- 可控的缓存次序。

Native probe 不应依赖目标 libc wrapper 来发关键 syscall，否则无法验证 wrapper Hook 绕过。

### 16.7 Control Probe 的职责

Control Probe 执行与 Target Probe 相同的只读用例，并执行受控兄弟项写入。它用于发现：

- hide 规则全局生效；
- shared inode operation 替换污染非目标观察者；
- negative dentry 被错误共享；
- storage parent 被整体 Overlay/mount 替换；
- 规则 disable 后视图未恢复。

### 16.8 Orchestrator

Host Orchestrator 负责：

1. 采集设备 fingerprint 和 capability；
2. 创建一次性 fixture；
3. 采集 Root Oracle 基线；
4. 启动 Control/Target 前置观察；
5. 请求 hide rule admission；
6. 验证 runtime status；
7. 按固定顺序执行测试矩阵；
8. 每组后执行 Oracle 校验；
9. disable rule 并验证恢复；
10. 只删除本次 manifest 明确记录的 fixture；
11. 生成 JSONL、summary 和人类可读报告。

APK 安装、权限变更和共享存储 mutation 都会改变设备状态。执行对应 runner 前仍需明确批准；本文只定义测试系统，不授权实际安装或 mutation。

## 17. HideLab 自动化测试矩阵

### 17.1 基础枚举与属性

| Case | Target 期望 | Control 期望 |
|---|---|---|
| Java `File.list/listFiles` | 不含 hidden | 包含 hidden |
| NIO `DirectoryStream` | 不含 hidden | 包含 hidden |
| libc `readdir` | 不含 hidden | 包含 hidden |
| raw `getdents64` | 不含 hidden | 包含 hidden |
| `stat/lstat/statx` | `ENOENT` | 成功 |
| `access/faccessat2` | `ENOENT` | 符合真实权限 |
| `readlinkat` | 不泄露隐藏目标 | 符合真实语义 |
| `O_PATH` | `ENOENT` | 成功 |

`getdents64` 必须覆盖 4 KiB、32 KiB、64 KiB、128 KiB buffer 和跨多轮读取，防止只过滤第一批目录项。

### 17.2 open 与后代

```text
open
openat(relative dirfd)
openat2 + RESOLVE_* flags
opendir
已知 hidden/child/nested.txt
先打开 Pictures dirfd，再启用 hide 后访问
```

所有 Target 结果必须为 `ENOENT`；Control 应按真实权限成功。`EACCES`、空目录或成功后关闭 FD 都不算通过。

### 17.3 alias 与规范化

```text
/sdcard/...
/storage/emulated/0/...
/mnt/user/0/emulated/0/...
应用 namespace 中可达的其他 alias
相对 cwd
相对 dirfd
符号链接 alias
`.` / `..` / 重复 `/`
```

同一 capability 声明内，任一可达 alias 泄漏即失败。backing/pass-through 若不在声明范围，必须先证明 Target namespace 不可达并报告 `out_of_scope`，不能跳过不说明。

### 17.4 mutation

| 操作 | Target 期望 | Root Oracle |
|---|---|---|
| `open(O_CREAT)` hidden basename | `ENOENT` | 未创建/未覆盖 |
| `open(O_EXCL)` | `ENOENT` | 原对象不变 |
| `open(O_TRUNC)` | `ENOENT` | canary hash/size 不变 |
| `mkdir/mknod/symlink` | `ENOENT` | 结构不变 |
| `unlink/rmdir` | `ENOENT` | 对象仍存在 |
| `rename hidden -> visible` | `ENOENT` | hidden 未移动 |
| `rename visible -> hidden` | `ENOENT` | hidden 未覆盖，source 仍在 |
| `renameat2(NOREPLACE)` | `ENOENT` | 两侧均不变 |
| `link visible -> hidden` | `ENOENT` | link count/结构不变 |

若 syscall 返回失败但 Oracle 发现任何副作用，分类必须是 `DESTRUCTIVE_FAIL`，严重级别高于普通 `LEAK`。

### 17.5 缓存和顺序敏感

必须显式测试：

```text
cold unique name -> direct open/openat
cold unique name -> opendir
cold -> stat -> open
readdir parent -> open hidden
Control 预热 positive dentry -> 启用 hide -> Target open
Target 预热 positive dentry -> 启用 hide -> Target open
启用 -> 禁用 -> Target/Control 再访问
禁用 -> 重新启用新 generation
parent rename/delete/recreate 后再访问
```

这组用例专门发现 Kasumi/NoMount 类 `.lookup` 与 `.atomic_open` 路径分裂、positive/negative dentry 污染和规则更新失效。

### 17.6 并发与容量

```text
1 / 20 / 21 / 40 / 41 / 128 threads
lookup + getdents + open 混合
规则 generation 切换
App force-stop/cold-start
namespace 创建/销毁
目录 rename/recreate
达到规则硬上限及上限 + 1
```

任何 `nmissed`、固定 probe instance 耗尽或偶发 basename 泄漏都判失败。错误必须 fail closed 或拒绝 admission，不能以统计成功率接受。

### 17.7 身份隔离

```text
Target package 主进程
Target :remote 进程
Control 独立 UID
同 user 的其他 App
副用户/工作资料
shared UID 两包
isolated process
WebView renderer / child zygote
```

Hide 1.0 若不支持其中某类身份，应在 admission 前拒绝或明确从规则中排除，不得运行后才出现全局误伤。

### 17.8 Provider 与选择器

Hide 2.0+ 分别增加：

```text
MediaStore query：projection 有/无 _data
album/recent/search/thumbnail
已知 content URI query/open
ContentResolver openFileDescriptor

SAF queryChildDocuments/search/recent/openDocument
Photo Picker local/recent/search/open
CloudMediaProvider query/open
```

Provider case 必须验证 Binder caller attribution；不能仅按 Provider 自身 UID 命中或全局隐藏。

### 17.9 mount 与可观察状态

规则启用前后比较：

```text
/proc/self/mountinfo
/proc/self/mounts
/proc/self/mountstats
namespace inode
```

Hide 本身不得产生新 mount。若同一 App 同时配置 deny/redirect，报告应区分既有 mount 与 hide 新增变化，不能误把其他能力的 mount 当成 hide 失败。

### 17.10 权限矩阵

Target/Control 至少覆盖：

```text
普通共享存储权限
READ_MEDIA_IMAGES
MANAGE_EXTERNAL_STORAGE
```

权限 profile 必须分批执行和记录。grant/revoke 会改变设备状态，不能由测试脚本静默完成。

## 18. 结果协议与准入门

### 18.1 JSONL 示例

```json
{"case":"direct_open_cold","observer":"target","expected":"ENOENT","actual":"SUCCESS","result":"LEAK"}
{"case":"rename_to_hidden","observer":"target","expected":"ENOENT_NO_SIDE_EFFECT","actual":"SUCCESS","result":"DESTRUCTIVE_FAIL"}
{"case":"control_readdir","observer":"control","expected":"CONTAINS","actual":"OMITTED","result":"OVERBLOCK"}
```

每条记录至少包含：

```text
schema/run_id/timestamp
device fingerprint/kernel/KMI/root framework
backend/protocol/generation/capability state
observer/package/uid/user/namespace
fixture/alias/operation/arguments
expected/actual/errno/duration
oracle before/after hash
result/reason
```

### 18.2 结果分类

```text
PASS
LEAK
OVERBLOCK
SEMANTIC_DRIFT
DESTRUCTIVE_FAIL
STATE_LIE
CRASH
HANG
UNSUPPORTED
INFRA_ERROR
```

`UNSUPPORTED` 是设备能力结论，不是测试通过；`INFRA_ERROR` 必须重跑，不能算后端失败或成功。

### 18.3 Hide 1.0 准入门

一个设备 profile 只有同时满足以下条件才能进入 `direct_vfs supported`：

1. 所有基础枚举、属性、open、后代、alias 和 mutation case 通过；
2. 所有缓存次序通过，包括 cold open 和 positive dentry 预热；
3. Target 不泄漏，Control 不误伤，Root Oracle 无副作用；
4. 并发和容量边界无漏拦、崩溃、hang、UAF 或 `nmissed`；
5. admission/status 与数据面一致；
6. hide 启用不新增 mount；
7. 模块加载、规则回滚、App restart 和 namespace 销毁可重复；
8. KMI/内核 build fingerprint 被明确记录并白名单化；
9. 真实 LocalSend 只读工作流通过，但不替代 probe 矩阵；
10. 有已验证的禁用/回滚路径。

任一核心 case 失败，整个 device profile 不支持 Hide 1.0。不能把失败入口降级为“该 API 不支持”，因为 direct VFS 内部入口可由攻击应用自由选择。

### 18.4 Provider 阶段准入

Hide 2.0/3.0/4.0 的 Provider capability 分别准入。某个 Provider adapter 失败不应撤销已经验证的 Direct backend，但请求组合规则不能部分激活。

示例：

```text
设备能力：direct_vfs=active, mediastore=unsupported
规则请求：direct_vfs
结果：允许 active

规则请求：direct_vfs + mediastore
结果：rejected，不发布 direct 部分
```

## 19. 性能与可靠性预算

### 19.1 热路径约束

建议沿用 `07` 的 H0 预算：

| 场景 | P95 回归上限 |
|---|---:|
| 无 active hide policy | 1% |
| 其他 namespace 有规则，当前无规则 | 2% |
| 当前 namespace 最多 256 条规则的 non-match | 3% |

同时报告绝对纳秒、样本数和至少 30 轮统计，避免微基准噪声。

热路径设计要求：

```text
零动态分配
零全局 mutex
零常规日志
有界 lookup
不可变 generation
容量在 admission 时检查
```

### 19.2 benchmark

```text
stat/open non-match
lookup match
readdir 100 / 1,000 / 10,000 / 100,000 entries
1 / 20 / 128 并发线程
应用冷启动
规则发布/回滚
MediaStore query 100 / 1,000 rows
```

### 19.3 稳定性

至少执行：

- 反复 app force-stop/cold-start；
- namespace create/destroy soak；
- policy generation 更新 soak；
- Provider restart（对应阶段）；
- 内存分配失败和容量故障注入；
- backend unload/disable 恢复；
- tombstone、kernel log、RCU stall 和 hung task 扫描。

## 20. 实施顺序

### Phase A：冻结契约和建设 HideLab

产物：

- 本文与 exact semantics checklist；
- Target/Control 双 App；
- native raw syscall probe；
- Root Oracle；
- disposable fixture 与 JSONL schema；
- baseline：无 hide 后端时 Target/Control 均可见。

停止条件：若测试不能可靠区分 LEAK、OVERBLOCK 与 DESTRUCTIVE_FAIL，不进入内核实现。

### Phase B：Kasumi 与 NoMount 对照实验

目的不是产品集成，而是用 HideLab 验证两种 VFS whiteout 源码审计：

- cold `atomic_open` 是否绕过；
- `stat -> open` 与 `open first` 是否分裂；
- mutation 是否触达真实对象；
- Kasumi 的 VIEW/SPOOF scope、NoMount 的 UID/bypass scope 是否误伤 Control；
- NoMount 同路径多 UID rule 是否互相覆盖；
- 安装失败时 ioctl/status 是否谎报。

产物是证据报告，不把 Kasumi 或 NoMount 代码并入生产树。

### Phase C：固定目标最小原型

只实现：

```text
单一受支持内核
单一 target UID/namespace
单一 parent/basename
lookup + atomic_open + readdir + mutation + d_revalidate
```

不实现动态 schema、UI、Provider、glob、文件级规则或通用 KMI。

停止条件：任一 direct case 无法 fail closed，或共享 inode operation 无法安全恢复，则停止后端路线并保持 unsupported。

### Phase D：versioned ABI 与多规则

在固定原型完整通过后才增加：

- immutable policy generation；
- 多 parent/child；
- versioned control ABI；
- package/user/UID/namespace admission；
- 硬容量和诊断；
- 事务 replace/rollback。

### Phase E：Hide 1.0 设备白名单

建立：

- kernel release/build fingerprint/KMI matrix；
- operation layout probe；
- OTA 后自动降级为 `unsupported` 的规则；
- 安装前检查与恢复方案；
- 至少两个 ROM/设备家族的证据，若产品只批准单设备发布则明确单设备声明。

### Phase F：Provider capabilities

按 MediaStore -> SAF -> Picker/Cloud 顺序独立推进。每个 adapter 都应拥有自己的 ADR、测试矩阵和 status bit，不扩大内核后端职责。

## 21. 停止条件与明确不实施项

遇到以下任一条件，应停止相应路线，而不是继续堆 Hook：

- 必须在 syscall 成功后改写返回值才能隐藏；
- mutation 副作用无法在执行前阻断；
- operation 包装存在已知未治理入口；
- 缓存只能通过全局关闭或频繁 `drop_caches` 保证；
- 规则不能逐 App/namespace 隔离；
- 固定 probe 容量耗尽会 fail-open；
- ABI/KMI 不匹配仍允许加载；
- 需要用 mountinfo 字符串 Hook 掩盖新增 mount；
- Provider 无法可靠恢复调用方，却尝试按猜测 package 过滤；
- Control App 或兄弟项语义发生变化。

当前明确不实施：

```text
用 bind mount 冒充 hide
OverlayFS 单目录 whiteout 后端
仅 Java/libc Hook 的 Hide 1.0
PathMask 式 post-success 返回值改写
没有 per-app ABI 的 SUSFS 全局 adapter
整体引入 Kasumi
整体引入 NoMount
hide 失败自动降级 deny
在 HideLab 完成前修改 rules.toml 和 Manager UI
```

## 22. 风险与明确不保证

### 22.1 不保证的攻击者

- Root 应用；
- 内核代码执行；
- ptrace/调试等高权限攻击者；
- 能读取 PathGuard 控制面或 Root 日志的主体。

### 22.2 不保证的对象引用

- hide 激活前已有 FD；
- 其他进程已经代开并传入的 FD；
- 对应 Provider capability 未 active 时的 content URI；
- 未纳入 capability 的 backing/pass-through view。

### 22.3 可写父目录侧信道

真实不存在的名称通常允许创建；隐藏一个真实对象后，为保护真实对象，PathGuard 对该名称的创建也返回 `ENOENT`。恶意应用可能通过写入结果、时序或系统 Provider 侧信道推断异常。

完全模拟“对象从未存在且可以创建一个私有同名新对象”需要 shadow writable filesystem，已经是完整隔离/虚拟化能力，不属于 hide。

### 22.4 兼容与 OTA 风险

VFS 内部实现不是稳定 Android 应用 ABI。任何内核、Root framework、MediaProvider APEX 或 OEM OTA 都可能改变能力判断。

正确策略是 OTA 后重新 admission，不是沿用旧的 `active` 缓存。无法证明兼容时报告 `unsupported`。

## 23. 架构原则

### KISS

- Hide 1.0 只解决 direct hidden child；
- 以 parent identity + basename 建模；
- 固定原型先于通用配置；
- capability 状态直接表达真实边界。

### YAGNI

- 首版不做文件、glob、regex、shadow writable view；
- 不在后端可行前升级 schema/UI；
- 不整体引入 Kasumi 的注入、合并和 spoof 能力；
- 不整体引入 NoMount 的系统文件注入、global UID bypass 和 superblock 改写；
- 不为尚未批准的 ROM 提前维护兼容分支。

### DRY

- 扩展 `tests/device/hide` 的现有 probe，而不是另写第二套；
- Target/Control 共享 native case library 和结果 schema；
- 所有 adapter 复用 capability/admission/status 协议；
- Root Oracle 统一校验所有 mutation。

### SOLID

- Direct VFS、MediaStore、SAF、Picker adapter 单一职责；
- 新 Provider 通过 capability 扩展，不修改 VFS 核心契约；
- 控制面依赖 versioned backend interface，不依赖具体项目实现；
- 测试 probe、orchestrator、oracle 分离，便于独立替换。

## 24. 决策记录

| 决策 | 状态 | 理由 |
|---|---|---|
| `hide` 与 `deny` 分离 | Accepted | 可观察语义和后端不同 |
| exact hide 核心语义不降级 | Accepted | 防止产品名称掩盖泄漏 |
| 允许设备/KMI 限定 | Accepted | 通用 stock kernel 缺少稳定机制 |
| 按访问面 capability 分阶段 | Accepted | Provider 与 direct VFS 是不同主体 |
| Hide 1.0 必须完整覆盖 direct VFS | Accepted | 入口遗漏即可绕过或破坏 |
| OverlayFS 正式路线 | Rejected | mountinfo 与兄弟写语义冲突 |
| PathMask/NoOpt 数据面 | Rejected | post-success、副作用和 fail-open |
| SUSFS 2.2 直接 adapter | Blocked | 缺少逐规则 per-app/namespace ABI |
| Kasumi 原版直接集成 | Rejected | atomic_open、mutation、scope、事务缺口 |
| NoMount 原版直接集成 | Rejected | atomic_open、mutation、namespace、多 UID rule、事务缺口 |
| 最小 PathGuard VFS 后端 | Proposed | 最接近冻结语义，需 HideLab 证明 |
| HideLab 先于生产实现 | Accepted | 没有攻击矩阵就无法证明能力 |

## 25. 后续交付清单

只有以下清单按顺序完成，才讨论对用户开放配置：

- [x] 将本文件与 `07` 的 exact semantics 转成机器可执行 case IDs；
- [x] 将现有 H0 probe 升级为 Target/Control 双 App；
- [x] 实现 Root Oracle 与 disposable fixture manifest；
- [x] 增加 raw syscall、mutation、alias、cache-order、并发矩阵；容量和生命周期尚未实现；
- [x] 定义 JSONL schema 和 baseline summary gate；
- [x] 运行无后端 baseline；
- [x] 运行 Kasumi API 17 与 NoMount v20 对照实验并归档证据；（源码审计完成；两者均无在 myron 上复用 SukiSU loader 的构建/加载证据，运行项保持 unsupported）
- [x] 复刻 SukiSU `android16-6.12` DDK + loader 的离线 `pathguard_probe.ko` 检查；（真实 DDK ELF 的单符号重定位、空 `__versions` 和 vermagic 内存适配已通过；尚未设备加载）
- [ ] 完成固定内核最小 VFS prototype；（仅完成 fail-closed admission shell，真实 VFS 数据面待匹配 KMI）
- [ ] 通过 Hide 1.0 全矩阵和可靠性测试；（无后端全量基线已完成，显式隐藏期望为 LEAK）
- [x] 决定支持设备/KMI 与 OTA 策略；（myron/android16-6.12 白名单，OTA 必须重新 admission）
- [ ] 设计 versioned ABI 和 capability status；
- [ ] 最后才修改规则 schema、daemon、CLI 与 Manager；
- [ ] MediaStore、SAF、Picker 按独立阶段推进。

### 25.1 2026-09-02 Phase A 实际进展

已新增 `tests/device/hide/hidelab_acceptance_matrix.json`，固定 Hide 1.0
direct-VFS case ID、Target/Control/Root Oracle 期望和失败分类。原有
`app-probe` 已改为 Kotlin/Gradle Kotlin DSL，使用与 YingLi-Player 相同的
JDK 21、Gradle 9.5、AGP 9.3.1、Kotlin 2.4.0 与 API 36；它构建两个独立 UID：

```text
dev.pathguard.hideprobe.target
dev.pathguard.hideprobe.control
```

native probe 已覆盖 4/32/64/128 KiB `getdents64` buffer，并提供默认关闭的
`--attack-mutations` 模式。该模式会对 disposable fixture 执行 create、truncate、
mkdir、unlink、rename source/destination、link、symlink，由 Root Oracle 前后
snapshot 判定 `DESTRUCTIVE_FAIL`；它不得在无后端 baseline 中启用。

本机 `myron` 的无后端 baseline 证据目录为：

```text
build/device-evidence/hidelab-baseline/20260902-000712
```

结果：Target UID `10448`、Control UID `10480` 分属不同 mount namespace；两者对
8 个 alias 都可见 fixture，`lstat/open/readdir` 与四种 `getdents64` buffer 均符合
“无后端可见”的基线，Root Oracle 前后无差异，fixture 已清理。该结果明确为
`BASELINE_VISIBLE_NOT_HIDE_PASS`。

Android 16 app seccomp 在 arm64 上会以 `SIGSYS` 终止 raw `openat2`（syscall 437）。
HideLab 因而在 APK 域将 `openat2`、`faccessat2`、`renameat2` 报告为
`UNSUPPORTED/ENOSYS`，并保留 shell/native executable 的真实 syscall 测试路径。
`UNSUPPORTED` 不是通过；当前设备尚未满足 Hide 1.0 准入，禁止启动生产 VFS 后端。

### 25.2 2026-09-11 Phase A 采集增强与设备证据

HideLab runner 已增加两类显式场景：

1. `-Scenario cache-order`：对每个 alias 执行 cold `open`、cold
   `opendir`、`stat -> open`、`readdir -> open` 和 positive dentry warmup
   后再次 `open`。该场景不修改共享存储 fixture。
2. `-AttackMutations -ConfirmMutation`：仅在随机 disposable fixture 内执行
   create、truncate、mkdir、unlink、rename source/destination、link 和
   symlink 攻击；Target 与 Control 之间重新建立 fixture，避免前一观察者污染
   后一观察者。

本机 `myron`（Android 16，6.12.23-android16，Target/Control 均授予
`MANAGE_EXTERNAL_STORAGE`）证据：

```text
build/device-evidence/hidelab-baseline/20260911-213720
conclusion = BASELINE_VISIBLE_NOT_HIDE_PASS
phase = cache-order
fixture_unchanged = true
```

该运行证明了 cache-order 采集协议和 Root Oracle 不变性检查可工作，但不证明
hide。当前设备的 `/mnt/user/0/primary`、`/mnt/runtime/*` 和 `/data/media/0`
alias 在 app namespace 中不可达并返回 `EACCES`；这属于权限/namespace
capability 事实，不能计为 Target 隐藏成功。

显式 mutation 证据：

```text
build/device-evidence/hidelab-baseline/20260911-223113
conclusion = BASELINE_MUTATION_VISIBLE
target_oracle_changed = true
control_oracle_changed = true
```

无后端 Target/Control 均成功触及共享 fixture。Root Oracle 捕获了创建文件、
截断 canary、创建目录、删除后代、双向 rename 等副作用。无后端攻击应记为
`BASELINE_MUTATION_VISIBLE`，这是后端开发前的攻击基线，不是 Hide 1.0 通过。
只有在实际后端运行时显式传入 `-ExpectTargetHidden`，Target Oracle 仍发生变化
才分类为 `DESTRUCTIVE_FAIL`；Control 的正常变化不应被误判为后端失败。

runner 的 Oracle 在 canary 缺失时记录 `MISSING|<path>`，因此删除攻击不会再
中断证据采集。任何后端测试若出现相同前后差异，必须归类为
`DESTRUCTIVE_FAIL` 并拒绝 admission。

隐藏期望模式也已在当前无后端设备上执行：

```text
build/device-evidence/hidelab-baseline/20260911-225254
backend = none
conclusion = LEAK
target_error = java.external.0.exists exposed hidden target
```

这验证了当 Target 仍能看到对象时，runner 会明确输出 `LEAK`，而不是因为
Control 正常可见或 APK 执行成功就报告通过。

runner/native 还增加了 `-Scenario concurrency`。该场景在每个观察者进程内
启动 20 个线程，每线程执行 100 轮混合 `stat/open/readdir`，并汇总三类成功次数；
无后端可见基线的期望值为每类 2,000 次，隐藏模式的期望值为 0。并发场景只读
disposable fixture，仍未覆盖规则 generation 切换、容量上限和 namespace
create/destroy，这三项保留为后端接入后的可靠性测试。

设备证据目录：

```text
build/device-evidence/hidelab-baseline/20260912-004228
target/control canonical concurrency.stat = 2000
target/control canonical concurrency.open = 2000
target/control canonical concurrency.readdir = 2000
fixture_unchanged = true
```

### 25.3 2026-09-12 Phase B/C/D/E 执行结果

按“可靠性测试 -> Kasumi/NoMount 对照 -> 固定设备 prototype -> 全量回归 ->
设备/KMI 白名单与 OTA 重新准入 -> 通过后激活”的顺序执行。本轮新增：

- `tests/device/hide/HIDE1_PHASE_B_KASUMI_NOMOUNT_COMPARISON_MYRON.md`：Kasumi
  API 17 与 NoMount v20 的源码证据、缺失入口、scope/事务风险和 myron LKM
  可加载性结论；两个参考项目均未并入生产树。
- `tests/device/hide/hide1_device_kmi_allowlist.json`：固定 myron、Android 16、
  `android16-6.12`、完整 kernel release 和 required operation mask；fingerprint
  或 kernel release 任一变化即重新进入 `unsupported`。
- `experimental/hide-vfs/`：固定 KMI 的 Hide 1.0 UAPI、构建约束和
  fail-closed admission shell。当前 shell 不安装部分 VFS shadow；没有精确
  kernel build tree/`Module.symvers` 时 install/enable 返回 `-EOPNOTSUPP`，
  防止控制面被误认为已激活。
- `tests/device/hide/run_hidelab_baseline.ps1` 的 `reliability` 场景：1000 轮
  stat/open/readdir 稳定性，以及 generation、capacity、namespace、unload 控制
  面缺失时的明确 `unsupported` 记录。
- `tests/device/hide/run_hide1_full_regression.ps1`：按固定顺序编排
  baseline、cache-order、concurrency、reliability 和显式 mutation 回归。
- `tests/device/hide/admit_hide1.ps1`：OTA 后重新读取 fingerprint/kernel release、
  检查模块 live 状态并输出 admission JSON。

本轮设备证据：

```text
build/device-evidence/hide1-admission/20260912-012409/admission.json
build/device-evidence/hide1-regression/20260912-014844/full-regression.json
build/device-evidence/hidelab-baseline/20260912-013910/summary.json
```

`myron` 的 allowlist 命中，但 `/sys/module/pathguard_hide1` 不存在，故 admission
为 `unsupported`。无后端基线回归结果为 baseline visible、cache-order visible、
concurrency visible、reliability stable、mutation `BASELINE_MUTATION_VISIBLE`。
真正的 `run_hide1_full_regression.ps1` 默认启用 `-ExpectTargetHidden`，本轮结果为
`LEAK, LEAK, LEAK, LEAK, DESTRUCTIVE_FAIL`，总判定 `blocked`；这证明激活闸门能
拒绝没有真实 VFS 后端的构建。当前不能宣称 Hide 1.0 通过，也不能激活 production hide。

因此后续唯一可接受的推进条件是取得与上述 release/KMI 完全匹配的 kernel output
tree，完成真正的 lookup + atomic_open + readdir + mutation + d_revalidate 数据面，
再运行同一 `run_hide1_full_regression.ps1` 并将所有 Target/Control/Oracle/no-new-mount
结果记录为 PASS；OTA 后必须重新运行 `admit_hide1.ps1`，不得复用旧的 active 状态。

在同一无后端设备上以 `-ExpectTargetHidden -Scenario concurrency` 运行，证据
目录 `build/device-evidence/hidelab-baseline/20260912-003754` 明确返回
`LEAK`（Target `concurrency.stat = 2000`，期望为 0）。这证明并发场景也会在
隐藏期望不满足时阻止伪通过。

### 25.4 2026-09-12 GKI LKM 构建链 smoke test

按 Kasumi/NoMount 的通用 GKI LKM 路线，使用 Android `android16-6.12.74_r00`
源码工作树、`android16-6.12` KDIR 和 Android clang r536225，在 WSL 中对
`experimental/hide-vfs` 执行了 out-of-tree Kbuild。为适配离线主机缺少
`pahole` 的情况，本次 smoke test 临时传入 `CONFIG_DEBUG_INFO_BTF_MODULES=`；
这只影响本地构建验证，不改变设备准入结论。

构建证据：

```text
build/device-evidence/hide1-build-tag74.log
experimental/hide-vfs/pathguard_hide1.ko
SHA-256: 31AB8D5E70189508D191396C06161FE8E562C7232EA8930416C474593BA761BA
ELF: AArch64 relocatable module
```

该 `.ko` 的 `vermagic` 为 `6.12.76-4k SMP preempt mod_unload modversions aarch64`，
而 `myron` 当前运行 `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`；
更关键的是该产物的 `__versions` 和 `__version_ext_crcs` section 均为空。按标准 Android
6.12 Kbuild/modversions 路径，这种通用产物不能直接用 `insmod` 绕过 release/CRC 检查；此前
的错误不应通过改写 `.modinfo` 或清空 `__versions` 制造“通过”。

已在 `experimental/hide-vfs/Makefile` 和 `verify-kmi.ps1` 增加 fail-closed
检查：Kbuild 输出的 `include/generated/utsrelease.h` 必须与设备证据一致，且
`.ko` 必须包含非空 CRC section；否则构建/验收直接失败。这些检查仍然适用于传统
Kbuild ABI 路径，但不覆盖 SukiSU 的专用加载器路径。

本轮从 `/vendor_dlkm/lib/modules/` 复制了 `common.ko`、`cfg80211.ko`、
`machine_dlkm.ko`、`xiaomi_touch.ko`、`zram.ko`、`bq27z561.ko` 作为设备已
接受的 CRC 参考样本。它们确认：

| 符号 | 设备 CRC | 通用 KDIR CRC | 结论 |
|---|---:|---:|---|
| `module_layout` | `0xe976b219` | `0xe143d454` | 不匹配 |
| `misc_register` | `0xd9a13df1` | `0x180d5d38` | 不匹配 |
| `misc_deregister` | `0x78d4940f` | `0xb3a29e45` | 不匹配 |
| `init_uts_ns` | `0x16cbd34a` | `0x8c45c050` | 不匹配 |
| `bcmp` | `0x5bf0d3e8` | `0x5bf0d3e8` | 一致 |
| `memcpy` | `0x8a7493b2` | `0x8a7493b2` | 一致 |

其中 `module_layout`、`misc_*`、`init_uts_ns` 的差异足以阻止模块加载，说明
仅改 vermagic 不能解决问题。运行时 release 为 `g16e473...-abogki...`，
vendor 模块 vermagic release 为 `gf79b...-mi-4k`；两者不同是 Xiaomi
GKI/vendor 构建的正常形态，不能把完整 `uname -r` 当作唯一匹配条件。

### 25.5 2026-09-13 SukiSU LKM 加载配方实证修正

用户从当前 slot 的 `init_boot_b` ramdisk 解出的 SukiSU 加载物记录为：`init` 为 Rust
`ksuinit`，`init.real` 为原始 init，`kernelsu.ko` 的 `vermagic` 为
`6.12.76-4k-gae4e2f4f997e-dirty`，`__versions` size 为 0，模块无签名；设备配置中
`CONFIG_MODULE_SIG_FORCE` 未启用。SukiSU `userspace/ksuinit/src/lib.rs` 会在内存中的 ELF
buffer 上用 `/proc/kallsyms` 解析并重定位 undefined symbols，调用 `init_module`，仅在
内核报告 vermagic mismatch 时替换内存 buffer 的 `.modinfo` 后重试。

这证明当前 myron 上存在一条不同于标准 Kbuild CRC 路径的、设备已运行的 LKM 装载方案：
**精确 `Module.symvers` 不是该方案的绝对装载前提**。它仍是结构布局、符号 CRC、函数原型
和高可靠 Hide 后端开发的重要证据，不能因此删除 ABI/CFI/签名准入。ramdisk 文件大小
374200 bytes 与 `/proc/modules` 的 200704 统计不同，必须用哈希、ELF section 和加载日志
确认是否为同一构建物。

因此设备级下一闸门调整为：先复刻 SukiSU 的 `android16-6.12` DDK + 离线 ELF 检查，构建
最小 `pathguard_probe.ko` 并实现只读 loader 适配；获得明确授权后才可在 myron 加载 probe。
Probe 通过后，才能进入固定设备的 VFS prototype；在 HideLab 全量回归、设备/KMI 白名单和
OTA 重新准入完成前，Hide 1.0 仍保持 `unsupported`，不得激活。

为此已新增手动 GitHub Actions workflow `.github/workflows/build-pathguard-probe.yml`，固定
使用 `ghcr.io/ylarod/ddk-min:android16-6.12-20260828`，构建无 hook、无设备节点、无策略状态的
`experimental/hide-vfs/probe/pathguard_probe.c`。workflow 会校验 AArch64、空 `__versions`、
undefined symbol 是否存在于 DDK `vmlinux`、`.modinfo`、签名标记和关键 Kconfig，并上传 stripped/
unstripped `.ko`、SHA-256 及检查报告。当前只完成本地 workflow/YAML 静态检查，尚未提交、推送或
在 GitHub 执行，因此 checklist 仍保持未完成。

后续已将 workflow 分四次小提交推送到 `feature/pattern-redirect-v6`（`1717d88`、`5648dc0`、
`f667a37`、`627d933`），并由受限的 branch/path `push` 触发器执行。最终 GitHub Actions run
`34734993002` 全部通过，构建产物及报告保存为 artifact `pathguard-probe-android16-6.12`，本地
下载证据位于 `build/device-evidence/pathguard-probe-action/34734993002/`。关键结果：

```text
ELF:                    ELF64 AArch64 relocatable
stripped size:          11456 bytes
vermagic:               6.12.76-4k SMP preempt mod_unload modversions aarch64
__versions size:        000000
undefined symbols:      none
missing DDK symbols:    none
module signature:       absent
stripped SHA-256:       2ec3d037662eb75616e4bec6405d3b45b9175d36772ae470844ff86501bd7094
unstripped SHA-256:     35805519cbe2beaac790417d1650fe01df35e36caa600900446327ef7e04be2e
```

该轮只完成“最小 LKM 的 DDK 构建与离线 ELF 资格检查”，尚未实现或执行 SukiSU 式 loader，也
没有证明模块能在 myron 加载。

### 25.6 2026-09-13 PathGuard 离线 LKM adapter

提交 `18b04f0` 新增纯 C++20 `OfflineModuleAdapter`。核心不包含 `init_module`、`/dev/kmsg`、
`kptr_restrict` 或 ADB 调用，只接受 ELF bytes、kernel symbol snapshot 和目标 vermagic，并返回新
的内存 image。解析器只接受 little-endian ELF64/AArch64 relocatable，要求唯一 `.symtab`、
`.modinfo` 和空 `__versions`；所有 undefined symbol 必须具有非零地址，随后才原子改写为
`SHN_ABS`。缺失/重复符号、畸形 offset/size、非空 `__versions` 和非法 vermagic 均 fail closed。

Host 验证：

```text
Windows CMake/MSVC build:       passed
pathguard_hide_loader_test:     passed
Clang -Wall -Wextra -Werror:    passed
真实 run 34734993002 artifact:
  vermagic-only offline adapt:  passed
  input SHA-256 unchanged:      true
```

随后把 probe 改为只读访问一次 `init_uts_ns`，使真实 DDK 模块产生一个确定的 undefined symbol，
并在 GitHub Actions run `34736258439` 中执行 loader 核心。结果：

```text
ELF:                    ELF64 AArch64 relocatable
undefined symbol:       init_uts_ns
relocated count:        1
adapted symbol:         ABS 0xffffffc0826f87b8
vermagic before:        6.12.76-4k SMP preempt mod_unload modversions aarch64
vermagic after:         6.12.23-android16-5-g16e473de48a3-abogki462654244-4k SMP preempt mod_unload modversions aarch64
__versions size:        000000
module signature:       absent
stripped SHA-256:       eac3140c4a91398c2941b20ee0b81baf0c28a200ec335d43cee934857fb88bf3
adapted SHA-256:        8e77846fd9bce5121ad2c1f62dc5ea368c4c647e787f5fbd58d35a1878a619a5
```

其中 `0xffffffc0826f87b8` 来自 DDK `vmlinux` fixture，不是 myron `/proc/kallsyms` 地址；适配产物
明确命名为 `pathguard_probe.offline-adapted-do-not-load.ko`，禁止加载设备。该结果证明离线 ELF
算法和 SukiSU 配方一致，不证明设备装载成功。下一闸门是实现受限的 Android loader shell，并在
取得明确授权后使用设备实时 kallsyms 生成内存 image、执行一次最小 probe 的加载/卸载和稳定性
采集；设备实验前 Hide 1.0 仍为 `unsupported`。

### 25.7 2026-09-13 受限 Android loader shell

已新增 `pathguard_lkm_loader`，把设备文件采集与纯 ELF adapter 分离。默认且当前唯一可执行模式为
`--prepare-only`：只读模块和 `/proc/kallsyms`，在进程私有内存中完成适配，输出模块大小、解析到的
非零符号数量、重定位数量及 vermagic 来源，然后丢弃适配 image。工具没有输出路径参数，不会把带
设备地址的 `.ko` 写入磁盘；`--load` 会在读取模块前直接失败，最终 Android ELF 不导入
`init_module`/`finit_module`。

边界进一步收紧如下：

- 不读取、不修改 `/proc/sys/kernel/kptr_restrict`；若 `/proc/kallsyms` 地址被隐藏为零，adapter
  因缺少符号而 fail closed；
- `--prepare-only` 不从历史 kmsg 猜测 vermagic，必须显式传入独立记录并审计过的设备值；
- `KernelLogCursor` 以非阻塞方式打开 `/dev/kmsg`（fallback `/kmsg`），先消费历史记录，后续只读取
  新增记录，为将来的“一次加载失败 -> 解析本次 vermagic mismatch”事务提供 RAII 文件描述符边界；
- 读取输入限制为 128 MiB；模块异常、符号歧义、零地址和 vermagic 异常继续沿用 adapter 的
  fail-closed 结果；
- Android arm64/API 26 构建使用静态 libc++，运行时只依赖系统 `libm`、`libdl`、`libc`。

本地验证已通过 Windows/MSVC 两组单测、完整 prepare 流程测试，以及 NDK 29
`aarch64-linux-android26-clang++ -Wall -Wextra -Werror` 构建。GitHub Actions 同步新增 host 单测、
Android ELF/依赖审计和禁用加载入口检查。此阶段仍未使用 ADB、没有加载/卸载模块，也没有修改设备
配置；下一闸门仍是取得单独明确授权后，在 myron 上只加载最小 `pathguard_probe.ko`。

### 25.8 2026-09-13 myron 最小 probe 设备闸门

在明确授权后，使用未适配的本地 DDK probe，通过设备已有的 SukiSU Ultra `ksud 4.1.3 insmod` 执行了一次
受控加载，并使用系统 `rmmod` 完成卸载。设备为 `25102RKBEC / myron`，运行内核为
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`；模块 SHA-256 为
`f0fe7b783b1996b1318d904a138b9a024a326f6b55b118818e8be1f35cdbd26a`。

结果：

- `ksud insmod` 返回 0，`pathguard_probe` 进入 `/proc/modules` 的 `Live` 状态；
- 仅观察到 `no extended symbol version for module_layout`，未观察到 vermagic、未知符号、CFI、签名或格式拒绝；
- 加载期间设备保持在线且 `sys.boot_completed=1`；
- `rmmod pathguard_probe` 返回 0，模块消失，卸载后设备继续在线；
- 全部原始证据位于 `build/device-evidence/pathguard-probe-local/20260913/device-load/`。

这一步把设备闸门从“离线可构建/可适配”推进到“myron 上最小 LKM 可加载、可卸载”。它**不**改变 Hide 1.0
准入结论：probe 没有任何 VFS hook、策略、设备节点或隐藏语义，HideLab 尚未对后端进行验证。下一步仍是
审计并实现固定设备范围的最小 VFS prototype，然后用同一套 HideLab 做全量回归；在此之前不得激活 Hide 1.0。

### 25.9 2026-09-13 只读 VFS capability probe

在最小 LKM probe 已通过 myron 加载/卸载闸门后，新增只读
pathguard_vfs_cap_probe.ko 作为 VFS prototype 前的独立能力探针。模块仅保留
lookup_one_len、vfs_create/mkdir/mknod/symlink/unlink/rmdir/link/rename 以及
register_kprobe/unregister_kprobe 的 undefined references，并通过 misc ioctl
报告能力位图；不注册 kprobe、不调用 VFS helper、不修改 inode/file_operations，
不安装规则，也不隐藏路径。

该模块已使用相同 android16-6.12 DDK 与 Android clang r536225 本地构建。ELF
静态证据位于 build/device-evidence/vfs-capability-probe-local/20260913/，其中
确认 AArch64 relocatable、目标 undefined 符号齐全、vermagic 为
6.12.76-4k SMP preempt mod_unload modversions aarch64，签名缺失且本地
__versions 为空。READY 只表示加载期符号解析成功，不等于 VFS 语义能力或
HideLab 通过。

下一步可在单独确认后，对该只读探针执行一次 myron 加载、读取
/dev/pathguard_vfs_cap_probe 状态并卸载；成功后才进入固定 parent/basename、
UID/namespace 范围的最小 VFS 数据面设计。Hide 1.0 在完整 HideLab 回归前仍
保持 unsupported。

### 25.10 2026-09-13 capability probe 设备验证

只读 pathguard_vfs_cap_probe.ko 已在 myron 上通过 SukiSU Ultra ksud 4.1.3 加载。ioctl
返回 ABI 1、size 160、state 1、available_ops=0x3ff、required_ops=0x3ff，且
kernel_release 与设备 uname -r 完全一致。随后 rmmod 成功，模块和 misc 设备节点均消失，
设备保持在线。

这一步证明后续 prototype 所需的十项导出符号在当前设备上可由加载期解析，并不证明任何
VFS 隐藏语义。下一步进入真实数据面前，仍需先冻结单 parent/basename、单 target UID 和
单 mount namespace 的规则模型，设计可回滚的 lookup/readdir/mutation/cache 覆盖方案，并
为每个入口准备 HideLab 对照用例；在完整回归前不得激活 Hide 1.0。

### 25.11 2026-09-13 只观测 VFS kprobe trace probe 本地构建

为验证“入口可观测”与“入口可改变”之间的边界，新增独立的
`experimental/hide-vfs/trace/pathguard_vfs_trace_probe.ko`。它只对
`lookup_one_len`、`vfs_create`、`vfs_mkdir`、`vfs_unlink`、`vfs_rmdir`、`vfs_rename`
注册 kprobe；每个 pre-handler 仅递增 `atomic64_t`，不读取或修改 `pt_regs`，不修改返回值、
dentry、inode、file_operations 或目录枚举。任一 probe 注册失败时，模块逆序注销已成功注册
项并返回错误；模块退出同样逆序注销全部 probe。

该模块使用与前两轮相同的 android16-6.12 DDK source/output 和 Android clang r536225
在本地构建成功。结果为 AArch64 `ET_REL`，vermagic 为本地输出的
`6.12.76-4k SMP preempt mod_unload modversions aarch64`，无签名，
`__versions`/`__version_ext_crcs` 均为空；stripped SHA-256 为
`0fe62c9a92fea52f371ed8678457c5d2815ee4b47e4f9e0864ff6e1854602cce`。
未定义符号包括通用模块运行时依赖、`register_kprobe`/`unregister_kprobe`，以及
`misc_register`/`misc_deregister`；动态符号名保存在模块数据中，不能据此宣称入口已被设备接受。

配套 `status_reader.c` 已用 Android NDK r27 编译为 AArch64 PIE。设备侧下一步只允许在
静态审查通过并取得单独确认后执行一次：加载 trace probe、读取
`/dev/pathguard_vfs_trace_probe` 的计数和 `nmissed`、触发少量受控文件操作、卸载模块，
然后核对设备在线、无异常日志和计数稳定。该实验只验证 kprobe 观测稳定性，不能证明能够
安全拦截或改变 VFS 语义，也不能替代 HideLab；Hide 1.0 继续保持 `unsupported`。

### 25.12 2026-09-13 myron kprobe trace probe 设备验证

在取得单独确认后，使用本地构建的 trace probe 在 `myron` 上完成一次受控事务。通过
`/data/adb/ksu/bin/ksud insmod` 加载，设备侧 SHA-256 与本地产物一致，模块进入 `Live`，
并创建 `/dev/pathguard_vfs_trace_probe`。ioctl 初始状态为 `state=1`、`registered=6`，
六个入口的 `hits=0`、`nmissed=0`，运行 release 与设备 uname 完全一致。

在专用临时目录执行 `mkdir`、`touch`、`rename`、`rmdir`、`unlink` 后再次读取：

```text
lookup_one_len: hits=14 nmissed=0
vfs_create:     hits=0  nmissed=0
vfs_mkdir:      hits=2  nmissed=0
vfs_unlink:     hits=12 nmissed=0
vfs_rmdir:      hits=2  nmissed=0
vfs_rename:     hits=13 nmissed=0
```

临时目录已确认删除。`/system/bin/rmmod pathguard_vfs_trace_probe` 返回 0；卸载后模块和
设备节点均消失，`sys.boot_completed=1`，设备仍在线，未发现本模块错误日志。原始证据位于
`build/device-evidence/vfs-trace-probe-local/20260913/device/`。

该结果证明当前设备允许对这六个符号注册和注销只读 kprobe，且在少量文件操作下没有 missed
probe；不证明可以安全改变 VFS 语义。`touch` 未命中 `vfs_create`，说明创建路径可能经由
`atomic_open` 或文件系统特定实现，真实 Hide prototype 必须覆盖并测试 `atomic_open`，不能
用 `vfs_create` 命中数替代创建语义覆盖。Hide 1.0 继续保持 `unsupported`。

### 25.13 2026-09-13 namei/FUSE 调用链 coverage probe

`vfs_create=0` 表明通用 mutation helper 计数不足以描述实际创建路径。android16-6.12 源码确认：
`lookup_open()` 在目录 inode 提供 `.atomic_open` 时调用该 operation；FUSE 目录实现了
`fuse_atomic_open`，枚举从 `iterate_dir()` 进入 `fuse_readdir()`，dentry 缓存重验由
`fuse_dentry_revalidate` 参与。因此新增只读 `pathguard_vfs_coverage_probe.ko`，观察：

```text
path_openat
fuse_atomic_open
iterate_dir
fuse_readdir
fuse_dentry_revalidate
do_filp_open
```

所有 handler 仍仅递增 `atomic64_t`，不读取参数或修改行为；任一入口无法注册即回滚并拒绝加载。
本地 android16-6.12 DDK 构建成功，结果为 AArch64 `ET_REL`，vermagic 为
`6.12.76-4k SMP preempt mod_unload modversions aarch64`，版本段为空且无签名，SHA-256 为
`1aaef8da70bb3b24b1916f480ff1e12d2a13846d09dca76e23b1bc45d37d5ac7`。配套 Android
arm64 状态读取器也已构建。设备实验尚未执行；该探针即使通过，也只用于冻结真实调用链，
不是 Hide backend。

### 25.14 2026-09-13 myron namei/FUSE coverage 设备验证

coverage probe 通过 SukiSU Ultra 在 myron 上加载成功，六个内部/导出符号全部注册。初始读取
已包含设备后台活动：`path_openat=9827`、`iterate_dir=397`、
`fuse_dentry_revalidate=6350`，因此本轮只采用受控操作前后差值，不把全局绝对计数归因于测试。

在唯一临时目录 `/storage/emulated/0/Pictures/PathGuardCoverage-<run-id>` 执行创建目录、创建并
读取文件、枚举、重命名、删除后，计数增量为：

```text
path_openat              +1327
fuse_atomic_open         +1
iterate_dir              +11
fuse_readdir             +2
fuse_dentry_revalidate   +143
do_filp_open             +1327
all nmissed              0
```

其中系统后台活动会显著放大通用 namei 入口和 revalidate 计数，但 `fuse_atomic_open` 与
`fuse_readdir` 从零产生的命中直接证明共享存储测试进入了这两条 FUSE 路径。模块随后成功
卸载，设备节点、测试目录和 `/data/local/tmp` 输入均已清理，设备保持在线且
`sys.boot_completed=1`；模块日志仅有六项注册成功信息。证据位于
`build/device-evidence/vfs-coverage-probe-local/20260913/device/`。

这把 fixed-device prototype 的硬覆盖条件进一步收紧为：不得只包装普通 lookup 或通用
`vfs_create`；必须显式处理 FUSE `.atomic_open`、`.iterate_shared` 和 observer-aware dentry
缓存重验。kprobe trace 仍只用于确认调用链，不作为正式数据面。

### 25.15 2026-09-13 VFS operation-table preflight 本地构建

为在修改任何 operation table 前确认目标父目录的实际回调集合，新增只读
`experimental/hide-vfs/preflight/pathguard_vfs_preflight.ko`。它通过 `kern_path` 解析一个
绝对父目录，读取 inode 的 `lookup/atomic_open/create/mkdir/mknod/symlink/unlink/rmdir/link/rename`、
file 的 `iterate_shared` 以及 dentry 的 `d_revalidate` 是否存在，返回 operation bitmask、
文件系统名称、parent inode 和设备号；不输出函数地址，不写入 `i_op/i_fop/d_op`，不安装规则。

本地 android16-6.12 DDK 构建成功：AArch64 `ET_REL`，vermagic 为
`6.12.76-4k SMP preempt mod_unload modversions aarch64`，无签名，SHA-256 为
`4e8c229a4562527327e2dda04c9ea27c67a57745339d67b3b764da4ca9e4c7e3`。构建期间发现该
6.12 `struct file_operations` 已删除旧 `.iterate` 字段，preflight 已按当前精确结构仅检查
`.iterate_shared`，不能直接复制旧内核兼容分支。Android arm64 状态读取器已编译。

下一步设备实验仅扫描 `/storage/emulated/0/Pictures` 和一个 ext4 对照父目录，读取两次状态
并卸载；它只冻结 operation-table 事实，不等于 Hide backend，也不改变 Hide 1.0 的
`unsupported` 状态。

### 25.16 2026-09-13 myron operation-table preflight 设备验证

首次加载命令遇到 `ksud` 路径短暂不可访问，模块未加载；重新定位
`/data/adb/ksu/bin/ksud` 后重试成功。首次扫描发现 `SCAN` 清空状态时丢失
`kernel_release`，operation mask 本身正确但证据不完整，因此卸载旧模块、修复状态初始化、
重新构建并以 SHA-256
`4e8c229a4562527327e2dda04c9ea27c67a57745339d67b3b764da4ca9e4c7e3` 重做完整实验。

修复版结果：

| 父目录 | 文件系统 | operation mask | 解释 |
|---|---|---:|---|
| `/storage/emulated/0/Pictures` | `fuse` | `0x0fff` | 12 项全部存在，包括 atomic_open、iterate_shared、d_revalidate 和全部 mutation |
| `/data/local/tmp` | `f2fs` | `0x0ffd` | 除 dentry d_revalidate 外其余 11 项存在 |

两次扫描均返回完整设备 release。模块随后成功卸载，misc 节点和 `/data/local/tmp` 输入已清理，
设备在线且 `sys.boot_completed=1`。证据位于
`build/device-evidence/vfs-preflight-local/20260913/device/`。

`0x0fff` 只说明目标 FUSE 父目录具备可包装的完整 operation 集合；它不证明包装实现、并发回收、
observer scope 或缓存语义正确。下一步 prototype 必须把这些 12 项作为原子安装/回滚单元，不能
只替换其中一部分后宣称 active。

### 25.17 2026-09-13 Hide 1.0 数据面决策模型

在编写 operation-table shadow 前，新增 `experimental/hide-vfs/model/`，把固定设备 prototype
的策略决策从 VFS 生命周期代码中分离。模型是无动态分配、无 STL 的 C11 实现；同一份
`hide_vfs_model.c` 同时由 Windows 宿主 CTest 和 android16-6.12 Kbuild 编译，避免宿主测试与
内核实现各维护一份判断逻辑。

冻结的数据模型为：

```text
resolved parent = (superblock cookie, inode)
entry           = exact basename
observer        = (exact UID, mount namespace cookie)
policy          = immutable generation
```

命中 target observer 时的统一结果为：

| operation | 结果 | 是否调用原文件系统 |
|---|---|---|
| lookup | synthetic negative / ENOENT 视图 | 否 |
| atomic_open | `-ENOENT` | 否 |
| iterate_shared | 省略匹配 dirent | 否，仅匹配项不下发 |
| create/mkdir/mknod/symlink/link | `-ENOENT`，无副作用 | 否 |
| unlink/rmdir | `-ENOENT`，无副作用 | 否 |
| rename | source 或 destination 任一端命中即 `-ENOENT` | 否 |

同 UID 但不同 mount namespace、不同 UID、root oracle、不同 parent 或不同 basename 全部 pass
through。缓存模型区分 real positive、real negative 和 synthetic negative：target 只接受当前
generation 的 synthetic negative；control/oracle 必须使 synthetic negative 失效；target 遇到
real positive 必须失效。规则撤销或 generation 变化后，旧 synthetic negative 也必须失效。

Android 16/6.12 头文件核对同时冻结了精确签名：`file_operations` 只有
`iterate_shared(struct file *, struct dir_context *)`；`atomic_open` 返回 `int`；`rename` 接受
source/destination 两组 inode/dentry 和 flags。`fs/namei.c` 证实 `lookup_open()` 在 FUSE 存在
`.atomic_open` 时优先调用它，返回错误会直接终止打开，因此 target wrapper 在调用
`fuse_atomic_open` 前返回 `-ENOENT` 才能保证 `O_CREAT/O_TRUNC` 无副作用。

验证结果：`pathguard_hide_vfs_model_test` 与既有 loader 两项定向测试共 3/3 通过；同一 C 源由
android16-6.12 DDK + clang r536225 成功编译为 arm64 内核对象。尚未安装 operation table、没有
构建或加载数据面 `.ko`、没有改变设备状态，Hide 1.0 继续保持 `unsupported`。下一步是先验证
mount namespace pin/report 所需符号，再实现默认 inactive、可事务回滚的 shadow；完整 HideLab
通过前仍不得激活。

## 26. 资料来源

### 26.1 项目文档与本地证据

| 资料 | 用途 |
|---|---|
| `docs/07-hide-capability-research-and-design.md` | exact semantics、候选路线、H0 结论、性能预算 |
| `tests/device/hide/README.md` | 已有 native/app probe、sandbox 安全约束和证据格式 |
| `tests/device/hide/H0.1_*` | Android 13/alioth baseline、权限与 selector 观察 |
| `tests/device/hide/H0.2_MEDIAPROVIDER_FUSE_ALIOTH_20260728.md` | MediaProvider FUSE 调研与 kill decision |
| `tests/device/hide/H0.3_LKM_INTERFACE_AUDIT_ALIOTH_20260728.md` | LKM/KMI 接口审计 |
| `tests/device/hide/H0.4_SUSFS_AND_H0_DECISION_ALIOTH_20260728.md` | SUSFS ABI 与 H0 unsupported 结论 |
| `build/device-evidence/p6-final-device-myron-v019-20260801/device-snapshot.json` | myron 既有设备、内核和 Root 版本证据 |
| `refer/Kasumi-main` | Kasumi API 17 本地源码审计 |
| `refer/hide-refer/nomount-master` | NoMount v20 静态源码审计；未在本机加载或运行 |

### 26.2 权威平台资料

| 资料 | 用途 |
|---|---|
| [Linux pathname lookup](https://docs.kernel.org/filesystems/path-lookup.html) | namei、dcache、lookup 语义 |
| [Linux Kprobes](https://docs.kernel.org/trace/kprobes.html) | kprobe/kretprobe、`maxactive/nmissed` 和 handler 约束 |
| [Linux OverlayFS](https://docs.kernel.org/filesystems/overlayfs.html) | whiteout、copy-up、merged readdir |
| [Android GKI stable KMI](https://source.android.com/docs/core/architecture/kernel/stable-kmi) | GKI 符号稳定边界 |
| [Android loadable kernel modules](https://source.android.com/docs/core/architecture/kernel/loadable-kernel-modules) | Android LKM 构建和加载约束 |
| [AOSP Android 16 FUSE dir.c](https://android.googlesource.com/kernel/common/+/refs/tags/android16-6.12-2025-12_r53/fs/fuse/dir.c) | FUSE lookup/atomic_open 参考实现 |
| [AOSP MediaProvider FuseDaemon.cpp](https://android.googlesource.com/platform/packages/providers/MediaProvider/+/refs/heads/main/jni/FuseDaemon.cpp) | 请求 UID、lookup、readdir、cache |
| [Android Photo Picker](https://developer.android.com/training/data-storage/shared/photopicker) | Picker 独立访问面 |
| [Android DocumentsProvider](https://developer.android.com/guide/topics/providers/document-provider) | SAF/DocumentsProvider 模型 |

### 26.3 参考项目

| 项目 | 结论 |
|---|---|
| [Rouyashiki/Kasumi](https://github.com/Rouyashiki/Kasumi) | dirhijack 机制参考；原版不满足 PathGuard exact hide |
| [SUSFS](https://gitlab.com/simonpunk/susfs4ksu) | namei/getdents 机制参考；现有 ABI 缺少逐规则 scope |
| `refer/hide-refer/LKM-PathMask-main` | KMI 打包/诊断参考；post-syscall 数据面拒绝 |
| `refer/hide-refer/nomount-master` | 无 mount、per-UID VFS whiteout 参考；原版不满足 exact hide |
| `refer/Storage-redirection-X-Public-main` | 整盘隔离、storage alias 和兄弟目录恢复成本参考 |

参考项目只用于事实分析和独立设计。正式实现不得未经许可证和架构审查复制 copyleft 源码；更重要的是，任何参考项目名称都不能替代 HideLab 的设备级行为证明。

## 27. 最终结论

PathGuard 可以继续研究“真隐藏”，但正确路线不是寻找一个看起来能过滤文件列表的 Hook，而是：

```text
冻结 exact semantics
-> 收缩设备、对象和访问面
-> 建设 HideLab 攻击矩阵
-> 验证最小 direct VFS 后端
-> 用 capability 分阶段扩展 MediaStore/SAF/Picker
-> 任一必需能力缺失时拒绝激活
```

最重要的产品边界是：

```text
deny 追求广覆盖、稳定地阻止访问；
hide 追求声明范围内不可发现，并接受更严格的设备准入。
```

分阶段交付能降低工程风险，但不能降低每个阶段已经承诺的语义质量。HideLab 是判断这条边界是否真实成立的“矛”；没有通过它的后端，只能继续保持 `unsupported`。

## 29. 2026-09-15：myron storage topology preflight

为进入只读 FUSE-aware 后端，重新构建了 ABI v2 的 operation-table preflight，并在
Redmi K90 Pro Max（`myron`）上加载后采集 `/storage/emulated/0`、`/sdcard` 和
`/storage/self/primary`。证据位于：

```text
build/device-evidence/vfs-topology/20260915/
```

三个 alias 的结果完全一致：

```text
filesystem = fuse
dev        = 0:280
parent_inode = 14139
operation_mask = 0x0000000000000fff
```

mount、superblock、parent inode、dentry、`i_op`、`f_op` 和 `d_op` 地址身份也完全一致。
因此，针对这三个 alias 的第一版实验后端可以共享一个 FUSE parent identity，而不必为
三个字符串路径分别安装 shadow。

同一 mount namespace 的 `mountinfo` 同时显示：

```text
9726  ... /storage/emulated              - fuse   /dev/fuse
9727  ... /mnt/user/0/emulated           - fuse   /dev/fuse
9818  ... /mnt/androidwritable/0/emulated - fuse  /dev/fuse
9870  ... /mnt/installer/0/emulated      - fuse   /dev/fuse
9931  ... /mnt/pass_through/0/emulated   - f2fs   /dev/block/dm-55
```

这确认了两个必须保留的边界：

1. `/storage/emulated` 前台是 FUSE，coverage probe 实测命中 `fuse_atomic_open`、
   `fuse_readdir` 和 `fuse_dentry_revalidate`；只替换普通 `lookup` 不能形成完整隐藏。
2. `/mnt/pass_through/0/emulated` 是独立 F2FS backing 平面。FUSE parent identity 的
   preflight 结果不能推导 backing 平面也已隐藏；若规则允许该路径可达，必须单独绑定或
   在 admission 阶段拒绝该 profile。

本轮探针只读观察，加载后已卸载。它不改变 Hide 1.0 状态，产品仍为 `unsupported`。
下一步进入阶段 2：固定该 FUSE parent、单 target UID、单 mount namespace 和单 basename，
实现 lookup、`atomic_open`、readdir、`d_revalidate` 的只读实验后端。

### 29.1 阶段 2 离线实现边界

基于上述拓扑结果，`pathguard_hide1` 原型增加了以下 fail-closed 约束：

- `INSTALL` 只接受 `fuse` superblock；F2FS backing 路径返回 `-EOPNOTSUPP`，不会被误判为
  同一隐藏平面。
- observer 判断同时校验 mount namespace、fsuid、superblock 和 parent inode identity，
  并要求当前 policy generation 仍等于安装 generation。
- `atomic_open` 隐藏分支现在安装 per-dentry shadow，并在返回 `-ENOENT` 前丢弃已存在的
  positive dentry，覆盖 FUSE cold-cache open 入口。
- `d_revalidate` 在 `LOOKUP_RCU` 下返回 `-ECHILD`，迫使 namei 进入可睡眠慢路径重新执行
  observer 判断；普通路径继续安排 stale dentry 回收。
- 实验包默认切换为 `shadow_mode=0`，同时安装 i_op、f_op 和 d_op shadow。`shadow_mode=1`
  仅保留为显式隔离调试模式，不能用于阶段 2 的隐藏结论。

本轮仅完成离线编译检查，尚未在设备上加载新的 Hide 后端。下一步必须在不执行 mutation
的前提下，重新构建实验包并进行受控 `INSTALL/ENABLE`，优先验证 FUSE `atomic_open`、
readdir、positive dentry 和 Control observer 是否保持原始语义；任何 `LEAK`、
`OVERBLOCK`、`CRASH` 或 `STATE_LIE` 都阻止进入阶段 3。

### 29.2 2026-09-15：FUSE read-only shadow 首次受控 ENABLE

设备安装了 `pathguard-hide1-lab-myron-fuse-ro-v1.zip`。模块以
`shadow_mode=0` 加载，`INSTALL` 成功绑定：

```text
target_uid=10551
target_pid=20451
target_mnt_ns=4026535994
generation=6001
operation_mask=0x0000000000000fff
parent_inode=15771
```

执行 `ENABLE` 后控制命令没有返回状态，ADB 连接立即断开；设备随后重新上线，
`ro.boot.bootreason` 和 `sys.boot.reason` 均为 `reboot`。重启后的 `/proc/modules`
中没有 `pathguard_hide1`，`/sys/fs/pstore` 为空，未取得可用的 kernel panic trace。

本轮结论为 `CRASH`，不是 `PASS`、`LEAK` 或 `ACTIVE`。证据归档于：

```text
build/device-evidence/hide1-fuse-ro-v1/20260915-enable-crash/
```

该结果阻止阶段 2 进入 HideLab active 回归。下一步必须离线审查 ENABLE 的事务安装
顺序、FUSE operation callback 的 CFI/owner 约束、现有正 dentry/打开目录 FD 处理，
并在没有新的离线安全证据前禁止再次执行 `ENABLE`。

## 28. 2026-09-13：inactive binding 阶段完成

`pathguard_hide1` 已采用 SukiSU Ultra 在 myron 上验证过的 android16-6.12 DDK LKM 路线。
精确 myron `Module.symvers` 不再是加载硬前提，但设备 release、运行时符号、CFI、allowlist、
OTA 重新准入和 HideLab 仍是硬门禁。

当前实现只绑定 target fsuid、mount namespace、真实 parent path、parent inode、basename 和
generation，要求完整 operation mask `0x0fff`，安装失败事务性保留旧 binding。设备实测成功进入
`INACTIVE`，错误 UID 为 `EPERM`，跨 namespace 为 `EXDEV`，`ENABLE` 为 `EOPNOTSUPP`，
`DISABLE/CLEAR` 分别保留/释放 binding，卸载后设备在线。

这不是 hide 实现。下一阶段仍必须实现完整 operation-table shadow、synthetic negative、
observer-aware dentry cache、atomic_open、iterate_shared、全部 mutation 与并发回收，并用同一
HideLab 全量回归后才可改变 `unsupported` 状态。

## 29. 2026-09-13：operation-table shadow 第一版本地构建

`pathguard_hide1.c` 已进入 shadow 数据面阶段。单 binding 在 `ENABLE` 时复制并替换 parent
inode 的 i_op/i_fop，并为 parent 及已缓存 basename dentry 建立逐对象 d_op shadow；`DISABLE`、
`CLEAR` 和模块卸载使用 release-store 恢复当前仍属于本模块的指针，SRCU 排空后才释放 metadata。
wrapper 已覆盖 Android 16/6.12 的完整 12 项：lookup、atomic_open、iterate_shared、9 项
mutation 和 d_revalidate。目标 fsuid/namespace 在 lookup、readdir、mutation、rename 双端及
dentry revalidate 中统一执行；目标 lookup 生成 negative dentry，目录枚举过滤 basename，目标
mutation 返回 `ENOENT`，非目标观察者调用保存的原始回调。

本地 clang r536225 + android16-6.12 Kbuild 成功生成 `pathguard_hide1.ko`，`__versions` 为
空段；宿主 loader/model/restricted-loader 三项回归通过。该版本尚未在 myron 上执行 ENABLE，
也没有 HideLab active 证据，因此产品状态仍为 `unsupported`。下一步是设备上的加载、启用、
warm-cache 与卸载恢复实验，再进入并发/生命周期回归和 HideLab 全量 active 验收。

## 30. 2026-09-13：ENABLE 设备闸门失败

在明确确认后，先后使用 generation `1003` 和 `1004` 在 myron 上执行两次 `ENABLE`。两次
`INSTALL` 均成功，但启用阶段均导致设备 ADB 断开并自动重启；修复 dentry `d_lock`、flags
和发布屏障后仍复现。重启后设备正常进入系统，模块未持久化加载，但没有 pstore/kmsg 留存
可用于定位。

因此 operation-table shadow 当前只能标记为“本地编译通过、设备 ENABLE 失败”。不得运行
HideLab active，不得进入设备准入，也不得将产品状态从 `unsupported` 改为 `active`。后续必须
先做最小化隔离发布或取得可观测崩溃证据，再决定是否继续该数据面路线。

代码审查随后修复了另一个生命周期 UAF：`d_revalidate` 必须先进入 SRCU 再从 `d_op` 解析
metadata，否则卸载线程可能在解析前释放 shadow。修复版已本地 Kbuild 通过，但尚未重新上机；
此前两次 ENABLE 导致设备重启的失败证据仍然有效，不能据此恢复 active 路线。

## 31. 2026-09-14：生命周期与并发修复（未上机）

本轮针对 operation-table shadow 做了离线结构修复：回调入口在 RCU 读侧内完成
metadata 查找并增加 iop/fop/dop active 计数；卸载改为
`STOP_NEW -> RESTORE -> DRAIN -> FREE`，在摘除 RCU 索引后执行两阶段
`synchronize_rcu_tasks()`、active 归零等待和 `synchronize_rcu()`；f_op 保持
ingress/live owner bridge；dentry stale 由 workqueue 退休，DISABLE/CLEAR/退出前
执行 `cancel_work_sync()`。卸载增加 ingress 指针预检，发现外部替换时返回
`-EAGAIN` 并保留 binding，避免释放后悬空指针；CLEAR 在卸载失败时同样保留状态。

`pathguard_hide_vfs_model_test`、`pathguard_hide_vfs_teardown_contract_test` 和
`pathguard_hide_vfs_concurrency_test` 均通过。尝试使用本地 WSL 重新构建 Android
16/6.12 LKM 时，原始源码树缺少生成配置头，执行 `gki_defconfig prepare` 又因环境缺少
`flex` 失败。随后复用工作区已有 prepared tree 和 Android clang 工具链完成源码编译、
modpost 与链接；BTF 因缺少 `pahole` 仅以 `PAHOLE=/bin/true` 生成实验 `.ko`。产物
`__versions` 为空但 vermagic 为通用 `6.12.76-4k`，不是 myron 精确 ABI，不能刷写设备。
因此本轮仍未进行设备验证，Hide 1.0 继续保持 `unsupported`，必须先完成真实设备上的
受控 ENABLE/卸载回归。

基于本轮链接产物已生成 `download/pathguard-hide1-lab-myron-iop-v7.zip`，包配置为手动
控制、默认 inactive、`shadow_mode=1`，仅供受控实验；在设备 ENABLE 通过前，产品状态
仍为 `unsupported`。

## 32. 2026-09-14：v7 设备加载成功，未启用

用户安装并重启后，myron 设备在线。使用包内控制器执行 `load 1` 成功，
`/proc/modules` 显示 `pathguard_hide1` 为 Live，`status` 返回 `state=0`、
`last_error=-95 (EOPNOTSUPP)`，运行时 release 与目标设备字符串一致。该结果仅证明
SukiSU LKM 加载和模块初始化稳定；尚未执行 INSTALL/ENABLE，也没有 HideLab active
证据，产品状态继续为 `unsupported`。

## 37. 2026-09-14：v8 ACTIVE 复核但观测无效

v8 在匹配 namespace 下 ENABLE 成功并保持 ACTIVE。由于 HideLab Activity 已是
top-most，错误的 `--es` 参数没有触发新的路径配置，metadata 仍是默认探测目录；本轮
没有有效隐藏结果，也未执行 DISABLE。设备暂保持 ACTIVE 供后续清理，产品状态仍为
`unsupported`。

## 38. 2026-09-14：修复 Activity 参数刷新并完成 APK 构建

HideLab 的 target Activity 增加 `onNewIntent()`，统一通过 `startProbeFromIntent()` 重新
读取 `observe_paths`，取消旧采集任务后再执行探测。这解决了 Activity 已在前台时，正确的
`--esa observe_paths` 不能生效、导致 metadata 继续记录默认路径的问题。

本地 `testTargetDebugUnitTest`、`testControlDebugUnitTest`、`assembleTargetDebug` 和
`assembleControlDebug` 全部通过。新版 APK 已放入 `download/`：

```text
hidelab-app-target-intent-refresh-v1.apk
hidelab-app-control-intent-refresh-v1.apk
```

设备侧暂不安装 target APK：当前 v8 binding 仍为 `ACTIVE`，安装会杀掉 PID `19278` 并使
namespace 绑定失效。待明确安排一次重新启动 target、重新 INSTALL/ENABLE 的受控实验后，
才能用新 APK 采集匹配路径的有效 HideLab 证据。当前不能把模块 ACTIVE 或旧观测解释为
Hide 1.0 通过，产品状态继续为 `unsupported`。

随后将 `scenario`、`attack_mutations` 和 `run_id` 在提交后台 probe 前复制为不可变快照，
避免 `onNewIntent()` 与旧任务并发时读取可变 Activity `intent`。修复后的 target/control
单元测试和 debug 构建再次通过；最新 APK 为 `download/hidelab-app-*-intent-refresh-v2.apk`。

## 39. 2026-09-14：安装新版 target 并验证参数快照

新版 target APK 已通过 `adb install -r` 安装并成功启动。安装使旧进程 `PID 19278` 退出，
模块仍保存 `generation=2002`、`target_mnt_ns=4026535993` 的旧 ACTIVE binding；新 target
进程 PID `12005` 使用 mount namespace `4026536086`，因此当前没有有效 Hide binding。

用不存在的临时路径 `/storage/emulated/0/Pictures/PathGuardHideLab/manual-v2-hidden` 启动
探测后，metadata 正确记录该路径，证明 `onNewIntent` 和后台 Intent 快照修复已在真实设备
生效。证据目录为 `build/device-evidence/hidelab-manual-v2-install/`。本轮没有重新
`INSTALL/ENABLE`，没有进行写入攻击，也不能据此改变 Hide 1.0 的 `unsupported` 状态。

## 36. 2026-09-14：v8 namespace 匹配 INSTALL

v8 加载稳定。启动存活的 HideLab target（PID `19278`、UID `10549`、mount namespace
`4026535993`），在相同 namespace 创建测试目录并成功 INSTALL generation `2002`，
返回完整 `operation_mask=0x0fff`。上轮的 PID/namespace 失配已排除，但尚未执行 ENABLE。

## 34. 2026-09-14：ENABLE 成功进入 ACTIVE，但 DISABLE 触发重启

`hide1_control enable 2001` 在 myron 上返回 `state=2`，模块保持 Live；但 INSTALL
绑定的 target PID 已退出，HideLab 新进程使用不同 mount namespace，导致目标路径未被
隐藏（观测到多项可见结果）。随后执行 `disable` 时 ADB 立即断开，设备约一分钟后重启
上线，模块未持久化加载。该结果确认 teardown 仍不安全，不能进入 HideLab active 验收，
Hide 1.0 继续保持 `unsupported`。

## 35. 2026-09-14：DISABLE 路径修复与 v8 实验包

对照 Kasumi 的 teardown 顺序，修复 `hide1_lock` 与 Tasks-RCU/active 等待的锁顺序，
并让 DISABLE/CLEAR 在 `cancel_work_sync()` 前释放控制锁。真实 Android Kbuild、modpost
和链接通过，三项 VFS 离线测试通过。生成 `download/pathguard-hide1-lab-myron-iop-v8.zip`；
该包尚未上机，Hide 1.0 继续保持 `unsupported`。

## 33. 2026-09-14：v7 INSTALL 通过，ENABLE 待确认

在设备上创建一次性 HideLab 目录后，以 UID `10549`、PID `19251`、generation `2001`
完成 INSTALL。返回 `state=1`、`operation_mask=0x0fff`，namespace、parent inode 和
kernel release 均符合预期；测试 Activity 退出后模块仍 Live、设备未重启。INSTALL
不执行任何 VFS 替换，因此不能作为 hide 成功证据。ENABLE 会进入已知高风险路径，需
单独确认后执行。
## 40. 2026-09-14：新版 target 重新 INSTALL 被旧 ACTIVE binding 拒绝

新版 target 进程为 `PID 12005`、UID `10549`、mount namespace `4026536086`。针对一次性
fixture 执行 generation `3001` 的 `INSTALL` 时，内核返回 `-EBUSY`，因为旧
`ACTIVE` binding（PID `19278`、namespace `4026535993`、generation `2002`）仍然存在。
该拒绝保持了活动 shadow 的原子性，没有部分替换；canary 哈希未变化。

证据目录：`build/device-evidence/hidelab-reinstall-20260914-233102/`。本轮没有执行
`ENABLE`、`DISABLE`、`CLEAR` 或重启。后续必须先经明确批准完成旧 binding 的恢复流程，
再重新 `INSTALL/ENABLE`，Hide 1.0 状态继续为 `unsupported`。

## 41. 2026-09-15：恢复后重新 INSTALL/ENABLE 成功，但采集证据无效

批准恢复流程后，`DISABLE` 触发设备重启；重启后手动加载模块。冷启动新版 target，
完成 `INSTALL generation=3002` 和 `ENABLE 21860 3002`，最终绑定为
`PID 21860 / namespace 4026535990`，状态 `ACTIVE`，设备保持在线。

ACTIVE 后重复发送新 Intent，HideLab metadata 仍写入旧 `run_id=reinstall-active-3002`，
未写入新请求的 run id。旧 native probe 在 Future 取消时不能被中断，单线程 executor
导致新任务排队并被旧任务输出覆盖。因此本轮没有可信的 HideLab active 观测，不能判定
任何 `LEAK/OVERBLOCK/PASS`，产品状态继续为 `unsupported`。

证据追加至：`build/device-evidence/hidelab-reinstall-20260914-233102/`。后续必须先修复
probe 任务生命周期或改为每次冷启动独立进程，再进行 active 回归；当前不再重复触发
`DISABLE`。

## 42. 2026-09-15：修复 HideLab 任务生命周期

确认旧任务覆盖新结果的根因是 JNI/native 探测不可中断，`Future.cancel(true)` 只设置
中断标志。新增 `ProbeRunGate` 和单调 run token：只有最新 Intent 对应的任务可以写入
共享结果文件，旧任务完成后丢弃输出；Activity 销毁同时使 token 失效。新增单元测试
覆盖新 Intent 淘汰旧任务及销毁失效路径。

target/control 单元测试与 debug 构建全部通过，产物已放入：

```text
download/hidelab-app-target-run-gate-v1.apk
download/hidelab-app-control-run-gate-v1.apk
```

设备未安装新 APK，当前 binding 仍为 `ACTIVE`（PID `21860`、namespace `4026535990`、
generation `3002`）。修复尚未完成真实设备重复 Intent 回归；后续需冷启动新 target 或
等待旧 native 任务结束后，再验证新 run id 和全量 HideLab 断言。Hide 1.0 继续为
`unsupported`。
## 43. 2026-09-15：run-gate APK 真机回归

run-gate v2 修复后的 target APK 安装后，恢复旧 binding 触发一次重启。重启后重新加载
模块，冷启动 target（UID `10551`、PID `19958`、namespace `4026536048`），完成
`INSTALL generation=5001` 与 `ENABLE`，状态进入 `ACTIVE`。

连续发送两个 Intent（`run-gate-v2-second-a`、`run-gate-v2-second-b`）后，最终 metadata
正确为 `run-gate-v2-second-b`、status 为 `complete`，旧不可中断 native 任务未覆盖新
结果。HideLab 任务生命周期修复通过真机回归。

但 direct VFS 的 `stat/open/readdir/getdents64` 仍可见目标，结论为 `LEAK`，不是 Hide 1.0
通过。证据目录：`build/device-evidence/hidelab-run-gate-v2/`，产品状态继续为
`unsupported`。

## 44. 2026-09-15：fuse-ro-v1 ENABLE 回归定位

对比稳定提交 `5590eab` 与 `pathguard-hide1-lab-myron-fuse-ro-v1` 后确认，稳定包的默认
加载模式是 `shadow_mode=1`，只替换父目录 inode 的 `i_op`；本轮 fuse-ro-v1 将默认改为
`shadow_mode=0`，同时替换 `i_op`、`i_fop` 和逐 dentry 的 `d_op`。本轮还在 `atomic_open`
回调中安装 dentry shadow 并对正 dentry 执行 `d_drop`。设备在 INSTALL 成功后执行 ENABLE
即断开 ADB 并重启，且没有 pstore 记录；该时间关系与新增 f_op/d_op 和 atomic_open 改写
一致，而不是稳定 i_op-only 路径的已知行为。

参考 Kasumi 的实现，dentry shadow 只在 lookup 结果路径安装，atomic_open 不修改 d_op、
不主动 drop 受 VFS 锁定的 dentry；f_op 则由独立的 ingress/live bridge 管理。当前原型的
默认值已恢复为 `shadow_mode=1`，并移除 atomic_open 中的 dentry 安装/drop。`f_op`、`d_op`
及完整模式仍可显式加载，但在 bridge、并发和设备 ENABLE 回归完成前不得作为默认或
Hide 1.0 依据。产品状态继续为 `unsupported`，本轮不重复执行 ENABLE。

## 46. 2026-09-15：target 身份绑定与采集闸门修复

针对 safe-v2 后续采集失效，内核 binding 增加 `target_task` 引用和 thread-group/退出态
校验，target 进程退出或 PID 复用时自动 fail-closed。HideLab runner 新增
`-KeepTargetProcess`、`-GrantReadMediaImages` 和只读 `-ExistingHiddenPath`；其中
`-KeepTargetProcess` 跳过 target 重装与 force-stop，并验证采集期间 mount namespace
不变。宿主 PowerShell 语法、VFS model/teardown/concurrency 测试以及 Android 16/6.12
Kbuild 全部通过。

生成并传送 `download/pathguard-hide1-lab-myron-fuse-iop-safe-v4.zip`（KO SHA-256
`934945c3b8e651902ccc2ea8657e2e962977844396e97ae5e0aa4251fa40ddab`，ZIP SHA-256
`e2e8b7e07971d0d097fb564ed06c1c15bc8f795d20eba7424d84dc46e3747d74`）。当前设备未安装
safe-v4，产品状态保持 `unsupported`。

## 45. 2026-09-15：safe-v2 i_op-only 真机 ENABLE 复验

安装并重启 `pathguard-hide1-lab-myron-fuse-iop-safe-v2.zip` 后，模块未自动加载；手动
`load 1` 成功。对 UID `10551`、PID `19500`、mount namespace `4026535993` 执行
`INSTALL generation=7001` 和 `ENABLE` 均成功，状态进入 `ACTIVE`，设备保持在线且无
pstore 崩溃记录。由此确认将默认模式恢复为 `shadow_mode=1` 后，fuse-ro-v1 的 ENABLE
重启回归未复现。

后续探测因 force-stop/restart 产生新的 namespace `4026536013`，与 binding 不一致，且
存储访问返回 `EACCES`；本轮没有可用于 HideLab 的有效隐藏结果。不得将 ACTIVE 或本次
稳定性复验视为 Hide 1.0 通过。当前 binding 未执行 DISABLE，产品状态保持
`unsupported`。

## 47. 2026-09-15：binding 提交/回滚与 stale dentry 生命周期修复

离线审查修复了 binding 所有权转移、安装回滚和 dentry stale worker 的三个 UAF/损坏窗口：
提交不再复制包含链表头和自旋锁的整个临时结构；所有 metadata 释放前都等待 active callback
计数归零。HideLab `-KeepTargetProcess` 同步锁定 PID、mount namespace 和当前 run id，拒绝
target 重启、namespace 失配及陈旧输出。验证包括宿主六项 hide/loader 测试、PowerShell
语法检查和 Android 16/6.12 Kbuild 全部通过；没有新的设备侧 ENABLE，Hide 1.0 仍为
`unsupported`。

修复后的未安装实验包：`download/pathguard-hide1-lab-myron-fuse-iop-safe-v5.zip`，KO
SHA-256 为 `0f03015e74966542c9089b76e92779971270b3230080ee85b69365772f370759`，ZIP
SHA-256 为 `e47acb79067e9229eee250e08f635d62d48e621e8fd96b97f40f69b57d7bc4a9`。

## 48. 2026-09-15：safe-v5 active baseline 仍为 LEAK

safe-v5 在 myron 上安装、重启、SukiSU 加载、INSTALL 和 i_op-only ENABLE 均稳定，设备未
重启，binding 为 PID `20371`、namespace `4026536101`、generation `8001`。固定同一身份
执行 active HideLab baseline 后，已有 `Nagram` 目录的 Java、direct-VFS 和目录读取接口
仍全部可见，结论为 `LEAK`；control 对照、fixture 和 root Oracle 未变化。证据位于
`build/device-evidence/hidelab-v5-active-baseline/20260915-231900/`。根因是目标路径已有
positive dentry/cache 未被 i_op-only 安装阶段失效；必须先实现并验证正 dentry 失效与 alias
处理，再进行后续回归。Hide 1.0 继续为 `unsupported`。

## 49. 2026-09-15：从 per-object shadow 转向分层 Virtual View

本轮重新审查 `refer/Kasumi-main`、`refer/hide-refer/nomount-master`、
`refer/hide-refer/susfs4ksu-master`、`refer/hide-refer/LKM-PathMask-main`，并检索
Kasumi、NoMount、HymoFS、ZeroMount、Linux VFS 文档及 SUSFS 的 FUSE 修复记录。
结论是：继续向当前单 binding、单 basename、父目录 `i_op` shadow 叠加
`f_op/d_op/atomic_open`，不能从根因上解决 FUSE positive dentry、alias 和路径
上下文问题。

### 新的主架构

PathGuard 改为按能力分层的 **Virtual View**：

1. **规则层**：完整路径组件树、per-directory 节点、RCU children snapshot、UID/
   mount namespace/generation 作用域。规则身份不再只由 basename 或 inode 决定。
2. **VFS shadow 后端**：针对 ext4/erofs 等非 FUSE 文件系统，采用 Kasumi/NoMount
   的 parent lookup + iterate + per-dentry `d_revalidate` + synthetic vnode/superblock
   生命周期模型。纯隐藏只返回 synthetic negative；只有重定向/注入才创建 virtual
   inode。
3. **FUSE namei 后端**：针对 `/storage/emulated/0`、`/sdcard`、
   `/storage/self/primary`，必须在 dcache 命中之前覆盖 `lookup_fast/lookup_slow`、
   open/namei、getattr/statx、readdir/filldir 等路径，并处理 FUSE 的 fake-qstr、
   inode 查询和 alias。普通父目录 `i_op->lookup` shadow 不足以覆盖这些入口。
4. **syscall mask 后端**：仅作为观测和补洞 feature，借鉴 PathMask 覆盖
   `statx/newfstatat/faccessat2/openat/openat2/getdents64` 等；不作为唯一隐藏数据面，
   不用“打开后关闭 fd”伪造 mutation 失败。
5. **namespace 后端**：仅用于受控应用实验。bind/overlay 视图可作为兼容性后备，
   但 mountinfo、传播、FUSE 共享存储和 namespace 切换必须单独验收，不能称为真隐藏。

### 设备准入重新定义

- **Hide 1.0-LKM**：只准入非 FUSE、单设备、单 UID、单 namespace、单路径、只读
  场景；必须通过 positive/negative dentry、alias、cold/warm cache、相对路径、
  `O_PATH`、statx、readdir 和 teardown 全矩阵。
- **Hide 2.0-Kernel/FUSE**：只有取得精确设备内核构建输入，或在设备内核中稳定挂接
  namei/FUSE 核心路径并通过全量回归后，才允许覆盖共享存储 FUSE。该阶段通用性显著
  降低，必须按 fingerprint、kernel release、KMI、工具链和模块哈希建立白名单。
- 当前 Redmi K90 Pro Max 的 `/storage/emulated/0` 仍保持 `unsupported`；不能因模块
  Live、`INSTALL` 或 iop shadow 稳定而进入 admitted。

### 参考项目与网页结论

- Kasumi：VFS lookup/vnode、fop bridge、dentry revalidate、synthetic inode 和
  superblock 回收值得借鉴；公开 README 在不同镜像/提交间存在 protocol 16/17 及
  syscall TSR/VFS-only 的描述漂移，必须以本地提交为准。
- NoMount：规则树、virtual directory topology、RCU children、lookup/readdir 和
  无 mount 表污染适合规则层与 VFS 后端；其 LKM 仍不是 FUSE 全路径保证。
- SUSFS：直接修改 namei/open/readdir 关键路径，且公开提交专门修复 FUSE
  `getdents/readdir` 泄漏并限制 fuse/tmpfs 的 `SUS_PATH`，证明 FUSE 需要独立后端，
  不能简单套用 inode 标记。
- PathMask：syscall kretprobe 覆盖面广但有 inline、热点时序、fd 泄漏和只覆盖用户
  syscall 的限制，只能作为 fallback/diagnostic。
- HymoFS/ZeroMount：说明真正的 mountless path view 通常需要内核级 namei 集成或
  定制内核；ZeroMount 的 VFS→OverlayFS→MagicMount 级联可借鉴为后备策略编排，
  不能把 OverlayFS/MagicMount 当成无痕隐藏。
- Linux VFS 文档明确 dcache 可直接返回已有 dentry，且同一 inode 可被多个 dentry
  引用；这正是当前 iop-only `LEAK` 的根因。

后续工作顺序改为：离线 Virtual View 模型和规则树 → 非 FUSE VFS shadow 后端 →
FUSE namei coverage 探针/实验后端 → 各后端独立 HideLab 回归 → 设备白名单准入。未
完成 FUSE namei coverage 前，不再把完整 iop/fop/dop 组合直接部署到设备。

## 50. 2026-09-15：网上“Redmi K90 Pro Max 定制 GKI”路线核验

网页检索和本地 `myron-prebuilt` 证据表明，社区所称“为 K90 Pro Max 定制 GKI”至少
包含四种不同技术路线，不能把它们都视为“拥有 myron 完整内核源码”。

| 路线 | 实际修改物 | 是否需要 myron 内核源码 | 对 Hide 的意义 |
|---|---|---:|---|
| KernelSU/SukiSU LKM | 保留 stock `boot`/kernel，只在 `init_boot` ramdisk 加载外部 `.ko` | 否 | 可做 operation shadow、探针和有限 fallback；不能覆盖未导出的 namei/FUSE 核心路径 |
| 通用 GKI 重编译 | 用 AOSP `android16-6.12` 或其它 SM8850 源码生成新的 `Image`，替换 `boot.img` 的 kernel | 否，但需要兼容的 vendor 镜像/DT/模块 | 可把 SUSFS/namei 改动编入内核；必须以设备实测证明兼容，不能只看 6.12.23 |
| stock kernel + vendor/GKI LKM | 使用 ROM 中原厂 kernel，按 GKI KMI 构建 `.ko`，通过 `system_dlkm/vendor_dlkm` 或 SukiSU loader 加载 | 通常不需要完整源码，但需要精确 KMI/符号输入 | 适合模块级功能；不能凭通用 `Module.symvers` 证明 FUSE/namei ABI |
| 真正 myron kernel rebuild | 获取 Xiaomi myron 源码、defconfig、DT、Kleaf 输出、符号和签名后重建 kernel/模块 | 是 | 才能严格实现并验证 Hide 2.0 的内核级 FUSE/namei 修改 |

### 社区项目实际采用的方式

- `cofor1ae-byte/xiaomi-kernel-for_sm8850` 和 `lsfqxd2006/RainKissM_Mi17pm_GkiKernel`
  的公开说明明确写着：6.12.23/6.12.38 主要基于 OnePlus 15、OnePlus Ace 6T 或
  通用 AOSP/SM8850 源码，“其它同内核版本非 SM8850 机型部分可兼容”。这证明的是
  平台级 GKI 可启动性，不是 `myron` 的精确 ABI。
- `LokumKernel-SM8850/lokum-release` 只覆盖 Xiaomi 17 系列，并根据静态 ROM 分析
  复用 stock Android 16/6.12 kernel payload；其 README 仍要求其它代号先用
  `fastboot boot` 验证，不等于 K90 Pro Max 已获准入。
- `haohao3001/android_device_xiaomi_myron` 是 Recovery/设备树项目，使用
  `myron-prebuilt` 复制 kernel、DTB、DTBO 和模块，并不是可重建的 myron kernel tree。
- 官方 MiCode 针对 myron 的 kernel source/device tree/vendor blob 请求仍是公开 issue；
  这与本地 prebuilt 仓库只有二进制 kernel/模块、没有 `Module.symvers`、`vmlinux`、
  `.config` 的结果一致。

### 为什么通用 GKI 有时能启动

Android GKI 将通用 kernel `Image`、vendor ramdisk、DT/DTBO、vendor/system DLKM 分离，
并通过同一 LTS/KMI 约束模块接口。社区构建者通常只替换 `boot.img` 中的 `Image`，继续
使用原厂 `vendor_boot`、`vendor_dlkm`、`system_dlkm` 和固件；如果设备实际依赖的
符号、DT、模块 ABI 和启动参数没有发生不兼容变化，就可能正常启动。

但以下做法不能制造精确兼容性：

- 把 `6.12.23` 或 `-4k` 写入 localversion；
- 把 OnePlus/Xiaomi 17 的 git hash 改成 `g16e473de48a3`；
- 只复制 `vermagic` 或关闭版本检查；
- 用另一个设备的 `Module.symvers` 代替 myron 的 CRC/CFI 输入。

### 对 PathGuard 的直接结论

如果目标是 **SUSFS/Hide 2.0 的 FUSE namei 隐藏**，最有希望的路线是：

1. 先保留当前 stock kernel，继续用 SukiSU LKM 做规则层、探针和非 FUSE View backend。
2. 另行构建一个可回滚的 SM8850/Android 16 GKI candidate，把 SUSFS/namei 改动编入
   `Image`，只替换 boot kernel，保留原厂 vendor 分区。
3. 通过 `fastboot boot` 或备份 slot 进行启动、硬件、模块、OTA 和 HideLab 验证。
4. 只有设备侧 `uname -r`、KMI、system/vendor DLKM 依赖、pstore 和全量 HideLab 都
   通过，才建立 candidate 白名单；否则继续保持 `unsupported`。

因此，网上“定制 GKI 能用”回答的是“GKI kernel image 可以在相近 SM8850 设备上启动”，
不是“已经获得了 K90 Pro Max 的精确内核 ABI”，更不是“FUSE 隐藏已经实现”。

## 51. 2026-09-16：LunarKernel AnyKernel3 包审计

审计对象：
`refer/hide-refer/LunarKernel6.12-V1.7-fix1-20260915-223331-os4-612-g1faff188861d-AnyKernel3`。

### 包内容与内核身份

该目录是 AnyKernel3 刷机包，不是可重建的内核源码或 prepared output tree。包内只有预编译
`Image`、`Resuki.lkpatch`、AnyKernel3 脚本、工具和 `lunarkernelmodule`；未发现源码、
`.config`、`Module.symvers`、`vmlinux`、`vmlinux.symvers`、`System.map`、Kleaf manifest、
DTB/DTBO 或 vendor/system DLKM 镜像。

`Image` 中的 release 为：

```text
6.12.69-android16-6-g1faff188861d-LunarKernel-V1.7-fix1
```

包内 `version` 同时声明 4K pages、基础 `CONFIG_KSU=n`，以及可选的 ReSukiSU + SUSFS
二进制修补。设备当前 release 是
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，两者的 LTS 版本、GKI generation、
commit 和 KMI build 均不同，不能把 LunarKernel `Image` 或补丁直接用于 myron。

附加模块的 `capabilities.prop` 还包含
`6.12.69-android16-6-gb1493ec68d4a-abogki514973465-4k`，与 Image 自身 release 不一致；
该字段只能视为模块配置/展示值，不能作为设备 ABI 证据。

### 刷写路径

`anykernel.sh` 设置 `do.devicecheck=0`、`block=boot`、`is_slot_device=auto`，先检查运行内核
主版本为 `6.12*`，再将包内 Image 重打包进当前 slot 的 boot 分区。若用户选择 root 版本，
脚本执行 `lkpatch apply Image Resuki.lkpatch Image.resuki`，并用补丁目标 SHA-256 校验后替换
Image；`Resuki.lkpatch` 文件头为 `LKPATCH1`，内部包含 BSDIFF 数据，属于针对特定 Image 的
二进制差分，不是可移植源码补丁。

`write_boot()` 虽然还会调用 `flash_generic vendor_boot`、`vendor_kernel_boot`、
`vendor_dlkm`、`system_dlkm` 和 `dtbo`，但 `flash_generic` 仅在包内存在对应镜像时执行。
本包没有这些文件，因此实际刷写范围是 boot（以及重打包时保留的原 boot ramdisk）；
`do.devicecheck=0` 也意味着包本身不做机型 allowlist 防护。对 myron 直接刷写此类包属于高风险
boot 替换实验，不能作为 PathGuard 模块的安装方式。

### 对 Hide 2.0 的借鉴

LunarKernel 可借鉴的只是发布工程流程：预编译 GKI Image、针对同一 Image 的可选二进制
ReSukiSU/SUSFS patch、AnyKernel3 交互式选择、目标哈希校验、slot 感知和附加模块的事务式
安装。其 `lunarkernelmodule` 只控制 CPU/调度/渲染、daemon、属性和 uname 展示，不包含
VFS、FUSE、namei 或路径隐藏实现。

因此本项目不能以该包作为 Hide 2.0 的内核基础。继续路线仍应是：保留 stock myron kernel
做 LKM/规则层开发；另行取得与目标设备匹配的 GKI candidate 输入后，才构建独立 boot Image，
保留原厂 vendor 分区并用备用 slot 或 `fastboot boot` 验证。没有精确 release/KMI、模块依赖、
启动日志和 HideLab 全量证据前，设备准入和产品状态继续保持 `unsupported`。

### 51.1 预编译 Image 的可能来源链

包内没有源码仓库 URL、manifest、CI run ID 或构建脚本，无法从包本身证明唯一上游。现有证据
支持以下分层判断：

1. **公开 GKI 基线**：Image 的 `6.12.69-android16-6`、Clang/LLD 19.0.1 与 AOSP
   `android16-6.12` 的 6.12.69 release line 一致。AOSP 的 release artifact 说明该线可提供
   Image、System.map、vmlinux、vmlinux.symvers 和 pinned manifest；但本包没有携带这些配套
   构建产物。
2. **社区/私有补丁层**：Image release 的 commit `g1faff188861d` 在公开
   `android.googlesource.com/kernel/common` 和 GitHub commit 搜索中没有命中，且 Image 包含
   `LunarKernel-DEV: Tsukiko Hakura&arclightx`、`sched_ext`、`mi_sched_ext_ops`、
   `Xiaomi dispatch zones` 等非纯 AOSP GKI 标识。这说明它不是未经修改的官方 GKI Image，
   而是另一个构建树或私有补丁树的结果。
3. **LunarKernel 打包层**：`version`、AnyKernel3 脚本和 `Resuki.lkpatch` 表明 LunarKernel
   团队拿到该构建结果后，按自己的版本号重新打包，并提供针对该 Image SHA-256 的可选
   ReSukiSU/SUSFS 二进制 patch。

因此最稳妥的表述是：

```text
AOSP android16-6.12 / 6.12.69 GKI 基线
        + 未公开的 Xiaomi/sched_ext/LunarKernel 补丁或构建树
        -> LunarKernel Image
        + 针对该 Image 的 ReSukiSU/SUSFS lkpatch
        -> AnyKernel3 刷机包
```

这不是已证实的唯一源码来源。要把“可能来自”升级为可复现来源，必须取得发布者的
`manifest_<build>.xml`、源码 commit、`.config`/defconfig、构建日志、`Module.symvers`/
`vmlinux.symvers` 以及对应 CI artifact；仅凭 `uname` 字符串、6.12.69 版本号或 Image
中的 commit 文本不能重建它。

## 52. myron coverage probe 设备回归（2026-09-16）

在清理陈旧的 `pathguard_hide1 ACTIVE` 绑定后，使用 SukiSU `ksud insmod` 加载 ABI v2
只读 coverage probe。设备精确 release 为
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，模块状态为 `READY`，required
入口注册 `3/3`，总注册 `13/15`，所有已注册入口 `nmissed=0`。

共享存储 FUSE 路径的只读回归命中 `iterate_dir`、`fuse_readdir`、`fuse_lookup`、
`fuse_dentry_revalidate`、`path_openat` 和 `do_filp_open`；模块卸载后设备未重启，旧
Hide 模块保持 `INACTIVE`。未执行 mutation，因此 `fuse_atomic_open` 尚无命中。

该结果仅完成 coverage/namei 观测门禁，不改变 Hide 1.0 的 `unsupported` 状态。后续仍需
实现并验证 operation-table shadow、mutation 封闭、正缓存失效、并发和完整 HideLab 回归。

## 53. 只读 FUSE-aware backend 离线实现（2026-09-16）

新增 `shadow_mode=4` 作为只读实验阶段：只发布 lookup、atomic_open、iterate_shared 和
dentry revalidate wrapper；不替换九个 mutation callback。目录枚举过滤限定到绑定的 parent
inode，INSTALL 在该模式只要求四项 capability，默认 mode 1 不变。

宿主契约测试和 Android 16/6.12 Kbuild 均通过。该模块尚未在设备上 INSTALL/ENABLE；下一步
是使用 mode 4 做受控设备回归，验证 positive/negative dentry cache、target/control namespace
隔离和 dentry revalidate 行为。mutation 必须保持原始回调，任何泄漏、过阻、语义漂移或设备
异常都阻止进入 Hide 1.0 admitted。

后续离线审查修复了两个 mode 4 正确性问题：dentry shadow 安装的存在性检查改为在
`dentry->d_lock` 下读取当前 `d_op`，避免把 RCU 元数据指针带出保护区；`iterate_shared`
代理同步原始 actor 的 `ctx->pos`，并让被过滤项推进 native cookie。该修复不扩大只读
backend 的语义范围，设备准入和 `unsupported` 状态不变。

另行确认 mode 4 的生命周期边界：ENABLE 前已打开的目录 FD 保留原始 `file->f_op`，不会
被后续 inode shadow 自动改写。HideLab 必须把该场景列为独立 LEAK 用例；在没有关闭并重建
这些 FD 的情况下，不能把新打开 FD 的 readdir 通过结果外推到全部访问面。

## 54. mode 4 首次受控设备结果（2026-09-16）

myron 上以 `shadow_mode=4` 完成 generation `7101` 的 INSTALL/ENABLE。设备没有重启或内核
异常，Target/Control Oracle 均未变化，DISABLE/CLEAR/rmmod 和恢复基线全部成功。

HideLab baseline 仍判定为 `LEAK`：Target 的 readdir/getdents 和 Java/NIO 目录枚举成功过滤
`Nagram`，但 Java exists、stat/lstat/access/open/openat 等直接解析仍然成功；Control 保持
完全可见。说明 `iterate_shared` shadow 已在真实 FUSE 数据面工作，但 positive dentry、
lookup 和 `d_revalidate` 链路尚未建立 observer 一致性。按 fail-closed 门禁，本轮在第一次
LEAK 后停止，未继续并发或 mutation。

下一阶段收缩为只读根因定位：给 lookup、atomic_open、d_revalidate、dentry shadow 安装和
scope reject 分支增加可读取计数，分别做 cold dentry 与 positive warm dentry 实验。只有直接
解析全部返回 ENOENT 后，才恢复 cache-order 和 concurrency；mutation 阶段继续冻结。

轮次 73 已完成离线修复：ENABLE 会主动解析并 shadow 绑定的正 dentry；目标命中的
`d_revalidate` 不再异步撤销 shadow。status 新增只读诊断计数，下一轮设备实验先读取计数，
再判断是否进入 cold/warm cache 回归。mutation 和 Hide 1.0 admission 继续冻结。

## 55. v3 诊断包真机只读回归通过（2026-09-16）

在 myron 上完成 v3 `shadow_mode=4` 受控验证。Target UID/PID 为 `10551/20230`，mount
namespace 为 `4026536018`，绑定 `/storage/emulated/0/Pictures/Nagram`，generation 为
`7102`。LOAD、INSTALL、ENABLE 成功，诊断计数显示 lookup、atomic_open、readdir、
d_revalidate 和正 dentry shadow 均实际命中；`dentry_install=32/32/0`。

HideLab baseline 证据位于
`build/device-evidence/hide1-fuse-ro-v3/20260916-233538/`，summary 为 `PASS`，Target/
Control Oracle 和 fixture 均未变化。该结果只覆盖只读 FUSE-aware backend 的单设备、单 UID、
单 namespace、单 basename 范围；未覆盖 mutation、cache-order、并发、生命周期和 OTA 准入。
测试结束后 DISABLE、CLEAR、rmmod 成功，boot ID 未变化，设备节点消失。Hide 1.0 仍保持
`unsupported`，不得据此激活或建立正式白名单。

## 56. 只读 cache-order、并发和恢复回归（2026-09-17）

在 v3 只读 FUSE-aware backend 上完成三组后续验证。主路径
`/storage/emulated/0/Pictures/Nagram` 的 cold/warm cache 顺序全部返回 ENOENT；20 线程
并发 stat/open/readdir 无隐藏泄漏；reliability 主访问循环也无泄漏。DISABLE/CLEAR 后恢复
baseline 可见，随后 rmmod 成功，boot ID 未变化，设备节点消失。

本轮证据分别位于：

- `build/device-evidence/hide1-fuse-ro-v3-cache-order/20260917-002731/`
- `build/device-evidence/hide1-fuse-ro-v3-concurrency/20260917-003012/`
- `build/device-evidence/hide1-fuse-ro-v3-reliability/20260917-003119/`
- `build/device-evidence/hide1-fuse-ro-v3-restore/20260917-003328/`

可靠性场景中的 generation、capacity、namespace、unload 仍报告 `unsupported`，因为当前
实验 ABI 没有提供这些控制接口；这属于正确的能力收缩，不是生命周期全覆盖。部分 Android
alias 由于权限限制返回 EACCES/setup_error，不能据此扩展结论。mutation 封闭、真实 namespace
销毁、OTA/slot 重新准入和正式 PathGuard 集成仍未完成，产品状态保持 `Hide 1.0 = unsupported`。
