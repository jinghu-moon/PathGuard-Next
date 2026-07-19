# PathGuard Next 参考项目说明

> 状态：Draft
>
> 对应下载脚本：`download_reference_repos.sh`
>
> 用途：记录每个参考项目的功能定位、技术路线，以及对 PathGuard Next 架构设计的具体参考价值。供 Phase 0A 平台预研阅读，不作为迁移或直接复用代码的依据。
>
> 源码复核版本：0.2（2026-07-19）

## 使用说明

- 本文档按脚本中的分类（A–F）组织，每个项目包含：**功能定位**、**技术路线**、**参考价值**、**注意事项**四部分。
- "参考价值"部分尽量对应架构设计文档的具体章节，方便交叉查阅。
- 标注了限制性许可证的项目，仅作阅读理解架构之用，不摘取代码、不再分发。
- 本文档基于公开 README、变更日志和文档整理，不替代真机验证；具体兼容性结论仍需 Phase 0 实测确认。

## 源码复核说明

本轮复核后，参考项目的“参考价值”按证据等级解释：

- **平台事实**：AOSP、Magisk、KernelSU 源码和 API 注释，可作为机制依据，但仍不替代目标设备测试。
- **实现样本**：NeoZygisk、NoHello、rvmm 等，必须结合具体源码、错误处理和生命周期阅读，不能只按 README 的功能描述判断正确性。
- **历史经验**：Storage Isolation、HMA 等 changelog/issue，只用于判断维护成本和产品边界，不作为当前 Android 行为保证。
- **内核路线**：SUSFS、NoMount、APatch 说明另一组能力和代价，不进入 PathGuard Next 用户态核心。

本地参考目录中的 `ZygiskNext` 仅包含 README，不能引用为源码级结论。所有“可直接参考”“完整、正确”等表述均以本节证据等级为前提。

---

## A 类：同路线 —— Zygisk / mount namespace 控制

这一类是和 PathGuard Next 后端选型（ADR-002）最直接对应的项目，都是通过控制应用进程的 mount namespace 来实现挂载可见性管理，值得逐个精读源码。

### A1. NeoZygisk（`JingMatrix/NeoZygisk`）

**功能定位**：为 KernelSU 和 APatch 提供 Zygisk API 支持的独立实现，也可作为 Magisk 内置 Zygisk 的替代品。核心功能是 DenyList——控制指定应用进程是否能看到 Root 和模块相关的挂载修改。

**技术路线**：通过 `ptrace` 注入 Zygote，在应用派生流程中控制 mount namespace。其核心不是“Root companion 进入每个目标 namespace 后逐条挂载”，而是由 daemon 预先创建并缓存 clean/root namespace FD，在 `unshare(CLONE_NEWNS)` 相关流程中将目标进程切换到相应模板；同时也支持在当前 namespace 直接反向卸载挂载点。

**参考价值**：
- 对应文档 **6.1 节**和 **8 节**：重点参考它的 namespace FD 缓存、`setns` 封装、挂载 ID 逆序卸载和 helper 生命周期。它与 PathGuard 的“策略挂载计划”不是同一时序，不能把 NeoZygisk 的模板切换直接当作逐规则挂载实现。
- 对应 **8.3 节失败策略**：NeoZygisk 在直接卸载不安全或失败时回退到缓存 namespace，这类“先能力判断、再选择执行路径”的经验可用于完善 PathGuard 的 capability probe；但 PathGuard 不应因此引入隐式弱后端 fallback。
- 对应 **19 节能力探测**：README 里明确列出了 DenyList 依赖的具体开关（Magisk 的 Enforce DenyList、KernelSU/APatch 的 Umount modules 选项），可以类比你的 capability bitset 需要探测哪些前置开关状态。

**注意事项**：项目活跃，issue 区有大量厂商兼容性讨论。其实现还包含 Root 隐藏、模块清理和 Zygisk 框架职责，不能按文件整体迁移。重点阅读 `zygiskd/src/mount.rs`、`zygiskd/src/companion.rs` 和 loader 的 namespace 切换逻辑。

### A2. ZygiskNext 原版（`Dr-TSNG/ZygiskNext`）

**功能定位**：NeoZygisk 的上游项目，同样是 Zygisk 的独立实现。

**技术路线**：与 NeoZygisk 相同的技术基础（NeoZygisk 是从这里 fork 出来的）。

