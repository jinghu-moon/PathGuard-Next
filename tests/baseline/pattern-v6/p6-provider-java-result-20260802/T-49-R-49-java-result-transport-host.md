# T-49～R-49 Provider Java result transport

## 范围

- 保证 LSPlant native dispatcher 的返回值不会被 Java Hooker 丢弃。
- 只接受同一 Hooker 类加载器下的 `ProviderHooker.DispatchResult`；其他对象和 null 均保持
  pass-through。
- 本阶段不构造真实 File/Uri/Cursor/ParcelFileDescriptor/Count 返回对象。

## 实现

- `installNativeDispatcher()` 保存 `nativeDispatch()` 返回值。
- 返回值是 `DispatchResult` 时直接交给既有 callback compatibility 检查；否则转换为
  `DispatchResult.pass()`。
- Java callback 的 backup invocation、异常传播和返回类型校验逻辑不变。

## 验证

- `run-provider-hooker-dispatcher-host-test.ps1`：通过。
- `pathguard_production_integration_guard`：通过。
- `pathguard_comparison_report_guard`：通过。
- `git diff --check`：通过。
- Candidate：`dist/pathguard-next-v0.1.37-dev-universal.zip`，versionCode `38`，SHA-256
  `5595160c8e83bb1b58b594675798dc9ef6009843e654ae047b9ddc90269e8437`。

## 边界

native dispatcher 当前仍返回 null/pass-through；真实 resolver、Java 结果对象构造、FD 与
provenance 一致性以及 bit 17 继续保持关闭。当前设备既有 passthrough/restart 证据不因本改动
升级为真实 mapping 证据。

`0.1.37-dev` 真机回归 `build/device-evidence/provider-lsplant-v1/20260802-141719` 通过：双
Provider hook group 完整，`provider_bridge_errno=0`，两条 admission active，bit 17 清零，
未捕获 Provider fatal/JNI 异常。该结果证明 result transport 未破坏 passthrough，不证明真实
result rewrite 已启用。
