# T-36～R-36 LSPlant production wiring Host 证据

## 范围

- Change scope: 方案 B 的 Java method Hook bridge 与 Zygisk lifecycle wiring
- Device behavior: `0.1.27-dev` 已观察到 post-specialize 时 Provider APK ClassLoader 尚未创建；
  `0.1.28-dev` 已安装延迟 hook，但 passthrough 调用仍失败
- Production bit 17: 保持 `unsupported`

## 合同与实现

- 冻结 alioth 两个 DocumentsProvider 方法与九个 MediaProvider/MediaDocumentsProvider 方法；
- 独立 `libpathguard_lsplant.so` 只导出版本化 initialize/install C ABI；
- Zygisk 先校验已归档 Provider APK SHA-256，未知 artifact 不加载 bridge；
- pre-specialize 初始化 LSPlant，post-specialize 加载 Hooker DEX 并成组安装 passthrough Hook；
- Hooker DEX loader 与 Provider target loader 分离，resolve 按 target/context/system/parent
  loader 去重尝试，并输出目标方法、loader 与 Java 异常；
- `post-specialize` 仅调度 bridge worker；worker 等待
  `ActivityThread.currentApplication().getClassLoader()`，完成后异步发布 runtime status，
  避免在 Zygote specialize 调用栈阻塞 Application 创建。
- 任一 resolve/hook/backup/`IsHooked` 失败会成组 unhook；
- runtime status 输出 profile、library、init、DEX 和四组 method mask；
- passthrough bridge 不修改 URI/document ID/route，不设置 bit 17。

## 依赖冻结

- LSPlant `84256d4cb51abd79280da5c29437fb7004391667`
- DexBuilder `ac7fb2230954ee311808bad469b0db501f31bfb8`
- parallel-hashmap `0cd57d29a959256ed66b2afdd1009928fc625d09`
- xDL `1e0b6254165a2ddcbd32f77a371700c69155acf8`
- Dobby Maven release `1.2`, AAR SHA-256
  `251f48ae21686d7f69276c50644ca345f450e45110057437f7d76bb14cddf3a1`

## Host/Android 结果

- MSVC Release CTest: `81/81`
- NDK r27d Zygisk: `arm64-v8a`、`armeabi-v7a` build + ELF isolation 通过
- NDK 29 LSPlant bridge: `arm64-v8a`、`armeabi-v7a` build + ELF guard 通过
- bridge 无 `libc++_shared.so`，无 `PT_TLS`，16 KiB max page，导出符号仅为版本化 C ABI
- `0.1.26-dev` 真机 before：两个 Provider 均通过 profile/init/DEX gate，但
  `resolved_methods=0`、`provider_bridge_errno=2`；证据：
  `build/device-evidence/provider-lsplant-v1/20260802-014120`
- `0.1.27-dev` 真机 before：新增日志确认两个 Provider 均在
  `Zygote.nativeSpecializeAppProcess` 中，所有候选 loader 对目标类均为
  `ClassNotFoundException`；证据：
  `build/device-evidence/provider-lsplant-v1/20260802-014742`
- 采集脚本已主动触发 MediaProvider 与 ExternalStorageProvider，并轮询两份状态；
  `0.1.26-dev` 重放确认两个进程均可稳定采集。
- 修复候选 ZIP: `pathguard-next-v0.1.27-dev-universal.zip`
- 修复候选 ZIP SHA-256:
  `97d0889136a6a5ce9862b5ff39f7c524f35816b6421e483abf1e077971e9ab5b`
- 延迟安装候选 ZIP: `pathguard-next-v0.1.28-dev-universal.zip`
- 延迟安装候选 ZIP SHA-256:
  `96503efe95da0bd2160c8a8e9cf880ae24aa3a09dc27a19eda60ccbd59832795`
- `0.1.28-dev` alioth 真机复测曾通过旧采集脚本的安装状态断言；证据：
  `build/device-evidence/provider-lsplant-v1/20260802-085431`
  - ExternalStorageProvider：`resolved=3 installed=3 backup=3 self_tested=3 errno=0`
  - MediaProvider：`resolved=2044 installed=2044 backup=2044 self_tested=2044 errno=0`
  - 两份 runtime status 均为 `build_matched=true`、`library_loaded=true`、
    `lsplant_initialized=true`、`hooker_dex_loaded=true`，`observed_capabilities`
    不包含 bit 17。
  - 完整 logcat 复核发现该结果是假阳性：ExternalStorageProvider 的
    `getDocIdForFile` passthrough 因 `null receiver` 发生 `FATAL EXCEPTION` 并退出，MediaProvider
    `query` 也返回同类异常；同时存在把 LSPlant global backup ref 传给 `DeleteLocalRef` 的 JNI
    告警。原因是 callback 从已被 LSPlant 改写的 backup Method 推断 static/instance 属性。
  - `0.1.29-dev` 在 hook 前冻结目标 Method 属性、修正 backup ref 生命周期，并让采集脚本对
    Provider fatal、`null receiver` 和非法 `DeleteLocalRef` 直接失败；待真机复测。
- 增强后的采集脚本在设备仍运行 `0.1.28-dev` 时按预期红测，命中 MediaProvider
  `null receiver -> ProviderHooker.callback`；证据：
  `build/device-evidence/provider-lsplant-v1/20260802-090813`
- `0.1.29-dev` Host/离线结果：MSVC Release 81/81、NDK r27d Zygisk 双 ABI、NDK 29
  LSPlant bridge 双 ABI、DEX、ELF 和 production integration guard 全部通过。
- 修复候选 ZIP: `pathguard-next-v0.1.29-dev-universal.zip`
- 修复候选 ZIP SHA-256:
  `23638ed227a2ac32050bab31e43acde01b5cfa4c6e0927ea1e9870b9efacb543`
- `0.1.29-dev` alioth 真机复测通过增强采集脚本；证据：
  `build/device-evidence/provider-lsplant-v1/20260802-092822`
  - ExternalStorageProvider：`resolved=3 installed=3 backup=3 self_tested=3 errno=0`
  - MediaProvider：`resolved=2044 installed=2044 backup=2044 self_tested=2044 errno=0`
  - 两端 runtime status 均为 `build_matched=true`、`library_loaded=true`、
    `lsplant_initialized=true`、`hooker_dex_loaded=true`，`observed_capabilities=65536`，
    未启用 bit 17。
  - 本轮 logcat 出现两个 Provider 的 deferred hook active/result，未出现
    `FATAL EXCEPTION`、`null receiver`、`JNI WARNING: DeleteLocalRef` 或 Provider 进程崩溃。

## 剩余边界

V-65 的 profile gate、LSPlant 初始化、DEX 加载和两个 Provider 的 11 方法成组 passthrough
已在 `0.1.29-dev` 真机证据中观察到，且增强 runtime fault gate 通过。失败回滚真机注入及
URI/document ID/route/FD/reverse 的真实数据改写仍未验证。因此完整 V-65 继续保持 pending，
方案 B 的虚拟映射能力和 bit 17 继续保持 `unsupported`。
