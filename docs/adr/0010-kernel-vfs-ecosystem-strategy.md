# ADR-0010：内核 VFS 生态采用部分跟随策略

状态：Accepted

日期：2026-07-29

## 背景

PathGuard 的文件级 glob deny/redirect 需要按调用方身份处理 lookup、create、rename、unlink、
readdir 和反向路径。目录 bind mount 无法表达该语义。NoMount、ZeroMount、Kasumi（原
HymoFS）等项目证明，内核 VFS/namei 层可以提供比 libc/Provider Hook 更完整的覆盖；但它们
当前没有共同的版本化 UAPI，也没有统一的 per-app shared-storage action 模型。

生态名称和维护状态以 2026-07-29 核验结果为准：HymoFS 仓库已明确标记改名为 Kasumi 且不再
维护，后续状态必须在 P4 adapter 评估时重新核验，不能把本记录当作永久项目清单。

完全忽略该生态会重复建设内核能力；直接依赖其中一个项目，则会把 PathGuard 的规则语义、
设备覆盖和发布周期绑定到第三方内核实现。AOSP MediaProvider/FUSE 可以覆盖 stock/GKI 设备，
但其内部 ABI 随 Mainline/OEM 版本变化，不能仅凭 Android 或内核版本声明支持。

本决策同时遵守[架构总纲 ADR-009](../00-architecture-design.md#25-首批架构决策记录)
“不依赖 SUSFS”的边界：SUSFS 或定制 GKI 能力可以被探测并由可选 adapter 使用，但不能成为
基础 deny/redirect 的安装前提。

## 决策

采用“部分跟随、核心零依赖、后端可插拔”策略：

1. `rules.toml`、Pattern IR、Action IR、`policy.bin` 和产品语义不引用 NoMount、ZeroMount、
   Kasumi、SUSFS 或任一第三方 ioctl/内核符号。
2. P1～P3 只建设后端中立的 Pattern Engine、Action Evaluator、Provider adapter 和已有 app
   path adapter，不等待定制内核生态。
3. P4 首先冻结 `DynamicPathBackend` 契约和 conformance suite。社区能力只能通过独立、可选、
   版本化 adapter 接入；adapter 不得修改 selector、precedence、collision 或 failure 语义。
4. 不把自建通用 FUSE 后端列为 P4 默认交付物。只先做有界 feasibility prototype，验证请求
   identity、lookup/create/rename/unlink/readdir、反向映射、原子 generation 切换及 OEM/Mainline
   兼容性；未通过门槛则停止投入，`complete` enforcement 保持 unsupported。
5. 若某个内核 VFS 项目满足下述接入门槛，可提供可选 adapter；用户未安装兼容内核时，模块
   仍可使用 Provider/app-path 能力，不能安装失败。

## 接入门槛

候选内核后端必须全部满足：

- 有版本化 UAPI、feature query 和明确许可证，不依赖设备特定未导出符号；
- 能按 caller UID、Android user 和 PathGuard package scope 执行 include rules；
- 至少覆盖 lookup/open/stat/access/create/rename/unlink/readdir 和唯一反向映射；
- 支持一次性发布完整 generation、撤销旧规则、失败回滚和 daemon/module 重启清理；
- 不把完整 VFS 语义静默降级成 OverlayFS、MagicMount、目录 bind 或部分 libc Hook；
- 通过相同的 backend conformance suite，并至少覆盖项目设备矩阵中的两个 GKI 代际；
- 后端缺失或 probe 失败时按 action admission 变为 unsupported，不伪造 active。

## FUSE 投入结论

FUSE 保留为 stock/GKI 设备上的候选 adapter，但不是主架构依赖，也不提前建设完整产品后端。
P4 的投入上限是“接口、探测、原型、兼容矩阵和停止条件”。只有原型在至少两个 Mainline/
OEM 组合上证明完整请求身份和操作矩阵，且维护成本低于引入内核 adapter 时，才以新 ADR
批准产品化 FUSE 后端。

因此，本决策不是“不做 FUSE”，而是“不在证据不足时先投入完整 FUSE 实现”。

## 否决方案

### 完全依赖一个社区 VFS 项目

否决。当前候选项目的目标、UAPI、fallback 和维护状态不同，无法作为所有 Android 12+
设备的稳定基础，也会违反规则与后端解耦原则。

### 完全不跟随内核生态并自建全部能力

否决。该方案会重复实现社区已在演进的 VFS 能力，并承担长期 GKI/内核版本维护成本。

### 立即产品化自建 FUSE

否决。AOSP 证明 FUSE 模型可行，但没有证明 PathGuard 能跨 OEM 稳定接入内部请求路径；先写
完整实现会把最大投入放在尚未通过 capability probe 的后端上。

## 后果

- P1～P3 的实现和交付不受定制内核项目进度影响。
- stock 设备可能只能获得 provider scope，而不能获得 `complete` enforcement；产品必须如实
  显示 unsupported。
- P4 必须先交付统一后端契约和测试套件，不能先写某个项目的专用分支。
- 社区 adapter 可以独立发布和淘汰，不引发规则格式迁移。

## 重开条件

出现以下任一变化时，新 ADR 可以替代本决策：

- Android/AOSP 提供稳定公开的按调用方路径转换接口；
- 某个社区 VFS 项目形成版本化、跨 GKI、持续维护的通用 UAPI，并通过全部接入门槛；
- FUSE feasibility prototype 在设备矩阵中通过完整语义和性能门槛；
- 项目产品范围改为只支持指定定制内核。

## 依据

- [Pattern redirect design §3.4、§3.6、§7.7](../08-pattern-redirect-design.md)
- [AOSP scoped storage/FUSE](https://source.android.com/docs/core/storage/scoped)
- [NoMount](https://github.com/maxsteeel/nomount)
- [ZeroMount](https://github.com/Enginex0/zeromount)
- [Kasumi（原 HymoFS）](https://github.com/Anatdx/Kasumi)
- [HymoFS 改名声明](https://github.com/Anatdx/HymoFS)
