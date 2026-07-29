# PathGuard Next 通用路径模式匹配与动作路由设计

> 状态：提案 / Draft
>
> 文档版本：0.8
>
> 日期：2026-07-29
>
> 适用范围：Android 12+、Magisk Zygisk / KernelSU + ZygiskNext

## 1. 摘要

PathGuard 当前的 `redirect = ["A" -> "B"]` 是目录前缀映射。它适合把一个完整子树
绑定到另一个目录，但不能表达以下需求：

```text
Pictures/**/*.jpg       -> Download/images
Pictures/**/*.png       -> Download/images
Pictures/**/IMG_*.heic  -> Download/camera
Download/*.apk          -> Download/packages
```

本设计新增独立的 Pattern Engine。glob 只负责回答“路径是否匹配”，不属于 redirect
实现；deny、redirect、observe、export 等功能都通过同一个 selector 调用它。这样可以
按文件名/后缀分流，也可以用同一模式拒绝访问或记录事件，同时保留现有字面量目录
redirect 的稳定语义。核心原则如下：

1. 字面量目录 redirect 仍由 mount namespace/VFS 提供同步语义。
2. 文件名和扩展名 glob 是动态路由，不伪装成 bind mount。
3. TOML 只在编译期解析；运行时只消费受校验的 Pattern IR、Action IR 和 `policy.bin`。
4. deny 优先于任何 redirect；可验证的 UID、user 和 package attribution 在匹配前确定，
   不用包名猜测补全缺失身份。
5. fanotify 只用于 observe/export 等异步动作，不能作为同步 redirect 的实现。
6. mount、app-path、Provider/SAF、complete VFS 和 event 是五个执行域；它们共享规则语义，
   但必须分别报告能力、操作覆盖和降级状态。

首个可交付版本优先实现 Provider/常用 libc 路径边界，保持当前目录 deny/redirect 的核心行为；
mount 内部接口和 plan 表达允许随统一 IR 重构；
只有在设备证明具备稳定 FUSE/VFS 动态能力时，才开启全路径目录视图。

### 1.1 事实来源与决策状态

本文不是把 `docs/adr` 汇总成一份更长的规范。项目处于未发布开发阶段，需要区分两类问题：

- 判断“当前实际做了什么”：以 `main` 可执行代码、自动化测试、真机日志和可复现行为为准；
- 判断“目标应该做什么”：以最新需求和核心业务闭环为首要约束，以 Android/Linux/POSIX 官方
  契约为平台边界，再用当前实验数据验证；ADR、现有接口和参考项目都允许修订或替换。

因此 ADR 与代码都不是不可推翻的权威。ADR-0002 记录过 policy format v4，而当前代码执行 v5，
只能说明事实已经演进；同理，当前 v5、`file_picker` 和 prefix mapper 也只是改动前基线，不能
阻止 format 6、统一 Action IR 或 Provider contract 的根因重构。

| 层级 | 当前状态 | 本文用法 |
| --- | --- | --- |
| 已实现基线 | rules TOML format 1；policy format v5/schema 2；literal deny/redirect；`file_picker`；mount 与 Provider literal virtualization | 记录为前测事实；不形成接口兼容承诺 |
| 已接受目标 | ADR-0010～0015 的后端策略、snapshot、capability、Glob v1 和有界反选 | 作为当前目标设计；发现与最新需求/平台事实冲突时必须修订 |
| 已接受目标 | ADR-0016 policy format 6/schema 3 | format 6 首版编码统一 Selector/Action、PatternTable 和有界 selector 差集；不增加 NOT token |
| 已接受目标 | ADR-0017 route provenance | daemon 单写持久 owner；多源反向只接受 strong identity + committed record，不恢复 canonical source fallback |

本文出现“当前”时只指上述已实现基线；出现“目标”“应”或 Phase P0～P5 时指尚待实现和验证的
设计。任何 capability 只有对应代码、probe 和 conformance test 同时成立后才能从目标变成运行时
事实。

### 1.2 关键结论的相互印证

| 议题 | 当前代码 | ADR/官方证据 | 本文结论 |
| --- | --- | --- | --- |
| policy 格式 | v5/schema 2 是唯一 reader | ADR-0002 的 v4 已过时；ADR-0016 已冻结 v6/schema 3 | v5 是前测基线；目标 v6 一次性替换，不要求运行时兼容 |
| Provider 身份 | Binder caller UID；shared UID 按 UID 共享 | ADR-0006 明确 package attribution 未完成；Binder/SAF 不保证自动给出唯一 package | UID/user 是最低安全 scope，package 只作可信附加限定 |
| 动态能力位 | 只有 bit 0～4、8～11 | ADR-0012/0013 接受 bit 16～19，但注明分阶段实现 | 目标协议已决，运行时能力尚未实现 |
| Provider 操作 | literal path virtualization + 部分 query deny | DocumentsProvider 将 query/create/open 分成不同接口 | glob Provider action 必须 composite admission |
| FUSE | 存在设备相关兼容 Hook，不构成 complete probe | AOSP Mainline 可更新；passthrough 可绕过后续 read/write daemon path | 按当前模块/内核 probe，并在 open/create 固定 FD route |
| snapshot | specialize 时独立读取当前 policy | ADR-0001 延期 Zygote mmap；ADR-0011 接受进程内 hazard pointer | 每进程 immutable snapshot，seq_cst 首版，不改变 ADR-0001 |
| 反选 | 当前代码尚未支持 glob/except | ADR-0014 接受字符类补集；ADR-0015 已 Accepted | `[!abc]` 属于 v1；selector except 已进入 ADR-0016 的 format 6/schema 3 |

### 1.3 未发布阶段的改动与验收原则

本设计允许破坏 rules schema、policy format、C/C++ 接口、状态字段和模块边界。是否允许破坏不由
“旧接口是否存在”决定，而由核心闭环能否在新实现中重放决定。

核心业务闭环冻结为行为场景，而不是旧类型或旧字节布局：

| ID | 核心场景 | 最低成功条件 |
| --- | --- | --- |
| C1 | app-scoped literal deny | 目标 UID/user 对受限目录得到 `EACCES`，其他应用不受影响 |
| C2 | app-scoped literal redirect | 目标应用访问 visible path 时实际落到 backing path，读写/rename/delete 一致 |
| C3 | LocalSend/Provider 代写 | Binder caller UID 归因正确，接收文件落到规则目标，Provider 查询和实际 FD 一致 |
| C4 | 多源到同一目标 | 每个合法源都能前向写入；同名碰撞确定性 reject；反向歧义不伪装成唯一映射 |
| C5 | 故障隔离 | 未命中 UID 透传；能力缺失 fail-open；active deny 和 collision 不被错误 fail-open |
| C6 | 新增 glob 路由/deny | 按文件名、后缀和目录分量匹配，作用域、优先级和执行域符合本文语义 |

旧 TOML 拼写、v5 row offset、类名、CLI 参数、内部 Hook 函数和 canonical reverse 的具体展示结果
属于非核心接口，可以计划内删除或重写。若某项次要功能妨碍核心设计，可以降级为明确的
`unsupported`，但不得伪装为 active。

任何影响 C1～C6、policy/compiler、mount transaction、Provider/FUSE Hook 或 capability admission
的改动必须执行同场景前后对比：

1. **改动前**：在当前提交运行受影响的 host tests，并在适用设备记录规则、UID/user、操作步骤、
   文件最终位置、errno、runtime status、关键日志、policy/模块 hash 和设备环境；
2. **改动后**：使用语义等价的新配置在相同设备或明确记录的等价环境重放相同步骤；
3. **对比**：逐项标记 `unchanged`、`planned_break` 或 `unexpected_regression`；接口/schema 变化只能
   归为 planned break，核心结果变化默认视为 unexpected，除非最新需求明确改变该结果；
4. **准入**：存在任何未解释的核心回归时禁止进入下一 Phase；修复后必须重新完整重放，不能只
   补跑失败步骤。

Host 前测写入 `tests/baseline/` 的清单/报告；真机证据沿用 `tests/device/` 的步骤说明和
`build/device-evidence/<change-id>/` 原始产物。构建产物不提交时，受版本控制的报告仍必须记录
命令、hash、结果摘要和证据路径。破坏性变更必须在同一 change set 更新测试、配置样例、共享
格式头、状态解析和相关 ADR/设计文档，禁止先合入不一致的半套协议。

## 2. 背景与当前实现边界

### 2.1 当前代码的实际语义

当前 `rules.toml` 仍是 `format = 1`，规则模型中的 `RedirectRule` 只有 `source` 和 `target`
两个规范化路径。宿主编译器生成唯一可执行的 `policy.bin` format v5/schema 2；其表只有
Package、MountRule、EventRule 和 String，动态 selector/action/token 表尚不存在。运行时
`provider_path_mapper` 以组件边界检查做最长前缀匹配，并把剩余 tail 拼接到目标路径。
因此：

```text
Pictures -> Download/redirect
Pictures/Album/a.jpg -> Download/redirect/Album/a.jpg
```

这不是 glob，而是整棵目录树的 prefix mapping。现有 mount executor 同样只能对准备好的
目录执行 bind；单个文件或一个目录中的部分文件无法通过一次 bind 表达。

当前 Provider Hook 已具备以下基础能力：

- 通过 Binder JNI 读取真实调用方 UID，而不是把 Provider 自身 UID 当作规则主体；
- 对 `open/stat/access/opendir/mkdir/rename` 等 libc 边界做路径虚拟化；
- 按 user 和 caller UID 隔离规则；
- 规则不命中时 fail-open。

Provider rule load 当前通过每个 user 的 `/data/user/<user>/<package>` owner UID 解析 caller
scope；shared UID 因而按 UID 共享语义，而不是已经完成 package attribution。Hook 当前对
`MediaProvider`/`ExternalStorageProvider` 进程安装 Binder identity、libc path 和部分 FUSE
兼容入口，并继续使用 `provider_compat = virtualize`。这些真机验证过的 literal 能力可以作为
新的 Pattern Engine/ActionEvaluator 的执行适配器，但不能直接扩展现有 `PathRule` 的字符串
前缀逻辑来实现 glob，也不能据此宣称 query/insert/reverse scan 或完整 FUSE glob 已准入。

当前稳定 capability 代码只定义 bit 0～4 和 8～11。ADR-0012/0013 分配的 bit 16～19 是已接受
但尚未进入共享头、probe 和 status 的目标协议；在 P1 落地前不得由日志中的“hook installed”
替代这些位。

### 2.2 需求定义

对每个应用、每个 Android user，用户可以声明一个通用 selector，再把它交给一个或多个
动作：

- `root`：匹配范围的目录；
- `pattern`：相对于 root 的 glob；
- `to`：redirect/export 动作的目标目录；
- `preserve`：redirect/export 是否保留 root 下的相对目录和文件名；
- `priority`：相同范围规则的显式优先级。

例如：

```toml
format = 2

[apps."org.example.transfer"]
users = [0]
provider = { enabled = true }

redirect = [
    "Download/incoming" -> "Download/incoming-redirect",
]

deny_rules = [
    {
        select = { root = "Pictures", glob = "**/private-*" },
        enforcement = "provider",
    },
]

redirect_rules = [
    { select = { root = "Pictures", glob = "**/*.jpg"  }, to = "Download/images" },
    { select = { root = "Pictures", glob = "**/*.jpeg" }, to = "Download/images" },
    { select = { root = "Pictures", glob = "**/IMG_*.png" }, to = "Download/camera" },
    { select = { root = "Download", glob = "*.apk" }, to = "Download/packages" },
]
```