**参考价值**：作为 A1 的对照，可以看两个项目分叉后各自的架构演进方向有什么不同，帮助判断哪些设计决策是"共识"、哪些是分叉后的个人选择。

**注意事项**：当前参考目录中的 `ZygiskNext` 只有 README，没有完整源码，不能据此作源码级结论。该项目从 v1.4 起改用限制性许可证，明确禁止任何 fork 冒充官方或非官方继承者。只读参考架构思路即可，不要摘取代码或复用到自己的项目中。

### A3. Magisk 官方源码（`topjohnwu/Magisk`）

**功能定位**：Magisk 本体，Root 方案 + 模块系统 + Zygisk 框架的官方实现。

**技术路线**：通过修改 boot image 的 ramdisk 实现系统性修改；Zygisk 部分通过替换 `app_process` 实现向 Zygote 的注入。

**参考价值**：
- 对应 **6.1/8 节**：Zygisk 的 `preAppSpecialize`/`postAppSpecialize` API 本身就是在这里定义的，是所有 A 类项目共同依赖的基础，读这里能确认 API 的原始语义边界（哪些操作在哪个回调里是被允许的），避免仅参考二手实现时理解跑偏。
- 对应 **7.1 节**：Magic Mount 的实现（`native/jni/core/module.cpp`）展示了 Magisk 自己如何用 tmpfs + bind mount 组合出模块叠加效果，可以类比你 deny 语义里"专用 deny 源目录 + bind mount + 只读重新挂载"的实现细节，尤其是权限和挂载标志的设置顺序。

**注意事项**：当前源码布局应重点阅读 `native/src/core/zygisk/api.hpp`、`module.cpp` 和 `hook.cpp`。API 注释明确指出：`preAppSpecialize` 发生在应用沙箱限制生效前，进程仍具有 zygote 权限；`postAppSpecialize` 已具有目标应用权限。需要 Root companion、模块目录 FD 或 `exemptFd` 的准备工作应放在 pre 阶段，post 阶段只适合收尾和卸载模块。不能按旧目录名或二手教程推断时序。

### A4. NoHello（`MhmRdd/NoHello`）

**功能定位**：轻量级 Zygisk 模块，用于隐藏 Root 存在。

**技术路线**：引入了"Mount Rule System"，允许用户按挂载点的 root path、mount path、文件系统类型、source 等属性定义规则，决定该挂载点在特定条件下是否被卸载。

**参考价值**：

- 对应 **8 节进程启动流程**：它在 `preAppSpecialize` 主动执行 `unshare(CLONE_NEWNS | CLONE_NEWCGROUP)`，设置挂载传播，再同步等待 companion fork helper 进入目标 namespace 完成卸载；同时临时 Hook Android 后续的 `unshare`，清除重复 namespace 标志。这比 rvmm 更接近 PathGuard Phase 0B 所需的同步执行模型。
- 对应 **11 节策略编译与验证**：它按 root path、mount point、文件系统类型和 source 匹配 mountinfo，并按反向顺序卸载嵌套挂载，可作为挂载计划排序的参考。
- 对应 **16 节失败处理**：companion 失败后回到当前进程执行 fallback，展示了同步状态返回的重要性；PathGuard 只借鉴同步协议，不采用其隐藏逻辑或隐式 fallback。

**注意事项**：项目同时处理 Root 痕迹、FD 清理、ptrace 状态、PLT Hook、cgroup 和文件系统错误选项，复杂度远高于 PathGuard 所需。只参考 namespace/companion 时序和 mountinfo 排序。README 提到白名单模式下每次进程启动评估 Mount Rule System 可能引发性能和发热问题，可作为“不要在未命中应用路径解析文本规则”的反例。

### A5. rvmm-zygisk-mount（`j-hc/rvmm-zygisk-mount`）

**功能定位**：一个用途单一的 Zygisk 模块，在 `preAppSpecialize` 阶段为特定应用注入挂载。

**技术路线**：只做一件事——为目标应用（ReVanced 相关模块）在 fork 早期注入固定的挂载点。

**参考价值**：代码量极小，可用于观察 `preAppSpecialize -> connectCompanion -> setns -> mount` 的最短调用链，以及 ABI-specific companion 的基本接口。

**注意事项**：它不是“完整、正确的最小实现”，只能作为实验反例。当前实现存在至少三项风险：

