# Hide 1.0 Phase B：Kasumi / NoMount 对照实验（myron）

日期：2026-09-12
设备：Redmi myron，serial `f3ba305a`
系统：Android 16 / API 36，fingerprint `Redmi/myron/myron:16/BP2A.250605.031.A3/OS3.0.23.0.WPMCNXM:user/release-keys`
内核：`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，arm64，KernelSU root 可用

## 结论

本阶段是源码审计和可加载性准入检查，不是把 Kasumi 或 NoMount 集成进 PathGuard。当前设备没有
与运行内核匹配的 kernel build tree、传统 Kbuild `Module.symvers` 和模块签名产物；虽然设备上
SukiSU 已证明另一条空 `__versions` + kallsyms loader 路径可运行，但 Kasumi/NoMount 本身没有
经过该 loader 的构建、符号重定位和设备加载证据。因此两个参考 LKM 的运行项仍必须记为
`UNSUPPORTED`，不能记为 PASS。

在源码层面，两个项目都证明了“在 VFS lookup/readdir 层生成 whiteout 或虚拟节点”是可行方向，但均不能直接满足 PathGuard Hide 1.0 的冻结语义：

| 维度 | Kasumi API 17 | NoMount v20 | PathGuard Hide 1.0 要求 |
|---|---|---|---|
| lookup | `kasumi_dirhijack.c` shadow `inode_operations.lookup`，隐藏规则发布 negative dentry | `nomount_hijacked_lookup` 按 child index 返回虚拟/负 dentry | target UID 得到稳定 `ENOENT`，Control 走原始对象 |
| readdir/getdents | shadow `iterate_shared`，过滤真实同名项并发出虚拟项 | shadow `iterate_shared/iterate`，bloom + child array 过滤/注入 | 所有 buffer 大小和 alias 都不泄漏 basename |
| cache | 对制造的 dentry 安装逐 dentry `d_revalidate`，并用 SRCU/Tasks-RCU 回收 | 主要依赖 dentry/inode shadow 和 seqcount/RCU child array | cold、positive dentry warmup、`stat -> open` 均保持 observer 一致 |
| atomic_open | 代码路径没有一个可独立验证的 `atomic_open` wrapper；依赖原始 lookup 后续流程 | 未发现 `atomic_open` wrapper | 必须覆盖 cold `open(O_CREAT/O_EXCL)` 和已有 dentry 命中 |
| mutation | bootstrap 动态解析 `vfs_create/mkdir/unlink/link/rename` 等；缺失时只告警并禁用部分目录 mutation | lookup/iterate 层没有隐藏名 create/remove/rename/link/symlink wrapper | mutation 在执行前返回 `ENOENT`，Root Oracle 无副作用 |
| scope | `kasumi_policy_current_scope()` 主要按 UID/provider 的 VIEW/SPOOF scope；不是逐规则 namespace ABI | rule exact key 含 `path + target_uid`，但没有 namespace scope；UID 0 具有特殊 bypass 语义 | target UID/namespace 精确隔离，Control 不受影响 |
| 事务/回滚 | 多个 shadow vector、superblock client、SRCU teardown，安装失败路径复杂 | rule tree/child array 事务有 RCU，但 payload 外层常返回 0，真实错误写 `payload->status` | 安装失败可观察、原子 replace/rollback、无 STATE_LIE |
| 可观测性 | 另有 mount/proc/maps/statfs spoof 能力，增加验证面 | 无 mount 注入，但存在虚拟 cookie/拓扑痕迹 | mountinfo/mounts/mountstats 无新增且不靠字符串 Hook 掩盖 |

## Kasumi 证据

源码位置：

- `refer/Kasumi-main/src/core/kasumi_dirhijack.c`
- `refer/Kasumi-main/src/core/kasumi_bootstrap.c`
- `refer/Kasumi-main/src/policy/kasumi_path_policy.c`
- `refer/Kasumi-main/src/control/kasumi_ioctl.c`

可直接由源码确认：

1. `kasumi_dh_lookup_inner()` 在命中 hide child 时发布 negative dentry，不调用真实 lookup；非目标观察者通过 `d_revalidate` 重新走原始对象。
2. `kasumi_dh_iterate_inner()` 先代理原始目录，再过滤/发出 child；实现同时处理 `iterate_shared` 和虚拟 cookie。
3. dentry shadow 保留原始 dentry operation，并在清理时执行 SRCU、Tasks-RCU 和 iterate client drain，说明作者意识到共享 inode operation 的生命周期风险。
4. bootstrap 通过动态 kallsyms/kprobe 解析 `lookup_one_len`、`vfs_create`、`vfs_mkdir`、`vfs_unlink`、`vfs_link`、`vfs_rename` 等符号；任一缺失只会告警，部分目录 mutation 被禁用。
5. policy scope 由当前 UID、root provider 和 allow/deny 列表决定，VIEW/SPOOF 是全局观察者分类，不等价于 PathGuard 的逐规则 package/namespace capability。

需要 LKM 才能验证：

- Android 16 / 6.12.23 的真实 `inode_operations`/`file_operations` 布局和 kallsyms 可见性；
- cold `atomic_open`、positive dentry warmup、rename 后 dentry 重验证；
- Target/Control 两个 app UID 是否会进入预期 scope；
- mutation delegate 缺失时是否一致 fail closed；
- unload 后 stale callback、RCU stall、hung task 和 tombstone。

## NoMount 证据

源码位置：

- `refer/hide-refer/nomount-master/kernel/src/nomount.c`
- `refer/hide-refer/nomount-master/kernel/src/nomount.h`

可直接由源码确认：

1. `nm_tree_search_exact()` 和 `nm_tree_insert()` 的比较键按 `hash + path length + path bytes + target_uid` 排序；因此当前 v20 代码支持同路径多 UID 规则并存，不能沿用早期“child index 覆盖”的旧判断。
2. `nomount_hijacked_lookup()` 只在目录 inode 的 shadow lookup 上查 child array；未发现独立 `atomic_open` wrapper。
3. `nomount_hijacked_iterate_dir()` 通过代理 actor 过滤真实同名项，并在虚拟 cookie 区间发出 child；cookie 高位带有 NoMount 标记，存在可观测差异。
4. `nomount_generate_virtual_topology()` 会为不存在的父级创建虚拟目录节点，超出 Hide 1.0 的单一真实 parent/basename 范围。
5. v20 控制面 `nm_process_payload()` 外层返回 0，命令错误通常写入 `payload->status`；调用方若只检查 key syscall 返回值，会出现 STATE_LIE。
6. 未发现面向隐藏名的 create/mkdir/unlink/rmdir/rename/link/symlink wrapper；仅有 lookup/iterate 过滤不能证明 mutation 无副作用。
7. 没有 namespace scope；UID 0/`target_uid == 0` 是全局规则语义，不符合逐 app 隔离要求。

需要 LKM 才能验证：

- child array 在并发替换、删除和目录 rename 下的完整 RCU 生命周期；
- shadow inode operation 在目标文件系统上的恢复顺序；
- Android 6.12 KMI 是否导出所需 VFS 内部符号；
- UID bypass 与 app mount namespace 的实际行为。

## 设备准入结果

```text
device = myron / f3ba305a
kernel = 6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
kernel_build_tree = missing
Module.symvers = missing (traditional Kbuild input; SukiSU loader path not tested for these projects)
Kasumi LKM = UNSUPPORTED (not built/loaded)
NoMount LKM = UNSUPPORTED (not built/loaded)
Hide 1.0 activation = BLOCKED
```

禁止把源码审计结果写成 Hide 1.0 通过。下一阶段应实现 PathGuard 自有固定 KMI prototype，并让同一套 HideLab 产生 Target hidden、Control visible、Oracle unchanged、no-new-mount 的可重复证据。
