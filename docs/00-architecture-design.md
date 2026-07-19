# PathGuard Next 架构设计

> 状态：Draft
>
> 文档版本：0.2
>
> 目标平台：Android 12+，Magisk Zygisk / KernelSU + ZygiskNext
>
> 外部资料核验日期：2026-07-19

## 1. 文档目的

本文档定义 PathGuard Next 的产品边界、威胁模型、核心架构、规则模型、运行流程、性能目标和实施计划。它是后续编码、评审和真机验收的基线。

PathGuard Next 不在旧项目上渐进修补。旧项目仅作为需求、兼容性问题和测试样本来源，不直接迁移其 PLT Hook、fanotify 写后搬运、Binder Parcel 修改和外围管理功能。

## 2. 项目目标

PathGuard Next 是一个按 Android 应用隔离文件夹视图的 Root 模块，核心能力只有两项：

1. `deny`：禁止指定应用访问、列举、读取、写入指定文件夹中的内容。
2. `redirect`：将指定应用对源文件夹的访问同步重定向到目标文件夹。

核心设计目标：

- 文件访问热路径不进入用户态策略代码。
- Java、Native、第三方库和直接系统调用看到一致的文件系统视图。
- 规则错误在编译阶段拒绝，不在应用运行期间猜测或自动修复。
- 不支持的设备明确报告，不静默退化为语义较弱的实现。
- 核心模块可独立于管理 App 工作和诊断。

## 3. 长期边界与分阶段交付

本文档描述的是可持续演进的架构，不把当前阶段的交付顺序误写成永久产品边界。核心数据面先稳定 `deny` 和 `redirect`，控制面和规则语言必须为后续能力保留版本化扩展点。某项能力在某个 Phase 尚未实现，只表示尚未接入该执行后端，不表示规则模型永远排除该能力。

初始交付不启用以下能力，但规则语言和状态模型不得与其冲突：

- 单文件或文件扩展名级别规则。
- 文件写入完成后的移动、删除、导出或媒体扫描。
- MediaStore 查询结果过滤。
- Storage Access Framework（SAF）结果过滤。
- 网络、DNS、隐私评分、诱饵文件和行为画像。
- 定时启停、云规则、社区预设和规则推荐。
- 通过另一个进程代理访问文件的拦截。
- 对定制内核、SELinux 策略或 ROM 行为作无条件兼容承诺。
- 隐藏 bind mount 在 `/proc/self/mountinfo`、`/proc/self/mounts` 中的痕迹。
- 要求用户安装 SUSFS 或其他定制内核补丁。

这些功能只有在核心能力稳定、具备独立需求和独立执行边界后，才能通过扩展组件引入。

## 4. 术语

| 术语 | 含义 |
|---|---|
| 控制面 | 配置、编译、发布、状态查询和诊断逻辑 |
| 数据面 | 实际决定应用看到何种目录视图的 mount namespace 和 VFS |
| 策略源 | 用户或管理 App 编辑的版本化 `rules.ini` |
| 策略快照 | daemon 编译生成的只读 `policy.bin` |
| 应用策略 | 一个包名及其进程、用户和目录规则集合 |
| 挂载计划 | 针对一个应用进程生成的有序 mount 操作集合 |
| companion | 具有 Root 权限的 Zygisk companion 进程 |

## 5. 威胁模型

### 5.1 保护对象

- 共享存储中的用户指定目录及其子项。
- 应用对指定源目录的读取和写入位置。
- 不同应用之间的目录视图隔离。

### 5.2 目标应用能力假设

目标应用可能：

- 使用 Java/Kotlin 文件 API。
- 使用 libc 或第三方 Native 库。
- 直接调用 `openat`、`getdents64`、`statx` 等系统调用。
- 创建多个子进程。
- 使用 `/sdcard`、`/storage/emulated/<user>` 等路径别名。
- 尝试通过 `..`、符号链接或备用路径访问目录。

目标应用默认不具备 Root、`CAP_SYS_ADMIN` 或逃离自身 mount namespace 的能力。

### 5.3 不覆盖的攻击路径

以下路径不由基础文件系统后端保证：

- MediaProvider、DocumentsProvider 或其他进程替目标应用打开文件并传递文件描述符。
- 目标应用与 Root 进程、系统进程或其他高权限应用串通。
- 应用获得内核代码执行、Root 或 namespace 管理能力。
- 规则安装前已经获得的外部文件描述符。
- OEM 修改导致应用绕过预期存储挂载树。

### 5.4 安全声明

`deny` 的基础语义是“目录内容不可访问”，不是“路径必须表现为不存在”。挂载点本身可能仍可被 `stat()` 观察到。

项目文档和 UI 不得使用“绝对防护”“不可绕过”等超出威胁模型的描述。

### 5.5 挂载可见性与应用自检

Linux 的 `/proc/<pid>/mountinfo` 展示目标 PID 所在 mount namespace 的挂载树，并包含 bind mount 的 root、mount point、mount ID 和父子关系。[S6][S7] 因此，PathGuard 创建的 deny/redirect 挂载对目标应用本身是可观察的。

这不仅是隐藏 Root 的问题，也可能造成实际功能兼容风险：

- 支付、银行、游戏或反作弊 SDK 可能把新增挂载判定为运行环境被修改。
- 应用可能拒绝启动、关闭部分功能或触发风控。
- 即使 Root 和 Zygisk 痕迹已被其他方案隐藏，PathGuard 自己的挂载仍可能被识别。

基础版本不篡改 procfs 输出，不承诺规避这类检测。能力状态和管理界面必须把“目录视图已生效”与“目标应用不会检测挂载”区分开。

### 5.6 与内核补丁路线的取舍

SUSFS 一类方案可以在内核层处理路径和挂载痕迹，但要求设备运行经过专门补丁的内核；其用户态模块也明确要求 SUSFS patched kernel。[S10] PathGuard Next 选择标准 Linux namespace、`setns` 和 bind mount，是主动接受以下取舍：

