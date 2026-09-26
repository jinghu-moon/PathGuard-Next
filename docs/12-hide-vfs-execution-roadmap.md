# PathGuard Hide VFS 执行路线图

> 状态：Research / Execution Plan
>
> 文档版本：1.0
>
> 日期：2026-09-17
>
> 关联文档：
> - `docs/07-hide-capability-research-and-design.md`
> - `docs/10-hide-capability-phased-roadmap-and-hidelab.md`
> - `docs/11-hide-source-audit-log.md`
>
> 适用对象：PathGuard Hide 1.0-LKM 实验后端，以及未来的 Hide 2.0 原生内核集成路线

## 1. 目的与结论

本文将当前调研结果收敛成可以执行、可以暂停、可以验收的任务路线。它不是把已有
参考项目直接移植到 PathGuard，也不把“模块能够加载”当作 hide 能力通过。

当前最重要的工程结论是：

1. 对 Redmi K90 Pro Max / `myron`、Android 16、kernel 6.12、共享存储 FUSE，现有
   per-object operation-table shadow 仍是 LKM 生命周期和缓存实验的有效路线；但要
   满足严格的 namei、Java/NIO、FUSE 和 mutation 语义，固定设备的 KPM/namei 或源码级
   kernel companion 才是优先数据面。
2. mutation 不能先于生命周期基础设施实现。必须先证明
   `STOP_NEW -> RESTORE -> DRAIN -> FREE`，再接入会改变真实文件系统的 callback。
3. NoMount、Kasumi、PathMask、SUSFS 和 SukiSU 都只能提供局部参考，不能作为完整
   PathGuard hide 数据面直接引入。
4. 只读 baseline、cache-order、20 线程并发、1000 轮 reliability、规则切换、
   DISABLE/CLEAR 竞态、已有 FD、rmmod 恢复和真实 namespace 销毁已经在当前设备的
   单路径实验范围内通过；期间修复了 readdir 后正 dentry 泄漏和 ACTIVE stale dentry
   生命周期竞态。但 OTA admission 和产品集成仍未完成。
5. 在所有阶段通过前，产品状态必须保持：

```text
Hide 1.0 = unsupported
```

## 启动链路补充（2026-09-23）

前一节描述的是正式 HideLab evidence 驱动的手工准入流程；实验包现在增加了明确
隔离的固定设备自动启动路径，以满足安装模块并重启后的自动加载验证。该路径不把
动态回归结果伪装成产品准入，产品状态仍固定为 `unsupported`。

启动顺序为：

```text
post-fs-data.sh -> boot-state
service.sh -> 单设备 profile 精确核对 -> SukiSU ksud insmod(shadow_mode=0)
           -> 核对 /proc/modules、/sys/module、/dev/pathguard_hide1
           -> 启动 pathguardd
pathguardd -> 二次核对 boot-state、boot ID、kernel、模块 live 状态
           -> 读取 rules.toml 并自动 reconcile
```

profile 模板位于 `experimental/hide-vfs/package/config/hide1_device_profile.json`，
由 `scripts/package-hide1-pathguardd-lab.ps1` 在打包时注入实际 `.ko` SHA-256；不能
复用多设备回归 allowlist。任何 profile、boot、模块或 loader 不匹配均保持 inactive。
删除有效 hide 规则会撤销部署，非法规则不会覆盖最后一份有效快照。

本轮主机证据：

```text
pathguard_hide1_backend_test                 PASS
rules_control / manager_save / content_gen   PASS
rules_hot_reload_integration                 PASS
validate_hidelab_offline                     PASS
NDK r27d arm64-v8a pathguardd                PASS
```

全量 CTest 未宣称通过：当前 `build/hide-h0` 未构建全部 91 个目标，并保留既有 hide
probe、规则迁移及 Release baseline 失败证据。实验包
`pathguard-hide1-lab-myron-auto-boot-v3.zip` 已生成并推送到设备 Download，待安装重启
后验证自动 load、daemon 自动启动和 rules.toml 热更新；即使设备实验通过，也不改变
Hide 1.0 正式状态。

### 规则切换、DISABLE/CLEAR 竞态与生命周期门禁

本阶段已在固定 myron 设备完成以下真机回归：

1. **规则切换**：generation `15001` 隐藏 `hidden2`；`DISABLE` 后恢复可见；
   `CLEAR` 后 generation `15002` 切换到 `hidden`，新规则生效、旧目录保持可见。
2. **DISABLE/CLEAR 竞态**：active reliability 访问期间并发控制命令均返回 `EBUSY`，
   不发生部分恢复或崩溃；访问 drain 后重试成功并清空 binding。
3. **已有目录 FD**：target 在 ENABLE 前持有目录 FD `194`，held-FD 九类 mutation
   全部 `ENOENT/no-side-effect`；随后即使 FD 仍存活也能完成 `DISABLE -> CLEAR -> rmmod`。
4. **namespace 生命周期**：销毁旧 target/namespace 后无崩溃；新 target 获得新的
   namespace `4026535693`，不继承旧 binding，目录恢复可见。

证据：

```text
build/device-evidence/hidelab-baseline/20260921-090205/
build/device-evidence/hidelab-baseline/20260921-090435/
build/device-evidence/hidelab-baseline/20260921-090721/
build/device-evidence/hidelab-baseline/20260921-090828/
build/device-evidence/hidelab-baseline/20260921-091117/
build/device-evidence/hidelab-heldfd/20260921-091455/
build/device-evidence/hidelab-baseline/20260921-092253/
```

这些结果关闭了路线 A 的规则切换、控制竞态、已有 FD、卸载和 namespace 生命周期
门禁，但不代表 Hide 1.0 已准入。OTA/KMI allowlist、完整 HideLab admission、跨版本
设备验证和产品 daemon 集成仍保持未完成，状态继续为 `unsupported`。

## 2. 当前基线

### 2.1 设备和内核边界

| 项目 | 当前值 |
|---|---|
| 设备 | Redmi K90 Pro Max |
| codename | `myron` |
| kernel release | `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` |
| loader | SukiSU Ultra LKM loader |
| 数据面 | 目标共享存储 FUSE 父目录 |
| 当前 scope | 单 UID、单 mount namespace、单父目录、单 basename |
| 当前 basename | `hidden`（一次性 HideLab fixture） |
| 当前 mode | `shadow_mode=0`，完整实验 shadow（仅在受控回归期间启用） |
| 产品状态 | `unsupported` |

### 2.2 已通过的实验

v8 只读后端已通过以下实验：

- Java File/NIO、libc `readdir`、多 buffer `getdents64`；
- `stat`、`lstat`、`access`、`open`、`openat`；
- cold/warm cache 顺序（含 readdir-then-open 与 positive-warm-then-open）；
- 20 线程 × 100 次 stat/open/readdir 并发；
- 1000 轮顺序 reliability；
- DISABLE、CLEAR、rmmod 和恢复 baseline；
- Target 隐藏、Control 可见、Root Oracle 未变化；
- boot ID 未变化，无已观测的 Oops、BUG、panic、Call trace。

这些结果只证明当前只读实验边界，不能外推到 mutation 或通用设备支持。

### 2.3 当前缺口

- mutation callback 已完成 mode 0 的单设备 disposable fixture 验证；create/mkdir/
  truncate/unlink/rmdir/rename/link 等均在真实修改前拒绝且 Root Oracle 不变，但
  governed basename 的 symlink 仍返回平台 `EACCES` 而非严格 `ENOENT`，因此 mutation
  全矩阵尚未通过；
- `link` 的隐藏源 inode 语义必须补强，不能只检查目标 dentry；
- `atomic_open` 的 `O_CREAT`、`O_EXCL`、`O_TRUNC` 分支尚未形成完整矩阵；
- lifecycle 状态机虽然已有雏形，仍需通过并发、已有 FD、Target 退出和卸载测试；
- generation、namespace 销毁、slot/OTA 变化后的重新准入尚未实现；
- production daemon 到 Hide UAPI 的正式集成尚未开始。

## 3. 总体架构选择

### 3.1 路线 A：Hide 1.0-LKM 实验路线

路线 A 只针对当前设备能力，不承诺通用 Android 兼容性：

```text
固定 KMI/FUSE 拓扑
    -> per-object shadow
    -> lifecycle 状态机
    -> mutation 前置拒绝
    -> cache/namei 一致性
    -> 真机 disposable fixture
    -> HideLab 全量回归
    -> 设备 admission
```

固定范围为：

```text
单设备 + 单 KMI + 单 UID + 单 mount namespace
+ 单 FUSE 父目录 + 单 basename
```

任何 scope、设备或 KMI 变化都必须重新 admission，不能复用旧的 `active` 状态。

