# T-46～R-46 Provider mapping decision adapter seam

## 范围

- 将已验证的 Java dispatch request 映射为既有 `ProviderMappingOperation`。
- 通过 `BuildProviderMappingRequest` 和 `EvaluateProviderMapping` 复用现有 profile、route
  binding 与 provenance 决策合同。
- 本阶段不注入 resolver/binding，不启用真实 Provider rewrite，保持 bit 17 清零。

## 合同与实现

- query、directory query、create、open read/write/read-write、metadata、rename、delete
  file/directory 与 reverse lookup 均使用集中 operation 映射。
- 未知 request version/operation、identifier 与 binding 不一致、缺 profile/binding/runtime/
  reverse 时不产生改写结果；决策层继续按既有 `pass`、`unsupported`、`ambiguous` 或
  `fail-open` 分类。
- `DispatchProviderRequest` 只构造按值 request 并调用既有 evaluator；不执行阻塞 I/O、不访问
  daemon/store、不创建 Java 返回对象。
- delete target 未解析时保持 incomplete，不从 URI、MIME、display name 或返回数量推断
  unlink/rmdir。

## 验证

- `pathguard_provider_mapping_test`：通过。
- LSPlant bridge 专项测试：通过。
- MSVC Release CTest：`82/82` 通过。
- production integration guard：通过。
- NDK 29 双 ABI：`arm64-v8a`、`armeabi-v7a` 通过。
- `git diff --check`：通过。

## 设备边界

`build/device-evidence/provider-lsplant-v1/20260802-122500` 中的 `0.1.35-dev` 回归确认：

- MediaProvider：`2044/2044/2044/2044`（resolved/installed/backup/self-tested）。
- ExternalStorageProvider：`3/3/3/3`（resolved/installed/backup/self-tested）。
- 两端 `action_total=2`、admission 均为 `active`、`provider_bridge_errno=0`。
- 两端 `observed_capabilities=65536`，Provider mapping capability bit 17 仍清零。
- 未发现 PathGuard JNI/LSPlant 错误。

该结果只证明 mapping adapter 接入没有破坏 LSPlant passthrough；当前设备尚未注入真实 route
resolver/provenance binding，因此不宣称真实 mapping rewrite 已启用。

下一步是为具备可信 binding/resolver 输入的运行时接线；在此之前继续保持 fail-open。

## 新候选包

- `dist/pathguard-next-v0.1.35-dev-universal.zip`
- module version `0.1.35-dev`，versionCode `36`
- SHA-256 `5c90b52db9b2512e200bb2a406d3ee6ec744242882570f905e0d902a711565b9`
- 已传送至 `/sdcard/Download/pathguard-next-v0.1.35-dev-universal.zip` 并完成 Magisk 安装。
- 设备复采证据：`build/device-evidence/provider-lsplant-v1/20260802-122500`。