- 获得更大的设备覆盖面，不要求刷入项目专属内核。
- 保留标准 VFS 一致性和较低的运行时开销。
- 放弃对 mountinfo 完全隐身和内核级路径伪装的能力。

项目可以探测 SUSFS 是否存在并写入诊断报告，但核心语义、测试和发布不得依赖它。

## 6. 核心架构决策

### 6.1 使用 mount namespace 作为唯一核心后端

每个命中策略的应用进程拥有独立 mount namespace。PathGuard 在应用代码开始执行前，将 deny 和 redirect 规则转换成 bind mount。

```text
rules.ini
    |
    v
pathguardd -- compile/validate/publish
    |
    v
policy.bin
    |
    v
Zygisk module -- select app/unshare namespace
    |
    v
Root companion -- setns/apply mount plan
    |
    v
Linux VFS -- enforce every file access
```

选择该方案的原因：

- 策略安装后没有逐文件调用的用户态开销。
- 不依赖目标库是否经过 PLT、是否动态加载或是否直接 syscall。
- 重定向对读、写、列举、元数据查询和重命名保持统一。
- 内核 VFS 负责路径解析和权限检查，执行模型更容易验证。

该方向与 Android 平台自身的存储隔离机制一致，而非未经验证的私有技巧。AOSP 文档说明：Zygote 为每个应用创建 mount namespace 并 bind mount 对应的外部存储视图；权限变化后，`vold` 会通过 `setns()` 进入已运行应用的 namespace 更新视图。[S1] AOSP `vold` 源码也包含进入应用 namespace、挂载 tmpfs 和 bind mount 应用目录的实现。[S3]

这只证明所用内核机制与平台设计一致，不证明任意 Root 实现、SELinux 策略和 OEM 存储布局都允许第三方模块执行相同操作。兼容性仍由 Phase 0 实测决定。

### 6.2 不提供隐式兼容后端

设备不支持 `unshare`、`setns` 或目标存储上的 bind mount 时，策略状态必须是 `unsupported` 或 `failed`。不得自动切换到 PLT Hook 并继续显示“已保护”。

未来若提供 Hook 后端，必须作为用户显式选择的 `compat` 模式，具有独立能力声明和测试矩阵。

### 6.3 控制面与数据面分离

- 控制面允许使用 JSON、日志和可读错误信息。
- 数据面只消费验证后的紧凑二进制策略。
- 应用启动路径不得解析 JSON、访问 SQLite 或启动 JVM 服务。

## 7. 目录隔离语义

### 7.1 Deny

daemon 在启动时准备一个专用 deny 源目录：

- 所有者为 Root。
- 权限为 `000`。
- 内容为空。
- bind mount 后重新挂载为只读。
- 设置 `nosuid,nodev,noexec`。

应用规则：

```text
deny /storage/emulated/0/Secret
```

在目标应用 namespace 中等价于：

```text
bind <deny-source> -> /storage/emulated/0/Secret
remount ro,nosuid,nodev,noexec
```

预期结果：

- 无法列举真实子项。
- 无法读取、创建、覆盖、删除真实子项。
- 真实目录对未命中策略的应用保持不变。

Phase 0 必须验证不同 ROM 上 FUSE/sdcardfs 对 bind mount、权限和 SELinux 的实际行为。

### 7.2 Redirect

应用规则：

```text
redirect SOURCE => TARGET
```

在目标应用 namespace 中执行递归 bind mount：

```text
mount --rbind TARGET SOURCE
```

规则生效后：

- 对 `SOURCE` 的读操作读取 `TARGET`。
- 对 `SOURCE` 的写操作写入 `TARGET`。
- 对 `SOURCE` 子目录的访问保持相对路径不变。
- 不存在写入原目录后再移动的窗口。

默认要求：

- `SOURCE` 和 `TARGET` 都已存在。
- 两者都是目录。
- `TARGET` 对目标应用原本可读写，或由控制面显式创建并设置正确标签和权限。
- 不自动覆盖目标已有文件。

### 7.3 路径别名

策略编译器将外部存储路径规范化为逻辑路径：

```text
/sdcard/...
/storage/self/primary/...
/storage/emulated/{user}/...
```

运行时根据 Android 用户展开为实际可见路径。Phase 0 需要检查 `/mnt/user`、`/mnt/runtime` 等备用路径是否构成可利用别名；如构成绕过，挂载计划必须覆盖所有可达别名，或将该 ROM 标记为不支持。

### 7.4 MediaStore、SAF 与 FUSE 边界

Android 11+ 的共享存储通常由 MediaProvider 作为 FUSE handler 管理，MediaProvider 可以检查直接文件路径操作；MediaStore API 同时维护并查询媒体数据库。[S2] bind mount 能改变目标应用自己的路径解析结果，但不能自动重写其他进程中的数据库查询、DocumentsProvider 选择结果或其他进程代开的文件描述符。

因此可能出现以下用户可见差异：

- 应用通过文件路径访问时已经 deny/redirect，但系统相册查询仍展示原媒体记录。
- 文件选择器展示某个条目，但目标应用拿到 provider 返回的文件描述符后仍可能读取。
- redirect 后的新目录内容未必自动获得符合用户预期的 MediaStore 索引。

Storage Isolation 的历史变更中，多次出现针对 Media Storage、Android 大版本、厂商实现、应用交互和 export 的专项修复，说明把 provider 兼容层并入核心会形成持续维护面。[S8][S9]

PathGuard Next 对此采取明确产品策略：

- 基础能力只保证目标进程内的直接文件系统视图。
- README、CLI `explain` 和管理 App 规则页面必须持续显示 MediaStore/SAF 限制。
- 来自上述边界的报告先分类为 `unsupported-access-path`，不能笼统显示为规则未生效。
- 未来若实现 provider 支持，必须作为独立后端、独立 ADR 和独立兼容矩阵，不修改基础 deny/redirect 的定义。

