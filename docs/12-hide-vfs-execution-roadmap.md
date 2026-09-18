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
   per-object operation-table shadow 是继续验证 Hide 1.0-LKM 的最合适实验路线。
2. mutation 不能先于生命周期基础设施实现。必须先证明
   `STOP_NEW -> RESTORE -> DRAIN -> FREE`，再接入会改变真实文件系统的 callback。
3. NoMount、Kasumi、PathMask、SUSFS 和 SukiSU 都只能提供局部参考，不能作为完整
   PathGuard hide 数据面直接引入。
4. 只读 baseline、cache-order、20 线程并发、1000 轮 reliability 和
   DISABLE/CLEAR/rmmod 恢复已经在当前设备的单路径实验范围内通过；期间修复了
   readdir 后正 dentry 泄漏和 ACTIVE stale dentry 生命周期竞态。但 mutation、真实
   namespace 销毁、OTA admission 和产品集成仍未完成。
5. 在所有阶段通过前，产品状态必须保持：

```text
Hide 1.0 = unsupported
```

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
| 当前 mode | `shadow_mode=4`，只读 FUSE-aware |
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

### 最终准入

- [ ] HideLab 全量 active 回归；
- [ ] 重启后重新 load/enable 回归；
- [ ] 设备/KMI/module/loader 白名单；
- [ ] OTA/slot 变化自动拒绝；
- [ ] daemon Hide UAPI 集成；
- [ ] 产品状态机只在完整 admission 后允许 `active`。

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
