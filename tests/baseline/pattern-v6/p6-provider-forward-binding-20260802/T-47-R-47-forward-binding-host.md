# T-47～R-47 Provider forward binding validation

## 范围

- 修正 `EvaluateProviderMapping` 的 binding validation 边界。
- 前向 Provider 操作只要求 context、source/target path 和外部标识事实完整。
- reverse lookup 继续要求 strong FD identity、unique committed reverse record、RuleId/scope/
  generation 一致。

## 实现

- 新增 operation-specific validator，避免 query/create/open/metadata/rename/delete 因缺少
  committed reverse record 被错误 fail-open。
- 保留 reverse lookup 对 `ResolveReverse` 的 missing、ambiguous、error 和 record mismatch 分类。
- `DispatchProviderRequest` 仍未注入真实 resolver/binding，当前设备行为继续 pass-through；bit 17
  不启用。

## 验证

- `pathguard_provider_mapping_test` 覆盖 query、directory query、create、open read/write/
  read-write、metadata、rename、delete file/directory 的无 reverse 正向矩阵。
- reverse lookup missing/ambiguous/mismatch/error 负矩阵保持原分类。
- MSVC Release CTest：`82/82` 通过。
- production integration guard：通过。
- NDK r27d 主模块双 ABI：`arm64-v8a`、`armeabi-v7a` 通过。
- NDK 29 LSPlant bridge 双 ABI：`arm64-v8a`、`armeabi-v7a` 通过。
- `git diff --check`：通过。

## 设备结果

- Candidate：`dist/pathguard-next-v0.1.36-dev-universal.zip`
- module version `0.1.36-dev`，versionCode `37`
- SHA-256 `0883eee7b3199600e7efd50a5980713d9dcdd117f9c3059c8803cfe3596c0286`
- Passthrough evidence：`build/device-evidence/provider-lsplant-v1/20260802-124319`
- Restart evidence：`build/device-evidence/provider-lsplant-v1/20260802-124446`

`20260802-124319` 确认 MediaProvider `2044/2044/2044/2044`、ExternalStorageProvider
`3/3/3/3`，两端 `provider_bridge_errno=0`、两条 admission active、bit 17 清零。

`20260802-124446` 确认 `0.1.36-dev` Provider restart/republication：ExternalStorageProvider
`7611 -> 11434`，MediaProvider `5085 -> 11302`，hook group 重新安装完整，状态重新发布，bit 17
仍清零。

MediaProvider early-attach 日志中的 `Volume external_primary not found` 来自原始 Provider query
路径和其他 LSPosed hook 的透传异常，不是 PathGuard dispatcher failure；无 fatal、null receiver
或 JNI local-reference diagnostics。

## 剩余边界

真实 route resolver/provenance binding 尚未注入 Provider callback，query/create/open/reverse 的
Java 返回对象改写仍未启用。V-65/V-66 保持 current-device partial，真实 mapping 子矩阵继续
`unsupported/not_observed`。