## 8. 进程启动流程

### 8.1 总体时序

```text
zygote child       Zygisk module       companion/helper       kernel
     |                   |                    |                  |
     | preSpecialize     |                    |                  |
     |------------------>|                    |                  |
     |                   | lookup policy      |                  |
     |                   | unshare(CLONE_NEWNS)----------------->|
     |                   | make mounts private------------------->|
     |<------------------|                    |                  |
     | Android specialize/storage setup       |                  |
     |                   |                    |                  |
     | postSpecialize    |                    |                  |
     |------------------>| request(pid, plan) |                  |
     |                   |------------------->|                  |
     |                   |                    | setns(pid/mnt)--->|
     |                   |                    | bind mounts------>|
     |                   |<-------------------| result            |
     |<------------------| continue or fail   |                  |
     | application code starts                |                  |
```

### 8.2 `preAppSpecialize`

职责限制为：

1. 读取包名、进程名、UID 和 Android 用户 ID。
2. 从 mmap 的 `policy.bin` 查找应用策略。
3. 没有策略时立即请求卸载 Zygisk 模块。
4. 有策略时执行 `unshare(CLONE_NEWNS)`。
5. 将挂载传播设置为 private，避免污染其他进程。
6. 保存策略索引和 generation，不执行目录扫描。

禁止在共享 zygote mount namespace 上直接修改挂载。

### 8.3 `postAppSpecialize`

职责限制为：

1. 连接 Root companion。
2. 发送 PID、UID、策略索引和 generation。
3. 等待 companion 应用挂载计划。
4. 根据失败策略决定继续启动还是终止进程。
5. 释放策略映射并卸载 Zygisk 模块。

默认失败策略为 `fail-open-with-alert`：应用继续启动，但状态明确记录为未保护。开发和严格模式可以使用 `fail-closed`，挂载失败时终止目标进程。

### 8.4 Companion helper

每个请求使用短生命周期 helper：

1. 校验请求者 PID、UID、包名和策略 generation。
2. 打开 `/proc/<pid>/ns/mnt`。
3. 再次检查 PID start time，防止 PID 复用。
4. `setns()` 进入目标 namespace。
5. 校验实际挂载树和路径类型。
6. 按编译顺序执行挂载计划。
7. 返回逐条结果后退出。

companion 主进程不得直接 `setns()`，避免污染后续请求。

## 9. 组件划分

```text
PathGuard-Next/
|-- core/
|   |-- policy/
|   |-- path/
|   `-- validation/
|-- zygisk/
|-- daemon/
|-- cli/
|-- module/
|-- manager/
|-- tests/
|   |-- unit/
|   |-- integration/
|   `-- device/
`-- docs/
```

### 9.1 `core`

纯 C++ 库，无 Android UI 依赖，负责：

- `rules.ini` 配置解析。
- schema 校验。
- 路径规范化。
- 冲突和重定向环检测。
- 生成二进制策略。
- 读取和查询二进制策略。

### 9.2 `zygisk`

只负责：

- 应用身份提取。
- 策略快速查询。
- namespace 创建。
- companion 请求。
- 生命周期和失败策略。

禁止包含 JSON 解析、规则编辑和逐文件 Hook。

### 9.3 `daemon`

`pathguardd` 负责：

- 配置编译和原子发布。
- Root 环境与内核能力探测。
- deny 源目录准备。
- companion mount 请求处理。
- 结构化状态和审计事件输出。
- 可选的运行中进程重新应用策略。

daemon 不监听每一次文件访问，不使用 fanotify 参与 deny 或 redirect。

### 9.4 `cli`

`pathguardctl` 是首个控制客户端，至少支持：

```text
pathguardctl validate <rules.ini>
pathguardctl compile
pathguardctl status
pathguardctl probe
pathguardctl explain <package> [user]
pathguardctl apply <package>
pathguardctl logs
```

管理 App 必须复用 daemon 协议，不允许通过 Root Shell 拼接命令直接修改内部状态。

### 9.5 `manager`

初始管理 App 只提供：

- 应用选择。
- deny/redirect 规则管理。
- 配置校验结果。
- 模块、daemon 和应用保护状态。
- 诊断报告导出。

管理 App 不实现自己的规则解析器，不直接解释策略语义。

Manager App 的 Root 是硬性前置条件：

- 首次启动必须通过 Root 方案授权。
- 用户拒绝、Root 方案不存在、Root 授权被撤销或 Root 检查失败时，不进入主界面。
- 不提供非 Root 浏览、只读模式或降级控制路径。
- 主界面只能在 `root_granted && daemon_reachable` 同时成立时显示。
- Root 权限由 Manager 进程通过 RootGateway 使用，不把 UI 进程本身设计为长期 UID 0 服务。

推荐控制链路：

```text
Manager UI
    -> RootGateway
    -> su -c pathguardctl rpc
    -> pathguardd
```

RootGateway 必须使用固定参数和长度前缀协议，不允许将用户输入拼接成 shell 命令。Root 被撤销或 daemon 不可达时，Manager 进入阻断页，只提供重新授权、诊断和退出操作。

管理 App 不默认直接连接 `/data/adb` 下的 daemon Unix socket。Android SELinux 通常会阻止 `untrusted_app` 访问 Root 模块路径；`SO_PEERCRED` 只能提供对端身份，不能绕过 SELinux。Phase 3 首选通过 Root 授权执行 `pathguardctl` 控制命令，后续再评估带专用 sepolicy、UID/package/certificate 白名单的长期 socket 桥接。

管理 App 的本地草稿可以保存在应用私有目录，但 active generation、应用保护状态和能力结果必须以 daemon 返回值为准。客户端缓存只能标记为 stale，不能推导为已生效。

## 10. 配置模型

### 10.1 用户规则文件

用户编辑的源文件采用按包名分组的版本化 `rules.ini`。它是稳定的人类可读语法；daemon 将其解析为内部策略 IR，再编译成 `policy.bin`。新增能力通过 schema 版本和明确的动作/选项扩展，不改变既有规则的含义。

```ini
schema = 1
failure_mode = fail_open_with_alert

