# V-46 CompleteVfs 投资门

- Change ID: `p6-complete-vfs-gate-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Decision: `adapter-only`
- Status: `complete`

## 评审结果

依据 ADR-0010 的全部接入门槛，当前没有已批准的真实 backend：

- 仓库内没有版本化的内核/FUSE UAPI、feature query 或跨 GKI 兼容声明；
- 没有同时覆盖 lookup/open/stat/access/create/rename/unlink/readdir 的生产 adapter；
- caller UID、Android user、route provenance reverse 与原子 generation 切换未在 backend 上闭环；
- 没有两个 GKI/Mainline/OEM 组合的完整 conformance 证据；
- 当前可验证代码仅是后端中立的 `OperationPlan`/`Backend` fake 和统一 `AdmitAction` gate。

因此不满足任务清单 T-29 的明确依赖“V-46 结论为 go”。决策固定为 `adapter-only`：保留零依赖
接口以防未来有合格 backend，但不实现、发布或声明 bit 18 active。

## 自动化安全边界

fake adapter 已验证：

- inactive/unsupported adapter 不调用 backend；
- capability/operation 缺失不调用 backend；
- capability generation 或 plan generation stale 不调用 backend；
- 只有统一 admission active 且 backend operation mask 完整时才调用一次 backend。

这些测试证明核心 C1～C6 不依赖 CompleteVfs，但不是完整 conformance suite，也不能转化为设备
capability observation。

## 后续任务状态

- T-29/I-29/R-29：`blocked`，原因是 V-46 非 go；
- V-47：`blocked`，不存在获批原型可做 available/unavailable/partial 重放；
- bit 18：保持 `unsupported`；
- 重开条件：ADR-0010 列出的公开接口、合格社区 UAPI 或双 GKI feasibility 证据出现后，先新增
  superseding ADR，再恢复 T-29。

该阻断是投资门的预期结果，不是核心功能回归。
