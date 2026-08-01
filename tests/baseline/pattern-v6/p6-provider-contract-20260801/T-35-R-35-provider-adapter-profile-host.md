# T-35～R-35 Provider adapter profile Host 合同

## 范围

- Change scope: 方案 B 的 version-pinned profile 与 route binding Host conformance
- Device behavior: 未改变
- Production bit 17: 保持 `unsupported`

## T-35 红测

新增 `pathguard_provider_adapter_profile_test` 后，MSVC Release 按预期失败：

```text
provider_adapter_profile_test.cpp(1,10): error C1083:
cannot open include file: 'pathguard/provider_adapter_profile.h': No such file or directory
```

失败原因仅为 adapter profile/mapping 合同尚未实现。

## I-35 绿测

新增 `core/include/pathguard/provider_adapter_profile.h`：

- `ProviderBuildIdentityV1` 精确包含 kind、SDK、versionCode、APEX version 和 32-byte APK SHA-256；
- 单 Provider profile 必须覆盖九项 contract check 与完整 composite operation mask；
- deployment profile 强制 DocumentsProvider 与 MediaStore 分别匹配，任一不匹配则整体拒绝；
- `ProviderRouteBindingV1` 同时约束 visible source、backing target、content URI、stable document ID、
  strong FD identity 与 ADR-0017 reverse record；
- 未知 build、零 hash、hash/kind/version mismatch、弱 identity、同址映射、错误 reverse 全部 fail-open。

测试向量使用 V-64 实机归档的 alioth 完整摘要：

- ExternalStorageProvider:
  `44a42eeef364a1bd538e75c3553e45c9adfbd04a4b7af2dcfaa3f76bb448856e`
- MediaProvider:
  `f8f71eaedd78a1bb0c3bb3d81405f2529221fe6358ca5fe4ce74a5c3853ca9ed`

专项结果：

```text
pathguard_provider_contract_test: Passed
pathguard_provider_adapter_profile_test: Passed
```

## R-35 与边界

deployment pair 复用单 profile matcher，route binding 直接复用 ADR-0017 的
`ObjectIdentity`/`RouteRecord`，没有创建第二套 Provider provenance。完整 MSVC Release 为
`80/80`。

本阶段只冻结和实现 Host conformance，没有声明任何 Java/native 私有入口、没有安装 production
adapter，也没有改变 capability snapshot 或 bit 17。V-65 必须在 version-pinned 设备 adapter wiring
完成后验证 virtual query/create/open/rename/delete/reverse、FD identity 与 fail-open。