[com.example.app]
enabled = true
users = *
processes = *

- Secret
Download/AppCache -> PathGuard/{package}/AppCache
```

初始阶段只执行目录级 `deny` 和静态 `redirect`；后续可在同一语法骨架中加入白名单、文件级匹配、动态导出、媒体查询策略等动作。每种扩展必须有独立 capability、编译校验、运行状态和兼容矩阵，不能通过未声明的隐式语义实现。

### 10.2 占位符

当前版本允许：

- `{user}`：Android 用户对应的 emulated storage ID。
- `{package}`：当前应用包名。

禁止环境变量、命令替换和任意模板表达式。未来扩展占位符必须增加 schema 版本并经过编译器白名单校验。

### 10.3 包和进程匹配

- 包名必须精确匹配安装包身份，不只依赖可伪造的 cmdline。
- `processes: ["*"]` 表示包所属的主进程和 `package:*` 子进程。
- 非包名前缀的自定义进程必须显式列出。
- UID、包名和进程名必须联合校验。
- shared UID 应用必须显式处理，默认拒绝生成策略。

### 10.4 路径输入与 SAF

规则的 `source` 和 `target` 必须是经过规范化的真实文件系统路径。`content://` URI、云盘文档和虚拟 DocumentsProvider 条目不能可靠转换为真实路径，不能直接写入策略。

管理 App 可以使用 SAF 选择器辅助用户选择或验证目录，但必须经过 `PathResolver` 明确转换；无法转换时返回 `unsupported-provider`，不得猜测 `/sdcard` 路径。初始管理 App 提供规范化路径输入和 daemon 侧受控目录浏览器，SAF 仅作为可选验证工具。

### 10.5 规则语言扩展契约

规则格式需要支持长期演进，但不允许通过“猜测未知语法”保持兼容。扩展遵循以下约束：

- `schema` 是整个源文件的格式版本；不兼容变更必须递增主版本。
- 新动作使用明确的动作标记或键名，并在 capability 中声明所需执行后端。
- daemon 遇到未知动作、未知键或不支持的 capability 时拒绝编译，并保留上一份有效快照。
- 已发布的规则语义必须保持向后兼容；语法迁移由 `pathguardctl migrate` 或 Manager 显式完成。
- 规则 IR 分离用户语法与执行后端，使同一条规则未来可以编译为 mount、provider 或其他受支持后端，而不让 Zygisk 解析用户格式。
- 扩展能力必须分别定义安全边界、失败模式、状态值、性能预算和真机兼容矩阵。

## 11. 策略编译与验证

编译器必须拒绝：

- 非绝对路径和包含 `.`、`..`、NUL 的路径。
- 源路径等于目标路径。
- redirect 环，如 `A -> B -> A`。
- redirect 目标位于源目录内部。
- 同一应用、用户、进程下的重复或矛盾规则。
- deny 父目录下的 redirect 子规则。
- 源和目标路径类型不一致。
- 超过长度、应用数或规则数上限的配置。
- 未知字段和未知 schema 版本。

编译器构建规则依赖图：

- 父 redirect 必须先于其视图内的子规则应用。
- deny 覆盖其子树时，子规则视为冲突而不是隐式失效。
- 无法得到唯一挂载顺序时拒绝发布。

路径验证尽量使用目录文件描述符和 `openat2` 的约束解析能力；设备不支持 `openat2` 时使用经过审计的 fallback，并在能力报告中体现。

## 12. 二进制策略格式

源配置不在应用启动路径解析。daemon 将 `rules.ini` 编译为只读快照：

`policy.bin` 是单一的全局策略快照，不按应用、进程或 ABI 分别生成。它包含所有应用策略的包索引和挂载规则；Zygisk 只 mmap 同一个文件并按包名查找，不在应用启动时重新解析 `rules.ini` 或重新编译策略。

```text
PolicyHeader
|-- magic
|-- format_version
|-- schema_version
|-- generation
|-- file_size
|-- checksum
|-- package_count
|-- package_index_offset
|-- rule_table_offset
`-- string_table_offset

PackageIndex[]
|-- package_hash
|-- package_name_offset
|-- first_rule
`-- rule_count

MountRule[]
|-- action
|-- flags
|-- source_offset
`-- target_offset
```

要求：

- 所有整数固定宽度并声明字节序。
- 所有 offset 和 count 在使用前做边界校验。
- 包索引按 hash 和包名排序，使用二分查找。
- 文件通过 `mmap(MAP_PRIVATE)` 只读访问。
- 发布流程为临时文件、`fsync`、校验、原子 `rename`。
- 旧 generation 在已有进程完成请求前保持可读。
- 损坏或版本不兼容的快照不得部分加载。

每次配置变化最多触发一次全局编译：

```text
rules.ini changed
    -> compile once
    -> policy.bin.tmp
    -> validate + fsync
    -> atomic rename to policy.bin