### 3.2 路线 B：Hide 2.0 原生内核路线

当获得 myron 精确 kernel source、prepared output tree 和 ABI 输入后，优先评估把
hide 机制集成到 GKI/厂商内核的 namei、dcache、getdents 和 mutation 路径。原生内核
集成可以减少运行时替换 `i_op/f_op/d_op` 的生命周期风险，更适合多设备、长期 OTA 和
多 namespace 支持。

在精确源码和设备构建输入缺失时，不得把 Xiaomi 17、OnePlus 15、通用 SM8850 或
`bsp-prague-w-oss` 的产物当作 myron ABI 证据。路线 B 与路线 A 并行调研，但不能以
未验证的 kernel 替换当前设备实验。

## 4. 阶段总览和依赖关系

```text
阶段 0  冻结契约和实验边界
       |
       v
阶段 1  shadow lifecycle：STOP_NEW -> RESTORE -> DRAIN -> FREE
       |
       v
阶段 2  i_op/f_op/d_op 事务安装、回滚和元数据生命周期
       |
       v
阶段 3  mutation callback 前置封闭
       |
       v
阶段 4  namei/dentry/cache 一致性与 mutation 离线回归
       |
       v
阶段 5  真机 disposable fixture 验证
       |
       v
阶段 6  HideLab 全量 active 回归
       |
       v
阶段 7  设备/KMI/OTA admission
       |
       v
阶段 8  PathGuard 正式集成

路线 B：精确 kernel source -> 原生内核 Hide 2.0 评估
```

阶段 1 和阶段 2 是阶段 3 的硬前置条件。阶段 3 未通过时，不得在真实媒体目录执行
mutation。阶段 6 未通过时，不得进入 admission。阶段 7 未通过时，daemon 不得报告
`active`。

## 5. 阶段 0：冻结契约和实验边界

### 5.1 任务

- 固定目标设备、kernel release、架构、KMI 和 loader 版本；
- 固定 FUSE 父目录、parent inode、superblock、mount namespace 和 Target UID；
- 定义 Target、Control、Root Oracle 三个观察者；
- 定义隐藏语义为 `ENOENT`，而不是 `EACCES`、空目录或后置返回值修改；
- 明确未覆盖 operation 不得继承后宣称 active；
- 明确 mutation 只能使用一次性 disposable fixture；
- 为每轮记录 boot ID、模块哈希、控制程序哈希和证据目录。

### 5.2 验收

- coverage/preflight 能重新采集目标父目录的 12 项 operation；
- Target/Control namespace 和 UID 可稳定区分；
- fixture Oracle 可在测试前后比较 inode、内容、目录结构和 canary；
- 设备、KMI、loader 任一变化都会使 admission 进入 `unsupported`。

## 6. 阶段 1：实现并证明 shadow lifecycle

这是 mutation 的前置阶段。此阶段不扩大隐藏语义，重点是证明 callback 和 metadata
的生命周期安全。

### 6.1 状态机

```text
ACTIVE
  |
  | disable / clear / unload
  v
STOP_NEW
  |
  v
RESTORE
  |
  v
DRAIN
  |
  v
FREE
  |
  v
INACTIVE
```

状态语义：

- `STOP_NEW`：拒绝新的 policy 进入和新的 shadow 安装，不释放任何仍可能被访问的对象；
- `RESTORE`：按事务记录恢复原始 `i_op/f_op/d_op`，并撤销 active publication；
- `DRAIN`：等待已进入 wrapper 的 callback、已打开 FD 和 stale worker 完成；
- `FREE`：在 SRCU/RCU/Tasks-RCU 宽限期后释放 metadata、dentry 引用和 module pin；
- 任一步失败：保持可恢复的中间状态，禁止报告 `active` 或完成卸载。

### 6.2 数据结构任务

每一个被替换对象都必须有独立 metadata：

- `struct inode_operations` shadow 和 `orig_iop`；
- `struct file_operations` ingress/live bridge 和 `orig_fop`；
- `struct dentry_operations` shadow 和 `orig_dop`；
- parent inode、superblock、mount namespace、generation、Target task 引用；
- RCU hash/index、binding 反向索引和撤销状态；
- iop/fop/dop 独立 active 计数和 wait queue；
- stale dentry work item、dget 引用和 module pin；
- 安装顺序、已替换指针和回滚标志。

### 6.3 安装事务

安装必须遵循：

1. preflight 校验当前 operation pointer 仍等于预期原始指针；
2. 分配并初始化全部 metadata，任何分配失败都不发布；
3. 先建立 RCU/SRCU 索引，再发布 shadow pointer；
4. i_op、f_op、d_op 每一步记录成功状态；
5. 任一步失败，按逆序恢复所有已发布 pointer；
6. 经过宽限期后释放失败事务的 metadata；
7. 只有完整安装成功才允许状态进入 `ACTIVE`。

### 6.4 卸载事务

卸载必须遵循：

1. `STOP_NEW`，阻止新 callback 和新 dentry shadow；
2. `RESTORE`，恢复原始 i_op/f_op/d_op；
3. 从 RCU index 摘除 metadata，但暂不释放；
4. 等待 SRCU、Tasks-RCU、RCU 和 active wait queue；
5. 关闭 stale work、释放 dentry 引用和 f_op ingress owner bridge；
6. `FREE`，释放 metadata 并撤销 module pin；
7. 验证 `/proc/modules`、设备节点、内核日志和 boot ID。

### 6.5 阶段 1 验收

- 20 线程只读访问期间重复 DISABLE/CLEAR 不崩溃、不挂死；
- callback 进入后执行 DISABLE，metadata 仍保持有效直到 callback 退出；
- 已打开目录 FD 在恢复前后都不会调用已释放的 shadow；
- Target 退出后不能继续作为有效 observer；
- dentry stale worker 与 CLEAR 并发时无 UAF、死锁或 double free；
- 安装任一步失败时，原始 operation pointer 和 module state 完整恢复；
- 宿主 contract、concurrency、Kbuild 和 sanitizer/静态检查通过。

## 7. 阶段 2：完善 operation-table shadow

### 7.1 i_op

`i_op` shadow 负责 lookup、atomic_open 和 mutation。wrapper 必须通过 metadata 找到
原始 operation，而不能依赖可变的全局单例。所有调用必须：

```text
进入 active 计数
-> 在 SRCU 保护下读取 immutable policy
-> 进行 observer/parent/name/generation 判断
-> 隐藏则返回 ENOENT
-> 否则调用原始 callback
-> 退出 active 计数
```

### 7.2 f_op

`f_op` 必须处理 ingress/owner bridge：

- 新打开文件获得可追踪的 live operation；
- 已打开 FD 的 `f_op` 不会被后来 ENABLE 的 inode shadow 静默改写；
- release/open 计数保持 module 和 metadata 存活；
- DISABLE 时若仍有 FD，必须按显式策略返回 `-EBUSY` 或等待可证明的 drain，不能
  直接释放 metadata。

### 7.3 d_op

dentry shadow 必须：

- 在 `d_lock` 下检查和替换 `d_op`；
- 保存原始 flags 和 `d_revalidate`；
- 对 positive、negative、alias dentry 使用 generation 和 scope 校验；
- stale 后先从 policy index 摘除，再在宽限期后释放；
- `LOOKUP_RCU` 无法安全判断时返回 `-ECHILD`，不得在 RCU 快路径中睡眠或分配。

## 8. 阶段 3：实现 mutation 封闭

### 8.1 必须覆盖的 callback

```text
create       mkdir       mknod       symlink
link         unlink      rmdir       rename
atomic_open（包含 O_CREAT/O_EXCL/O_TRUNC 分支）
```

### 8.2 通用拒绝规则

所有 wrapper 必须在调用原始 filesystem callback 之前完成：

1. observer scope 判断；
2. parent superblock/inode 判断；
3. basename 和 generation 判断；
4. 对象身份和 dentry 状态判断；
5. 返回 `-ENOENT`，且不得触碰真实对象。

Target 的隐藏对象必须表现为不存在；Control 和不匹配 observer 必须继续调用原始
callback，并保持原始 filesystem 语义。

### 8.3 各 operation 要求

| Operation | 必须检查 | 失败语义 |
|---|---|---|
| `create` | new dentry/name、O_EXCL 相关路径 | `-ENOENT`，不创建对象 |
| `mkdir` | new dentry/name | `-ENOENT`，不创建目录 |
| `mknod` | new dentry/name、设备节点类型 | `-ENOENT`，不创建节点 |
| `symlink` | new dentry/name、link target 不得先落盘 | `-ENOENT` |
| `link` | old source dentry/inode 和 new destination dentry | 任一端命中即 `-ENOENT` |
| `unlink` | parent 和被删除 dentry | `-ENOENT`，原对象不变 |
| `rmdir` | parent 和被删除目录 dentry | `-ENOENT`，目录不变 |
| `rename` | old source 和 new destination 两端、flags | 任一端命中即 `-ENOENT` |
| `atomic_open` | 普通 open 与 `O_CREAT/O_EXCL/O_TRUNC` 分支 | 隐藏对象及创建路径均 `-ENOENT` |