`select` 是通用路径选择器；`deny_rules` 和 `redirect_rules` 只是不同动作表。以后新增
`observe_rules` 或 `export_rules` 时复用相同的 selector decoder、Pattern IR 和 matcher，
不复制一套 glob 语法。

匹配结果保留相对 tail：

```text
Pictures/Album/a.jpg
 -> Download/images/Album/a.jpg

Pictures/2026/IMG_001.png
 -> Download/camera/2026/IMG_001.png

Download/tool.apk
 -> Download/packages/tool.apk
```

首版不支持通过 pattern 重命名文件。若将来需要重命名，应增加独立的
`target_template`，不能把 `to` 同时解释为目录和模板。

## 3. 参考项目与外部资料

### 3.1 Storage Redirect X

Storage Redirect X 将 `path_mappings` 定义为最长 `request_path` 前缀映射，并明确把
`*`、`?` wildcard 限定在 `allowed_real_paths`，不允许用于 `path_mappings`。这说明目录
mount 映射和文件选择规则是两类能力，不能共用一个字符串字段。

参考：

- [Storage Redirect X README](https://github.com/Kindness-Kismet/Storage-redirection-X-Public)

PathGuard 采用相同能力边界：`LiteralPrefix` selector 保持目录 prefix 语义，glob selector 进入
动态动作；不把 wildcard 塞入箭头字符串。format 1 的旧 `redirect` 拼写可以在 v6 切换时删除，
不影响这条语义边界。

### 3.2 MaterialCleaner

MaterialCleaner 的 `MountRules` 将规则保存为 source/target pair，并根据 mount 顺序计算
最长前缀结果。其 MediaProvider Hook 负责把已知 mount 结果同步到 MediaStore 查询和插入，
FileObserver/事件记录则是兼容和审计用途，不是把写后搬运当作同步重定向。

参考源码：

- `refer/MaterialCleaner-main/shared/src/main/java/me/gm/cleaner/dao/MountRules.kt`
- `refer/MaterialCleaner-main/server/src/main/java/me/gm/cleaner/xposed/MediaProviderHook.java`

可借鉴的是“统一 mount 规则计算”和“Provider 兼容层复用同一结果”，不能照搬其 Java/Xposed
入口来解决 PathGuard 的 native/UID 隔离问题。

### 3.3 Riru Storage Redirect

Riru Storage Redirect 的增强模式长期维护了 Media Storage、shared UID、child zygote 和
Android 版本兼容。这些历史变更表明：Provider、MediaStore 和直接文件路径是不同的访问面，
一个路径 Hook 成功不等于 MediaProvider 查询结果也已经同步。

参考：

- [Riru - Enhanced mode for Storage Isolation](https://github.com/Magisk-Modules-Repo/riru_storage_redirect)

### 3.4 AOSP MediaProvider/FUSE

Android 11+ 的 MediaProvider 通过 FUSE 观察并约束文件操作，FUSE 请求包含调用方 UID/PID，
MediaProvider 也对路径执行按 UID 的 lookup 和 transform。该模型支持按调用方决定路径，但
FUSE/MediaProvider 属于系统模块边界，内部符号和 OEM 行为不能假定固定。AOSP 还明确说明
MediaProvider 是可独立更新的 Mainline 模块，所以不能只按 Android 版本或 OEM 名称推断 ABI；
必须在当前进程和当前模块版本上做语义 probe。

Android 12 的 FUSE passthrough 允许 daemon 在 `open`/`create-and-open` 时授权后，让后续
`read`/`write` 直接进入 lower filesystem。因此 PathGuard 若实现 complete FUSE backend，路由和
身份必须在 open/create 阶段稳定决定并绑定到 handle/FD 生命周期，不能假设每次 read/write 都会
再次进入用户态 matcher。

参考：

- [Scoped storage](https://source.android.com/docs/core/storage/scoped)
- [MediaProvider module](https://source.android.com/docs/core/media/media-provider)
- [FUSE passthrough](https://source.android.com/docs/core/storage/fuse-passthrough)
- [AOSP FuseDaemon.cpp](https://android.googlesource.com/platform/packages/providers/MediaProvider/+/refs/heads/main/jni/FuseDaemon.cpp)

PathGuard 因此必须把 FUSE 作为可选能力，而不是 glob 功能的唯一前提。

### 3.5 Glob 语法资料

Git 和 POSIX pathname pattern 都把 `*`、`?` 和 bracket expression 作为基础匹配能力；Git
另行定义组件级 `**`。PathGuard 吸收这组确定性、可编译的核心语义，但不追求 shell 全兼容：
字符类限定 ASCII bitmap，路径仍可用 UTF-8 字面量和通配符匹配，且不引入 locale、Unicode
normalization 或隐式 dotfile 规则。

POSIX.1-2024 还定义了可选的 `FNM_CASEFOLD/FNM_IGNORECASE`，但 PathGuard v1 有意保持
大小写敏感，以避免把 locale/Unicode case folding 引入 policy canonicalization。这是一种受限
Glob 方言，不宣称与 shell、gitignore 或 POSIX `fnmatch()` 完全兼容。

Bash 明确在 filename expansion 前执行 brace expansion；rsync 命令行中的 brace 通常也由
shell 预处理。这支持把 `{a,b}` 放在宿主规则编译器，而不是 Pattern IR。node-glob 移除 pattern
首字符否定并改用独立 ignore，也印证排除语义应由 action/precedence 表达。CVE-2026-14257
说明枚举展开必须同时限制结果数量和累计字节，超限应拒绝而不能截断。完整语言边界由
[ADR-0014](adr/0014-glob-language-boundary.md) 冻结。

参考：

- [gitignore pattern format](https://git-scm.com/docs/gitignore#_pattern_format)
- [POSIX fnmatch](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/functions/fnmatch.html)
- [Bash brace expansion](https://www.gnu.org/software/bash/manual/html_node/Brace-Expansion.html)
- [rsync manpage](https://download.samba.org/pub/rsync/rsync.1)
- [node-glob comments and negation](https://github.com/isaacs/node-glob#comments-and-negation)
- [CVE-2026-14257](https://www.cve.org/CVERecord?id=CVE-2026-14257)

### 3.6 内核 VFS 项目与生态状态

NoMount、ZeroMount 和 Kasumi（原 HymoFS）证明内核 VFS/namei 层可以实现单文件 redirect、
反向路径、目录项注入和更完整的 syscall 覆盖，但三者当前解决的问题和成熟度不同：

- NoMount 直接 Hook VFS 路径解析和目录遍历，规则接口是字面量 path redirect，并提供
  UID 排除；仓库明确标记为研究用途且可能存在缺陷。
- ZeroMount 以系统模块文件注入为目标，使用自定义内核驱动，并在 VFS 不可用时回退到
  OverlayFS/MagicMount；其 app exclusion 不等于 PathGuard 的 per-app shared-storage action。
- Kasumi（HymoFS 的后继名称）是 out-of-tree LKM，提供 redirect、reverse lookup、readdir 和
  spoof 能力；官方 README 将其定位为 root/SU 受控环境，并明确涉及 VFS/syscall hot-path Hook。
  这说明它有参考价值，也说明其版本化 UAPI、KMI 和设备维护成本必须单独验证，不能由功能列表
  推断可直接集成。
- GKI KernelSU/SUSFS 发行生态证明定制 GKI 能力正在普及，但 SUSFS 本身主要是隐藏/伪装
  能力，不等于一个稳定、通用、按 app UID 执行 PathGuard selector 的 redirect ABI。

因此这些项目是未来 kernel adapter 的候选参考，不是 format 2/Pattern Engine 的硬依赖。
Pattern IR 必须保持后端中立，不能把某个第三方 ioctl、内核符号或 fallback 顺序写进规则
语义。

参考：

- `refer/NoMount/README.md`
- [NoMount](https://github.com/maxsteeel/nomount)
- [ZeroMount](https://github.com/Enginex0/zeromount)
- [Kasumi](https://github.com/Anatdx/Kasumi)
- [WildKernels GKI KernelSU/SUSFS](https://github.com/WildKernels/GKI_KernelSU_SUSFS)
- [SUSFS userspace module](https://github.com/sidex15/susfs4ksu-module)

### 3.7 性能资料的使用边界

`glob-match`、`fast-glob` 的项目基准显示，无回溯、零分配的单 pattern matcher 可以明显
快于“每次构建 regex/globset 后匹配”的测试用例；`globset` 则明确强调其优势是把多个
glob 作为集合同时匹配，并使用 literal prefix/suffix 和 Aho-Corasick 等策略预过滤。

这些数字受 pattern、是否预编译、CPU 和 benchmark harness 影响，不能直接成为 PathGuard
的验收阈值。本文只吸收两条可验证结论：

1. 运行时不得解析 TOML、编译 regex 或堆分配；
2. 多 pattern 场景必须有候选索引和退化上限，不能逐条无界扫描。

绝对延迟和相对回归阈值由 Phase P0 在固定 host runner 和真机基线中测量后冻结。

参考：

- [globset README](https://github.com/BurntSushi/ripgrep/tree/master/crates/globset)
- [glob-match](https://github.com/devongovett/glob-match)
- [fast-glob](https://github.com/oxc-project/fast-glob)

## 4. 规则语法

### 4.1 版本策略

带 selector 的动作字段不加入 format 1 的普通 `redirect` 或 `deny` 数组。规则编译器升级到
`format = 2`，生成新的 `policy.bin` format 6/schema 3；运行时只读取一个受支持的二进制格式。
在 P1 切换前，format 1/v5 仍是当前事实；切换 change set 必须同时更新规则 compiler、daemon、
Zygisk/Provider reader、CLI/status、默认规则和全部 fixtures，然后直接以 format 2/v6 取代旧格式。
不实现 format 1 到 format 2 的生产迁移命令，不保留双格式 reader，也不要求新版本读取 v5。

切换前保存 C1～C5 的 format 1/v5 前测规则和证据；切换后手工/fixture 定义语义等价的 format 2
规则并重放。旧 TOML 被新 compiler 拒绝、旧 policy 被新 reader 拒绝属于计划内破坏；literal
deny/redirect、LocalSend 和作用域隔离结果变化则不是。版本不匹配始终 fail-open 并产生一次
状态诊断，禁止部分读取或猜测字段。

### 4.2 语法定义

```toml
redirect_rules = [
    {
        select = { root = "Pictures", glob = "**/*.jpg", type = "file" },
        to = "Download/images",
        priority = 0,
        preserve = "relative",
    },
]
```

deny 只需要 selector，不需要目标：

```toml
deny_rules = [
    {
        select = { root = "Pictures", glob = "**/*.tmp", type = "file" },
        priority = 100,
        enforcement = "provider",
    },
]
```

字段：

| 字段 | 必选 | 语义 |
| --- | --- | --- |
| `select.root` | 是 | 相对于当前 user storage root 的目录，不能为绝对路径 |
| `select.glob` | 是 | 相对于 `root` 的 glob，匹配文件或目录项 |
| `select.except` | 否 | 同 root/type 下从 base glob 减去的 pattern 数组；语义见 ADR-0015 |
| `select.type` | 否 | `file`、`directory` 或 `any`；默认 `file` |
| `to` | redirect/export 必选 | 目标目录，必须位于允许的 storage root 内 |
| `priority` | 否 | 有符号 32 位整数，默认 0；数值越大优先级越高 |
| `preserve` | 否 | 首版固定为 `relative`，保留匹配项相对 root 的 tail |
| `collision` | 否 | redirect/export 的碰撞策略；首版只允许 `reject` |
| `enforcement` | glob deny 必选 | `provider` 或 `complete`，禁止隐式扩大保障范围 |

format 2 用 `provider = { enabled = true }` 表达用户意图，替代含义过宽的
`file_picker = true`。它只表示允许 Provider adapter 参与，不代表设备已经具备 caller UID、
query/insert、path I/O 或 FUSE 能力。实际能力只能来自运行时 probe，准入位由
[ADR-0012](adr/0012-provider-capability-split.md) 冻结。

首版 glob 语法：

- 普通 UTF-8 字面量精确匹配；匹配区分大小写，不做 normalization、locale collation 或
  Unicode 大小写折叠；
- `*` 匹配当前组件内零个或多个 Unicode scalar value，不跨 `/`；
- `?` 匹配当前组件内恰好一个 Unicode scalar value，不跨 `/`；
- `**` 只允许作为完整路径组件，匹配零个或多个完整目录组件；`a/**` 和 bare `**` 都只
  匹配非空后代，不匹配 `a` 或 root 自身；
- `[abc]`、`[a-z]` 匹配一个 ASCII 集合成员；`[!abc]` 是补集，`[^abc]` 是兼容别名；成员和
  range endpoint 限 ASCII，negated class 也不匹配 `/`；
- glob 语法中的 `\` 转义下一个字符；末尾孤立 `\` 编译失败；在 TOML basic string 中需要
  再经过 TOML 转义；
- `.` 是普通字符，`*` 可以匹配组件开头的 dotfile，不采用 shell 的隐式隐藏文件规则；
- pattern 开头未转义的 `!`、extglob、正则、环境变量、命令替换和 tilde expansion 均拒绝。

例如 `**/*.jpg` 必须同时匹配 `a.jpg`、`Album/a.jpg` 和更深层目录；`*.jpg` 只匹配
root 直接子项，不匹配 `Album/a.jpg`。字符类编译为固定 128-bit ASCII bitmap；不支持 POSIX
named class、collating symbol 或 equivalence class。非法 UTF-8 runtime path 不伪装为普通
NoMatch，而是 fail-open 并产生限速 `InvalidPathEncoding` 诊断。

format 2 宿主规则编译器额外接受受限 `{a,b,c}` 便利语法，例如
`**/*.{jpg,jpeg,png}`。它在 glob parser 之前展开为普通 selector/action：不进入 Pattern IR，
不产生 BRACE token，也不写入 `policy.bin`。不允许嵌套、range sequence、空 alternative、`/`
或包含 glob metacharacter 的 alternative；单条 source rule 最多展开 32 个 pattern，展开结果
UTF-8 字节合计最多 64 KiB。任一上限超出时拒绝整条规则，禁止截断或部分发布。精确定义及
保留语法见 [ADR-0014](adr/0014-glob-language-boundary.md)。

### 4.3 目标路径规则

目标始终解释为目录。匹配到的相对 tail 按组件拼接到目标：

```text
target = user_root / to / relative_tail
```

PathGuard 不解析应用可见路径中的符号链接来决定目标；编译期拒绝 `.`、`..`、空组件、
NUL 和超出长度限制的 pattern。运行时使用已固定的 storage topology 和逐组件安全解析。

### 4.4 Selector 是值对象

首版不增加全局命名 selector registry。每条动作规则内嵌 `select` 值，编译器按规范化
`(root, canonical pattern tokens, type)` 内容生成 selector ID 并自动去重；`[^...]`/`[!...]`
别名和 brace 展开后的重复结果不会产生不同 selector。这样配置不需要维护跨应用引用，
而 deny、redirect、observe、export 在 IR 和运行时仍共享同一个 matcher。

如果以后配置中确有大量人工复用需求，再增加只作用于单个 app section 的命名 selector；
当前不为此预留第二套解析路径。

### 4.5 有界反选

[ADR-0015](adr/0015-bounded-selector-negation.md) 已 Accepted。它将 selector 反选定义为
显式有界集合差，而不是 Glob token：

```text
Effective = Base(root, type, glob) - Union(except patterns)
```

该模型能表达“deny 除允许目录外的全部内容”和严格互斥的剩余分区，并保持 base candidate
索引。V-08 已用真实规则样本、1/16/64 candidate 退化样本和 Release 结构微基准关闭决策门：
format 6 首版必须编码 canonical except ref table，并维持每 selector 8 个、每 app 256 refs、
单 bucket 64 candidates 和单次 4096 transitions 的硬上限。普通 redirect 剩余路由仍优先用低
priority catch-all；deny 白名单和要求显式互斥的剩余分区才使用 `select.except`。

继续拒绝裸 `!pattern`、尾项 `!`、顺序反转和一般布尔表达式。字符类中的 `[!abc]` 不受影响，
它只是单字符集合补集，已经属于 ADR-0014 的 Glob v1。`except` 的全集以
`IdentityKey=(caller_uid,user_id)` 为最低可信边界；只有 adapter 验证 package attribution 后才进入
package 子桶，禁止从 policy 包名反推主体。

## 5. 通用 Pattern IR 与动作 IR

### 5.1 数据结构

规则解析、语义校验和运行时之间增加独立的 Pattern IR 与 Action IR，避免让 Zygisk
重新解析 TOML，也避免 deny/redirect/export 各自实现一套 glob：

```cpp
enum class PatternKind : uint8_t {
    LiteralPrefix,
    Glob,
};

enum class ActionKind : uint8_t {
    Deny,
    Redirect,
    Observe,
    Export,
};

enum class ExecutionDomain : uint8_t {
    Mount,
    AppPath,
    Provider,
    CompleteVfs,
    Event,
};

struct PathSelector {
    PatternKind kind;
    StringId root;
    PatternId base_pattern;  // LiteralPrefix 使用 invalid id
    uint32_t first_except;
    uint16_t except_count;
    uint8_t object_type;     // file/directory/any
    uint16_t depth;
    uint32_t first_action;
    uint16_t action_count;
};

struct PatternProgram {
    uint32_t first_token;
    uint16_t token_count;
    uint16_t component_count;
};

struct SelectorExceptRef {
    PatternId pattern;
};

struct ActionRule {
    ActionKind action;
    ExecutionDomain execution_domain;
    SelectorId selector;
    StringId target;
    int32_t priority;
    uint32_t options;
    uint8_t preserve;
    uint8_t collision;
    uint8_t reverse_mode;  // none/static_unique/provenance
    CapabilityMask required_capabilities;
    OperationMask required_operations;
};

struct PatternPlan {
    uint64_t plan_generation;
    Span<PathSelector> selectors;
    Span<PatternProgram> patterns;
    Span<PatternToken> tokens;
    Span<CharacterClass> character_classes;
    Span<SelectorExceptRef> except_refs;
    Span<ActionRule> actions;
};
```

`PatternKind::LiteralPrefix` 继续生成现有 mount plan；`PatternKind::Glob` 不生成 bind
mount，而是进入动态匹配表。`ActionRule` 只引用 selector ID，因此一个 selector 可以被
多个动作复用。`execution_domain` 是编译期选择的执行位置，不是运行时 fallback 顺序；动作
只有在同 domain 的 capability 和 operation mask 全部准入时才进入 MatcherSnapshot。

### 5.2 Matcher API

Pattern Engine 应保持与 Android ABI、libc Hook 和 TOML parser 无关：

```cpp
struct PathOperand {
    ObjectType object_type;
    std::string_view logical_path;
};

struct OperationContext {
    uint32_t user_id;
    int32_t caller_uid;
    PackageId subject_package;  // 无可信 attribution 时为 Unknown
    AttributionKind attribution;
    PathOperation operation;
    Span<PathOperand> operands;  // open=1, rename/link=2
};

struct MatchedSelector {
    SelectorId selector;
    uint16_t specificity;
    uint32_t first_action;
    uint16_t action_count;
};

struct MatchSet {
    Span<MatchedSelector> ordered_matches;
};

enum class DecisionReason : uint16_t {
    Matched,
    NoMatch,
    Denied,
    Collision,
    AmbiguousReverse,
    CapabilityMissing,
    RuntimeUnavailable,
    BudgetExceeded,
    InvalidPathEncoding,
    UnsafeTarget,
};

enum class PrimaryDisposition : uint8_t {
    Pass,
    Deny,
    Redirect,
};

enum EffectMask : uint8_t {
    EffectNone = 0,
    EffectObserve = 1 << 0,
    EffectExport = 1 << 1,
};

struct Decision {
    PrimaryDisposition primary;
    uint8_t effects;
    DecisionReason reason;
    // target、RuleId/SelectorId、冲突 ID 和 generations 省略
};

class PatternEngine {
public:
    MatchSet MatchOperand(const OperationContext& context,
                          uint8_t operand_index,
                          MutableSpan<MatchedSelector> scratch) const;
};

class ActionEvaluator {
public:
    Decision Evaluate(const OperationContext& context,
                      Span<MatchSet> operand_matches) const;
};
```

`PatternEngine` 只返回按 canonical SelectorId 排列的命中 selector，不执行 deny、重写路径或
移动文件。selector 没有 action priority：同一 selector 可以同时被不同优先级的 deny、redirect
或 observe 引用，因此 matcher 不能按 priority 排 selector。`MatchedSelector` 携带 specificity 和
该 selector 的连续 action range；Action Evaluator 对所有命中 action 做一次有界线性扫描，在扫描
中比较 action precedence、priority 和 specificity，不反查 selector 表，也不做运行时排序。
`scratch` 来自固定容量的栈/TLS buffer，匹配不分配内存。

`ActionEvaluator` 生成一个主处置 `Pass/Deny/Redirect` 和零个或多个副作用
`Observe/Export`。这避免把“记录事件”和“是否允许 I/O”误建模成四选一。Provider、
应用 Hook、FUSE 和 fanotify worker 都调用这两个抽象，而不是自己解析 glob。
`rename/link` 等双路径操作在一个 snapshot guard 内依次匹配 source/destination operand，再由
一次 `Evaluate` 原子地检查两边的 scope、路由目标、跨域和覆盖语义；禁止把两边作为两个可见
操作分别决策，否则 reload 或冲突规则可能产生一半旧 generation、一半新 generation 的结果。

每个 `Decision` 必须携带稳定的 `DecisionReason`、主 RuleId/SelectorId、冲突 RuleId 和
`plan_generation`、`capability_generation`。日志、CLI 和未来 UI 使用诊断 ID 定位运行时
collision/capability 错误，
不能只返回一个裸 `EACCES` 或 `false`。

### 5.3 Pattern 编译

编译器把 glob 转换为无回溯 token/NFA：

```text
LITERAL("IMG_")
STAR_COMPONENT
LITERAL(".png")

CHAR_CLASS(class_id) -> CharacterClassTable[128-bit bitmap, negated]
```

宿主 brace expansion 在 glob parser 和 IR 构建之前完成，因此 matcher 不存在 BRACE token。
`**` 编译为“跳过零个或多个完整组件”的 token，而不是 `.*` 正则；`CHAR_CLASS` 引用
canonical CharacterClassTable 条目并消费一个 Unicode scalar value。每个 pattern 具有：

- token 数和最大匹配步数上限；
- root 深度和首个字面量组件索引；
- 是否只匹配文件；
- 编译期计算的 specificity score。

specificity 的 token 顺序冻结为 literal 高于 char class，高于 `?`，高于 `*`，高于 `**`；
具体数值由 P0 golden 固定。字符类交集使用 bitmap 运算，不能回退到运行时正则或枚举 class
成员。首版不为 extension bucket 展开字符类，避免制造另一条组合爆炸路径。

运行时使用无回溯 NFA/bitset 状态推进；复杂度受 `pattern_tokens * path_components` 的状态
转移数约束，不能笼统承诺所有 glob 都是加法复杂度。超过 token、路径组件或 transition
预算立即按 fail-open 处理并记录 `pattern_budget_exceeded`，不能让恶意配置占满 Provider 线程。

### 5.4 索引

动态路由先以可验证的 `IdentityKey = (caller_uid, user_id)` 建立一级索引；只有 adapter 能提供并
校验 package attribution 时，才进入该 UID 下的 package 子桶，再按路径特征分桶：

```text
IdentityKey -> attribution bucket -> root literal prefix
            -> first literal component/extension -> candidates
```

没有首个字面量组件的 `**/*.jpg` 仍可加入 root 的 extension bucket，但不能扫描整个
存储目录。identity/attribution/bucket 不存在时直接返回 NoMatch，不调用 Pattern Engine token
matcher；其他 UID 即使访问相同逻辑路径也查不到目标 app 的规则。候选表在 policy load 时构造
并只读发布，热路径不分配内存、不加全局写锁。

app-path adapter 从已校验的 specialize process plan 获得 package attribution。Provider 当前
通常只能可靠得到 Binder caller UID；shared UID 下若不能从受信任 Binder attribution/URI grant
恢复唯一 package，规则必须按 UID 共享，或者对要求 package 隔离的动作报告
`AttributionUnavailable` 并不准入。禁止把 policy 中的包名反向当作运行时身份，也禁止在同一
shared UID 下按任意规则声明顺序选择 package。

首版冻结以下防御性上限；Phase P0 可以根据基准下调，扩大上限必须修改 limits profile、
golden 和 reader 测试，不能只改 UI：
生产代码的唯一数值定义为 `core/include/pathguard/pattern_limits.h` 中的
`kPatternLimitsProfileV1`；编译器、reader、matcher 与测试只能引用该定义或显式断言其冻结值，
不得在各模块复制另一组预算常量。

| 限制 | 首版值 |
| --- | ---: |
| 每 app selector | 256 |
| 每 app action | 512 |
| 单 pattern token | 64 |
| 每 app pattern token 总数 | 4096 |
| CharacterClassTable entry | 不大于 pattern token 总数 |
| 单 source rule brace 展开数 | 32 |
| 单 source rule expanded bytes | 64 KiB |
| 单 root 无固定前缀且无固定后缀的退化 pattern | 16 |
| 每 app 退化 pattern | 32 |
| 单 bucket candidate | 64 |
| 单次 MatchSet | 64 |
| 单次 matcher transition budget | 4096 |

“退化 pattern”包括 `**/*`、`**/??*` 等无法进入 literal/extension bucket 的模式。宿主
编译器验证 brace source expansion，编译器验证展开后的全局预算；reader 只验证 policy 中已经
展开的 selector/action/token/class tables 和自身 ceiling。超限在编译期失败，运行时
`BudgetExceeded` 只是损坏策略和内部错误的最后防线。

### 5.5 不可变快照与原子发布

本节生命周期机制由 [ADR-0011](adr/0011-pattern-snapshot-publication.md) 冻结；P1/P2 不再
重新选择读写锁、引用计数或 epoch/QSBR。

policy reload 不在 active snapshot 上增量修改。loader 完整校验 policy、构造索引并完成
admission 后，发布新的不可变 `MatcherSnapshot`：

```text
build new snapshot
  -> validate counts/tokens/capabilities
  -> seq_cst exchange active pointer（generation 存在 snapshot 内）
  -> new readers seq_cst load new or old complete snapshot
  -> retire old snapshot after all pre-switch readers leave
```

实现锁定为 userspace hazard-pointer publish/subscribe，不在编码阶段再选择另一套 epoch/QSBR
协议。固定 slot 容量、retire 上限、埋点及 fork 协议见 ADR-0011；普通 app-path 进程为 128
slots，Provider/SAF 进程为 256 slots。每个已注册线程持有一个 TLS hazard slot，首版
active/hazard 操作使用 sequentially consistent 原子。读者按“load active → store hazard →
再次 load active；指针变化则重试”的
协议固定 snapshot；writer exchange active pointer 后把旧 snapshot 放入 retire list，并扫描
全部 slots；只有旧指针不再被任何 slot 引用时才回收。generation 从已经受保护的不可变
snapshot 读取，不依赖未必 lock-free 的 128 位 tagged pointer。热路径
不取得全局 mutex；线程首次注册可以走慢路径，后续 Match 只执行有界原子读写。线程退出必须
通过 pthread TLS destructor 清空 slot；slot 耗尽时不得无保护读取，当前操作 fail-open 并返回
`RuntimeUnavailable`，adapter health 标记 degraded，但不能误报为设备 `CapabilityMissing`。
`pthread_atfork` child handler 只设置异步安全的 dirty 标志，首次正常公共入口停用继承
admission 并完成幂等 registry/snapshot 重建；禁止在 child handler 内加锁、分配或记录日志。
atfork 注册失败时 adapter 不准入，不退回每个调用方手工清理，也不在每次 Match 增加
`getpid()` syscall。

MatcherSnapshot 是每个已准入进程在读取并验证当前 policy 后构建的进程内快照，不是 Zygote
长期持有并由所有子进程继承的 policy mmap。ADR-0001 当前仍决定“不采用 Zygote 继承 policy
mmap”；hazard registry 的 atfork 协议只处理进程内 matcher 状态，不能被解释成撤销该边界。

读者允许在一次操作中继续使用旧 snapshot，但必须从匹配到动作执行都持有同一 generation；
禁止用旧 MatchSet 配合新 ActionTable。Linux RCU 文档中的 publish/subscribe 保证是模型
参考，PathGuard 仍需为 userspace hazard-pointer 实现编写 TSAN、并发 reload 和线程退出测试。

Linux RCU 文档只用于核对“先初始化、后发布、宽限期后回收”的内存顺序要求，不表示直接
复用内核 RCU API。具体正确性以本文 hazard-pointer 协议及本项目并发测试为准。

参考：

- [Linux RCU publish/subscribe requirements](https://docs.kernel.org/RCU/Design/Requirements/Requirements.html)
- [POSIX pthread_atfork](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_atfork.html)

### 5.6 热路径折叠与审计

能力 probe、action requirements admission 和候选索引构建都发生在新 `MatcherSnapshot` 发布
之前。运行时文件操作不重新探测 capability，只读取 snapshot 中已准入的 action 和 operation
mask；capability generation 变化时由 loader 构建并原子发布新 snapshot。

在不改变 `PatternEngine -> MatchSet -> ActionEvaluator` 职责边界的前提下，orchestrator 使用
以下有界路径：

1. identity/attribution scope 不存在或 candidate bucket 为空：直接 NoMatch，不调用 token matcher；
2. 0 个 selector 命中：直接透传；
3. 1 个 selector 命中：ActionEvaluator 只线性扫描该 selector 的连续 action range，不执行
   多 selector precedence 选择；
4. 2 个及以上 selector 命中：对候选 action 做有界线性 precedence、priority 和 specificity
   比较，仍不做运行时排序。

普通 NoMatch/透传不产生逐操作日志、结构化审计事件或同步 I/O。Deny、Redirect、Collision、
Unsupported/CapabilityMissing 和内部预算错误可以进入有界、限速、异步审计；能力缺失状态变化
优先记录一次状态事件，不能对每次透传重复刷日志。显式 `observe` action 按其自身采样和队列
策略产生事件，不受 NoMatch 静默规则影响。

## 6. 匹配和冲突语义

### 6.1 决策顺序

对每个路径操作，Pattern Engine 和 Action Evaluator 按以下顺序决策：

1. adapter 从当前不可变 snapshot 获取已经完成 capability admission 的 action；运行时不
   重新 probe capability。
2. 使用已验证 identity/attribution scope 和路径 bucket 定位候选；scope/bucket miss 直接透传，object type 或
   storage root 不匹配也不进入 token matcher。
3. Pattern Engine 为该操作的每个 path operand 返回所有命中的 selector，Action Evaluator 在
   同一 snapshot/generation 内合并同一 app/user scope 的动作；0/1/multi selector 分别走
   5.6 节的等价快路径和有界多命中路径。
4. active deny 的动作级 precedence 高于其他主动作；命中 deny 时主处置固定为 `Deny`，不再
   选择 redirect。
5. evaluator 在同一次扫描中独立收集副作用：observe 可以记录 Pass/Redirect/Deny 的尝试；
   export 只有在主处置允许、原始操作成功且 event 后端 active 时才排队。Deny 决策完成后 adapter
   返回 `EACCES`，绝不执行 redirect、原始写操作或 export。
6. 对同一 app/user scope 内相互排斥的非 deny 主动作，按 `priority` 降序选择；同优先级按
   specificity score、root 深度和 literal prefix 长度降序选择。`priority` 不能把 redirect
   提升到 deny 之上。
7. 若两个互斥动作或不同目标规则在编译期可判定为同优先级重叠，直接拒绝策略；
   observe 等非阻断动作可与主动作并存，运行时不依赖声明顺序
   猜测结果。
8. 只有最终动作为 redirect/export 时生成目标路径，并做目标域、循环和碰撞保护；失败
   时按 action capability 和 failure mode 透传或拒绝。

literal redirect 与 glob redirect 都是引用 selector 的主动作，不设置隐藏的“旧规则优先”。
同时命中时统一按 action precedence、显式 `priority`、specificity、root 深度和 literal prefix
长度选择；glob deny 若命中仍优先阻断。因更具体 glob 在同 priority 下覆盖宽泛 literal redirect
属于新统一语义下的计划内行为，必须由 conflict/explain 测试给出确定结果。没有重叠 glob 时，
C2 的 literal redirect 端到端结果必须与前测一致。

### 6.2 规则冲突

编译期拒绝以下情况：

- 同一 enforcement、相同 selector、相互排斥的非 deny 主动作或不同 redirect 目标且
  priority 相同；
- pattern 目标落入自身 source root，可能形成递归重写；
- pattern 目标落入另一个更高优先级 source root，且无法证明只读；
- 两个 selector 的匹配语言存在交集、非 deny 主动作互斥、priority 和 specificity 均相同；
- 同一路径操作可能选择不同 redirect 目标，且 precedence/priority/specificity 仍无法唯一决策。

glob 交集判断可以使用编译后的有限自动机；实现初期如果无法证明“不相交”，采取保守
拒绝，而不是接受后依赖声明顺序。编译器必须尽可能在发布前报告稳定 diagnostic code、
两个 RuleId 和最小反例路径；只有文件系统现状相关的同名碰撞才允许留到运行时。
deny 与其他动作重叠不是配置冲突：只要 deny 声明的 enforcement 能通过 admission，就由
动作级 precedence 确定性地拒绝；deny 的 `priority` 只用于多个 deny 诊断归因，不用于放行。

### 6.3 多源到同一目标

多源到同一目标是核心场景 C4，前向路由不因目标 tail 语言可能重叠而在编译期拒绝：

- 每个源使用 `preserve = "relative"`，目标目录不在任一 source root 内；
- 多条规则生成相同 target path 时，首个已存在实体拥有该路径，后续写入按
  `collision = "reject"` 返回 `EEXIST`；
- 反向映射不能按规则声明顺序或 visible path 字典序编造来源。

反向路由分为两种模式：

1. **静态唯一**：编译器证明目标 relative-tail 语言不相交，直接由 target 唯一恢复 selector；
2. **来源追踪**：语言可能相交时，成功 create/rename 事务提交一条 route provenance，至少包含
   identity/attribution scope 与 identity epoch、稳定 target object identity、target relative path、
   RuleId、原始 logical path 和 generations。query/open/reverse scan 使用该记录恢复来源。

来源追踪属于 daemon 单写的 redirect router 共享服务，不进入 Pattern Engine，也不改变 glob
语法。精确事务和持久格式由
[ADR-0017](adr/0017-route-provenance-transactions.md) 冻结：mutation 前 durable prepare，文件系统
成功后记录 strong identity，再 durable commit，最后才向 create/rename 调用方返回成功；delete
使用 prepare + tombstone。跨文件系统与 store 无法形成真正 ACID 事务，无法补偿的中间状态必须
保留为 unowned/ambiguous，不能自动删除文件或伪造来源。

durable target identity 优先使用可连接 file handle/FID；fallback 只接受经过 probe 的稳定 volume
identity + inode + `statx` birth time。`(st_dev, st_ino, ctime)` 会随正常写入变化，只能用于诊断或
同 boot 快速拒绝，不能恢复跨重启 owner。强 identity 不可用时 provenance mode 不准入。

当前 v5 `RestoreAbsolutePath` 对歧义选择 canonical visible source 的行为记录为前测事实，但在
v6 中计划内删除。policy reload 只按当前 scope 中语义相同的 RuleId rebind；无关规则改变不能让
有效记录整体失效，RuleId/target/scope/identity epoch 变化则进入 stale/ambiguous。新文件的
provenance 完整时，C3/C4 必须保持 Provider query 与实际 FD 一致；
历史/外部文件缺少 provenance 且静态无法唯一恢复时，返回 `AmbiguousReverse` 并保持真实 target
视图或明确省略虚拟别名，不能伪造来源。Provider composite admission 必须把 provenance
prepare/commit/reload 测试纳入 bit 17 的 reverse-mapping substatus；不需要反向展示的 app-path
前向 redirect 可以独立准入。

如果多个源可能把不同文件映射到同一个目标 basename，策略必须显式指定：

```toml
collision = "reject"   # 首版唯一允许值
```

`collision = "reject"` 是默认值，处理目标目录中已经存在同名实体的动态碰撞，并保证同一
target path 同时最多有一个 provenance owner。首版不自动覆盖、不追加随机后缀、不通过搬运解决碰撞。需要覆盖或版本化命名时，应
增加独立的目标模板和事务语义。运行时 collision 返回 `DecisionReason::Collision`，并记录
source、target、两个 RuleId 和 plan generation；Provider adapter 将其映射为稳定 errno，
CLI/status 保留诊断 ID，避免用户只看到无来源的“保存失败”。

## 7. 执行后端

### 7.1 统一能力与分离执行域

项目需要统一的是“同一条规则是什么意思”，不是“所有后端如何执行”。公共层统一以下能力：

- selector/Pattern IR、canonicalization、specificity 和冲突分析；
- `PrimaryDisposition + EffectMask` 的 Action/Decision 语义；
- logical path、identity/attribution scope 和 generation；
- 多源反向所需的 route provenance contract；Provider/FUSE 只能做存取适配，不能各建来源语义；
- requirement/observation/admission 外壳及 operation mask 的公共编号；
- DecisionReason、status/explain schema、审计字段和 adapter conformance vectors。

执行仍按五个正交 domain 分离：

| Domain | 身份来源 | 典型执行 | 不能声称 |
| --- | --- | --- | --- |
| `mount` | specialize process plan/UID | literal subtree bind/deny | 文件级 glob、Provider query 语义 |
| `app_path` | 目标 app 进程和 package plan | 已 Hook 的 libc/Java path API | SAF 代写、direct syscall、complete |
| `provider` | Binder caller UID，可选可信 package attribution | Provider path/query/create/rename 映射 | app 内全部 API、complete VFS |
| `complete_vfs` | VFS/FUSE request context | lookup/open/create/readdir/rename 等完整路径面 | 未经 probe 的 OEM/内核兼容性 |
| `event` | fanotify event metadata | observe/异步 export | 同步 redirect、无丢失事务 |

统一准入公式为：

```text
Requirement = execution_domain + required_capabilities + required_operations
Observation = adapter_state + observed_capabilities + observed_operations
Admission   = same domain
              AND adapter_state == active
              AND required_capabilities subset_of observed_capabilities
              AND required_operations subset_of observed_operations
```

capability snapshot 可以使用统一容器，但每个 domain 独立生成 observation、生命周期和回滚状态。
编译器必须把 action 固定到一个 domain；若产品希望同一意图支持多个后端，应显式生成多份具有
清楚保障范围的 action/状态，而不是在一次文件操作中从 complete VFS 隐式退到 Provider、
app-path、mount 或 event。

### 7.2 LiteralPrefix：继续使用 mount

字面量目录 deny/redirect 沿用当前严格/legacy mount backend、topology probe、mutation
lease 和 rollback 协议。Pattern IR 只负责把 selector 标为 `LiteralPrefix`，不改变现有
真机行为。

mount backend 必须在 mutation lease 之前针对整个 `ProcessPlan` 的 required action mask 选择。
允许的 strict 候选只能在 preflight/probe 中按 ADR-0005 判定；取得 lease 前必须选定一个支持
整个事务的 backend。取得 lease 后不得切换 backend，也不得逐规则 fallback 到 legacy。失败按
同一 journal 精确回滚；回滚无法证明恢复时 namespace 标记 tainted 并终止目标进程，不能继续
执行动态 action 掩盖不一致。

### 7.3 Glob：动态动作执行

glob 不能由一次目录 bind 实现。deny、redirect 等动态动作需要在路径操作边界执行：

```text
应用 libc path API / Provider libc path API
    -> UID scope
    -> PatternEngine::Match(operation, absolute_path)
    -> ActionEvaluator::Evaluate(matches)
    -> deny: return EACCES
    -> redirect: rewrite path and create target parent if needed
    -> observe/export: emit a bounded event
    -> call original operation
```

统一入口至少覆盖：

- `open/openat`、`creat`、`stat/lstat/fstatat`、`access/faccessat`；
- `opendir/readdir`、`mkdir/mkdirat`；
- `rename/renameat/renameat2`、`unlink/unlinkat`；
- `realpath/readlink`、`chmod/chown`、`truncate`、`utimensat`；
- `inotify_add_watch` 的 watch path。

这不是把所有路径都交给一个 PLT Hook，而是将当前已有 Hook 的 path transformation
逻辑重构为薄适配层；每个适配层都调用同一个 Pattern Engine/Action Evaluator，并在缺少完整能力时保持
透传。Direct syscall、静态链接库和未覆盖的 native 入口仍然不承诺被拦截。

glob deny 是安全动作，active 判定比 redirect 更严格：

- `enforcement = "provider"`：只承诺阻断 SAF/MediaProvider 请求，必须明确显示为 provider scope；
- `enforcement = "complete"`：要求 FUSE/VFS 或等价的完整路径能力，缺失时规则保持 inactive；
- 不提供把若干 libc Hook 标记为 complete 的选项；
- 不把 pattern deny 自动降级为 observe，也不因 fail-open 而谎报 deny active。

### 7.4 Provider/SAF 适配

Provider 进程收到路径操作时，Pattern Engine 使用 Binder raw calling UID；禁止使用
Provider 自身 UID。Provider 适配器还必须处理：

- 查询返回的 `_data`/relative path；
- create/insert 返回的 URI 与实际 FD；
- rename、delete、directory query 的正向和反向映射；
- MediaStore 扫描后从 target path 归因到 virtual source path。

这是目标 composite contract。Android `DocumentsProvider` 把 query、create 和 open 定义为分离
接口：create 返回 document ID，客户端随后还会 open；因此“path Hook 能重写 FD”不能证明
query/create 映射已经一致。当前主分支只提供 literal prefix path virtualization 和 media query
deny 的一部分基础，尚未满足上述 glob contract。

只有同时具备 caller UID、文件系统操作和 query/insert 映射能力时，`provider.enabled = true`
的 glob redirect 才标记为 active。provider-scope glob deny 还必须覆盖 Provider 的 read、
write、query、insert、delete 和 rename 拒绝入口。Hook 安装失败、Binder identity 不可用或
OEM Provider ABI 不明时，状态为 `unsupported`，不发布半成品 pattern 动作。

Provider PLT Hook 只允许提交到经审计、进程全程常驻且不会 `dlclose` 的 library 白名单。Hook
事务一旦提交，模块必须驻留；后续 self-test 或 composite admission 失败时保持 Hook 已安装但
全量透传，不能卸载模块留下 backup pointer。MediaProvider 是 Mainline 可更新模块，白名单、
符号和 operation mask 必须按实际模块构建和 probe 结果决定，不能按 Android 大版本硬编码成功。

参考：

- [DocumentsProvider API](https://developer.android.com/reference/android/provider/DocumentsProvider)
- [Create a custom document provider](https://developer.android.com/guide/topics/providers/create-document-provider)

### 7.5 FUSE 适配

FUSE 是可选的全路径后端，适用于：

- 目录 listing 必须同时呈现 source 虚拟项和 target 实体；
- 应用绕过 libc 直接发起 syscall；
- Provider/SAF 不覆盖的文件操作。

FUSE 后端必须通过公共可验证 ABI 或设备 capability probe 证明 request context、lookup、
create、rename、unlink、readdir 和 reply 路径完整可用。只 Hook 到部分 `fuse_reply_*` 函数
不能宣称 pattern active；当前设备未设置 `fuse_complete_path` 时保持该能力关闭。

在启用 FUSE passthrough 的设备上，open/create 之后的 read/write 可能直接进入 lower
filesystem。complete backend 必须在 open/create 决策时把 subject、virtual/source route、
generation 和所需权限绑定到 handle/FD，直到 close 前保持一致；不能依赖后续 read/write Hook
重新匹配，也不能在 reload 后让已打开 FD 半途切换目标。多源反向 lookup/readdir 必须消费
6.3 节同一 route provenance contract，禁止另建一套 FUSE-only 来源规则。

### 7.6 Capability snapshot 与 admission

策略需求和设备事实分开存储：

```text
policy.bin: required action/backend capabilities
runtime capability snapshot: probed device/process capabilities
admission: requirements subset_of observed capabilities
status: active/inactive/unsupported + missing bits + reason
```

`policy.bin` 不能写入“本设备支持 openat2/FUSE”之类观察结果，否则同一策略在重启、ROM
升级或另一台设备上会携带过期事实。format 6 只编码 action 需要哪些能力；daemon/Zygisk
在带 generation 的 runtime snapshot 中记录实际能力。

目标 format 6 的稳定动态路径 admission bits 已由 ADR-0012/0013 冻结为：

```text
provider_caller_uid
provider_query_insert_mapping
fuse_complete_path
app_path_adapter
```

`fuse_complete_path` 属于 FUSE backend domain，不是 Provider 子 bit。每个 probe 继续输出
path read/write、query、insert/create、rename/delete、reverse scan、request identity、lookup、
readdir 等 action mask/substatus；这些用于诊断和动作准入，但不把“Hook 已安装”升级成稳定语义
能力。`app_path_adapter`（bit 19）只表示进程级 adapter semantic baseline；具体 API 覆盖必须
同时满足 versioned operation mask，见 [ADR-0013](adr/0013-app-path-api-capability.md)。Provider/
FUSE 准入矩阵见 [ADR-0012](adr/0012-provider-capability-split.md)，resolver 继续使用既有能力位。
这些 bit 当前尚未出现在 `core/include/pathguard/capabilities.h`；P1 必须一次补齐共享常量、reader
校验、probe、status 和故障注入，禁止只更新文档或只设置一个无消费者的 bit。

`openat2()` 自 Linux 5.6 提供，但 Android 不能根据版本字符串推断支持。capability probe 在
daemon/进程初始化时对实际 syscall 和所需 resolve flags 各执行一次，缓存到 snapshot；
后续操作不反复用 `ENOSYS/EINVAL` 探测。`openat2` 不可用时只有通过逐组件
`openat(O_PATH | O_NOFOLLOW | O_DIRECTORY)` 打开中间目录、并按最终操作单独验证末组件的
`component_fd_walk` 才能满足 resolver requirement。

参考：[openat2(2)](https://man7.org/linux/man-pages/man2/openat2.2.html)

规则的 enforcement 仍只有 `provider`/`complete`，不增加模糊的中间保障等级。adapter health
独立报告 `active/inactive/unsupported/degraded`；action admission 再报告 active/inactive/
unsupported、missing capability/operation、probe errno、generation 和 backend matrix，让 UI/CLI
能准确显示“Provider redirect 可用、complete deny 不可用”或“Hook 驻留但当前退化为透传”。

### 7.7 fanotify/export 边界

fanotify 的 `FAN_CLOSE_WRITE`、`FAN_MOVED_TO` 等事件适合记录或异步 export。它不能代替
同步 redirect，因为写入期间源路径仍可见，事件可能合并、丢失或队列溢出，且移动操作
可能跨文件系统失败。

如果未来提供显式异步功能，语义应单独写成：

```toml
export_rules = [
    {
        select = { root = "Pictures", glob = "**/*.jpg", type = "file" },
        to = "Download/images",
        mode = "move",
    },
]
```

`export_rules` 复用 selector 和 Pattern Engine，但其状态、重试、幂等和媒体扫描与
`redirect_rules` 的同步执行完全分离。用户
选择同步 redirect 时，系统不得偷偷降级成 export move。

export 的幂等身份优先使用 fanotify FID 的 `(fsid, opaque file_handle, mount identity,
policy generation)`，路径只用于显示。文件系统不支持可连接 file handle 时，经过 capability
验证后才允许退化为 `(st_dev, st_ino, ctime, policy generation)`；裸路径和裸 inode 都不能
作为唯一 key，因为 rename 会使路径失效，inode 也可能复用。

参考：

- [fanotify(7)](https://man7.org/linux/man-pages/man7/fanotify.7.html)
- [fanotify_mark(2)](https://man7.org/linux/man-pages/man2/fanotify_mark.2.html)
- [name_to_handle_at(2)](https://man7.org/linux/man-pages/man2/name_to_handle_at.2.html)

### 7.8 内核 VFS adapter 策略

该选择已由 [ADR-0010](adr/0010-kernel-vfs-ecosystem-strategy.md) 冻结为“部分跟随、核心零
依赖、后端可插拔”。NoMount、ZeroMount、Kasumi（原 HymoFS）和可用 SUSFS ABI 只作为
optional adapter 候选。只有候选后端同时满足以下条件才允许接入：

1. 有版本化 UAPI 和 feature query，不依赖设备特定未导出符号；
2. 能按 caller UID/user scope 施加 include rules，而不只是 UID 排除；
3. 覆盖 open/stat/access/rename/unlink/readdir/reverse mapping；
4. 支持原子 generation 切换、规则清理和失败回滚；
5. 至少覆盖项目设备矩阵中的两个 GKI 代际，维护状态和许可证可接受；
6. adapter 只消费 PathGuard Pattern/Action IR，不改变规则语义。

当前结论是“不硬依赖、保留 adapter 接口、P4 先做契约与 conformance suite”。ZeroMount 的
VFS→OverlayFS→MagicMount fallback 面向系统模块加载，不能直接继承到 PathGuard：pattern
deny 不得从完整 VFS 静默降级到不完整 mount/Hook，后端仍必须按 action capability 做
admission。FUSE 只做有界 feasibility prototype；原型未通过两个 Mainline/OEM 组合的完整
语义门槛时停止投入，`complete` enforcement 保持 unsupported。产品化 FUSE 必须由新 ADR
批准。

## 8. policy.bin format 6

format 6 是对当前 v5 的破坏性替代，不是在 v5 文件末尾追加数据，也不保留独立 MountTable 和
EventTable。literal、glob、deny、redirect、observe、export 全部编码为 SelectorTable +
ActionTable；loader 根据 `execution_domain` 从同一 Action IR 物化 mount `ProcessPlan`、动态 matcher
plan 或 event subscription。这样只保留一套 scope、冲突、generation、capability 和诊断逻辑。

v6 继承的是 v5 已验证有效的安全属性：little-endian 显式编码、固定宽度 row、CRC-32、canonical
generation、严格 offset/count/reserved 校验和完整包名比较，不继承 v5 row layout 或双 reader。
旧 reader 按 version 拒绝 v6，新 reader 按 version 拒绝 v5；切换由一个协调 change set 完成。

精确字节契约由 [ADR-0016](adr/0016-policy-format-v6.md) 唯一定义，本文只保留结构关系，避免在
两处复制 row offset、enum 和硬上限。目标版本固定为 policy format 6/schema 3，布局为：

```text
Header[128]
PackageTable -> package scope、selector/action ranges、plan generation、requirement unions
ScopeRefTable -> user/process scope refs
SelectorTable -> root、base PatternId、except/action ranges、匹配缓存
ActionTable -> selector、action/domain、target、priority、requirements 与路由选项
PatternTable -> canonical PatternProgram 与 token range
PatternTokenTable -> literal/star/question/globstar/class/separator tokens
CharacterClassTable -> canonical 128-bit ASCII bitmap
SelectorExceptRefTable -> selector 的 canonical PatternId 差集引用
StringIndexTable -> StringId 到 StringData range
StringData -> canonical UTF-8 bytes
```

要求：

- 数值仍使用 little-endian 显式编码，固定 row 不依赖 C/C++ struct padding；
- Header 与九张固定表的 offset、count、row size、token kind、enum 和 reserved 字段严格校验；
- `PatternId` 必须引用 PatternTable；Pattern 的 token range、selector/action/scope/except ranges
  连续覆盖各自表，不允许空洞、重复或未引用 row；
- `CHAR_CLASS` 引用必须在 `class_count` 范围内；class flag 只能包含 `negated`，reserved bytes
  必须为零，bitmap 不得包含 `/`；
- CRC-32 checksum 覆盖完整 payload；`content_generation`/`plan_generation` 继续基于 canonical IR
  的 FNV-1a 64，而不是原始 TOML 字节；
- package hash 命中后仍逐项比较完整包名；
- selector table 在每个 PackageTable 拥有的 range 内按 root、pattern token 的 canonical 顺序
  编码并去重；
- action table 在每个 package range 内按 selector id、execution domain、action、priority 降序的 canonical 顺序
  编码，使 selector 的 action range 连续；
- `content_generation`/`plan_generation` 覆盖 failure mode、header semantic flags、selector
  patterns/classes/except、action domain/priority/target/reverse mode、capability/operation requirements
  和 package scope，而不是只覆盖原始字符串；
- `StringId` 是 StringIndexTable row index，不是裸 StringData offset；StringId 0 固定为空串；
- Zygisk、Provider、宿主编译器、CLI 和 device probe 共享纯 C/C++ 格式头及 golden vectors；
- policy 中不保存正则字符串，不在运行时编译正则；
- brace source 原文和 expansion metadata 不进入 policy；reader 只验证展开后的表、引用和
  自身预算；
- deny、redirect、observe、export 只能引用已验证的 selector ID，禁止在各 action table
  内复制未经校验的 pattern 字符串。
- execution domain、required capabilities 和 required operations 必须是已知值且组合合法；
  bit 16～19 及 operation mask v1 的数值服从 ADR-0016，不在 adapter 内重复编号；
- `Glob + Mount`、`LiteralPrefix + Event` 等非法 action/domain 组合由 compiler 和 reader 同时
  拒绝；首版 mount domain 只接受 literal deny/redirect，event domain 只接受 observe/export。

ADR-0015 已 Accepted。v6 首版必须包含固定宽度 `SelectorExceptRefTable`，SelectorTable 记录
`first_except/except_count`；base 与 except 共同引用全局去重的 PatternTable，并把全部 token
计入同一预算和 canonical generation。不存在不含 except table 的条件式 v6 变体。

SelectorTable 条目本身不携带 package/UID 授权。首版只在单个 package plan 内去重 selector；
PackageTable 拥有 ActionTable range，ActionRule 再引用该 package range 内的 SelectorId。
Pattern/Token/Class/String 可以跨 package 共享不可变内容，但 scope/admission 仍只能沿
`PackageTable -> ActionTable -> SelectorTable` 引用链确定，禁止从 selector 推断 package。
PackageTable 的 required capability flags 是其 ActionTable requirements 的预计算并集，用于
快速状态汇总；admission 仍按单条 action 的 requirements 判断，不能因一个 action 缺能力而
停用同 package 中所有可执行动作。reader 必须复算并校验这个并集。

Header 中的 count 是待验证输入，不是安全预算。reader 使用自身编译期 `PolicyLimitsProfile`
重复验证每 app selector/action、token/class 总数、退化 pattern、bucket candidate 和文件总大小；
不能相信由策略文件自己声明的“上限”，也不增加会被误解为协商配额的
`limits_profile_version`。策略超过当前 reader ceiling 就按稳定诊断码拒绝；扩大上限必须随 reader
升级和 format/golden 兼容性评审完成，不能由 header 请求扩容。

## 9. 安全模型

### 9.1 路径与符号链接

- 规则路径只能是当前 user storage 的逻辑相对路径；
- source root 和 target 均需通过 topology snapshot 验证；
- 目标准备使用
  `openat2(RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS)`，不支持时逐组件
  `openat(O_NOFOLLOW | O_DIRECTORY)`；
- target parent 的创建不能跟随用户可控 symlink；
- route tail 重新逐组件校验，禁止 `.`、`..`、空组件和 NUL；
- 目标不得穿越 `Android/data` 等受保护区域，除非另有 capability 和专项验证。

### 9.2 UID 与 user 隔离

规则索引的最小安全边界是 `(caller_uid, user_id)`；package 只有在当前 execution domain 提供
可信 attribution 时才作为额外限定。UID 解析失败时不尝试用包名猜测。shared UID 下没有可信
package attribution 时，Provider 规则只能采用明确的 UID 共享语义；若同 UID 的 package 规则
目标冲突或用户要求 package 隔离，则该动作不准入并报告 `AttributionUnavailable`。多用户和
work profile 必须为每个 user 生成独立计划。Provider 的
`clearCallingIdentity/restoreCallingIdentity` 区间必须成对跟踪，无法确认调用方时透传。

### 9.3 失败模式

默认 `failure = open` 适用于能力缺失、matcher budget、Hook、Provider query 映射、target
安全创建或 reverse mapping 等内部失败：保持原路径，不删除源文件、不移动文件、不伪造
成功。编译错误在发布前拒绝整份新策略并继续使用上一份有效 snapshot，不进入运行时。对 glob
deny，能力未通过 admission 时规则状态必须是 `inactive/unsupported`，不能显示为“已禁止”。

用户规则的确定性结果不属于内部失败：active deny 返回 `EACCES`；`collision = "reject"`
发现目标实体已存在时返回 `EEXIST`；静态多对一歧义在编译期拒绝。适配器必须区分
`DecisionReason` 与内部错误，不能把用户要求的拒绝再 fail-open。

未来若支持 `failure = closed`，必须只对显式 deny 或已证明安全的 provider operation 开放；
不能用 closed 弥补动态 Hook 覆盖不完整的问题。

## 10. 重构计划

### Phase P0：规范与纯函数

1. 在任何运行时代码改动前，按 1.3/11.0 节记录 C1～C5 host + 真机前测；缺少前测证据不得
   开始 format/Hook/mount 重构。
2. ADR-0015 已 Accepted；公共模型和 golden 直接实现 `base - Union(except)`，不再保留条件分支。
3. 新增 `PathSelector`、`PatternToken`、`MatchSet`、`ActionRule` 和 `Decision` 公共模型；Decision
   使用 primary disposition + effect mask，ActionRule 明确 execution domain 和 operation mask。
4. 新增 Glob v1 parser/compiler，包括 ASCII bitmap 字符类、UTF-8 scalar 推进、组件级 `**`
   和稳定错误诊断，并限制 token、深度和总规则预算。
5. 在宿主规则编译器实现受限 brace expansion；在 glob parser 前完成，执行 32 个结果/
   64 KiB 累计字节硬上限，超限原子拒绝。
6. 实现 path component matcher、specificity、字符类交集和冲突诊断。
7. 建立唯一的 selector decoder，供 deny/redirect/observe/export 调用。
8. 冻结 `PolicyLimitsProfile`、退化 pattern 定义和 bucket 上限。
9. 建立 host golden、property test、fuzz 和 benchmark；v5 literal/provider vectors 只保存为 before
   oracle，新 golden 以统一 v6 IR 为准，此阶段不改运行时。
10. CI 在固定 runner 验证：identity/attribution/candidate miss 零分配且不进入 matcher；0/1/multi selector
   路径与统一 evaluator 参考实现具有相同 Decision；1/16/64 candidate、
   16/32 个退化 pattern 和 1000 条恶意输入均有显式 compile reject 或有界结果。相对性能
   gate 由首次基线测量后写入 benchmark 配置，不引用第三方绝对数字。

### Phase P1：policy v6

1. 实现已接受的 ADR-0016；它 supersede ADR-0002 及 ADR-0006 的 v5 格式部分，row/table、enum、
   operation mask 和 limits 只从共享格式头生成，相关 ADR 不复制另一套数值。
2. 删除 v6 中独立的 mount/event policy 表；literal/glob 和全部动作统一编码为 selector/action，
   再按 execution domain 物化后端 plan。
3. 实现 v6 little-endian 显式 encode/decode、CRC-32、canonical FNV generation 和独立 reader
   验证；禁止依赖 struct padding 或双格式运行时 reader。
4. 在同一 change set 切换 compiler、daemon、Zygisk/Provider、CLI/status、默认配置和 fixtures；
   删除 format 1/v5 生产解析路径，新组件明确拒绝旧输入。
5. 使用语义等价的 format 2 规则重放 C1～C5；允许 IR、row、RuleId 和日志字段计划内变化，
   literal deny/redirect、LocalSend、作用域隔离和故障结果不得出现非预期变化。
6. 实现 runtime capability snapshot、requirements admission 和细粒度 status bits。
7. 按 ADR-0011 实现 immutable MatcherSnapshot、TLS hazard-pointer slots、原子发布、安全回收、
   atfork 惰性重建以及 slot/retire 观测指标。
8. 按 ADR-0016 已冻结的 execution domain 和 operation mask v1 数值实现共享头与 golden，禁止
   adapter 自行编号。
9. 每个准入进程从已验证 policy 构建自己的 MatcherSnapshot；不引入 Zygote 继承 policy mmap。

### Phase P2：Pattern Engine、Action Evaluator 与现有 Hook 复用

1. 从 `provider_path_mapper` 抽出无 Android ABI 依赖的 Pattern Engine 和 Action Evaluator。
2. 将 libc Hook 的 open/stat/mkdir/rename/unlink 等适配器改为调用统一接口。
3. 实现 glob deny 的 `EACCES` 返回，以及 redirect target parent 的安全创建和
   operation-aware rename 规则。
4. 为每个 action/adapter 声明 required admission bits 和 action mask；缺能力时相应动作保持
   inactive，不为每个操作随意新增稳定 bit。
5. app-path 动作同时要求 bit 19、active adapter state 和 required operations 子集；任一不满足
   时报告 missing capability 或 operation mask，不切换后端。
6. P2 验收只覆盖目标 app 进程中已 Hook 的直接 Java/libc 文件路径工作流，例如浏览器
   直接下载、文件管理器直接复制和 native path API；不宣称 SAF、MediaStore、Photo Picker、
   direct syscall 或完整 glob deny 已完成。

### Phase P3：Provider/MediaStore

1. 实现已接受的 ADR-0017，保持 daemon 单写 store、事务状态机、恢复矩阵和 GC 边界一致。
2. Provider 使用 Binder caller UID 建立 route scope。
3. 补齐 query、insert、create、rename、delete、scan 的正向/反向映射。
4. 补齐 provider-scope glob deny 的全操作拒绝矩阵。
5. 实现 route provenance 的事务、持久化、identity 校验和 daemon/Provider 重启恢复。
6. 为多源到同一目标增加 static/provenance reverse mode 和 ambiguity 诊断；删除 v5 canonical
   source fallback。
7. 按 ADR-0012 实现 Provider 所属的 bit 16～17；path I/O、query、insert/create、rename/delete、
   reverse scan 作为 action mask/substatus 分别报告，不再使用一个 bool 表示 Provider 整体能力。
8. 在真实设备上验证 LocalSend/SAF、MediaStore 相册保存、文件管理器和浏览器四类 glob
   工作流；现有 LocalSend literal prefix 回归从 P0 起持续验证，但 LocalSend/相册的 glob
   contract 属于 P3 验收，不计入 P2 完成状态。

### Phase P4：动态路径后端契约、原型与可选实现

1. 按 ADR-0010 冻结 `DynamicPathBackend` 契约和 conformance suite。
2. 对 FUSE 做有界 feasibility prototype，只针对 capability probe 通过的 ROM/Provider 版本。
3. 评估满足版本化 UAPI 门槛的社区内核 adapter，不把任一项目写入核心规则语义。
4. 原型补齐 readdir/lookup/create/rename/unlink、请求身份和反向映射后才能参与比较。
5. 只有完整 conformance probe 通过后才设置 ADR-0012 的 bit 18；将 direct syscall 覆盖纳入
   active 判定，未通过时 `complete` 保持 unsupported。
6. FUSE 或社区 adapter 产品化需要新 ADR，不因原型存在自动进入发布范围。

### Phase P5：异步 export（可选）

1. 定义 `export_rules` 和任务队列，复用 selector/Pattern Engine。
2. 使用 FID/file handle 优先的文件身份、幂等 key 和 bounded retry。
3. 明确 copy/move/trash、媒体扫描、跨文件系统回滚和队列溢出语义。

## 11. 测试矩阵

### 11.0 强制前后对比门

每个可能影响行为的 change 必须先声明 `affected_scenarios = [C1..C6]`，并生成一份受版本控制的
对比报告。报告至少包含：

```text
change id / before commit / after commit
device + Android + kernel + root framework + SELinux state
rules source hash / policy hash / module ABI hashes
scenario id / exact steps / expected result
before actual / after actual
classification = unchanged | planned_break | unexpected_regression
evidence paths / reviewer conclusion
```

执行规则：

- before 必须来自改动前可执行提交，不能在代码改完后凭记忆补写；
- host 使用 `tests/baseline/host-tests.md` 的 Release 基线并增加本 change 受影响专项；
- device 沿用 `tests/device/r1-safety-validation.md` 的环境、hash、mountinfo、runtime status 和残留
  检查方式；场景涉及 Provider 时同时采集 Binder caller UID、query/create/open 和最终文件位置；
- after 使用语义等价的新 schema 配置，不要求配置文本或接口签名相同；
- planned break 必须能回指本文或对应 ADR 的明确决策，并同步修改测试、配置和文档；
- 任一 C1～C6 出现 `unexpected_regression`，或证据不足以分类时，change 不通过。

### 11.1 编译器

- `*.jpg`、`**/*.jpg`、`**/IMG_*.png` 的组件边界；
- UTF-8 scalar、大小写敏感、无 normalization、dotfile、转义 `\\*` 和 invalid UTF-8 稳定诊断；
- `[abc]`、`[a-z]`、`[!abc]`、`[^abc]` 的 bitmap/canonical dedup/交集/specificity；非 ASCII
  class、逆序 range、空/未闭合 class 和 POSIX named class 必须拒绝；
- brace 单组/多组展开、canonical dedup、稳定 RuleId、32/64 KiB 边界、嵌套/空项/sequence
  拒绝；超限不得产生部分 policy，policy 中不得出现 BRACE token 或 expansion metadata；
- 非法 `..`、空组件、绝对路径、超长 token、非法 `**`；
- leading `!`、extglob、正则、环境变量、命令替换和 tilde expansion 必须拒绝；
- selector 去重、同优先级交集、动作冲突、deny 覆盖、循环和目标碰撞；
- 单 root 大量 `**/*`、无前缀/后缀 pattern 和 bucket candidate 超限必须编译失败；
- 两个 package 使用相同 selector 时 ActionTable/UID scope 不串包；
- format 1/v5 输入在切换后稳定拒绝；语义等价 format 2/v6 配置重放 C1～C5 成功；
- literal/glob 多源同 target 编译成功；静态唯一与 provenance reverse mode 选择确定；
- format 6 保留 little-endian/CRC/canonical generation 等安全属性，v5/v6 reader 均严格拒绝
  非本版本、未知字段和非零 reserved 字段；
- `except`/except ref 必须按 ADR-0015 的 canonical、预算和引用范围通过专项 golden；
- fuzz 不产生越界、指数匹配或非确定性诊断。

### 11.2 Pattern Engine 与 Action Evaluator

- open/read/write/create/stat/access/opendir/readdir；
- mkdir 后首次创建文件；
- rename source->source、source->target、target->source；
- unlink、truncate、chmod/chown、inotify watch；
- caller UID 不匹配时完全透传；
- identity/attribution scope miss 和 candidate bucket miss 不调用 token matcher；
- 0/1/multi selector 优化路径与不折叠的参考 evaluator 进行 property equivalence 验证；
- glob deny 与 glob/literal redirect 同时命中时返回拒绝；
- 同一 selector 被 deny、observe 和 export 引用时只执行一次 pattern match；
- 目标目录不存在、symlink、跨 user、跨 mount 时的失败模式。
- reload 并发期间每次操作只看到一个完整 plan generation；旧 snapshot 在最后一个 reader
  退出前不回收，线程退出/slot 耗尽不产生 use-after-free；
- 弱内存模型压力测试覆盖 hazard publish 后的二次 active 校验、地址释放/复用竞争、Zygote
  fork 后固定 slot array 的 owner/hazard 状态重建；
- rename/link 两个 operand 在并发 reload 时必须使用同一 snapshot，不能出现半旧半新决策；
- app-path 128 slots、Provider 256 slots、retired 8 个/8 MiB 的边界与耗尽故障注入；status
  必须报告 slot/retire high-water mark 和拒绝计数；
- bit 19 与 operation mask 分别故障注入，adapter 整体失败和单项 API 缺失必须可区分；
- collision、capability missing 和 budget exceeded 返回稳定 DecisionReason/RuleId。
- observe 与 Deny/Redirect/Pass 的 effect 组合确定；Deny 永不执行原始写、redirect 或 export；
- shared UID 无 attribution、可信 attribution 和冲突 package rules 三种情况均不得串包；
- 多源 route provenance 的 prepare/commit/abort、rename、delete、reload 和 generation 失效；失败
  create 不留记录，缺失/陈旧 provenance 返回 `AmbiguousReverse`，不得回退到 canonical source；
- mount backend 在首个 mutation 前按整个 ProcessPlan 选择，提交后故障不得逐规则降级，rollback
  失败必须产生 namespace taint。

### 11.3 真机

- LocalSend 图片、视频、压缩包和同名文件；
- LocalSend 分别从 Pictures、Download 和其他声明源写入同一 target，重启 Provider/daemon 后
  query/open 仍依据 provenance 恢复正确逻辑源；同名 target 返回 `EEXIST`；
- provenance 在 prepare 后、文件创建后、commit 前和 rename 更新中分别注入进程崩溃；恢复后
  不得出现已提交文件无来源、失败文件残留 owner 或同一 target 多 owner；
- 浏览器 Download 的 apk/zip/pdf 分流；
- 相册保存到 Pictures 的 jpg/png/heic；
- 文件管理器复制、移动、重命名和删除；
- MediaProvider/ExternalStorageProvider 重启后规则恢复；
- DocumentsProvider query/create/open 分离流程保持 virtual path、document ID 和实际 FD 一致；
- Provider Hook 失败、FUSE hook inactive、模块禁用时 fail-open；
- openat2 缺失、只缺某个 resolve flag、component walk fallback 三种能力组合；
- Provider caller UID/query-insert composite bit 及 path I/O、query、insert、rename、reverse
  action substatus 分别故障注入，稳定 bits、action mask 与 admission 精确一致；
- 多 user、work profile、shared UID 和 Android 版本差异。
- FUSE passthrough 下 open/create 固定 route，policy reload 后已打开 FD 不切换目标；
- MediaProvider Mainline 模块独立升级后重新 probe，不复用仅按 Android/OEM 命中的缓存。

### 11.4 性能与稳定性

- 无 pattern 规则时热路径与当前 literal redirect 基准相同；
- identity/attribution/candidate miss 不进入 matcher，NoMatch/普通透传不产生逐操作日志、审计 I/O 或
  队列写入；
- 1/16/64/256 条 pattern 的 P50/P95/P99；
- 同 root 64 candidates、32 个退化 pattern 和 1000 条应拒绝 pattern 的最坏情况；
- Provider 线程不发生每次匹配堆分配；
- 分别测量 static-unique reverse 与 provenance prepare/commit 的 P50/P95/P99 和持久化开销；
  provenance 不允许异步到“文件已对调用方成功但来源尚未提交”的窗口；
- 高并发创建和 rename 不死锁、不重复创建、不崩溃；
- Deny、Redirect、Collision、Unsupported/CapabilityMissing 和内部失败进入有界、限速、异步
  审计；显式 observe 按独立采样策略记录；
- pattern token、候选数、route decision 和失败原因的诊断不得在文件操作线程执行同步 I/O。

CI gate 分为结构性和性能两类。结构性 gate（零分配、empty plan/identity/attribution/candidate miss 直接
bypass、candidate/transition 上限）从 P0 起强制；性能 gate 使用固定 runner 的基线百分比和设备专项数据，
不得用第三方 README 中的纳秒/微秒数字替代本项目测量。

## 12. 已知限制与取舍

1. 单纯目录 bind 不能实现文件 glob；任何宣称“只增加 mount 规则即可支持后缀分流”的
   方案都是错误建模。
2. libc Hook 不能覆盖静态链接、直接 syscall 或未覆盖的 native 入口；因此必须公开
   capability 状态，不能把 partial hook 标成全功能 active。
3. 多源到同一目标在正向写入上可行；反向使用 static unique 或持久 provenance。来源记录缺失、
   陈旧或身份不匹配时只能报告歧义，不能恢复虚假 canonical source。
4. glob deny 只有在声明的 enforcement scope 完整可用时才能标为 active；provider scope
   不能冒充完整文件系统 deny。
5. fanotify move 只能作为用户显式选择的异步 export，不作为 redirect fallback。
6. FUSE/VFS 后端覆盖面更高，但依赖 ROM、内核和 Mainline Provider 版本，必须独立维护
   capability matrix。
7. Provider 只拿到 shared UID 时无法天然区分同 UID package；没有可信 attribution 的 package
   级隔离必须保持 unsupported，不能靠规则包名猜测。
8. 有界反选已由 ADR-0015 接受；正式语法包含显式 `select.except`，但不支持任何
   `!pattern` 简写、顺序反转或运行时 NOT token。
9. format 2/v6 是破坏性切换，不提供生产兼容 reader 或自动迁移工具；安全来自完整 change set、
   格式拒绝和 C1～C6 前后重放，不来自长期维护旧协议。

## 13. 评审意见吸收决策

| 评审建议 | 决策 | 落地方式 |
| --- | --- | --- |
| ADR 与当前项目冲突时以 ADR 为准 | 不采纳 | 代码/真机确定当前事实；最新需求和官方契约决定目标，冲突 ADR 必须修订 |
| 将所有路径后端统一为一条实现/fallback 链 | 不采纳 | 统一 selector、decision、admission 和诊断；mount/app-path/provider/complete_vfs/event 五域分离 |
| policy format 6 已可执行 | 不采纳 | 当前唯一基线是 rules format 1 + policy v5/schema 2；v6 是 P1 目标 |
| 未发布阶段继续维护 format 1/v5 兼容 | 不采纳 | format 2/v6 协调切换，删除双 reader/自动迁移；用 C1～C6 前后重放保护行为 |
| v6 保留独立 MountTable/EventTable | 不采纳 | 全部规则统一为 SelectorTable + ActionTable，由 execution domain 物化后端 plan |
| 破坏性重构只跑改后测试 | 不采纳 | 受影响场景必须先保存 before 证据，再同场景 replay 和差异分类 |
| P4 前重评内核 VFS 生态 | 已决策 | ADR-0010 采用部分跟随；核心零依赖，FUSE 先原型后另行批准产品化 |
| openat2 一次 probe 并缓存 | 采纳并修正 | 观察结果进入 runtime capability snapshot，不写入可移植 policy |
| 提前冻结 pattern/bucket budget | 采纳 | 5.4 给出首版硬上限，编译器和 reader 双重校验 |
| 把安全上限写入 header | 修正后不采纳 | header 只写待验证 count；实际 ceiling 由 reader 固定，避免把输入误当协商配额 |
| RCU 式原子切换 | 已决策 | ADR-0011：immutable snapshot + TLS hazard pointer + 顺序一致原子发布/安全回收 |
| MatchSet 携带排序信息 | 采纳 | 携带 specificity 与 action range，禁止 evaluator 反查/重排 |
| collision 提供可读诊断 | 采纳 | DecisionReason + RuleId/SelectorId + generation |
| 多源同 target 因 tail 可能相交而编译拒绝 | 不采纳 | 前向允许并 reject 动态碰撞；反向采用 static unique 或事务性 route provenance |
| enforcement 增加中间态 | 不改变语义，吸收诊断需求 | 保留 provider/complete，另输出细粒度 capability bitmap/status |
| Provider bool 拆分 | 已决策 | ADR-0012：provider intent 与 caller UID/query-insert/FUSE complete 三个准入位分离 |
| app-path capability 悬空 | 已决策 | ADR-0013：bit 19 冻结 adapter baseline，具体 API 由 execution domain + operation mask 准入 |
| glob 核心语法与 brace 边界 | 已决策 | ADR-0014：字符类进入 Glob v1；brace 只在宿主编译期有界展开；否定/extglob 不进入 pattern |
| selector 反选直接进入 v6 | 已采纳 | ADR-0015 已 Accepted；format 6 首版编码 canonical except ref，不保留占位兼容分支 |
| 多源反向选择 canonical source | 不采纳 | ADR-0017：按 strong object identity 持久追踪 owner；无法证明唯一时返回 `AmbiguousReverse` |
| 每次操作检查 capability | 修正后采纳 | probe/admission 在 snapshot 发布前完成，运行时只消费已准入 action |
| UID 校验和索引分成两步 | 修正后采纳 | `(caller_uid,user_id)` 是最低可信 identity；package attribution 可信时才进子桶，scope/bucket miss 不进入 matcher |
| observe/export 与 Deny/Redirect 共用单一 Decision 枚举 | 不采纳 | 主处置使用 Pass/Deny/Redirect，observe/export 作为独立 effect；Deny 禁止 export |
| Provider path Hook 等于完整 Provider glob 能力 | 不采纳 | DocumentsProvider query/create/open 和 reverse mapping 必须组合准入；当前 literal virtualization 只作基线 |
| FUSE 每次 read/write 都会经过 daemon | 不采纳 | passthrough 可能绕过后续 daemon I/O；route 在 open/create 固定到 handle/FD 生命周期 |
| 单命中由 Pattern Engine 直接产出 Decision | 不采纳接口耦合，吸收快路径 | 保留 matcher/evaluator 分离；orchestrator 对 0/1/multi 命中走语义等价的有界路径 |
| 透传结果全量日志 | 不采纳 | NoMatch 静默；仅动作、异常和显式 observe 进入有界异步审计 |
| export 使用 inode key | 采纳并加强 | 优先 FID/file handle；dev/ino/ctime 仅作为经过 probe 的 fallback |
| 直接依赖 NoMount/ZeroMount/Kasumi | 暂不采纳 | 目标域、UAPI 和维护风险尚不满足硬依赖条件，保留 optional adapter |
| 采用第三方 benchmark 绝对数字 | 不采纳 | 只吸收算法性质；阈值由本项目固定 runner/真机基线冻结 |

## 14. 结论

最小而正确的重构不是把 `*` 分别塞进 `deny`、`redirect` 字符串，而是建立独立 Pattern
Engine，并让动作层引用 selector：

```text
verified identity + path selector -> candidate index -> Pattern Engine -> MatchSet
                                                        |
                                                        v
                                                 ActionEvaluator
                                      primary: Pass | Deny | Redirect
                                      effects: Observe | Export
                                                        |
                                                        v
                         admitted execution domain: mount/app_path/provider/
                                                    complete_vfs/event
```

各动作共享 glob 编译、可信主体 scope、路径安全、匹配索引、Decision 和 admission 外壳，但
不共享身份来源、事务、生命周期或错误的执行语义。这样既能
实现“不同后缀、文件名落到不同目录”，又能保留当前 LocalSend 已验证的目录重定向和
多应用通用能力，也能让 deny 和未来功能复用同一套模式匹配，并为后续 FUSE/VFS 高覆盖
后端留下清晰接口。