- companion 异步 fork 后不向应用侧确认挂载完成，存在启动竞态。
- 请求只携带 PID 和进程名，缺少 pidfd、starttime、UID/package 联合校验。
- `receiveProcInfo()` 返回指向配置缓冲区的 `src/dst` 指针，随后释放缓冲区，子进程继续使用这些指针可能产生悬空引用。

Phase 0B 不得直接照抄该项目；必须改为有界消息、同步确认、短生命周期 helper、PID 复用校验和失败回滚。

---

## B 类：旧一代 Hook 路线（反面教材）

这一类项目代表 PathGuard Next 明确决定不采用的技术路线——基于 Riru/Xposed 的 Hook + Binder 拦截。读它们的价值不在于复用实现，而在于理解"为什么这条路线的长期维护成本高"，为 ADR-001、ADR-005 提供具体依据。

### B1. riru_storage_redirect（`Magisk-Modules-Repo/riru_storage_redirect`）

**功能定位**：Rikka 的 Storage Isolation（现名 Storage Redirect）项目早期基于 Riru 的开源增强模块，为应用提供隔离存储视图，防止应用把存储搞乱。

**技术路线**：通过 Riru 注入 Zygote，Hook `untrusted_app` 域进程的 Binder 事务，拦截和过滤对 MediaProvider 的查询和调用，实现路径级的访问控制。

**参考价值**：
- 对应文档 **1 节和 23 节**中"不迁移旧项目的 PLT Hook、Binder Parcel 修改"的判断：这个项目的变更历史完整记录了走 Hook 路线要为每个 Android 大版本单独适配 Media Storage 拦截逻辑，也记录过在华为等厂商 ROM 上隔离会随机失效且难以定位原因的案例。这些具体的历史教训可以直接引用来支撑架构决策的合理性，而不只是空泛地说"Hook 路线维护成本高"。
- 对应 **7.4 节 MediaStore/SAF 边界**：它是极少数真正尝试过"把 MediaStore 兼容做进核心"的项目，它的功能列表膨胀过程（Media Storage 处理、应用交互修复、导出规则……）正好印证了 7.4 节"把 provider 兼容层并入核心会形成持续维护面"这个判断的现实依据。

**注意事项**：这是已停止更新的历史开源版本，当前 Storage Redirect 主体已闭源商业化，不要参考其现状功能列表作为"用户期望的完整功能集"，那会把你重新拖回旧路线的功能陷阱。

### B2. HMA-OSS（`frknkrc44/HMA-OSS`）

**功能定位**：Hide My Applist 的开源分支，用于隐藏应用列表检测、设置项、包安装器等。

**技术路线**：LSPosed/Zygisk 模块，通过 Hook 拦截应用对包管理器和文件系统的查询调用。

**参考价值**：目标虽然不同（隐藏应用可见性而非文件目录隔离），但技术路线和 B1 同属 Hook 类。可以作为观察"Hook 类方案在 Android 13+ 新增反检测保护后如何持续被针对性加固追着跑"的另一个样本，进一步佐证 Hook 路线的长期维护负担不是个例。

**注意事项**：功能域和 PathGuard Next 差异较大，不需要深读实现，浏览提交历史里的适配性 commit 即可。

### B3. Hide-My-Applist 原版（`Dr-TSNG/Hide-My-Applist`）

**功能定位**：拦截应用列表检测的 Xposed 模块，是 B2 的上游原版。

**技术路线**：同 B2。

**参考价值**：与 A2 类似，作为对照了解分叉后两个项目的设计取向差异。

**注意事项**：该项目从 v3.4 起改用限制性许可证，明确禁止修改、再分发和摘取代码片段用于其他项目。只用于阅读理解，不要复制任何代码。

---

## C 类：内核级路线（取舍参考，明确不采用）

这一类项目代表比 mount namespace 更彻底、但也要求更多前置条件（定制内核）的路线。读它们的目的是为 5.6 节"与内核补丁路线的取舍"提供具体、可核实的技术依据，而不是评估要不要采用。

### C1. susfs4ksu-module（`sidex15/susfs4ksu-module`）

**功能定位**：为 KernelSU 提供的 SUSFS（内核文件系统补丁）配套用户态模块，用于路径隐藏、挂载隐藏和路径重定向。

**技术路线**：依赖一个打了 SUSFS patch 的定制内核，通过 `sus_path`、`sus_mount`、`sus_open_redirect` 等配置项在内核层面直接处理路径解析和挂载表可见性。

