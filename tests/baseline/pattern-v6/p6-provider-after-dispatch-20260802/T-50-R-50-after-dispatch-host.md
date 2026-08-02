# T-50～R-50 Provider before/after dispatch lifecycle

## 范围

- 为需要观察原始返回值的 query/insert/document-ID 操作增加 after-dispatch 阶段。
- before 可安全短路；未短路时调用 LSPlant backup；after 仅在 backup 正常返回后运行。
- native after 入口当前只重建并校验 immutable request，固定返回 pass-through。

## 实现

- `Dispatcher.afterDispatch` 提供默认 pass-through，保持现有 lambda/dispatcher 兼容。
- `ProviderHooker.callback` 冻结参数数组引用，依次执行 before、backup、after；before/after
  异常和不兼容返回值均 fail-open。
- 原方法抛出的异常继续解包并传播，不运行 after，保持 Android Provider 原始语义。
- 新增 `nativeAfterDispatch` JNI 入口和注册描述符；native before/after 共用
  `ReadNativeDispatchRequest`，避免复制 URI/document ID/open mode/ContentValues 提取逻辑。

## 官方依据

- Android `ContentProvider` 的 query/insert/openFile/update/delete 返回类型和调用边界各自独立：
  <https://developer.android.com/reference/android/content/ContentProvider>
- Android `DocumentsProvider` 的 document ID 一经返回必须稳定；create/rename 可产生需要后处理的
  外部标识：<https://developer.android.com/reference/android/provider/DocumentsProvider>

## 验证

- ProviderHooker Java Host：before rewrite/pass/failure、after rewrite/incompatible/failure 通过。
- MSVC Release CTest：`82/82` 通过。
- Provider mapping/LSPlant/production/comparison guards：通过。
- NDK r27d Zygisk 双 ABI、NDK 29 LSPlant 双 ABI 和 ELF guard：通过。
- `git diff --check`：通过。
- Candidate：`dist/pathguard-next-v0.1.38-dev-universal.zip`，module version
  `0.1.38-dev`、versionCode `39`，SHA-256
  `0be0d606279489ba3940ed39e0f75c23ab6b7d2d5ede8159a153f7653e31c9ee`。

## 边界

after-dispatch 当前不读取或改写 `Cursor`、`Uri`、`File`、document ID、
`ParcelFileDescriptor` 或 count。真实结果 adapter、route binding/provenance resolver 和 bit 17
仍未启用；本任务只建立正确且 fail-open 的调用生命周期。

## 设备结果

- Evidence：`build/device-evidence/provider-lsplant-v1/20260802-142638`。
- Module：`0.1.38-dev`，versionCode `39`。
- MediaProvider：`2044/2044/2044/2044`，`provider_bridge_errno=0`。
- ExternalStorageProvider：`3/3/3/3`，`provider_bridge_errno=0`。
- 两端 `action_total=2`、admission active、`observed_capabilities=65536`，bit 17 清零。
- 未发现 Provider fatal、PathGuard dispatcher failure、null receiver 或 JNI local-reference
  异常。
- SAF trigger 中 UID 2000 的 `SecurityException` 是系统公开权限模型拒绝未授权 shell query；
  logcat 中其他 Class/Method 异常来自 MIUI ThemeManager/DeviceUtils，不属于目标 Provider 进程。

该结果证明新增 `nativeAfterDispatch` 注册和 before/backup/after passthrough 在当前设备稳定，
不证明真实 result rewrite 已启用。
