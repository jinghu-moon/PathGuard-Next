# T-37～R-37 Provider mapping decision Host 证据

## 范围

- Change scope: Provider route binding 到 Java/native 改写前的纯 Host 决策合同
- Android/JNI behavior: 未接线、未改变 passthrough callback
- Production bit 17: 保持 `unsupported`

## 红测

- 新增 `pathguard_provider_mapping_test` 后首次构建失败；唯一失败点为
  `pathguard/provider_mapping.h` 不存在。
- 红测冻结 URI/document ID 缺失、source/target 同址、弱 FD identity、binding reverse
  mismatch、ambiguous/missing reverse、profile mismatch、operation mask 缺失、runtime
  unavailable 和无 route 透传。

## 实现

- `ProviderMappingRequestV1` 只消费既有 `ProviderAdapterProfileMatchV1`、
  `ProviderRouteBindingV1` 和 `provenance::ResolveResult`，没有建立第二套 Provider route store。
- 只有 profile 匹配、operation 完整、runtime 可用、binding conformance 通过且 committed reverse
  唯一一致时返回 `kRewrite`。
- 未命中 route 返回 `kPass`；未知 profile/缺 operation/缺 reverse 返回 `kUnsupported`；
  reverse ambiguous 或 unique record 不一致返回 `kAmbiguousReverse`；无效 binding 和 runtime/
  reverse error 返回 `kFailOpen`。
- evaluator 无 JNI、无 Android 私有 ABI、无 I/O，不读取 daemon/store，不设置 capability。

## 结果

- 专测：`pathguard_provider_mapping_test` 1/1 通过。
- MSVC Release：82/82 通过。
- `zygisk/src/module_entry.cpp` 的 bit 17 清零逻辑未修改。

## 剩余边界

T-37～R-37 只关闭 Host 决策合同。Java Hooker/native bridge 尚未将 Provider 参数/返回值转换为
统一 binding，也没有执行 query/create/open/rename/delete/reverse 的真实改写；因此 V-65 继续
保持 `pending`，bit 17 继续保持 `unsupported`。
