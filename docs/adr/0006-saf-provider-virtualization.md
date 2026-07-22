# ADR 0006：SAF 系统代写进程虚拟化

- 状态：试验性完整目录虚拟化
- 日期：2026-07-22

## 背景

应用直接访问路径时，PathGuard 可以在应用 mount namespace 内用 bind mount 实现
选择性目录重定向。但 SAF 的文件描述符由 DocumentsProvider 进程打开。以 LocalSend
为例，应用向 `com.android.externalstorage.documents` 发送 source tree URI，实际执行
`createDocument` 和 `openDocument` 的进程是 ExternalStorageProvider；应用自己的
namespace mount 不参与该文件打开。

在调用方改写 URI 不可行：应用只持有 source tree 的 URI grant，改成 redirect tree
后会在 Provider 权限校验阶段失败。在 ExternalStorageProvider namespace 做全局 bind
也不可接受，因为它会影响所有调用方，破坏 per-app 语义。

## 决策

新增显式应用选项：

```ini
provider = virtualize
```

该选项与基础 mount redirect 相互独立。启用后，PathGuard 注入
ExternalStorageProvider，并在文件系统写操作发生时读取 Binder calling UID。仅当 UID、
user 和 redirect 规则同时匹配时，才把 source 路径改写为 redirect 路径。

v0.1.9 的首个试验版本只改写写入 `open/openat`。该方案被否决：`createDocument` 在
backing 创建文件后，`DocumentsProvider.enforceTree()` 会先在 source 执行存在性与
canonical child 校验；由于 source 不存在，后续 open 尚未执行便抛出
`FileNotFoundException`，并在 backing 留下零字节文件。

替代方案是在 ExternalStorageProvider 内提供 bind-mount 等价的完整虚拟目录视图。
同一 Binder 调用方命中规则后，source 根及其所有子路径的以下操作统一映射到 backing：

- 打开：`open/open64/openat/openat64`；
- 查询：`stat/stat64/lstat/lstat64/fstatat/fstatat64`、`access/faccessat`；
- 目录：`opendir`、`mkdir/mkdirat`、`inotify_add_watch`；
- 变更：`remove/unlink/unlinkat/rmdir`、`rename/renameat/renameat2`；
- 规范与属性：`realpath`、`chmod/chown` 系列、`truncate`、`utimensat`、`statvfs`。

`realpath` 先在 backing 上做真实 canonical 校验，再把结果反向翻译成 source，避免
Provider 对外泄露 backing document ID。嵌套规则按最长 visible path 匹配，`.`/`..`
尾部拒绝映射。Provider 调用 `clearCallingIdentity()` 时保存当前 Binder UID，并在
`restoreCallingIdentity()` 后清除，避免身份清除期间失去应用归属。

文件关闭后的媒体扫描在独立进程中执行，已不存在原 Binder UID。因此显式启用
provider virtualization 时，同时向主线 MediaProvider 进程安装 source→backing 映射，
使 ModernMediaScanner 和后续 MediaStore FD 操作能够访问真实文件。MediaStore 是系统
全局索引面，这部分映射不声明为 per-app 私有查询语义；它是 SAF 写入完成后的系统兼容面。

Provider 仍接收和校验原始 source URI，也仍向应用返回 source document URI；路径只在
权限校验后的文件系统调用边界改写，因此不要求应用持有 redirect tree grant。

## PLT hook 目标约束（v0.1.11 修复）

v0.1.10 的实现从 `/proc/self/maps` 收集进程内**全部已加载镜像**（上限 256），并对每个
镜像的 GOT 注册文件操作 hook。该做法会命中生命周期短暂的镜像：ART 在
`art::jit::Jit::PostZygoteFork()` 阶段创建 JIT 线程池并 dlclose 一批临时库
（JIT cache、动态加载的 HAL/AIDL client 库）。PLT-hook 后端（lsplt）在其记账表中
保留指向这些镜像 GOT/备份槽的指针；库被卸载后，dlclose 的 `call_destructors`
遍历触及已 unmap 的地址，触发 `SIGSEGV/SEGV_MAPERR`，使 MediaProvider 陷入
崩溃-重启循环，并级联导致 StorageManagerService 异常与文件管理器无法访问目录。
这直接违反本 ADR「Provider 崩溃时完整 fail-open」的验收要求。

因此 PLT hook 只允许注入**进程全程常驻、绝不 dlclose** 的库。当前白名单按 basename
匹配（源自设备实测的 Java runtime 文件调用链）：

- `libjavacore.so`：`libcore.io.Linux` JNI，承载 `java.io.File` 与 `android.system.Os`；
- `libopenjdk.so`：`java.nio` / `java.io` 原生文件访问；
- `libnativehelper.so`：JNI 文件辅助；
- `libandroid_runtime.so`：framework 原生文件访问。

实测这些库导入 `open`（非 `openat`）、`stat/lstat64/stat64`、`access`、`opendir`、
`mkdir`、`rename`、`remove/unlink/rmdir`、`realpath`、`chmod/chown`、`statvfs`、
`inotify_add_watch`、`readlink`，与 provider 的实际 Java 文件路径完全对应；
`capability` 掩码与全量 hook 相同（`0x5ff`）。禁止为追平 `openat` 等 `*at` 变体而把
非常驻库（HAL/AIDL、JIT、vendor 动态库）加入白名单——它们不在 provider 的真实文件
路径上，只会重新引入卸载期悬空指针。命中白名单之外的 caller 时退化为 fail-open
（不改写），不得崩溃。

## 安全边界

- 只有 `provider = virtualize` 的规则参与系统代写。
- `users = *` 不允许进入 Provider hook；必须使用显式数字 user。
- 调用方 UID 无法解析、路径无法归一化、hook 安装失败时一律 fail-open。
- 不在 Provider namespace 安装全局 bind mount。
- 不根据 Provider 自身 UID 应用普通应用规则。
- shared UID 当前按 UID 共享语义处理；在 package 级 Binder attribution 完成前需明确记录。
- ExternalStorageProvider 的读取、查询和变更必须使用同一映射，否则无法形成一致目录视图。
- MediaProvider 只装载显式 provider 规则，用于异步扫描与媒体 FD；不从普通 mount redirect
  隐式推导。

## 格式影响

policy format 升级到 v5。package entry 偏移 42 的保留字节改为
`provider_compat`：0 为 off，1 为 virtualize；偏移 43 继续保留且必须为 0。未知值拒绝。

## 验证门槛

- LocalSend 通过 ExternalStorageProvider 接收普通文件，source 为空且 redirect 出现文件。
- Provider 日志同时包含调用方 UID 和 from/to 路径，且 UID 等于 LocalSend UID。
- 未启用的应用写入 source 不被重定向。
- Provider 重启、模块 hook 失败和未知 OEM 路径均 fail-open，不导致 Provider 崩溃。
- create-directory、rename、delete、query、目录观察、canonical child 和媒体扫描均需通过。
- shared UID package attribution 与 OEM 非 AOSP Provider 仍需专项矩阵，完成前保持试验状态。