### 8.4 rename 特别要求

必须同时检查：

```text
rename(source=hidden, destination=visible) -> 拒绝
rename(source=visible, destination=hidden) -> 拒绝
rename(source=hidden, destination=hidden) -> 拒绝
rename(exchange/whiteout/overwrite flags) -> 未覆盖则 unsupported
```

不得只检查 `old_dir` 或只检查 `new_dir`。跨父目录、跨 mount、exchange、whiteout 等
flags 在没有明确实现和测试前必须拒绝 admission，而不是默认调用原始 callback。

### 8.5 link 特别要求

`link(old_dentry, new_dir, new_dentry)` 必须同时检查：

- old dentry 是否属于隐藏对象；
- old dentry 的 parent/superblock 是否属于规则 scope；
- new destination 是否命中隐藏 basename；
- hard-link 是否跨 parent、跨 alias 或跨 mount。

只检查 `new_dentry` 会允许通过新硬链接重新发现隐藏 inode，不能作为完整实现。

## 9. 阶段 4：namei、dentry 和 cache 一致性

### 9.1 必须实现的语义

- synthetic negative dentry；
- positive dentry 失效；
- negative dentry 按 UID、namespace、generation 隔离；
- `d_revalidate` 返回值与 `LOOKUP_RCU` 规则一致；
- alias 路径映射到同一 FUSE parent 时保持一致；
- parent rename/delete/recreate 后旧 metadata 不得继续命中；
- ENABLE 前已有 FD 和 ENABLE 后新 FD 分开测试；
- generation 变化后旧 dentry 不得复用。

### 9.2 cache 测试顺序

至少执行：

```text
cold open
cold opendir
stat -> open
readdir -> open
positive warm -> open
negative warm -> open
alias cold/warm
parent recreate
```

每项都要同时记录 Target、Control、Root Oracle 和 kernel diagnostic counter。

## 10. 阶段 5：离线 mutation 和生命周期回归

### 10.1 fixture 规则

mutation 只能作用于随机、一次性、可删除的 fixture，例如：

```text
/storage/emulated/0/Pictures/PathGuardHideLab/<run-id>/
```

禁止对真实媒体目录执行 create、truncate、rename、unlink 或 rmdir。真实
`Pictures/Nagram` 仅可用于只读观察。

### 10.2 离线矩阵

- 每个 callback 的 Target reject 和 Control passthrough；
- `rename` 的 source/destination 双端矩阵；
- link hidden source 与 hidden destination；
- `O_CREAT`、`O_EXCL`、`O_TRUNC`；
- mutation 与 DISABLE/CLEAR 并发；
- 20 线程 mutation；
- 重复 ENABLE/DISABLE；
- 规则切换和 generation 变化；
- Target 退出、mount namespace 销毁、mount 切换；
- 已有 FD、dentry stale、operation restore、rmmod。

### 10.3 强制 Oracle

每个 fixture 都要在测试前后比较：

- canary 内容和 hash；
- 目录项集合；
- inode number、file type、size、mode；
- source/destination 是否移动或覆盖；
- hard-link count；
- Root 视图与 Control 视图；
- kernel log、boot ID、pstore 和设备在线状态。

以下任一结果都阻止进入下一阶段：

```text
LEAK
OVERBLOCK
SEMANTIC_DRIFT
DESTRUCTIVE_FAIL
STATE_LIE
CRASH
HANG
UAF
DEADLOCK
```

## 11. 阶段 6：真机 disposable fixture 验证

### 11.1 执行顺序

```text
记录 boot ID
-> load
-> status（计数应为零或仅有安装计数）
-> INSTALL disposable fixture
-> ENABLE
-> baseline
-> cache-order
-> mutation matrix
-> concurrency
-> DISABLE
-> CLEAR
-> restore baseline
-> unload
-> 检查 boot ID、模块、设备节点和日志
```

首次出现任何阻断结果，立即执行恢复，不继续后续场景。

### 11.2 真机限制

- 不使用真实 `Pictures/Nagram` 做 mutation；
- 不在未确认 Target PID、UID、mount namespace 时 ENABLE；
- 不把模块 live、status active 或单个 callback 计数当作 hide 通过；
- 设备重启、boot ID 变化、Oops 或 pstore 记录都视为本轮失败；
- v3 只读实验包与 mutation 实验包分开命名、分开哈希和分开证据目录。

## 12. 阶段 7：HideLab 全量 active 回归

### 12.1 访问面

- Java File、Java NIO；
- libc `readdir`；
- 多种 buffer 的 `getdents64`；
- `stat`、`lstat`、`statx`、`access`、`faccessat2`；
- `open`、`openat`、`openat2`、`O_PATH`；
- `atomic_open` cold path；
- 相对路径、`dirfd`、descendant；
- `/storage/emulated/0`、`/sdcard`、`/storage/self/primary` alias；
- create/mkdir/mknod/symlink/link/unlink/rmdir/rename；
- cache-order、20 线程并发和生命周期。

### 12.2 Target/Control 对照

Target 必须得到隐藏语义，Control 必须保持正常语义。Root Oracle 必须证明任何
mutation 没有真实副作用。不能因为 Target 通过而忽略 Control 的 OVERBLOCK。

### 12.3 active 进入条件

只有以下条件同时满足才允许建立 candidate：

1. 所有必需 operation 已安装且有诊断计数；
2. 所有只读和 mutation 场景通过；
3. cache、alias、descendant、并发和生命周期通过；
4. DISABLE/CLEAR/rmmod 恢复通过；
5. 没有失败结果或未解释的 `setup_error`；
6. 设备信息和模块哈希记录完整；
7. 重新启动后可以再次 load、ENABLE 和回归。

## 13. 阶段 7：设备准入和 OTA 重新准入

### 13.1 admission 输入

- build fingerprint；
- product/codename；
- kernel release 和 KMI generation；
- 架构；
- parent superblock/inode 与 FUSE topology；
- module SHA-256、control SHA-256；
- undefined symbol 集合和运行时 kallsyms 解析结果；
- SukiSU loader 版本；
- HideLab full-regression 证据摘要和哈希；
- boot 后重新加载与回归结果。

### 13.2 重新准入触发器

以下任一变化必须自动降级为 `unsupported` 并重新运行 admission：

- OTA 或安全补丁更新；
- boot slot 切换；
- kernel release、KMI、fingerprint 或 product 变化；
- loader、module、control binary 哈希变化；
- mount namespace、FUSE parent、superblock 或 operation mask 变化；
- Target UID、scope 或规则模型变化超出已验证范围。

## 14. 阶段 8：PathGuard 正式集成

只有阶段 0 至 7 完成后才允许接入生产状态机：

- daemon 把规则转换为 versioned Hide UAPI；
- generation 由 control plane 分配和撤销；
- `DISABLE`、`CLEAR`、Target 退出和异常路径统一 fail-closed；

### 2026-09-18：Target 退出状态契约修复

v8 真机实验发现，旧 Target 退出后，回调侧已经通过 `PF_EXITING` 阻止新
进程继承隐藏视图，但状态仍可能保留 `ACTIVE`，造成控制面陈旧状态。该行为
不能作为 Hide 1.0 生命周期通过。

本阶段采用不增加 `sched_process_exit` 内核 hook 的收缩方案：

1. 在持有模块全局锁的 `STATUS`/`DISABLE` 路径检查被 pin 的
   `target_task->flags & PF_EXITING`；
2. 检测到退出后发布 `retiring=true`、`lifecycle=STOP_NEW`、
   `state=INACTIVE`、`last_error=-ESRCH`；
3. `DISABLE` 不再只依赖 `state=ACTIVE`，只要 i_op/f_op ingress 或 dentry
   shadow 仍存在，就继续执行完整的 `RESTORE -> DRAIN -> FREE`；
4. 不自动在任意进程退出回调中执行恢复，避免在 VFS 回调上下文扩大锁和卸载
   风险；userspace watcher 仍需在正式准入前显式执行 `DISABLE -> CLEAR`。

