# V-01 工作区与工具链基线

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-01` |
| 记录时间 | 2026-07-29，Asia/Shanghai |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| After commit | N/A（只记录环境） |
| Branch | `feature/pattern-redirect-v6` |
| Remote | `https://github.com/jinghu-moon/PathGuard-Next.git` |
| Classification | `unchanged` |
| Reviewer conclusion | V-01 环境前置条件满足，可以进入 V-02 |

## 工作区边界

采集时存在一项非本任务产生的用户改动：`.gitignore` 增加 `/build-release/`。
本批次保持该改动不变，不把它纳入 Pattern v6 实现，也不清理已有 ignored build artifacts。

基线生成目录固定为：

```text
build/pattern-v6-v02-release/
```

该目录受仓库 `/build/` ignore 规则保护。不得复用现有 `build-release/`，避免历史缓存污染
V-02 的干净构建证据。

## Host 工具链

| 工具 | 版本/位置 |
| --- | --- |
| OS | Windows 10.0.26100 |
| Git | 2.47.1.windows.2 |
| CMake | 4.0.2 |
| Visual Studio | Visual Studio 2022 Community |
| MSVC tools | 14.44.35207 |
| Ninja | 1.13.2（仅可选；V-02 使用 VS 生成器） |
| Java | Temurin OpenJDK 21.0.10 LTS |
| Android SDK | `C:/Users/seeyuer/AppData/Local/Android/Sdk` |
| ADB | 1.0.41 / platform-tools 36.0.2-14143358 |
| Android NDK | r27d / 27.3.13750724 |
| Native API | android-31 |
| C++ mode | C++20、no exceptions、no RTTI |

`cl.exe` 不在普通 PowerShell PATH；CMake 的 Visual Studio 生成器负责载入 MSVC 环境。

## Android 设备

| 字段 | 值 |
| --- | --- |
| ADB serial | `dc39c31d` |
| Product/device | M2012K11AC / `alioth` |
| Android/API | 13 / 33 |
| Build fingerprint | `Redmi/alioth/alioth:13/TKQ1.220829.002/V14.0.8.0.TKHCNXM:user/release-keys` |
| Kernel | Linux 4.19.157-perf-g9607d8651312, aarch64 |
| ABI list | `arm64-v8a,armeabi-v7a,armeabi` |
| SELinux | Enforcing |
| Root framework | Magisk 30.6, version code 30600 |
| Root context | `u:r:magisk:s0` |
| Boot ID | `99f966e8-bebb-48f3-a2ba-9ee80d2eba1a` |

设备未发现 `ksud`；因此本设备只能作为 Magisk/alioth tier 的证据，不能代表 KernelSU 或
跨 ROM 兼容矩阵。

## 固定命令

V-02 使用新的 VS Release 构建目录：

```powershell
cmake -S . -B "build/pattern-v6-v02-release" -G "Visual Studio 17 2022" -A x64 -DPATHGUARD_BUILD_TESTS=ON
cmake --build "build/pattern-v6-v02-release" --config Release --parallel 2
ctest --test-dir "build/pattern-v6-v02-release" -C Release --output-on-failure --output-junit "v02-ctest.xml"
```

Android production build门使用项目脚本，不在 V-02 执行：

```powershell
./scripts/build-native.ps1 -Abi arm64-v8a,armeabi-v7a
```

## 验收结论

- CMake、MSVC、NDK、ADB 和目标设备均可用；
- Host 和 Android ABI 前置条件明确；
- 干净构建目录和固定命令已冻结；
- 用户已有改动和生成物边界已记录；
- V-01 判定 `complete`。
