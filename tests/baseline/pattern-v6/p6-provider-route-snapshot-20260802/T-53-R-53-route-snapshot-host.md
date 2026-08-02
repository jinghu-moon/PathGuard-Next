# T-53～R-53 immutable Provider route snapshot registry

## 范围

- 建立 Provider route binding 与 reverse result 的进程内不可变 registry 合同。
- registry 在 hook 安装前构造；回调热路径只读，不访问 daemon/store，不持有全局锁。
- 本任务不发布真实 route 数据、不接 production resolver、不构造 Java 替代对象、不启用 bit 17。

## 合同

- snapshot generation 必须非零，并与 resolver facts 中的 generation 完全一致。
- binding ID 和 reverse record ID 分属独立命名空间，均必须非零且在各自表内唯一。
- lookup 以 `(generation, binding_id, reverse_record_id)` 校验；generation 陈旧、binding 未知、
  reverse ID 未知或 registry 无效均返回空结果，由调用方 fail-open/pass-through。
- forward lookup 允许 `reverse_record_id=0`；reverse lookup 只返回 snapshot 内稳定的 const 指针，
  不复制 `ProviderRouteBindingV1` 或 `ResolveResult`。

## 实现

- `ProviderRouteSnapshotRegistryV1` 在构造阶段复制、排序并验证 entries，之后不公开 mutation API。
- 运行期使用 `std::lower_bound` 二分查找，不分配内存、不加锁、不调用外部 callback。
- LSPlant Android CMake 显式链接 registry 实现；production guard 固定该链接及关键失败开放合同。

## 验证

- MSVC Release 专项：generation、零/重复 ID、unknown/stale binding、独立 reverse ID 矩阵通过。
- 相邻 Provider mapping 与 LSPlant bridge Host 测试通过。
- NDK 29 LSPlant：`arm64-v8a`、`armeabi-v7a` 编译及 ELF guard 通过。
- production integration guard 与 `git diff --check` 通过。

## 边界

真实 snapshot 编码/发布、Zygisk resolver wiring、caller UID scope、route/provenance 数据构建、Java
result factory 和完整 query/insert/open/reverse 设备矩阵仍为后续任务。bit 17 必须保持清零。