离线契约测试覆盖：`PF_EXITING` 检测、状态发布顺序、退出后 DISABLE 的
shadow 存在性判断及 STATUS 触发的 fail-closed 观察。设备侧原始证据仍保留在
`build/device-evidence/hide1-v8-target-exit/20260918-210016/`：旧 Target
退出后新同 UID 进程得到 `BASELINE_VISIBLE_NOT_HIDE_PASS`，设备未重启；在
本阶段修复模块构建并完成真机回归前，产品状态继续为
`Hide 1.0 = unsupported`。

### v9 真机验收结果（2026-09-18）

v9 在 myron 上完成了 Target 退出生命周期验证：ENABLE 成功且设备未重启；
旧 Target force-stop 后状态自动发布为 `INACTIVE/STOP_NEW/-ESRCH`；新同 UID、
新 mount namespace 的进程结果为 `BASELINE_VISIBLE_NOT_HIDE_PASS`，不会继承旧
binding；`DISABLE -> CLEAR -> rmmod` 和 fixture 清理均通过，boot ID 未变化。

同一轮的 `shadow_mode=1` 数据面 baseline 为 `LEAK`（Java exists、lstat、open
仍可见），所以该结果只能关闭 Target-exit 生命周期风险，不能提升产品准入状态。
下一阶段必须转向 FUSE-aware 的只读模式/完整 operation shadow，并重新执行
HideLab cache、concurrency、reliability 与 mutation 门禁。

v9 随后以 `shadow_mode=4` 完成设备只读回归：baseline、cache-order、concurrency、
reliability 四组均为 `PASS`，fixture 与 Root Oracle 未变化，active/open 计数最终
归零。该结果支持继续推进 mutation 封闭和 namespace 生命周期门禁，但不改变
Hide 1.0 的 unsupported 状态。
- 默认状态为 `inactive`；
- capability、admission、runtime state 三者分离；
- lab module 与 production module 分离；
- unsupported 不能静默降级为 deny、mount 或 syscall mask；
- 只有完整 admission 后才允许产品状态变为 `active`。

## 15. 参考项目的采用边界

### 15.1 NoMount v20

参考目录：`refer/hide-refer/nomount-master`。

值得借鉴：

- `fake_iop/fake_fop + orig_iop/orig_fop`；
- per-object metadata 和 RCU index；
- SRCU 保护规则数组；
- `kfree_rcu`、`call_rcu` 和 dentry 恢复；
- dentry lock 下的 operation 替换；
- seqcount、bloom filter、有序 child array；
- restore pointer 后等待宽限期再释放对象。

不能直接采用：

- 虚拟 inode/whiteout 作为完整 PathGuard 数据面；
- superblock 全局劫持和全量 inode 扫描；
- 未覆盖全部 mutation 的 operation table；
- 没有 namespace scope 的规则模型；
- 失败后静默继续的安装路径。

NoMount 是 lifecycle/RCU 参考，不是 Hide 1.0 的可直接集成实现。

### 15.2 Kasumi

Kasumi 的 lookup、dentry observer 和 shadow 发布思路有参考价值，尤其适合解释
positive/negative dentry 和 revalidate 的关系。但原版只覆盖部分 lookup/dirhijack
路径，FUSE `atomic_open` 和 mutation 封闭不足，不能满足 PathGuard exact hide contract。

Kasumi 主要作为：

- lookup/dentry 设计参考；
- atomic_open 绕过的对照对象；
- HideLab 的负面测试样本。

### 15.3 LKM-PathMask

参考目录：`refer/hide-refer/LKM-PathMask-main`。

值得借鉴：

- dev/inode 对象身份；
- UID deny/allow scope；
- KMI 分包和加载诊断；
- kprobe 解析 OEM 裁剪符号；
- fail-closed 的模块配置校验。

禁止作为 Hide 1.0 数据面照搬：

- syscall/kretprobe 返回值篡改；
- openat 已创建 FD 后再 `close_fd` 并改写返回值；
- getdents 返回后才修改用户缓冲区；
- syscall 热路径作为通用 exact hide。

这些手段可作为 fallback 或对照实验，但不能证明“真实对象修改之前已经拒绝”。

### 15.4 SUSFS

SUSFS 的长期价值在于原生内核集成：它把 hide 判断放到 namei、dcache、getdents 等
更接近根因的位置，减少运行时 operation shadow 的卸载风险。当前不能直接把其
`gki-android16-6.12` patch 当作 myron 兼容证据，原因是缺少精确 myron kernel source、
prepared tree、Module.symvers 和设备构建输入。

SUSFS 是路线 B 的内核集成参考，不是当前路线 A 的直接依赖。

### 15.5 SukiSU / KernelSU

SukiSU/KernelSU 解决的是 LKM 构建和加载：DDK、kallsyms 重定位、vermagic 适配、CFI
兼容和 module lifecycle。它不提供 PathGuard 的 lookup、cache、mutation 和 scope 数据面。

当前继续使用 SukiSU loader 作为实验加载层，但 loader 成功不改变 Hide 1.0 admission。

## 16. 风险、停止条件和回滚

### 16.1 必须停止的结果

```text
LEAK              Target 仍能发现或访问隐藏对象
OVERBLOCK         Control 或非目标 observer 被错误隐藏
SEMANTIC_DRIFT    errno/返回值/目录语义不符合契约
DESTRUCTIVE_FAIL  真实对象被创建、删除、覆盖或移动
STATE_LIE         runtime 报 active 但能力未完整安装
CRASH             Oops、BUG、panic、Call trace 或设备掉线
HANG              命令、callback、workqueue 或 drain 无界等待
```

首次发现停止条件后：

1. 不继续下一场景；
2. 保存 status、kernel log、boot ID 和 fixture Oracle；
3. 执行 DISABLE/CLEAR；
4. 若安全，执行 unload；
5. 复现前先离线修复根因；
6. 产品状态保持 `unsupported`。

### 16.2 回滚顺序

```text
停止新规则
-> 恢复原始 operation pointer
-> 摘除 RCU/SRCU index
-> 等待 callback/FD/workqueue drain
-> 释放 dentry 和 metadata
-> unload
-> 恢复 baseline
```

## 17. 任务清单

### 当前阶段：生命周期基础设施

- [x] 明确 lifecycle 状态枚举和状态迁移表；
- [x] 为 i_op/f_op/d_op metadata 增加独立 active counter；
- [x] 为三类 callback 增加 wait queue 和 drain helper；
- [ ] 完成 f_op ingress/owner bridge；
- [x] 完成 f_op ingress/owner bridge；
- [x] 完成 dentry stale workqueue 和 dget 生命周期；
- [x] 完成安装事务和失败回滚；
- [x] 离线运行 20 线程、DISABLE、CLEAR、Target exit、已有 FD、rmmod 测试；
- [ ] 通过后再进入 mutation。

阶段 1 离线验收记录（2026-09-17）：`pathguard_hide_vfs_model_test`、
`pathguard_hide_vfs_concurrency_test` 和 `pathguard_hide_vfs_teardown_contract_test`
全部通过；Android 16/6.12 prepared DDK 完成 `CC -> MODPOST -> LD -> BTF`。本阶段还将
lifecycle 编号和 i_op/f_op/d_op 全局及 per-object active/open 计数暴露到 status，卸载前
对 dentry 所有权做事务预检，generation 变化或 dentry unhash 会排队 stale worker。该结果
仍只是离线生命周期证据，尚未授权设备 mutation 或 Hide 1.0 admission。

### 下一阶段：mutation

- [x] create；
- [x] mkdir；
- [x] mknod；
- [x] symlink；
- [x] link source/destination 双端；
- [x] unlink；
- [x] rmdir；
- [x] rename source/destination 双端和 flags；
- [x] atomic_open 的普通 open、O_CREAT、O_EXCL、O_TRUNC；
- [ ] disposable fixture 的 Root Oracle；
- [x] 离线 mutation matrix；
- [ ] 真机受控 mutation matrix。

阶段 3 离线实现记录（2026-09-17）：mutation wrapper 增加调用、前置拒绝、原始回调和
unsupported 计数；`link` 同时检查目标 dentry、源 parent/name 以及已知隐藏 inode，避免
通过 hard-link alias 重新暴露对象；`rename` 对 source/destination 双端检查，未实现的
`RENAME_NOREPLACE`、`RENAME_EXCHANGE`、`RENAME_WHITEOUT` 以及跨 superblock 路径 fail
closed；`atomic_open` 对普通 open 与 `O_CREAT/O_EXCL/O_TRUNC` 分支统一在真实 callback
之前拒绝隐藏 basename。宿主模型和源码契约测试已覆盖这些矩阵，但 disposable fixture
和真机 mutation 尚未执行。

### mode=0 真机 mutation 结果（2026-09-18）

