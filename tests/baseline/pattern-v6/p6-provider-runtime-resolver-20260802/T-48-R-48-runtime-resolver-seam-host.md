# T-48～R-48 Provider runtime resolver C ABI seam

## 范围

- 冻结 Provider callback 到 route/provenance evaluator 的 runtime facts resolver seam。
- 将 LSPlant bridge 配置入口保持为 C ABI，避免 Zygisk 入口依赖 C++ 核心头或共享 C++
  runtime。
- 真实 resolver、Java 返回对象构造和 bit 17 enable 继续关闭。

## 实现

- `ProviderMappingRuntimeFactsV1` 携带 profile match、supported operations、route binding、
  reverse resolve result 和 runtime availability，并复用既有 `EvaluateProviderMapping`。
- `EvaluateProviderMappingWithResolver` 在 resolver 缺失或返回失败时返回 pass-through；成功时
  只构造既有按值 `ProviderMappingRequestV1`。
- `PathGuardLsplantMappingRuntimeV1`/`pathguard_lsplant_configure_mapping_v1` 使用不透明指针
  callback 的 C ABI；native bridge 通过 `ResolveMappingRuntimeFacts` 适配到 C++ evaluator。
- 配置在 hook 安装前固定；hook 已安装或安装 pending 时返回 `EBUSY`；不在 Provider 热路径
  访问 daemon/store，热路径不持有全局配置锁。
- Zygisk Provider prepare 阶段显式清除 resolver，保持当前生产行为为 LSPlant passthrough。

## 官方约束依据

- Android `ContentProvider` 官方 API 说明 query/insert/update/delete/openFile 是独立数据访问
  边界，且数据访问方法可能并发调用，Provider 必须线程安全：
  <https://developer.android.com/reference/android/content/ContentProvider>
- Android `DocumentsProvider` 官方 API 要求已返回的 document ID 保持稳定，不能用 display
  name、URI 末段或返回数量替代稳定外部标识：
  <https://developer.android.com/reference/android/provider/DocumentsProvider>
- AOSP MediaProvider/MediaDocumentsProvider 的具体 query/openDocument 实现属于 Mainline/版本
  代码，不能当作稳定私有 ABI：
  <https://android.googlesource.com/platform/packages/providers/MediaProvider/+/master/src/com/android/providers/media/MediaProvider.java>
  <https://android.googlesource.com/platform/packages/providers/MediaProvider/+/master/src/com/android/providers/media/MediaDocumentsProvider.java>
- LSPlant 官方 README 说明 Hook 同一目标方法的并发操作不保证原子性，并说明 inline
  deoptimization 只应在确认调用者集合后使用：
  <https://github.com/LSPosed/LSPlant/blob/master/README.md>

## 验证

- MSVC Release CTest：`82/82` 通过。
- 目标测试：`pathguard_provider_mapping_test`、`pathguard_provider_lsplant_bridge_test`、
  `pathguard_production_integration_guard`、`pathguard_comparison_report_guard` 全部通过。
- NDK r27d Zygisk 主模块：`arm64-v8a`、`armeabi-v7a` 通过。
- NDK 29 LSPlant bridge：`arm64-v8a`、`armeabi-v7a` 通过。
- C 语法头检查：`provider_lsplant_bridge_api.h` 可独立作为 C11 头解析。
- `git diff --check`：通过。
- ELF export guard 包含并验证 `pathguard_lsplant_configure_mapping_v1`。

## 设备边界

本轮生成 `dist/pathguard-next-v0.1.37-dev-universal.zip`，module version
`0.1.37-dev`、versionCode `38`，SHA-256
`5595160c8e83bb1b58b594675798dc9ef6009843e654ae047b9ddc90269e8437`，待真机安装。
现有 `0.1.36-dev` 设备证据
`build/device-evidence/provider-lsplant-v1/20260802-124319` 与 `20260802-124446` 仍只证明
LSPlant passthrough/restart 稳定；两端 `provider_bridge_errno=0`、admission active，bit 17 清零。

`0.1.37-dev` 真机回归 `build/device-evidence/provider-lsplant-v1/20260802-141719` 通过：
MediaProvider `2044/2044/2044/2044`，ExternalStorageProvider `3/3/3/3`，两端
`provider_bridge_errno=0`、`action_total=2`、admission active，`observed_capabilities=65536`，
bit 17 清零；未捕获 Provider fatal、null receiver 或 JNI local-reference 异常。

真实 route binding/provenance resolver、Java `File`/`Uri`/`Cursor`/`ParcelFileDescriptor`/
`Count` 返回对象改写，以及 V-65 完整 mapping 矩阵仍为当前设备无法构造的
`unsupported/not_observed`，不得据此宣称方案 B 已完成。
