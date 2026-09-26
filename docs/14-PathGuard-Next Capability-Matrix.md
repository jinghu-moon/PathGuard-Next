# PathGuard-Next 能力矩阵

本文定义当前开发期的设备能力边界。能力按执行域记录，不能把 Provider 结果描述为完整文件系统保护。

| 设备 / 内核 | deny | redirect | hide | hide backend | 状态 |
| --- | --- | --- | --- | --- | --- |
| `myron / Android 16 / 6.12` | Provider | Provider | supported_scope_myron | direct VFS LKM | 固定设备生产范围 |
| `alioth / Android 13 / 4.19` | Provider | Provider | unsupported | 无 | 当前设备不具备 Hide 后端 |

固定优先级为：`deny > hide > redirect > passthrough`。其中 deny/redirect 是 Provider 执行域结果，hide 是固定设备 direct-VFS 执行域结果；三者不能互相扩大覆盖范围。规则编译器会拒绝同一应用、同一选择器、同一 priority 下目标不同的 redirect 冲突，确定性组合按上述优先级处理。

Hide 包必须携带与设备绑定的 `device`、`fingerprint`、`kernel_release`、`kmi` 和 `module_sha256`。最终产品状态为 `supported_scope_myron`，表示仅 Redmi K90 Pro Max（myron）支持，不表示通用 Hide。任何身份不匹配都保持 fail-closed；不得通过切换 profile 把 alioth 声明为 Hide active。

ZIP 内的 `build-manifest.json` 是产物的机器可读身份记录，至少包含：

`device`、`fingerprint`、`kernel_release`、`kmi`、`toolchain`、`module_sha256`、`daemon_sha256`、`supported_backends`。