v9 完整 shadow mode 已完成一次 Target/Control disposable-fixture 对照。Target 多数
操作在 lookup 阶段即返回 `ENOENT` 且未修改 fixture，但 `unlinkat` 仍被探针标记为
`side_effect=true`；状态计数 `mutation=3/1/2/0` 也表明许多请求没有进入预期的逐
callback 统计。因此该轮只能判定为“部分封闭”，不能作为 mutation 全矩阵通过。
下一步必须定位 unlink 的真实修改窗口、补齐九类 operation 的命中证据，并继续处理
symlink 严格 `ENOENT` 语义。

### 最终准入

- [ ] HideLab 全量 active 回归；
- [ ] 重启后重新 load/enable 回归；
- [ ] 设备/KMI/module/loader 白名单；
- [ ] OTA/slot 变化自动拒绝；
- [ ] daemon Hide UAPI 集成；
- [ ] 产品状态机只在完整 admission 后允许 `active`。

### syscall/namei 适配前置门禁（2026-09-18 新增）

- [ ] 离线确认 SukiSU syscall bridge 是否具有稳定、可供 LKM 使用的导出符号；
- [ ] capability probe 仅验证符号解析、CFI、register/unregister、并发和 unload，
      不注册真实隐藏 hook；
- [ ] 若内部 `ksu_*` 符号未导出、设备 kallsyms 地址为零或无法证明卸载安全，
      禁止普通 LKM 直接调用，必须转 KernelSU companion patch/KPM 评估；
- [ ] 只有在上述门禁通过后，才可把 `newfstatat/faccessat(2)/openat(2)/symlinkat`
      接入 namei adapter；任何 post-syscall 返回值改写或真实对象修改后的补偿都不
      满足 Hide 1.0。

### KPM/namei 执行门禁（2026-09-19）

- [x] 增加只读 `collect_kernel_backend_capability.ps1`，采集设备 release、KPM/KALLSYMS/
      KPROBES 配置、SukiSU KPM 查询和 kallsyms 可见性；不执行 KPM load/unload 或行为 hook；
- [ ] 设备采集报告 `eligible_for_kpm_probe`（当前 ADB 未连接，不能伪造结果）；
- [ ] capability-only KPM/namei probe 证明符号解析、调用约定、CFI、注册/注销和卸载；
- [ ] capability-only probe 通过前禁止加载改变 namei、readdir、stat 或 mutation 行为的模块。

### 只读 KPM capability probe（2026-09-19）

已新增固定内核实验用探针：

```text
experimental/hide-kpm/capability/
```

该探针复用 `refer/hide-refer/SukiSU_KernelPatch_patch` 的 KPM ABI，仅调用
KernelPatch 导出的 `kallsyms_lookup_name()` 查询 18 个 namei/VFS/FUSE 候选符号的
存在性，并通过 `ctl0 status` 返回计数和位图。探针不调用解析后的地址，不安装
inline hook/syscall hook/kprobe/tracepoint，不修改 VFS 状态，也没有文件系统 mutation。
卸载回调只清理自身快照。

离线验收已完成：

```text
Windows NDK 28.2.13676358 编译       PASS
AArch64 relocatable ELF              PASS
.kpm.info/.kpm.init/.kpm.ctl0/.kpm.exit PASS
唯一外部依赖                         kallsyms_lookup_name
生成产物清理                         PASS
缺失 NDK 参数门禁                    PASS
```

Windows 构建使用 NDK 的 `.cmd` clang 包装器；仓库内 Android 16 DDK clang 不能替代
Android NDK target sysroot。该离线结果只证明 KPM 文件格式和本地构建链正确，尚未证明
设备具备 `CONFIG_KPM`、可用的 SukiSU KPM 接口或安全的 namei hook 条件。

下一步仍必须先连接唯一目标设备并执行：

```powershell
./tests/device/hide/collect_kernel_backend_capability.ps1
```

只有报告为 `eligible_for_kpm_probe` 才允许在用户明确授权后加载该只读探针，采集
`load -> ctl0 status -> unload`、boot ID 和内核日志。任何符号解析失败、调用约定/CFI
不确定、卸载异常或设备重启都将停止 KPM/namei 路线，产品状态继续为
`Hide 1.0 = unsupported`。

#### 设备门禁结果（2026-09-19 11:14 CST）

设备已重新连接并完���两次只读采集；第二次修复采集器后的证据目录为：

```text
build/device-evidence/kernel-backend-capability/20260919-111448/
```

关键事实：

```text
device       = myron
fingerprint  = Redmi/myron/myron:16/BP2A.250605.031.A3/OS3.0.23.0.WPMCNXM:user/release-keys
kernel       = 6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
CONFIG_KPM   = 未出现 CONFIG_KPM=y（设备不提供 KPM 配置证据）
KALLSYMS     = y
KPROBES      = y
CFI_CLANG    = y
MODULES      = y
ksud kpm version/num/list = ENOTTY
decision     = unsupported
```

`ksud kpm --help` 仅表示 CLI 具有 KPM 子命令，不表示内核实现了 KPM ioctl；三条
实际查询均返回 `Error: Inappropriate ioctl for device (os error 25)`。采集器明确记录
`kpm_load_attempted=false`、`kpm_unload_attempted=false`、`module_insert_attempted=false`。
因此本机不能进入只读 KPM load/status/unload 验证，不能把 KPM/namei 作为当前设备的
可执行后端。

## 18. 完成定义

本路线只有在以下条件同时满足时，才可以宣布当前设备范围的 Hide 1.0 candidate：

1. lifecycle 状态机可证明完成，且 callback、FD、workqueue 和 metadata 无 UAF/死锁；
2. 全部九类 mutation 在真实对象修改前封闭；
3. lookup、atomic_open、readdir、d_revalidate、cache 和 alias 语义一致；
4. Target 隐藏、Control 可见、Root Oracle 无副作用；
5. disposable fixture 的 mutation、并发、退出和卸载回归通过；
6. 重启、slot、OTA、KMI 和模块哈希 admission 通过；
7. 证据、哈希、设备信息和回滚记录完整。

在此之前，任何“模块已加载”“ENABLE 成功”“目录枚举被过滤”或“只读 baseline PASS”
都只能描述实验事实，不能把产品状态改为 `active`。

## 20. 2026-09-20：held-FD mutation 门禁结果

严格的两阶段实验已经执行。disabled 阶段持有的目录 FD 在 ENABLE 后仍能绕过当前
shadow：`openat(fd, "canary.txt", O_TRUNC)` 成功并修改真实 canary。该结果满足
`DESTRUCTIVE_FAIL`，因此路线图中的“mutation 封闭”门禁保持未通过。

下一阶段不能通过增加路径 lookup 或放宽测试断言解决；必须在 namei/mutation 的真实
对象入口覆盖 `dirfd` 已解析对象，或明确引入内核级统一策略入口。至少需要重新设计
并验证：

- pre-opened directory FD 的 create/truncate/unlink/rmdir/link/rename/symlink/mknod；
- `symlinkat`、`linkat` 的统一 `ENOENT` 语义；
- mutation 发生前的 target UID、mount namespace、parent inode 和 generation 校验；
- failed install、disable、clear、unload 时已有 FD 的恢复和 drain；
- root oracle 不变、target 隐藏、control 可见的全量回归。

在这些证据完成前，设备 admission 不得从 `pending_hidelab` 进入 `admitted`，产品状态
继续保持 `Hide 1.0 = unsupported`。

## 21. 2026-09-20：held-FD v3 结果与 do_symlinkat 诊断门禁

generation `7001` 的严格 held-FD 回归证明 cached-child 修复已经关闭原先的
`openat(O_TRUNC)` 真实修改：全部已覆盖 mutation 均返回 `ENOENT`，root oracle
保持不变。唯一剩余差异为 `symlinkat -> EACCES`；它没有副作用，但仍属于
`SEMANTIC_DRIFT`，因此 mutation 门禁尚未通过。

当前执行顺序更新为：

1. 构建并加载 ABI v4 diagnostics 包，确认 `do_symlinkat` kprobe 可注册；
2. 使用同一 PID/namespace/held FD 执行一次 `symlinkat`，要求
   `calls/target/fd/hidden_fd` 对应增长；
3. 验证 control observer 不计入 target/hidden_fd，DISABLE/CLEAR/UNLOAD 可完整恢复；
4. 只有诊断证据正确，才设计固定 myron 的窄 `ENOENT` bridge；
5. bridge 必须位于 `security_path_symlink` 前，同时保留 fsuid、thread group、mount
   namespace、generation 和 hidden inode 五重约束；
6. 重新执行完整 held-FD mutation、cache-order、并发和生命周期矩阵。

ABI v4 diagnostics 明确禁止修改 instruction pointer 或 syscall 返回值。它只解决
参数与身份观测问题，不是隐藏数据面；探针注册成功或 counter 命中均不能改变
`Hide 1.0 = unsupported` 的产品状态。