**参考价值**：对应 **5.6 节 ADR-009**：这是"要求定制内核"这条路径的直接证据来源——它的 README 和 issue 模板都明确要求用户提供"SUSFS patched kernel"，issue 里大量关于"内核版本不匹配导致模块失效"的报告，具体印证了 5.6 节"以更大设备覆盖面换取内核补丁能力"这个取舍判断的现实合理性。

**注意事项**：不要花时间读具体实现代码，读文档和 issue 了解"需要什么前置条件、解决了什么问题"就够，这条路线本身已经被 ADR-009 排除在核心方案之外。

### C2. susfs4ksu 内核补丁（GitHub 镜像 `Star-Seven/susfs4ksu`）

**功能定位**：C1 依赖的内核补丁本体。

**技术路线**：直接修改内核符号表和 VFS 相关代码路径，实现路径隐藏和重定向。

**参考价值**：了解"内核级方案到底要改多深"的直观印象——对比你自己纯用户态 + 标准 namespace API 的方案，能更清楚地说明为什么 PathGuard Next 选择的路线对设备的侵入性小得多。

**注意事项**：原始仓库在 GitLab（`gitlab.com/simonpunk/susfs4ksu`），按内核版本分了多个分支维护，本仓库是 GitHub 镜像，可能不是最新内容，只用于快速浏览代码结构，不作为准确性依据。

### C3. NoMount（`maxsteeel/nomount`）

**功能定位**：内核级文件注入和路径重定向框架，专门设计为不触碰挂载表。

**技术路线**：完全不调用 `mount()` 系统调用，而是在内核 VFS 层处理路径解析和虚拟目录。实际补丁面不止 `iterate_dir`、`getname`，还涉及 `generic_permission`、`inode_permission`、`d_path`、`getattr`、mmap 元数据和 `statfs` 等路径，以维持虚拟路径、权限、设备号和文件系统信息的一致性。

**参考价值**：对应 **5.5 节挂载可见性**的技术背景资料。它证明避免 mountinfo 暴露需要在 VFS 多个入口保持虚拟路径与真实路径一致，而不是简单过滤一个 procfs 文件。可以作为未来内核后端评估复杂度的旁证。

**注意事项**：内核态改动完全超出 PathGuard Next 的项目边界（C++20 用户态 + 标准 Root 能力），不需要理解实现细节，了解设计目标即可。

### C4. APatch（`bmax121/APatch`）

**功能定位**：融合 Magisk 便捷安装方式和 KernelSU 内核修补能力的 Root 方案，依赖底层的 KernelPatch。

**技术路线**：不需要设备内核源码，只需 stock boot.img；核心是 KernelPatch 引入的新系统调用 SuperCall，配合 KPM（Kernel Patch Module）支持内核态的 inline-hook 和 syscall-table-hook；可选择不修改 SELinux 上下文，通过 Hook 绕过而非依赖策略修改。

**参考价值**：对应 **5.6 节**取舍谱系里此前缺失的第三个点——它既不要求内核源码级补丁（比 SUSFS 门槛低），又不是纯用户态 mount namespace（比你现在的方案侵入性更强，直接做内核态 syscall Hook）。可以在 5.6 节明确写一句排除理由：不采用是因为它引入了内核态 Hook 的攻击面和稳定性风险，与"Zygisk 模块禁止 JSON 解析、Native Hook 只做挂载注入"这类工程约束（22 节）的克制精神相悖。

**注意事项**：仅做架构对照阅读，不涉及任何代码复用。

---

## D 类：底层平台与 Root 基础设施

这一类是 PathGuard Next 运行所依赖的平台机制本身，属于第一手权威资料，优先级高于任何第三方模块的实现。

### D1. KernelSU（`tiann/KernelSU`）

**功能定位**：基于内核模块（LKM）或 GKI 内核集成的 Root 方案。

**技术路线**：通过内核态代码授予特定进程 Root 权限，配合用户态守护进程和 Manager App 完成权限管理；App Profile 功能里包含"Umount modules"选项，控制指定应用是否能看到模块相关挂载。

**参考价值**：
- 对应 **8.4 节 companion helper**：KernelSU 的内核侧重点是校验当前 task、UID、zygote 派生来源以及 mount namespace 模式。它不能替代 PathGuard daemon 对外部 PID 请求执行的 `pidfd + starttime + UID/package` 联合校验；PathGuard 的 start time 方案应自行设计和测试，不能宣称来自 KernelSU 的现成实现。
- 对应 **19 节能力探测**：官方文档明确说明了"Umount modules"这个行为受内核版本影响——低于 5.10 的内核可能需要 `path_umount` 的回移支持才能真正执行卸载，这类"同一个 API 在不同内核版本上行为不一致"的具体案例，正是你 26 节待验证问题里需要通过真机数据回答的那类问题的典型样本。

