# V-64 alioth Provider 公共合同基线

- Change ID: `p6-provider-contract-20260801`
- Before commit: `88cc9d3787c98521400258404959b182a3586891`
- After commit: `working_tree`
- Status: `complete (alioth public-contract scope)`

## 环境

设备为 Xiaomi M2012K11AC（alioth），Android 13/API 33，MIUI
`V14.0.8.0.TKHCNXM`，fingerprint 为
`Redmi/alioth/alioth:13/TKQ1.220829.002/V14.0.8.0.TKHCNXM:user/release-keys`，kernel 为
`4.19.157-perf-g9607d8651312`，SELinux Enforcing，ABI 列表为
`arm64-v8a,armeabi-v7a,armeabi`。探针包为 `dev.pathguard.providercontract`，UID 10437，
minSdk 31、targetSdk 35。

## Provider 身份

| 域 | package / class | build identity | APK SHA-256 |
| --- | --- | --- | --- |
| DocumentsProvider | `com.android.externalstorage/.ExternalStorageProvider` | versionCode 33；versionName 13；`/system/priv-app/ExternalStorageProvider/ExternalStorageProvider.apk` | `44a42eeef364a1bd538e75c3553e45c9adfbd04a4b7af2dcfaa3f76bb448856e` |
| MediaStore | `com.android.providers.media.module/com.android.providers.media.MediaProvider` | versionCode 33；versionName 13；MediaProvider APEX versionCode 339990000；`/apex/com.android.mediaprovider/priv-app/MediaProvider@TKQ1.220829.002/MediaProvider.apk` | `f8f71eaedd78a1bb0c3bb3d81405f2529221fe6358ca5fe4ce74a5c3853ca9ed` |

MediaProvider package 同时注册 `com.android.providers.media.MediaDocumentsProvider`；本轮
DocumentsProvider 公共合同通过 `com.android.externalstorage.documents` 的 SAF tree 验证。

## 操作结果

SAF tree 为 `primary:Download/test-SAF`。12 个必需操作全部通过：

| 域 | 通过的必需操作 |
| --- | --- |
| MediaStore | insert、open_write、query、open_read、rename、delete |
| DocumentsProvider | create、query、open_write、open_read、rename、delete |

MediaStore 的额外 publish 检查也通过。测试结束后在 SAF tree、`Pictures/PathGuardContract` 和
`Pictures/PathGuardContractMoved` 搜索 `pg-contract-*`，结果为空。逐操作数据见
`V-64-provider-public-contract-alioth-observations.jsonl`；未提交的原始 dumpsys 证据位于
`build/device-evidence/provider-contract-v1/20260801-231015`。

## 结论边界

V-64 的公开 Provider 操作基线在 alioth 上通过，可进入 T-35 的 Host adapter profile 与虚拟映射
合同设计。该探针只调用公开 SAF/MediaStore API，没有安装生产私有 ABI hook，也没有验证 virtual
source/target、FD identity、reverse、Provider restart 或 fail-open，因此 bit 17 继续保持
`unsupported`。原目标 myron/Android 16 本轮未连接，结果记为 `not_observed`，不由 alioth 证据替代。