### generation 8002 诊断结果与下一门禁

完整 `shadow_mode=0` 的 generation `8002` 已满足 ABI v4 诊断目标：

```text
operation_mask = 0x0fff
dentry_install = 5/5/0
symlink_probe  = 1/8/3/3/1
```

最后一项 `hidden_fd=1` 证明 target held-FD 调用已被正确归属到 governed hidden inode。
target/control 均返回 `EACCES` 且无副作用，说明剩余问题不是 `dirfd` 识别失败，而是
PathGuard 的 operation-table shadow 位于 Android/FUSE 的既有拒绝之后，无法把 target
语义收敛为 `ENOENT`。

下一阶段按以下顺序执行：

1. 审查目标内核中 `do_symlinkat -> filename_create -> security_path_symlink ->
   vfs_symlink` 的精确调用及清理路径；
2. 增加 `security_path_symlink` 调用点的只观测 probe，记录 target、parent hidden
   inode 和 child dentry 命中，严禁改返回值或 instruction pointer；
3. 只有只观测证据全部正确，才实现固定 myron 的窄 `ENOENT` 返回 bridge；
4. bridge 必须让 `do_symlinkat()` 继续执行 `done_path_create()` 和 `putname()`，不得
   从函数入口直接跳转返回；
5. 先重跑单操作 `symlink-held-fd`，再重跑完整 held-FD mutation、cache-order、并发、
   DISABLE/CLEAR/UNLOAD 生命周期矩阵。

generation 8002 结束后已执行 `DISABLE -> CLEAR -> UNLOAD`，设备未重启且模块不再
live。strict `symlinkat -> ENOENT` 尚未实现，所以 mutation 门禁仍为未通过，产品状态
仍为 `Hide 1.0 = unsupported`。

### 设备配置修正：`security_path_symlink` 不可用

打包下一诊断模块前的设备只读核对证明：

```text
# CONFIG_SECURITY_PATH is not set
security_path_symlink  absent from /proc/kallsyms
vfs_symlink            present in /proc/kallsyms
security_inode_symlink present in /proc/kallsyms
may_create             present in /proc/kallsyms
```

所以前述 `security_path_symlink` 诊断步骤在本设备不可执行；源码中的调用已被编译成
返回 0 的 inline stub。这不是加载器或符号解析问题，不能通过改 vermagic 或放宽模块
注册门禁解决。

执行顺序修正为：

1. ABI v5 同时注册只观测 `do_symlinkat` 与 `vfs_symlink` kprobe；
2. 用单操作 `symlink-held-fd` 验证 `vfs_symlink` 的 target、hidden parent、child-parent
   identity 和 negative child；
3. 若命中，说明 `EACCES` 来自 `vfs_symlink()` 内的 `may_create`、缺少 filesystem
   callback、inode LSM 或原始 `i_op->symlink`；再评估能保留 `done_path_create()` 的窄
   返回 bridge；
4. 若不命中，说明拒绝发生在 `filename_create()`，继续以只观测 probe 定位该路径，
   不实施 syscall 入口 PC redirect；
5. 只有 `symlinkat -> ENOENT` 且无副作用后，才恢复完整 held-FD mutation、cache-order、
   并发及生命周期回归。

ABI v5 产物为
`pathguard-hide1-lab-myron-ddk-v5-vfs-symlink-diagnostics.zip`；它不自动加载或 ENABLE。

### generation 9001 `vfs_symlink` 结果

ABI v5 真机单操作诊断已通过调用链门禁：

```text
symlink_probe     = 1/8/3/3/1
vfs_symlink_probe = 1/8/3/3/1/1/1
mutation.symlink  = 0/0/0/0
```

这证明 `filename_create()` 成功并把正确的 hidden parent 与 negative child 传入
`vfs_symlink()`；`EACCES` 发生在 PathGuard `i_op->symlink` wrapper 之前。下一步不再
探测 `filename_create()`，而是依次区分 `may_create()`、callback presence check 和
`security_inode_symlink()`：

1. 使用 per-instance kretprobe data 标记 target + hidden parent；
2. 只记录 `may_create` 与 `security_inode_symlink` 的 `0/EACCES/other` 返回分类；
3. 在 `vfs_symlink` 入口记录 `dir->i_op->symlink == hide1_symlink`；
4. 任一 kretprobe 注册失败时模块加载失败并完整回滚已注册 probe；
5. 再次运行单操作 `symlink-held-fd` 后立即恢复；
6. 只有确认真实失败分支且证明真实对象未修改，才评估将该次失败收敛为 target-only
   `ENOENT` 的窄返回 bridge。

generation 9001 已完成 `DISABLE -> CLEAR -> UNLOAD`，fixture 已删除，设备稳定。

### generation 10001 stage 结果

v6 已证明：

```text
vfs_symlink shadow_iop = 1
may_create stage calls = 0
inode-security calls   = 1
PathGuard i_op calls   = 0
```

失败点是 `security_inode_symlink()`，`may_create()` 在该 ThinLTO 构建中没有经过独立
符号。v6 返回分类把 arm64 的 32 位 `int` 返回当成 64 位 `long`，导致 `-EACCES` 被错误
归入 `other`；该分类不能作为 bridge 验收证据。

路线增加一个不可跳过的修正门禁：先用 `(int)regs_return_value(regs)` 构建 v7，只读
复测并要求 `inode_security_stage=1/0/1/0/0`。只有该结果满足后，才评估在
`security_inode_symlink()` 返回、`dir->i_op->symlink()` 调用之前，把匹配 target 的
既有拒绝从 `-EACCES` 收敛成 `-ENOENT`。这不是 syscall 返回值补偿：真实 filesystem
callback 尚未执行，`do_symlinkat()` 仍会正常执行 `done_path_create()` 与 `putname()`。

generation `11001` 已取得预期 `inode_security_stage=1/0/1/0/0`，且 root oracle 无
变化、恢复链完整通过。下一门禁改为验证 ABI v7/v8 窄 bridge：target 单操作必须返回
`ENOENT/no-side-effect`，control 必须保持 `EACCES/no-side-effect`，同时要求
`inode_security_bridge_enoent=1`。该门禁通过前，不恢复完整 mutation 回归，也不改变
`Hide 1.0 = unsupported`。

generation `12001` 已通过该门禁：target 为 `ENOENT/no-side-effect`，control 保持
`EACCES/no-side-effect`，`inode_security_bridge_enoent=1`，root oracle 无变化，且
`DISABLE -> CLEAR -> UNLOAD` 完整恢复。下一步按路线恢复 `shadow_mode=0` 的完整 held-FD
mutation 回归；只有九类 mutation 全部为 `ENOENT` 且无真实修改，才可关闭 mutation
封闭门禁。产品状态仍为 `Hide 1.0 = unsupported`。

generation `12002` 已完成该完整回归。target 的 open-hidden、create、truncate、mkdir、
unlink、symlink、rmdir、rename、link 和 mknod 全部为 `ENOENT/no-side-effect`，target
oracle 未变化；control 保持可见并可执行允许的 mutation。`dentry_install=5/5/0`，
恢复链和卸载通过，boot ID 未变化。held-FD mutation 门禁已关闭，下一门禁为 cache-order、
并发、规则切换和生命周期全矩阵；产品状态仍为 `Hide 1.0 = unsupported`。

generation `13001` 已通过 cache-order：三个可访问 shared-storage alias 的 cold open、
cold opendir、stat-then-open、readdir-then-open 和 positive-warm-then-open 对 target 全部
为 `ENOENT`，control 全部可见，fixture/oracle 未变化；恢复和卸载通过。cache-order
门禁已关闭，下一项是 20 线程并发与规则切换/生命周期回归。

### generation 14002：20 线程只读并发门禁

generation `14001` 因 target PID 退出而作废，禁止复用其 PID、namespace 或确认记录。
清理旧 binding 后重新建立 generation `14002`：target PID `30544`、mount namespace
`4026536066`、UID `10552`，fixture 为
`/storage/emulated/0/Pictures/PathGuardHideLab/20260921-084328/hidden`。

用户明确确认后执行 `ENABLE 14002`，随后在同一 PID/namespace 执行 HideLab concurrency：

- target 三个可访问 alias × `stat/open/readdir` × 20 线程 × 100 次，全部返回隐藏结果，
  每类成功数为 `0`；
- control 同矩阵每类成功数为 `2000`；
- target/control oracle 均未变化，`summary.json` 为 `PASS`；
- target PID、mount namespace 和 boot ID 全程稳定；
- 模块 status 的 `active=0/0/0`、`open_count=0`，未见引用泄漏。