**注意事项**：这是你目标 Root 方案之一（另一个是 Magisk），建议作为长期跟踪对象而不只是一次性阅读，后续内核版本、SELinux 策略变化都可能影响 PathGuard Next 的兼容矩阵。

源码补充：KernelSU 的 `su_mount_ns.c` 展示了 `global` 和 `individual` 两种 namespace 模式；`individual` 模式执行 `unshare(CLONE_NEWNS)` 并将根挂载设为 `MS_PRIVATE | MS_REC`。其 `kernel_umount.c` 还特别限制处理 zygote 派生的 app/isolated process，避免误伤只是切换 UID 的全局 namespace 进程。这些是 PathGuard 身份与能力探测的参考，不是可直接调用的通用 API。

### D2. AOSP MediaProvider（`aosp-mirror/platform_packages_providers_mediaprovider`）

**功能定位**：Android 官方的媒体存储提供者实现，包含 FUSE daemon（`jni/FuseDaemon.cpp`）和 MediaProvider 服务本体。

**技术路线**：Android 11+ 上，共享存储的路径访问由这里的 FUSE handler 拦截处理，同时维护一份媒体数据库供 MediaStore API 查询。

**参考价值**：对应 **7.4 节**的一手代码依据。文档引用的 AOSP Scoped Storage 说明文档是这份代码的用户可读版本，直接读 `FuseDaemon.cpp` 能看到具体哪些文件操作会经过 FUSE 层的额外处理、哪些会触发数据库同步，这比只读文档更能精确定位"bind mount 改变了路径解析结果，但改变不了什么"的确切边界，为 7.4 节和 26 节问题 10（各 ROM 上 MediaStore/SAF 与直接路径视图差异）的真机测试设计提供代码级依据。

**注意事项**：仓库体量很大，只需要看 `jni/` 目录下和路径解析、权限检查相关的部分，不需要通读整个 MediaProvider 服务实现。

---

## E 类：控制协议 / IPC 参考

这一类项目本身和文件隔离无关，但控制协议、本地 IPC 安全校验的设计思路可以直接复用到 PathGuard Next 的控制面实现上。

### E1. Shizuku（`RikkaApps/Shizuku`）

**功能定位**：让普通应用通过 IPC 使用 Root 或 ADB 授予的高权限 API 的服务框架。

**技术路线**：Root 或 ADB 启动一个 Java 服务进程，通过 Binder 把自己的接口暴露给系统服务，客户端应用通过 Shizuku API 获取这个 Binder 引用后即可直接调用高权限操作，无需自己维持 Root shell。

**参考价值**：对应 **13 节控制协议**的设计参考——虽然 Shizuku 用的是 Binder 而不是你选择的 Unix Domain Socket，但它"请求方身份如何被验证、服务端如何拒绝未授权调用"的模型和你的设计目标一致，值得参考它的权限校验流程作为 UDS 消息设计的补充视角。

**注意事项**：不需要理解 Binder 相关的实现细节（那部分和你的技术选型无关），重点看它的权限模型和客户端-服务端信任边界设计。

### E2. Sui（`RikkaApps/Sui`）

**功能定位**：Shizuku API 的另一种 Root 接口实现，作为 Magisk 模块直接提供服务，无需用户手动启动 Shizuku。

**技术路线**：与 Shizuku 共享 API，但通过 Magisk 模块在开机时自动启动服务进程。

**参考价值**：对应 **8.4 节**和 **21 节**——作为 Magisk 模块随系统启动的服务进程，如何在没有用户交互的情况下建立信任链、如何处理模块生命周期与服务生命周期的绑定关系，这和你 daemon（`pathguardd`）作为常驻服务的设计目标相似，可以参考它的模块化启动方式。

**注意事项**：代码复杂度较高（README 里也提到"这个 App 复杂度很高"），建议只看服务启动和权限校验部分，不必通读全部。

---

## F 类：非 Root 架构对照组

这一类项目完全不依赖 Root，用 Android 系统原生机制实现类似"隔离"效果。它们不是 PathGuard Next 的候选实现路线，但读它们有助于精确回答"为什么不用不需要 Root 的方案"这个大概率会被用户或评审问到的问题。

