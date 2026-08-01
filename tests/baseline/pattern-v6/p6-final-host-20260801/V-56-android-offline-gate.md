# V-56 Android NDK、ABI、ELF 与模块离线门

- Change ID: `p6-final-host-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- NDK: r27d `27.3.13750724`
- API: 31
- Status: `complete`（离线门；设备 parity 不属于本任务的离线范围）

## 构建与隔离

`scripts/build-native.ps1` 默认 production 配置完成：

- arm64-v8a、armeabi-v7a 的 `pathguardd`、`pathguardctl` 和全部 release probes 构建通过；
- 两个 ABI 的 `pathguard_zygisk` 以 `APP_STL=none` 强制重建并通过 ELF isolation guard；
- arm64 Zygisk 为 ELF64/AArch64，armeabi-v7a 为 ELF32/ARM，均为 DYN，均无 PT_TLS；
- dynamic symbol/link map/strings 扫描无 rules parser/compiler、toml、Rust、Binder identity ABI 泄漏；
- production 调用未传任何 `PATHGUARD_TEST_*` 参数，模块二进制 strings/旧符号扫描无测试注入和
  policy v5/format 1 artifact。

首次双 ABI 执行在 armeabi-v7a `pathguardctl` install 阶段遇到 Windows 临时文件锁；无构建进程
残留后重放成功，最终双 ABI production build exit 0。

## 模块内容 SHA-256

| Artifact | SHA-256 |
| --- | --- |
| `bin/arm64-v8a/pathguardd` | `E0B8E60FDA347A0DFA50F68E39EA147CF0CFFF5E2284461C83C8F41CB4E3541F` |
| `bin/arm64-v8a/pathguardctl` | `8E84E08AA87712D17FC52F3CC6B9B66C4907322C51968971C5600490A0B7D61F` |
| `bin/armeabi-v7a/pathguardd` | `DF80753E22BDCF762D5BBAE58773831D9A951B5AED15451C665851552A843437` |
| `bin/armeabi-v7a/pathguardctl` | `B8CF4A3B814F088657B93FA6470E19E28B9F47E03F41E216926CBC0270E13695` |
| `zygisk/arm64-v8a.so` | `5BF73F48FCA8F1DED18418118AC8210811730ACEDDC1C94C842AF741065B60C2` |
| `zygisk/armeabi-v7a.so` | `0117BDD1B4F2D5B05414A05DD8B49E23DDCC4F6C757CD861E1AD7780B29E6F59` |

Host/Android runtime parity 的设备执行需要恰好一台 ADB 设备，故未传 `HostParityProbe`；共享
format/capability/static layout 继续由同源头文件、NDK compile-time assertions 与 Host guards
约束。V-57/V-58 承担实际设备 parity 和操作矩阵。