```

原子替换后，已经 mmap 旧 inode 的进程仍可完成 generation N 的读取，新进程打开路径后读取 generation N+1。daemon 可以保留最近 generation 的引用元数据用于诊断，但不为每个应用复制完整快照。

初期可先使用简单自定义格式；只有出现真实的跨语言消费者需求时才考虑 FlatBuffers。避免为未来可能性引入复杂依赖。

## 13. 控制协议

控制面与应用启动面使用两套协议：

- `control protocol`：daemon、CLI、管理 App 之间的低频管理请求。
- `bootstrap protocol`：Zygisk 模块与 Root companion 之间的应用启动关键路径请求。

两者不能共享未区分的消息类型、错误码或超时策略。

控制面协议版本化。第一版可采用长度前缀的本地 Unix Domain Socket 消息，但普通 Android App 直连模块目录 socket 需要额外 sepolicy，不能仅依赖文件权限和 `SO_PEERCRED`。在此之前由管理 App 通过 Root 授权调用 `pathguardctl`。

每个请求包含：

- 协议版本。
- 请求类型。
- request ID。
- payload 长度。
- 调用方 UID/PID（以 socket credential 为准，不信任 payload 声明）。

核心请求：

- `COMPILE_CONFIG`
- `GET_STATUS`
- `GET_CAPABILITIES`
- `EXPLAIN_POLICY`
- `APPLY_PROCESS_POLICY`
- `GET_EVENTS`

Companion 请求与普通控制请求使用不同 socket 或不同不可混淆的消息类型。

跨 Rust、C++、Kotlin 的消息必须定义 canonical wire format、golden vectors 和 fuzz 用例。若不引入 IDL，至少要保留独立的协议规范和三端编解码一致性测试，不能靠手工复制结构体维持兼容。

## 14. 状态模型

模块和每个应用策略使用明确状态：

```text
disabled
ready
unsupported
pending_restart
applying
active
failed
stale
```

`active` 只能在 companion 确认全部挂载完成后设置。配置文件存在、Zygisk 已加载或 daemon 正在运行都不能单独推导出 `active`。

规则编辑器使用两层状态，不把草稿规则误报为运行时已生效：

```text
应用级：draft / valid / applying / active / pending_restart / failed / stale
规则级：unchanged / new / modified / invalid / conflict
```

挂载计划按应用执行事务。任一规则挂载失败时，默认回滚本次计划，应用状态为 `failed`；不能在没有明确证据时把同一计划中的部分规则标记为 `active`。

每次状态至少携带：

- daemon 当前发布 generation。
- 目标进程已应用 generation。
- 草稿 generation（如存在）。
- 最后成功应用时间。
- 目标 PID 和进程 start time。
- 成功规则数。
- 失败规则 ID。
- 稳定错误码和可读错误信息。

## 15. 热更新语义

“策略热发布”和“运行中 namespace 热应用”是两种不同能力。

配置保存后：

1. daemon 对整个配置编译一次并原子发布新 generation。
2. 新启动的目标进程自动使用新策略。
3. 已运行进程仍保留旧 generation 对应的实际 mounts，直到 live apply 成功或进程重启。

`policy.bin` 的原子替换不会自动修改已有 mount namespace。运行中进程按规则差异分类：

| 策略变化 | 新启动进程 | 已运行进程默认行为 |
|---|---|---|
| 新增 deny | 使用新 generation | 可选 live apply，完成前存在访问竞态 |
| 新增 redirect | 使用新 generation | 可选 live apply，要求目标已存在且校验通过 |
| 删除 deny | 使用新 generation | `pending_restart` |
| 删除 redirect | 使用新 generation | `pending_restart` |
| 修改 redirect 目标 | 使用新 generation | `pending_restart` |
| 编译失败 | 继续使用旧 generation | 保持旧 mounts |

第一版保证策略热编译和新进程即时使用最新 generation。删除规则、改变已有 redirect 或无法安全增量应用的变化，通过 `force-stop + restart` 完成；不承诺所有变化无感热应用。

可选 live apply 由 daemon fork helper 进入现有进程 namespace，按应用级事务执行新增挂载。成功后更新该 PID 的 applied generation；失败则回滚本次新增挂载并保持旧 generation 状态。

完整 live apply 在支持删除和修改规则前必须解决：

- 旧挂载安全卸载。
- 被占用目录。
- 规则删除后的真实视图恢复。
- 部分失败回滚。
- 多线程进程在切换期间的视图一致性。

在这些问题有完整事务模型前，不实现“伪热更新”。

## 16. 事务与失败处理

### 16.1 配置事务

- 新配置必须完整验证后才能替换旧快照。
- 编译失败时继续使用上一份有效策略。
- 管理 App 展示编译错误，不修改 active generation。

### 16.2 挂载事务

Linux mount 操作不是天然的多操作事务。helper 应：

1. 在应用规则前记录当前 mount IDs。
2. 按计划执行挂载。
3. 任何一步失败时逆序卸载本次已完成挂载。
4. 回滚失败则将进程标记为 `failed`，严格模式下终止进程。

### 16.3 重启恢复

- namespace 挂载随应用进程退出自动消失。
- daemon 重启不应影响已安装的应用视图。
- 设备重启后 daemon 重新验证配置并发布策略。
- 不需要扫描或恢复历史 fanotify 任务。

## 17. 性能设计

### 17.1 热路径

规则安装后，应用文件调用不经过 PathGuard 用户态代码，因此禁止引入：

- libc PLT Hook。
- 每次访问日志。
- 路径匹配锁。
- fanotify 事件处理。
- 用户态代理文件系统。

### 17.2 启动路径

- `policy.bin` 使用 mmap，不反序列化为对象树。
- 包策略使用排序索引二分查找。
- 不命中策略的应用只进行头部校验和一次查询。
- 命中策略的应用只生成固定大小的挂载请求。
- companion 使用预分配缓冲区和有界消息。

### 17.3 初始性能目标

| 指标 | 目标 |
|---|---:|
| 未配置应用的 Zygisk 额外耗时 P95 | `< 0.5 ms` |
| 配置应用策略查询 P95 | `< 0.1 ms` |
| 16 条规则挂载完成 P95 | `< 10 ms` |
| 1000 条规则编译耗时 | `< 10 ms` |
| Zygisk 模块卸载后额外常驻内存 | 接近 0 |
| daemon 稳态 RSS | `< 3 MiB` |
| 文件访问额外用户态调用 | `0` |

所有目标必须由基准数据验证。未测量前不得在 README 中作为既成性能结果发布。

### 17.4 性能曲线而非单点

挂载延迟主要由 `setns`、路径解析和 mount syscall 决定，不能仅用一组固定的 16 条规则代表所有设备。基准必须分别报告：

- 策略规模：1、10、100、1000、10000 个应用策略。
- 单应用规则数：0、1、4、16、32、64 条。
- 操作类型：deny bind、redirect bind、recursive bind、只读 remount、失败回滚。
- 冷热状态：首次冷启动、缓存后启动、daemon 重启后首次请求。
- 设备档位：低端、中端、旗舰设备。
- 存储实现：FUSE、可获得的旧 sdcardfs 设备、OEM 定制实现。

每组至少输出 P50、P95、P99、最大值和样本数。额外记录以下原子成本：

```text
T_lookup          policy 查找
T_unshare         namespace 创建
T_companion       IPC 与 helper 创建
T_setns           进入目标 namespace
T_mount_one       单条 bind mount
T_remount_one     单条 remount
T_rollback_one    单条失败回滚
T_total           对应用启动的总影响
```

`16 条规则挂载 P95 < 10 ms` 保留为早期目标，但不是发布硬承诺。Phase 0 测量后按设备档位制定预算；若内核操作本身超过预算，应限制单应用规则数或调整产品预期，不能通过省略校验换取数字。

## 18. 可观测性

只记录控制面事件：

- 配置编译成功或失败。
- 能力探测结果。
- 应用策略匹配。
- namespace 创建。
- 每条挂载规则成功或失败。
- 回滚和失败模式结果。

不记录目标应用每次文件访问。日志采用结构化单行格式，包含时间、generation、package、pid、rule ID、event、result 和 error code。

日志必须有大小上限和轮转策略，默认不包含用户文件名之外的额外敏感内容；规则路径是否脱敏由诊断级别控制。

## 19. 能力探测

安装后和每次 daemon 启动时执行：

- Zygisk/companion 是否可用。
- Root UID 和所需 capabilities。
- `unshare(CLONE_NEWNS)`。
- `/proc/<pid>/ns/mnt` 和 `setns()`。
- 挂载传播隔离。
- `/data/media` 或 emulated storage 上的 bind/rbind/remount。
- SELinux 是否阻止关键操作。
- 主用户和次用户存储路径映射。

探测必须在临时 namespace 中执行，不得修改真实应用或全局挂载树。

探测结果形成稳定的 capability bitset，供 CLI 和管理 App 使用。

纸面调研得到的初始结论：

- AOSP 已使用每应用 namespace、bind mount 和 `vold setns` 更新存储视图，证明机制在平台中有先例。[S1][S3]
- AOSP SELinux 策略为 zygote 明确授予 `sys_admin`、`mounton`、tmpfs mount/unmount 等有限权限，说明成功不仅依赖 Linux capability，也依赖具体 SELinux domain 和对象标签。[S4]
- KernelSU 的“Umount modules”行为受内核版本和实现能力影响；官方文档说明旧于 5.10 的内核可能需要回移 `path_umount` 才执行实际卸载。[S5]
- NeoZygisk 的公开设计同时使用直接卸载和缓存 clean namespace + `setns`，并公开记录过 SELinux 规则未生效等设备/Root 实现差异。[S11]

这些资料支持技术方向，也证明不能用“Android 12+”替代实际 capability probe。

## 20. 测试策略

### 20.1 单元测试

- JSON schema 和错误定位。
- 路径规范化及恶意输入。
- 占位符展开。
- 包、进程和用户匹配。
- 冲突、环和依赖图检测。
- 二进制快照读写及损坏输入。
- 挂载计划排序和回滚计划。

### 20.2 Host 集成测试

- daemon 控制协议。
- 原子发布和 generation 切换。
- 单个 `policy.bin` 被多个模拟进程共享 mmap，并按包索引读取不同策略。
- 一次配置编译只产生一个全局快照，不随应用启动重复编译。
- 并发读取旧快照与发布新快照。
- helper 请求身份校验。
- 模拟 mount executor 的部分失败回滚。

### 20.3 Android 真机测试

至少验证：

- Java `File`、流和 NIO API。
- libc `open/openat/stat/statx/opendir/readdir`。
- 直接 syscall。
- `mmap`、rename、link 和子进程。
- `/sdcard` 与 `/storage/emulated/<user>` 路径别名。
- 主进程和 `package:*` 子进程。
- 多用户和工作资料。
- 应用崩溃、daemon 崩溃和设备重启。
- fail-open 和 fail-closed。
- 目标进程读取 `/proc/self/mountinfo` 和 `/proc/self/mounts` 时的可见结果。
- 至少一个会进行 mount/root 完整性检查的支付或银行类 App。
- 至少一个带反作弊或强环境检测的游戏；只记录兼容结果，不研究绕过其安全机制。
- MediaStore 查询、系统相册和 SAF 文件选择器与直接路径视图之间的差异。

初始设备矩阵：

| 维度 | 最低覆盖 |
|---|---|
| Android | 12、14、16 |
| ROM | AOSP/Pixel、HyperOS、ColorOS 或 One UI |
| Root | Magisk、KernelSU + ZygiskNext |
| ABI | arm64-v8a 为发布门槛，其余按需求评估 |

## 21. 安全要求

- 不信任管理 App payload 中的 UID、PID 和包名。
- 使用 `SO_PEERCRED` 或等价机制校验本地客户端。
- companion 请求必须校验进程 start time，避免 PID 复用。
- mount 目标必须位于允许的存储根目录中。
- 所有数组长度、offset、消息长度和路径长度都有硬上限。
- 不执行 shell 命令拼接。
- 不加载管理 App 提供的任意 Native 代码。
- 配置文件和策略快照必须具有严格所有者和权限。
- daemon 仅保留必要 capabilities，并记录 capability 探测结果。

## 22. 构建与工程约束

- `core` 的编译器、验证器和策略快照生成器可使用 Rust；`daemon` 和 `cli` 也可以使用 Rust，但必须用同步 IO 或轻量事件循环，不引入没有必要的异步运行时。
- Zygisk `.so` 保持 C++20 薄实现，只包含策略快照 reader、身份提取和 companion client，不将完整 Rust core 静态链接进每个应用进程。
- 如果 Rust 出现在 Zygisk 边界，crate 必须 `panic = "abort"`，FFI 入口不得允许 panic、异常或分配失败穿过边界；`catch_unwind` 不能用于捕获 abort panic。
- Native 代码使用 C++20，但避免异常和 RTTI进入 Zygisk 热启动组件。
- 依赖必须说明体积、许可证和更新策略。
- 核心库应可在 Host 上构建并运行测试。
- Android 专属系统调用封装在独立适配层。
- 错误使用稳定枚举，不以日志文本作为协议。
- 不在一个源文件中混合规则解析、Zygisk 生命周期和 mount 执行。
- 第一阶段仅以 `arm64-v8a` 作为功能验证目标，验证通过后再扩展 ABI。

## 23. 旧项目迁移策略

可以参考或迁移：

- 已验证的外部存储路径规范化测试。
- 包名与子进程匹配用例。
- 规则冲突和边界路径测试思想。
- Magisk 模块打包流程中的通用部分。

不迁移：

- `folder_manager.cpp` 的 PLT Hook 和 Binder Hook。
- `folder_manager_daemon.cpp` 的 fanotify、移动、删除和导出逻辑。
- 重复的 daemon 规则匹配器。
- 当前扩张过度的管理 App 功能。
- 与新状态模型不一致的配置和日志协议。

迁移必须以测试用例或重新设计后的独立组件为单位，不复制整个旧文件。

## 24. 实施路线

### Phase 0：技术可行性验证

Phase 0 分为先行调研和真机原型，不允许直接跳到产品功能开发。

#### Phase 0A：平台与生态预研

1. 阅读目标 Android 版本的 Zygote 存储挂载、`vold` namespace 更新和 SELinux 策略。
2. 汇总 Magisk、KernelSU、ZygiskNext/NeoZygisk 对 mount namespace、模块卸载和 sepolicy 的已知差异。
3. 为候选设备记录内核版本、存储实现、Root 方案、SELinux enforcing 状态和相关已知问题。
4. 基于资料将设备矩阵分为“高概率可行”“需验证”“已知不支持”，减少无效测试。
5. 输出 `docs/01-platform-research.md`，记录来源、结论置信度和仍需实测的假设。

Phase 0A 只能收窄范围，不能替代真机验证。Issue 和社区实现不能作为支持承诺。

#### Phase 0B：真机最小原型

只实现最小原型：

1. Zygisk 在目标应用派生时创建独立 mount namespace。
2. 在 `preAppSpecialize` 建立 companion 通道并同步等待结果；`postAppSpecialize` 只做收尾和卸载。
3. Root helper 使用 `pidfd` 优先、`/proc/<pid>/ns/mnt` fallback，进入目标 namespace。
4. 对共享存储应用一条 deny bind mount。
5. 应用一条 redirect bind mount。
6. 验证直接 syscall、路径别名、子进程和 SELinux 行为。
7. 对一个已运行目标进程新增一条挂载，验证可选 live apply 的事务、回滚和竞态窗口；本阶段不实现删除或修改已有挂载的热应用。

放行条件：

- 至少一台 Android 12 和一台 Android 14/16 arm64 真机通过。
- 挂载不污染 zygote、系统服务和其他应用。
- 应用代码执行前规则已生效。
- 进程退出后挂载自动回收。
- 不存在已知的普通应用可用路径别名绕过。
- mountinfo 痕迹已被记录，并完成至少两类强校验应用的兼容性观察。
- MediaStore/SAF 与直接路径访问的差异已形成可复现测试和用户文案。
- 记录 1、4、16、32、64 条规则的单条挂载成本和总启动延迟曲线。
- 验证 companion 请求具备 PID start time、UID/package 和策略 generation 联合校验。
- 验证 live apply 成功后按 PID 更新 applied generation，进程退出后状态记录可被可靠回收。

Phase 0 未通过时，暂停产品开发并重新评估后端，不先建设 GUI。

### Phase 1：核心策略与 CLI

- 建立 `core`。
- 完成 schema、验证器和二进制快照。
- 验证每次配置变化只编译并原子发布一个全局 `policy.bin`，多个进程共享 mmap 后按包索引读取。
- 完成 daemon、companion 协议和 CLI。
- 建立 Host 单元测试和集成测试。

### Phase 2：模块生命周期

- 完成安装、升级、禁用和卸载。
- 完成能力探测、状态模型和故障恢复。
- 建立设备自动化测试脚本。

### Phase 3：最小管理 App

- 应用选择。
- deny/redirect 编辑。
- 编译错误展示。
- active/failed/pending_restart 状态。
- 诊断报告。

### Phase 4：兼容性与发布

- 扩展 ROM、Android 版本和 Root 方案矩阵。
- 完成性能基准和长时间稳定性测试。
- 基于实测结果确定支持列表。

## 25. 首批架构决策记录

| ID | 决策 | 状态 |
|---|---|---|
| ADR-001 | 新项目不兼容旧项目内部架构 | Accepted |
| ADR-002 | mount namespace 是唯一核心执行后端 | Proposed，等待 Phase 0 验证 |
| ADR-003 | 初始执行后端先实现目录级 deny 和 redirect，规则语言保留可版本化扩展 | Accepted |
| ADR-004 | 用户源文件使用严格、版本化 `rules.ini`；运行时使用二进制快照 | Accepted |
| ADR-005 | 不实现隐式 PLT Hook fallback | Accepted |
| ADR-006 | 新增规则可选 live apply；删除规则或修改已有挂载默认重启应用 | Accepted |
| ADR-007 | CLI 先于管理 App 实现 | Accepted |
| ADR-008 | 不隐藏 `/proc/*/mountinfo` 中的 PathGuard 挂载 | Accepted |
| ADR-009 | 不依赖 SUSFS；定制内核能力只作为诊断信息 | Accepted |
| ADR-010 | MediaStore/SAF 不属于基础文件系统后端的保证范围 | Accepted |
| ADR-011 | Phase 0 先做平台预研，再做真机原型 | Accepted |
| ADR-012 | `policy.bin` 是单一全局、原子发布的 generation 快照 | Accepted |
| ADR-013 | 策略热发布与运行中 namespace 热应用是两项独立能力 | Accepted |

## 26. 待验证问题

以下问题必须通过原型和真机数据回答：

1. Zygisk 回调时序中，何处创建 namespace 能兼容 Android 存储挂载初始化。
2. companion 在不同 Root 方案和 SELinux 环境下进入应用 mount namespace 的成功率。
3. FUSE、sdcardfs 和 OEM 存储实现是否允许目标 bind/rbind/remount 操作。
4. deny 源目录权限在不同存储实现中的最终可见行为。
5. `/mnt/user`、`/mnt/runtime`、`/storage/self` 等别名是否需要同时挂载。
6. Android 多用户和工作资料的路径及 UID 映射。
7. shared UID、isolated process 和 WebView sandbox 进程如何定义策略归属。
8. 应用启动阻塞等待 companion 的合理超时和失败模式。
9. 目标应用读取 mountinfo 后的兼容行为，以及哪些应用类别需要明确警告。
10. 每个 ROM 上 MediaStore/SAF 与直接文件路径视图的具体差异。
11. 单条 mount syscall 延迟随规则数、存储实现和设备性能的变化曲线。
12. 对已运行进程 live apply 新增挂载时，从策略发布到挂载完成之间的访问竞态窗口是否可接受。
13. daemon 按 PID 记录 applied generation 时，如何可靠处理进程退出、PID 复用和状态回收。
14. 删除或修改已有挂载时，安全卸载、挂载依赖顺序和已打开文件描述符的一致性边界。

这些问题解决前，项目处于架构验证阶段，不发布稳定版本承诺。

## 27. 完成定义

PathGuard Next 的核心版本只有在满足以下条件后才算完成：

- deny 和 redirect 语义与本文一致。
- 文件访问不存在 PathGuard 用户态热路径。
- 所有支持设备通过真机访问矩阵。
- 状态能够准确区分 active、unsupported 和 failed。
- 配置错误不会破坏上一份有效策略。
- 挂载部分失败可以回滚或终止目标进程。
- CLI 能独立完成配置验证、状态查询和诊断。
- 性能数据来自可复现基准，不使用推测值。
- README 的能力声明不超过威胁模型和实测支持范围。

## 28. 外部资料与核验结论

本节记录架构判断所依据的外部资料。平台官方文档和源码优先级高于项目 README、Issue 和社区文章。

| ID | 来源 | 支撑的结论 |
|---|---|---|
| S1 | [AOSP Storage](https://source.android.com/docs/core/storage) | Android 在 Zygote fork 时为应用创建 mount namespace，并由 vold 通过 setns 更新存储视图 |
| S2 | [AOSP Scoped storage](https://source.android.com/docs/core/storage/scoped) | Android 11+ 的 FUSE/MediaProvider 关系，以及直接路径与 provider 路径的性能和语义差异 |
| S3 | [AOSP vold VolumeManager.cpp](https://android.googlesource.com/platform/system/vold/+/refs/heads/main/VolumeManager.cpp) | vold 进入应用 namespace 并执行 tmpfs/bind mount 的平台实现先例 |
| S4 | [AOSP zygote SELinux policy](https://android.googlesource.com/platform/system/sepolicy/+/master/private/zygote.te) | zygote mount/setns 能力依赖明确的 SELinux 权限和标签 |
| S5 | [KernelSU App Profile](https://kernelsu.org/guide/app-profile.html) | Umount modules 的内核版本条件和 Root 实现差异 |
| S6 | [Linux mount_namespaces(7)](https://man7.org/linux/man-pages/man7/mount_namespaces.7.html) | `/proc/<pid>/mountinfo` 对应目标 PID 的 mount namespace 视图 |
| S7 | [Linux proc_pid_mountinfo(5)](https://man7.org/linux/man-pages/man5/proc_pid_mountinfo.5.html) | mountinfo 暴露 mount ID、父子关系、bind root 和 mount point |
| S8 | [Storage Isolation Changelog](https://sr.rikka.app/changelog/) | Media Storage、厂商 ROM、Android 版本和应用交互带来的长期兼容维护面 |
| S9 | [Storage Isolation Guide](https://sr.rikka.app/guide/) | MediaStore、SAF、可访问目录和导出功能产生的用户预期差异 |
| S10 | [SUSFS4KSU module](https://github.com/sidex15/susfs4ksu-module) | SUSFS 用户态模块明确要求经过 SUSFS patch 的定制内核 |
| S11 | [NeoZygisk](https://github.com/JingMatrix/NeoZygisk) | Root 生态使用 mount namespace/setns 清理应用视图，以及 fallback 的现实必要性 |

核验后的总体判断：

- mount namespace + bind mount 是有 AOSP 实现先例的正确主方向。
- 该方向解决的是目标进程文件系统视图一致性，不解决 provider 代理访问和挂载隐身。
- 项目最大的工程风险是 SELinux、Root 实现、OEM 存储布局和应用自检兼容性，而不是规则匹配算法。
- 性能优势来自消除文件访问用户态热路径；应用启动阶段的 mount 成本仍必须按规模测量。
- 不应为了追求旧方案的功能数量，把 Binder/MediaStore 兼容逻辑重新塞回核心。
