# V-06 参考项目证据与禁止照搬项

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-06` |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| After commit | N/A（实现前证据归档） |
| Branch | `feature/pattern-redirect-v6` |
| Classification | `unchanged`（只读核验参考实现） |
| Reviewer conclusion | 参考项目只提供 adapter 与生命周期证据，不改变 PathGuard 核心零依赖边界 |

## 样本版本

| 项目 | 仓内版本 |
| --- | --- |
| Magisk | `14ea5cfb4a5771c742f7c3fd1e685bdbfac7aa8c` |
| KernelSU | `d523eb3236bb1c5f9397e26f8bd4acd47f7983ba` |
| MaterialCleaner | 仓内源码快照；目录无独立 `.git` 元数据，不能可靠声明 upstream commit |
| NoMount | `b431bed7a11326681b94e8e5a37523ebd4d1441e` |
| zygisk_cleanerhooks | release `v1.9.2`, versionCode `1615`；归档只含二进制、脚本与校验和 |

这些版本号只固定本次证据，不代表后续 P4 adapter 评估时的生态现状。

## 证据表

| 来源与位置 | 已验证事实 | 可借鉴契约 | PathGuard 不采用/不推断 |
| --- | --- | --- | --- |
| `refer/Magisk/native/src/core/zygisk/api.hpp:121`、`:127`、`:132` | Zygisk 明确区分 app specialize 前后的权限与生命周期；特权工作可通过 companion IPC 完成 | 在 `preAppSpecialize` 获取身份和准备只读状态，在 `postAppSpecialize` 安装应用权限下所需能力；特权动作留给 companion/daemon | 不把 specialize 回调本身当作 action 已准入，也不假设所有 root framework 都有相同回调时序 |
| `refer/Magisk/native/src/core/zygisk/api.hpp:196`、`:199`、`:283` | `DLCLOSE_MODULE_LIBRARY` 明确禁止在 Hook 函数后启用；PLT Hook 需要显式 commit | PathGuard 安装常驻 Hook 后模块代码必须保持映射；Hook commit 失败进入 adapter unsupported/fail-open | 不以卸载模块作为运行期降级方案，不把 framework 的 unload/fallback 当作 action admission fallback |
| `refer/Magisk/native/src/core/zygisk/api.hpp:301`、`:306` | companion handler 可并发运行，官方注释要求处理共享状态竞争 | companion 协议必须有界、可并发，策略发布仍由 PathGuard 单 writer 管理 | 不直接复用 Magisk 内部全局对象或依赖其未公开实现细节 |
| `refer/KernelSU/website/docs/zh_CN/guide/difference-with-magisk.md:8`、`:24` | KernelSU 与 Magisk 都使用 `/data/adb/modules`，但 KernelSU 把 system mount 委托给 metamodule | 可共享模块包的表层目录约定，安装探针需识别实际 root framework/runtime mode | 不能因目录相同就推断 mount backend、namespace 或生命周期相同 |
| `refer/KernelSU/website/docs/zh_CN/guide/module.md:8`、`:180` | KernelSU 的 system 修改依赖 metamodule/OverlayFS；文档明确其与 Magisk magic mount 差异很大 | mount adapter 必须报告自身 backend 与 transaction 状态 | PathGuard 核心不依赖 metamodule、OverlayFS 或 SUSFS；不能把 framework mount 成功等价为 pattern action 准入 |
| `refer/KernelSU/website/docs/zh_CN/guide/module.md:309`、`:344`、`:437` | KernelSU 存在 built-in、LKM、late-load 等模式，脚本执行阶段也不同 | 启动阶段和能力探测按实际 runtime mode 建模，不能只按产品名称分支 | 不硬编码“KernelSU 一定早于 Zygote”或固定 post-fs-data/service 时序 |
| `refer/MaterialCleaner-main/server/src/main/java/me/gm/cleaner/xposed/XposedInit.java:24`、`:34`、`:64` | MaterialCleaner 在 MediaStore authority attach 时建立专门的 MediaProvider hooks service | Provider adapter 应有独立生命周期、状态和 capability observation | 不照搬 Xposed/Java 反射入口到 Zygisk native adapter，不按类名存在就宣称兼容 |
| `refer/MaterialCleaner-main/server/src/main/java/me/gm/cleaner/xposed/QueryHooker.java:133`、`:134` | query 路径显式读取 MediaProvider 内部 calling identity 的 UID | Provider caller identity 必须作为独立、实测 capability；query 适配是独立操作面 | 不照搬私有字段 `mCallingIdentity`；OEM/Mainline ABI 必须通过符号/行为 probe，不按 ROM 名称猜测 |
| `refer/MaterialCleaner-main/server/src/main/java/me/gm/cleaner/xposed/InsertHooker.java:137`、`:141` | insert 路径单独请求 mounted path，并改写 MediaStore `DATA` | query/insert 映射必须与文件 FD 路由共享同一 Decision/route provenance | 不把单次 `DATA` 改写当作 query、scan、rename、FUSE 全覆盖；不复制其 regex/path owner 假设 |
| `refer/MaterialCleaner-main/shared/src/main/java/me/gm/cleaner/io/RecursiveFileListener.java:73`、`:129`、`:143` | RecursiveFileListener 有 watch 数量保护，只把事件交给 listener；它不参与同步 open/create 决策 | FileObserver/fanotify 适合 observe/export 审计与异步工作队列，并需要明确容量和丢事件语义 | 不用观察器实现同步 redirect/deny，不把收到事件等价为操作已被原子拦截或 MediaStore 已一致 |
| `refer/zygisk_cleanerhooks_v1.9.2-release/util_functions.sh:33`、`:34` | release 为每个 ABI 安装 Zygisk `.so`；源码不在归档中 | 可参考 artifact 命名、ABI 打包和安装布局 | 不能从闭源 `.so` 推断 Hook 点、线程安全、常驻正确性或 Pattern Engine 设计 |
| `refer/zygisk_cleanerhooks_v1.9.2-release/service.sh:6`、`:8` | service 脚本从应用私有目录读取启动入口和 source path | module/app 间配置交接需要显式文件存在性检查和失败隔离 | 不采用其应用私有路径、启动协议或 release 脚本作为核心策略协议 |
| `refer/NoMount/kernel/src/nomount.c:224`、`:295`、`:374` | NoMount 分别实现 `d_path` 反向呈现、输入路径 rewrite 和目录项注入 | complete VFS adapter 必须成套覆盖 lookup/getname、reverse/d_path、readdir，而不是只有正向 rewrite | 不能只复制 `getname` rewrite 就声明 `complete_vfs`；也不能把内核实现直接变成核心依赖 |
| `refer/NoMount/kernel/src/nomount.c:259`、`:472` | permission 与 stat/inode 呈现另有处理 | 完整适配还要覆盖权限、metadata identity 与错误语义 | 不假定路径改写后权限、stat、SELinux 和 inode 语义会自动正确 |
| `refer/NoMount/kernel/src/nomount.c:32`、`:35`、`:1001`、`:1011` | 实现包含 UID 过滤、RCU 哈希索引，以及 real/virtual 的 device+inode 记录 | 可借鉴只读索引、RCU 发布和稳定对象身份；route provenance 应包含足以区分来源的稳定标识 | 不照搬其“blocked UID”方向、UAPI、hash key 或内核内存模型；PathGuard 仍以自己的 selector/action/scope 契约为准 |

## 跨项目结论

### 1. Root framework 只承载，不决定动作语义

Magisk/KernelSU 可以提供模块安装、进程注入、脚本阶段、namespace 或 companion 通道，但
`deny`、`redirect`、`observe` 是否 active 必须由 PathGuard 的 execution-domain probe、action
requirements 和 admission 共同决定。framework backend 失败只能让相应 adapter
`inactive/unsupported` 并按策略 fail-open，禁止隐式切换到语义较弱的 action。

### 2. Provider、观察器和完整 VFS 是三个能力域

- Provider query/insert/scan 负责内容模型与路径映射的一致性；
- app/provider path API 负责具体同步路径操作；
- FileObserver/fanotify 只负责异步观察、审计或 export；
- complete VFS 还必须覆盖 lookup、reverse、readdir、permission、metadata 和多 operand 操作。

它们可以共享 `MatcherSnapshot`、`Decision` 和 route provenance，但不能互相冒充，也不能形成
未声明的 fallback 链。V-03 已观测到“FD 写入成功而 MediaStore scan/query 失败”，是这一边界的
直接本项目证据。

### 3. 内核 VFS 只作为可插拔 adapter 候选

NoMount 证明 VFS 层能实现更完整的虚拟视图，也同时证明覆盖面远超一次路径替换。PathGuard
保持 ADR-0010 的“部分跟随、核心零依赖、后端可插拔”：P4 前重新核验维护状态、许可证、公开
ABI、OEM/GKI 兼容性、SELinux 和完整 conformance matrix，未通过不得发布
`fuse_complete_path`/`complete_vfs` capability。

## 后续任务约束

| 约束 | 后续任务 |
| --- | --- |
| Provider caller UID、query/insert、app path、complete VFS 必须分别 probe/admit | V-28、T-19、I-19、V-29 |
| Hook commit 后保持模块常驻，失败不得用 unload 或 framework fallback 掩盖 | T-16、I-16、V-27、V-39 |
| FileObserver/fanotify 只进入 observe/export；覆盖 overflow、幂等和容量 | V-42、T-27、I-27、V-43 |
| complete VFS 必须通过 lookup/reverse/readdir/permission/metadata/rename 契约 | V-46、T-29、I-29、V-47 |
| KernelSU/Magisk/runtime mode 差异必须进入 adapter probe，不进入 Pattern Engine | V-07、V-39、V-59 |
| 共享语义只来自 Selector/Action/Decision/route provenance | V-09、V-10、T-08～T-11、I-08～I-11 |

## 验收结论

- 已覆盖任务要求的 KernelSU/Magisk module/namespace 生命周期边界；
- 已区分 MaterialCleaner Provider/MediaStore 与观察器职责；
- 已用 Magisk 官方仓内 API 和 cleanerhooks release 布局固定 Hook 常驻边界，未反编译或臆测二进制；
- 已覆盖 NoMount 的 lookup、reverse、readdir、permission、UID 与 inode/device 索引；
- 每项均记录了仓内路径/行号、可借鉴契约和禁止照搬项；
- 明确禁止把 root framework fallback 当作 action admission fallback；
- V-06 判定 `complete`。