### F1. Shelter（`PeterCxy/Shelter`）

**功能定位**：利用 Android 原生 Work Profile 功能提供隔离空间，可以把应用安装或克隆到隔离空间中，互不干扰。

**技术路线**：完全基于系统提供的 Work Profile（企业管理场景下的多用户机制）API，不需要 Root 权限，因此功能集受限于系统开放的 Work Profile API 能力。

**参考价值**：对应架构设计文档里没有明确写但实际隐含的一个产品定位问题——PathGuard Next 解决的是"同一用户空间内、目录级别"的细粒度隔离，而 Work Profile 方案解决的是"整个 Profile 级别"的粗粒度隔离。两者不是竞争关系而是不同粒度的解法，读这个项目能帮你在 README 或者产品说明里更精确地划出"如果你只需要把某个应用完全隔离到另一个空间，用 Work Profile 方案即可，不需要 Root；如果你需要同一应用内、按目录做差异化访问控制，才需要 PathGuard Next 这类方案"这样的边界说明。

**注意事项**：作者在项目说明中提到目前已进入"维护模式"，主要工作是跟随新 Android 版本适配，不再主动添加新功能——这也从另一个角度印证了"系统原生机制能提供的隔离粒度是有天花板的"，这个天花板正是 PathGuard Next 存在的价值所在。

### F2. Haven（`Kenneth-Cho-InfoSec/Haven`）

**功能定位**：Shelter/Island 系谱的现代化 fork，同样基于 Work Profile，重点是把代码库更新到现代 SDK 和构建工具，并提供 Material Design 3 界面。

**技术路线**：与 F1 相同的底层机制。

**参考价值**：主要用于观察同一套底层机制（Work Profile）在不同团队接手后，产品层面会往什么方向演化（这里是现代化 UI 和维护性，而不是功能扩展），可以作为 F1 的补充样本，不需要单独深读。

**注意事项**：功能上和 F1 高度重合，如果时间有限可以只读 F1。

---

## 优先级建议

如果时间有限，建议按以下顺序安排精读：

1. **Magisk 官方源码（A3）**、**NoHello（A4）** 和 **NeoZygisk（A1）** —— 分别确认 Zygisk 回调语义、同步 companion + namespace 原型和 namespace FD 管理；三者只能提取设计事实，不能整体抄作业。
2. **AOSP MediaProvider（D2）** —— 7.4 节的一手依据，读代码比读文档更精确。
3. **riru_storage_redirect（B1）** —— 反面教材里信息密度最高的一个，变更日志本身就是一部维护成本的血泪史。
4. **KernelSU（D1）** —— 长期跟踪对象，不是一次性阅读完就结束。

5. **rvmm-zygisk-mount（A5）** —— 仅用于阅读最短调用链和识别启动竞态、悬空指针等反例，不作为实现模板。

其余项目（C 类内核路线、E 类控制协议、F 类非 Root 对照组）建议只读 README 和关键文档；其中 NoMount 的 VFS 集成文档值得查看，以理解内核后端的实际复杂度。E 类项目重点看身份校验和服务生命周期，不需要迁移 Binder 实现。

## 源码复核后的直接行动项

Phase 0B 原型应按以下顺序实现和验证：

1. 按 Magisk API 语义在 `preAppSpecialize` 建立 companion 通道和应用身份上下文。
2. 在目标进程中创建独立 mount namespace，并处理 Android 后续 namespace 初始化的交互。
3. 通过同步协议把 PID、UID、进程 start time、策略 generation 发送给 Root helper。
4. helper 使用 `pidfd` 优先、`/proc/<pid>/ns/mnt` fallback，进入目标 namespace 后执行一条挂载规则。
5. 返回明确成功/失败，应用继续启动前完成回滚或进入预设失败模式。
6. 测试 mountinfo 可见性、直接 syscall、MediaStore/SAF 差异和应用退出后的挂载回收。
7. Manager App 采用 Root 硬性前置条件；通过 RootGateway 调用 `pathguardctl`，不提供非 Root 降级模式，也不把普通 App 直连 `/data/adb` UDS 作为默认方案。
8. 不把 SAF `content://` URI 直接转换为规则真实路径；不可映射的 Provider 必须返回 `unsupported-provider`。

在上述链路通过前，不实现完整规则编辑器、热更新和 provider 兼容层；原型 UI 只能使用模拟状态和明确的 generation 语义。
