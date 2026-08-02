# T-51～R-51 Provider result-kind observation

## 范围

- 冻结 Provider 原始返回值的种类和兼容性状态，不解释对象内容。
- 在 `nativeAfterDispatch` 内验证实际对象形状，验证后仍固定 pass-through。
- 覆盖 `File`、`String` document ID、`Cursor`、`Uri`、
  `ParcelFileDescriptor`、boxed count 和 `void`。

## 合同

- 引用结果：非空值必须是指定 Java 类型；null 记为 compatible-null。
- count：反射调用把 primitive `int` 装箱为非空 `Integer`；null 或其他类型不兼容。
- void：反射调用正常完成后返回 null；任何非空值不兼容。
- 未知 result kind、类型不兼容或 JNI 检查异常均 fail-open；不替换原结果。
- 观察器不读取 `Cursor`、`Uri`、`File`、document ID、PFD 或 count 内容，不持有 JNI ref。

## 官方依据

访问日期：2026-08-02。

- Android `ContentProvider` API 分别声明 query/insert/openFile/update/delete 的返回类型，并
  明确数据访问方法可能由多个线程并发调用：
  <https://developer.android.com/reference/android/content/ContentProvider>
- Android `DocumentsProvider` API 分别声明 query/open/delete 等文档操作的返回类型和稳定
  document ID 合同：
  <https://developer.android.com/reference/android/provider/DocumentsProvider>
- Android `Method.invoke` 明确 primitive 返回值会装箱，`void` 返回 null：
  <https://developer.android.com/reference/java/lang/reflect/Method#invoke(java.lang.Object,%20java.lang.Object...)>

## 实现

- `ProviderJavaResultObservationV1` 用一个无分配 constexpr 状态机表达 compatible-value、
  compatible-null、type-mismatch 和 invalid-kind。
- `ObserveNativeProviderResult` 只通过 `IsInstanceOf` 检查静态合同中的平台类。
- `NativeAfterDispatch` 在 request 重建前验证结果形状；失败时记录 method/kind/state，随后
  返回 pass-through。
- 结果观察与后续 result adapter 分离，未增加锁、daemon/store I/O 或全局 JNI ref。

## 验证

- Provider result Host 矩阵：5 类引用的 value/null/mismatch、count、void、unknown kind 通过。
- ProviderHooker Java Host：before/after、primitive、nullable reference 和 void 通过。
- MSVC Release CTest：`82/82` 通过。
- production integration guard：通过。
- NDK r27d Zygisk：`arm64-v8a`、`armeabi-v7a` 通过，ELF isolation 通过。
- NDK 29 LSPlant：`arm64-v8a`、`armeabi-v7a` 通过，ELF export guard 通过。
- `git diff --check`：通过。

## 设备结果

- Candidate：`dist/pathguard-next-v0.1.39-dev-universal.zip`，SHA-256
  `78349fbaddb2939108adc01bf6ef577028732f6d82cbd8394dbdae1fe0cab8ed`。
- Evidence：`build/device-evidence/provider-lsplant-v1/20260802-145017`。
- Module：`0.1.39-dev`，versionCode `40`。
- MediaProvider：`provider_bridge_resolved_methods=2044`、installed/backup/self-tested 均为
  `2044`，`provider_bridge_errno=0`。
- ExternalStorageProvider：`provider_bridge_resolved_methods=3`、installed/backup/self-tested
  均为 `3`，`provider_bridge_errno=0`。
- 两端 `action_total=2`，两条 admission 均为 `active`，`observed_capabilities=65536`，bit 17
  清零；`logcat.txt` 存在且为空，未发现 Provider fatal、dispatcher failure、null receiver、
  JNI local-reference 或 result observation rejection。
- SAF trigger 的两个 `SecurityException` 仍是 UID 2000 未经授权直接 query DocumentsProvider 的
  系统权限拒绝，不属于 PathGuard 或 Provider Hooker 故障。

## 边界

本任务不构造替代对象，不读取结果内容，不连接真实 route/provenance resolver，不设置 bit 17。
真实 query/insert/reverse rewrite 和 V-65 完整矩阵仍为 `unsupported/not_observed`。
