# T-52～R-52 Provider resolver request/facts C ABI

## 范围

- 修正 resolver 跨库数据面，禁止把 C++ 对象布局暴露给 Zygisk `APP_STL=none` 侧。
- 定义 `PathGuardLsplantMappingRequestV1` 与 `PathGuardLsplantMappingFactsV1`，用于 future
  resolver wiring。
- 当前仍不连接真实 resolver，不读取 daemon/store，不启用 bit 17。

## 合同

- request 使用固定容量 byte arrays 承载 identifier 和 file path，容量分别为 1024 和 4096。
- facts 只承载 profile/runtime/operation 支持位和 numeric snapshot/binding/reverse handles。
- `ProviderRouteBindingV1`、`RouteRecord`、`ResolveResult` 等含 STL 的对象只允许在 LSPlant bridge
  内部构造。
- 在 immutable snapshot registry 未实现前，resolver 返回非零 binding/snapshot/reverse handle
  均拒绝并保持 pass-through/fail-open。
- `configure_mapping(nullptr)` 继续作为生产默认路径；安装 pending/complete 后修改配置仍返回
  `EBUSY`。

## 实现

- `provider_lsplant_bridge_api.h` 新增纯 C request/facts 结构和枚举常量。
- `ResolveMappingRuntimeFacts` 把 bridge 内部 request 复制为 C ABI request，调用 resolver 后
  验证版本、size、boolean 字段、operation mask 和 handle 状态。
- 当前 adapter 只接受 `PATHGUARD_LSPLANT_MAPPING_BINDING_NONE`；因此不会构造 route binding，
  evaluator 仍返回 pass-through。
- Host C 测试用 MSVC C17 解析 API 头并固定 request/facts size 与关键 offset。

## 验证

- Provider LSPlant Host：C17 API 解析、result observation 和 dispatch spec 矩阵通过。
- production integration guard：通过。
- NDK 29 LSPlant：`arm64-v8a`、`armeabi-v7a` 通过，ELF export guard 通过。
- MSVC Release CTest：`82/82` 通过。
- NDK r27d Zygisk：`arm64-v8a`、`armeabi-v7a` 通过，ELF isolation 通过。
- `git diff --check`：通过。
- 当前设备 passthrough：候选 `0.1.40-dev`（`versionCode=41`）证据
  `build/device-evidence/provider-lsplant-v1/20260802-152004` 通过。MediaProvider 的
  resolved/installed/backup/self-tested 均为 `2044`，ExternalStorageProvider 均为 `3`；两端
  `provider_bridge_errno=0`、`action_total=2` 且两条 admission 均为 `active`。两端
  `observed_capabilities=65536`，bit 17 保持清零；`logcat.txt` 存在且为空。shell 触发
  DocumentsProvider 的 `SecurityException` 是未持有 SAF 授权的系统拒绝，不属于 bridge fault。

## 边界

本任务只解除 C ABI 数据面阻断，不提供真实 route/provenance snapshot registry。真实
query/insert/open/reverse rewrite、trusted caller UID、FD identity 和 Java object factory 仍为后续任务，
bit 17 必须保持清零。