证据：

```text
build/device-evidence/hidelab-baseline/20260921-084601/
```

测试结束后执行 `DISABLE -> CLEAR -> UNLOAD`，`rmmod` 成功且模块已从
`/proc/modules` 消失。该阶段只证明只读并发访问的 target isolation 和 control
非误阻断；尚未证明规则切换、DISABLE/CLEAR 竞态、卸载期间已有 FD、mutation、
namespace 销毁、OTA 重新准入或 HideLab 全量矩阵。因此产品状态不变：

```text
Hide 1.0 = unsupported
```

### held-FD 修复候选

离线实现已覆盖 cached positive child：所有缓存文件/目录都安装 observer-aware
`d_revalidate`，hidden directory 下的新 lookup 则在进入 FUSE 前返回 synthetic
negative。缓存枚举溢出或任一 descendant shadow 安装失败会使 ENABLE 事务回滚。

该候选只有在以下真机证据同时满足时才能关闭本门禁：

- prepare/post 的 native PID、mount namespace 和 held FD 均保持一致；
- existing-child `O_TRUNC` 与 create/mkdir/unlink/rmdir/rename/link/symlink/mknod
  全部返回 `ENOENT`；
- target oracle 不变，control observer 保持可见且可正常操作；
- `d_revalidate_hidden` 或对应 mutation blocked counter 与测试路径一致增长；
- `DISABLE -> CLEAR -> UNLOAD` 完整恢复且设备稳定。

## 19. 2026-09-19：统一策略入口与 KPM/namei 执行门禁

本轮结合 Linux pathname lookup 官方文档、SukiSU Ultra、KPatch-Next、SUSFS、NoMount
和 Kasumi 源码，调整后续执行顺序：不再把新增 syscall 白名单作为主路线，而是把
PathGuard 定义为“path-resolution policy + backend”。策略身份固定为：

```text
fsuid + mount namespace + parent superblock/device/inode + basename + generation
```

后端按能力分层：

| 后端 | 责任 | 当前定位 |
|---|---|---|
| KPM/namei | 固定 myron 内核上的最终组件、open/create、stat、readdir 和 mutation 前置拒绝 | 首选实验数据面，必须先通过 capability gate |
| VFS shadow | lookup、synthetic negative、readdir、d_revalidate、cache/lifecycle 辅助 | 保留为现有 LKM 实验和协同数据面 |
| LSM | mutation/access 的辅助拒绝 | 不能单独提供 exact hide |
| SukiSU bridge | 提供极窄的 PathGuard policy 注册/撤销面 | 只有 KPM 不可用且完成 companion 设计后评估 |
| syscall hook | 诊断和缺口定位 | 不作为生产隐藏主路线 |

Linux 官方文档明确区分 RCU-walk、REF-walk、最终组件、`LOOKUP_CREATE`、
`LOOKUP_OPEN`、相对 `dirfd`、重命名并发和 dcache 语义；因此不存在可由普通 LKM 稳定
注册的万能 VFS hook。SUSFS 的真实实现修改 `fs/namei.c`、`fs/readdir.c`、`fs/stat.c`
和多个 mutation 路径，证明 exact hide 需要在真实对象 callback 之前以及目录项写入用户
缓冲之前决策。NoMount 同样要求 `namei/readdir/stat/d_path` 内核集成。Kasumi 可复用
per-object metadata、SRCU/Tasks-RCU、stale workqueue 和 STOP/RESTORE/DRAIN/FREE
生命周期，但不能直接作为完整 mutation/FUSE backend。KPatch-Next 的 KPM 仅证明固定
内核上的函数 inline hook 能力，不构成跨设备 ABI。

本轮新增只读采集器：

```text
tests/device/hide/collect_kernel_backend_capability.ps1
```

它只查询设备 release、`CONFIG_KPM`/`CONFIG_KALLSYMS`/`CONFIG_KPROBES`、SukiSU KPM
只读命令和 kallsyms 可见性，不执行 KPM load/unload、insmod 或行为 hook。只有以下条件
同时成立才允许进入 KPM capability probe；KPM 查询还必须通过输出内容校验，不能只看
CLI 进程退出码：

```text
CONFIG_KPM=y
CONFIG_KALLSYMS=y
ksud kpm version 非空且无失败文本
ksud kpm num 为整数且无失败文本
ksud kpm list 无失败文本
```

否则状态必须保持 `unsupported` 或 `indeterminate`。历史设备证据已有
`ksud kpm -> ENOTTY`，所以当前不能预设 KPM 可用；ADB 未连接时也不能伪造采集结果。

后续执行顺序冻结为：

1. 设备只读 capability gate；
2. KPM/namei 不改变行为的符号和调用约定 probe；
3. 固定 myron 的只读 namei backend；
4. mutation 前置封闭和 strict `symlinkat -> ENOENT`；
5. cache、alias、namespace、生命周期和卸载回归；
6. HideLab 全量回归及设备/OTA admission；
7. 仅在完整证据通过后评估 SukiSU companion bridge 和产品集成。

参考链接：

- Linux pathname lookup：<https://www.kernel.org/doc/html/latest/filesystems/path-lookup.html>
- KPatch-Next：<https://github.com/KernelSU-Next/KPatch-Next>
- SukiSU Tracepoint Hook：<https://github.com/SukiSU-Ultra/SukiSU-Ultra/blob/main/docs/guide/tracepoint-hook.md>
- SukiSU KPM 文档：<https://github.com/SukiSU-Ultra/SukiSU-Ultra/blob/main/docs/guide/installation.md>
- SUSFS：<https://gitlab.com/simonpunk/susfs4ksu>

## 22. 2026-09-22：修复全量回归 fixture identity 编排门禁

对 v12 全量 active 证据复核发现，旧版编排器在每个场景启动时都会对传入的
`FixtureRoot` 执行 `rm -rf` 后重建目录。若 `INSTALL`/`ENABLE` 已经绑定了原 parent
inode，这会把规则绑定对象替换成新的 inode，导致后续场景全部脱离绑定并表现为
`LEAK`；mutation 场景还可能把该编排错误误报为 `DESTRUCTIVE_FAIL`。这不是有效的
后端能力证据，必须归类为测试基础设施错误。

已完成的修复：

- `run_hidelab_baseline.ps1` 新增显式 `-InitializeFixture`；只有初始化阶段允许删除并
  创建一次性 fixture；
- 传入 `-FixtureRoot` 的 active 场景只验证 fixture 存在，不再重建 parent 或 hidden
  目录；
- 每轮记录 `hidden_path`、`parent_inode` 和 `shadow_mode`；
- 可选 `-ExpectedParentInode` 在 probe 前强制校验 parent identity，不匹配直接失败；
- full runner 对每个 summary 强制要求 `parent_inode` 为数值，缺失或类型漂移直接归类为
  `INFRA_ERROR`，不得将空值隐式转换为 `0`；
- active full runner 在启动前读取 `/sys/module/pathguard_hide1/parameters/shadow_mode`，
  强制核对实际已加载模块参数与 runner 参数一致；只在 summary 中记录参数而不核对内核
  实例的做法无效。
- mutation 场景中 target 无法打开隐藏目录而产生的 `held_fd` `setup_error/EBADF` 是
  fail-closed 的预期结果，不得误报为 `SEMANTIC_DRIFT`；control 的可写 mutation
  只用于验证 caller isolation，允许其改变 disposable fixture，不能据此否定 target。
- `run_hide1_full_regression.ps1` 的 active 模式强制要求预创建 `-FixtureRoot`、
  `-KeepTargetProcess`、`-ShadowMode 0` 和 mutation 确认；
- 离线契约检查覆盖初始化/active 分离、parent inode 校验和 mode 记录。

新的执行顺序冻结为：

```text
InitializeFixture（无模块）
-> 记录 fixture_root/hidden_path/parent_inode
-> INSTALL/ENABLE（不得删除或重建 fixture）
-> shadow_mode=0 full regression
-> DISABLE/CLEAR/rmmod
-> 删除 disposable fixture
```

修复后的离线验证：

```text
PowerShell parser（两个 runner） PASS
validate_hidelab_offline.ps1    PASS
git diff --check                 PASS
```

修复前的 v12 全量证据不得直接用于判定当前数据面失败；修复后必须使用同一模块、
同一 generation、同一 target PID/namespace 和同一 parent inode 重新执行 mode 4 正确
路径 smoke test，以及 `shadow_mode=0` 的全量 active 回归。产品状态在新证据完成前仍为：

```text
Hide 1.0 = unsupported
```

## 23. 2026-09-22：真实 mode 0 full active 回归完成

在 runner 增加实际模块参数一致性校验后，使用 generation `22002`、固定 target
PID/namespace 和 parent inode `427341` 完成真实 `shadow_mode=0` full active regression。
五组场景（baseline、cache-order、concurrency、reliability、mutation）全部 PASS，
mountinfo 未变化，结论为 `candidate_pass_requires_admission`。随后已执行
`DISABLE -> CLEAR -> rmmod`，设备恢复到无模块状态。

下一步不是继续重复同一套 HideLab，而是执行设备 admission：精确 fingerprint、kernel
release、KMI/架构、模块哈希和 undefined symbol 可解析性校验，保存本轮 full-regression
证据，并在重启/OTA/slot 变化后重新拒绝准入。只有 admission 脚本明确消费
`candidate_pass_requires_admission` 且全量证据有效时，才允许评估 `admitted`；在此之前
产品状态保持 `Hide 1.0 = unsupported`。

## 24. 2026-09-22：完成设备准入、重启重新准入和 OTA/slot 拒绝模拟

按路线完成 1-4：

1. 重构 `tests/device/hide/admit_hide1.ps1`，从“模块存在”升级为证据驱动门禁，校验
   fingerprint、kernel release、aarch64、KMI、模块 SHA-256、undefined symbol 集合和
   kallsyms 名称可解析性，以及 active/generation/shadow mode/parent inode 和完整
   `candidate_pass_requires_admission` 证据。
2. 当前 myron 实例通过设备级准入：

   ```text
   admission=admitted
   product_state=unsupported
   generation=22002
   shadow_mode=0
   module_sha256=0bc66ebfa6c741280f37fc97bd4d771341b2460b1c27da63011cddd52a5e7b13
   undefined_symbol_count=76
   ```

3. 完成 `DISABLE -> CLEAR -> rmmod -> reboot`。重启后模块未加载，admission 明确返回
   `unsupported`；重新加载、启动新 target、INSTALL/ENABLE 后再次通过，证明 active
   状态不会跨重启伪复用。
4. 使用隔离 allowlist 模拟 fingerprint 和 kernel release/slot 变化，两者均返回
   `unsupported`，未接受旧设备准入。

证据：

```text
build/device-evidence/hide1-admission/20260922-130814/admission.json  # admitted
build/device-evidence/hide1-admission/20260922-130653/admission.json  # reboot 后 unsupported
build/device-evidence/hide1-admission/20260922-130914/               # fingerprint 模拟拒绝
build/device-evidence/hide1-admission/20260922-130920/               # kernel 模拟拒绝
```

设备级 admission 已完成，但产品状态仍为 `Hide 1.0 = unsupported`。后续才可进入
daemon/UAPI 正式集成；任何设备、KMI、模块哈希、slot 或 OTA 变化都必须重新执行本门禁。

## 25. 2026-09-22：阶段 8.1-8.4 完成 daemon Hide adapter 离线实现

在设备 admission 之后，开始执行 daemon 正式集成的前四个子阶段。实现位于：

```text
daemon/include/pathguard/hide1_backend.h
daemon/src/hide1_backend.cpp
tests/unit/hide1_backend_test.cpp
```

### 25.1 规则转换门禁

`TranslateRule` 只接受当前实验 UAPI 能表达的严格子集：

- 单 package、单 selector、单 action；
- `deny + complete_vfs`，拒绝 redirect、mount 和其它 domain；
- literal selector，拒绝 glob、except 和 descendant；
- selector 必须是绝对路径，并拆分成一个 parent 与一个 basename；
- target UID、PID、starttime、mount namespace 必须已确认；
- 不接受多路径、SAF/Provider URI 或无法映射到真实 filesystem path 的规则。

任何不满足条件的规则都返回明确的 `hide-unsupported-rule` 或
`hide-scope-out-of-range`，不会静默降级为 deny/redirect。

### 25.2 独立 Hide 状态机和事务

`Backend` 不复用 redirect 的 `ControlStatus`，状态为：

```text
unsupported -> admission_pending -> inactive -> installing -> active
active -> stopping -> inactive
```

`Apply` 的固定顺序为：

```text
admission + identity 校验
-> INSTALL
-> STATUS(INACTIVE/READY/generation)
-> ENABLE
-> STATUS(ACTIVE/RUNNING/generation)
```

任意一步失败，都会执行 `DISABLE -> CLEAR`；回滚失败会进入 `failed`，禁止继续
报告 active。`Stop` 会重新读取身份并联合校验 UID、PID、starttime 和 mount namespace，
身份过期时拒绝撤销并保持 fail-closed。

### 25.3 generation 与设备 transport

daemon 每次发布 Hide 规则使用单调递增的 deployment generation，并将 generation
写入 UAPI `expected_generation`。Linux transport 直接使用 `/dev/pathguard_hide1` ioctl；
主机测试通过注入 transport 验证事务，不通过 shell 拼接命令。

该组件已加入 daemon 构建，但 `pathguardd` 默认不会自动启用 Hide；没有 admission、
身份快照和明确的 Hide 规则时状态保持 `unsupported/inactive`。

### 25.4 离线验证

```text
cmake --build build --target pathguard_hide1_backend_test
ctest --test-dir build -C Debug -R pathguard_hide1_backend_test --output-on-failure
```

结果：`pathguard_hide1_backend_test` PASS，覆盖规则收缩、admission 缺失、generation
递增、身份变化拒绝、ENABLE 失败完整回滚。此阶段仍未改变产品状态：

```text
Hide 1.0 = unsupported
```

## 26. 2026-09-23：实验实现第 1-5 项修复与验证

按源码审查提出的五项顺序执行：

1. 修复 synthetic-negative 在 Control observer 重新验证后变成 positive 时仍保留
   target-only 标记的问题；正 dentry 会清除 marker 和 generation，再走正常策略判断。
2. 补充源码契约和模型并发验证，确认正/负缓存、UID/namespace 隔离、生命周期顺序和
   新增行为均有断言。
3. 固定 mount alias 语义：同一 mount namespace 内按 `(super_block, i_ino)` 识别 parent，
   因此同 inode alias 一致隐藏；不同 superblock/inode/namespace/UID/generation 不命中。
   这与现有三 alias 真机证据一致，当前不扩展到通用 vfsmount 隔离。
4. 解决已有 FD 的根因：目标 task 已持有 governed parent 或 hidden directory FD 时，
   `ENABLE` 返回 `-EBUSY`，不发布半有效 shadow；关闭 FD 后才能重试。该行为优先于把
   old-FD 泄漏误判为通过。
5. 将 `do_symlinkat/vfs_symlink/may_create/security_inode_symlink` probe 变为显式实验
   参数。默认核心模块不依赖这些 kernel-specific 符号；实验包手动加载时才打开诊断和
   strict errno bridge。

本轮验证：

```text
android16-6.12 DDK pathguard_hide1.ko       PASS
full CTest                                    91/91 PASS
git diff --check                              PASS
```

设备只读检查通过，未执行新的 ENABLE。由于当前仓库没有 `docs/13-*` 文档，后续仍以本
路线图和 `docs/11-hide-source-audit-log.md` 为准。下一步必须使用重新构建的实验包，在
disposable fixture 上验证 `ENABLE -> old-FD -EBUSY -> close FD -> ENABLE`，然后再运行
cache-order、mutation、并发和完整生命周期矩阵。任何设备证据不足时产品状态仍为：

```text
Hide 1.0 = unsupported
```
# 2026-09-25 P0-P2 验收收口

本轮设备级任务已完成：P0 admission 负向撤权、P1 Target 生命周期、规则热更新、deny/redirect/hide 组合回归、重启重新准入，以及 P2 工程构建收口均有独立 evidence。当前手机 boot 的 admission 为 `admitted`，daemon 自动 `INSTALL/ENABLE` 已恢复；Hide 最终状态为 `product_state=supported_scope_myron`，仅支持 Redmi K90 Pro Max（myron）。

关键证据：

- `build/device-evidence/hide1-admission-revocation/20260925-091536/negative-admission.json`
- `build/device-evidence/hide1-target-lifecycle/20260925-093043/target-lifecycle.json`
- `build/device-evidence/hide1-rules-hot-reload/20260925-095113/rules-hot-reload.json`
- `build/device-evidence/hide1-combination/20260925-095229/full-regression.json`
- `build/device-evidence/hide1-reboot/admission/20260925-095833/admission.json`

工程验证：最新 Release CTest `91/91`；NDK arm64-v8a、一次性 WSL LKM、ZIP 均通过。随后已安装该 WSL 构建，当前设备 boot `ed8bbcca-6d99-4e53-9b0e-440d1ecd5f4d` 的准入模块 hash 为 `0aaf0bf4...`，并完成 P0/P1 设备回归。
