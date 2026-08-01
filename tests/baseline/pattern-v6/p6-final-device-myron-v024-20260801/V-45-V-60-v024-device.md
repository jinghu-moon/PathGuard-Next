# V-45/V-48/V-60 myron 设备补充验证

- Change ID: `p6-final-device-myron-v024-20260801`
- Before commit: `1fcb35ba327dd7e24e42286ad1ecf734326d07bd`
- After commit: `working_tree`
- Status: `partial device observed`

## 环境与输入

设备为 Xiaomi myron，Android 16/API 36，kernel
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，SukiSU Ultra，SELinux
Enforcing，arm64-only。LocalSend 为 1.17.0/versionCode 583/UID 10358。

规则 SHA-256 为
`dea93c4b7a3fff4853f552f76ec581ebc3aa674bb70ffdd0452b00d6fe869419`，policy SHA-256
为 `e2b2e771b4daccedceb49b2e6064f9c833b44ae942a69832352cc3baf6aef9af`。
`0.1.24-dev` ZIP 及双 ABI Zygisk、daemon、CLI 共七项 Host/设备 SHA-256 逐项一致，完整值见
`V-45-V-60-v024-device-results.json`。

同一源码的 MSVC Release CTest 与 Clang UBSan/static-runtime CTest 均为 `78/78`；NDK r27d
arm64-v8a/armeabi-v7a、Zygisk `APP_STL=none`、ELF isolation、production integration guard、
comparison report guard 和 `git diff --check` 均通过。

## Provider 与实际文件结果

ExternalStorageProvider 和 MediaProvider 均发布独立 `pathguard.runtime_status.v2`：

| 字段 | ExternalStorageProvider | MediaProvider |
| --- | ---: | ---: |
| enforcement | active | active |
| observed capabilities | 65536 | 65536 |
| action count/total | 2/2 | 2/2 |
| active admission | 2 | 2 |
| missing capability/operation | 0/0 | 0/0 |

LocalSend 状态为 `active/complete/fd_pinned/backend=1/error=0`。其 `action_total=0` 是正确的域隔离：
当前 policy 只有 mount 与 Provider action，没有 app-path action；两条逐 action admission 归属于两个
Provider 状态，不能复制进 LocalSend 状态伪装为 app-path admission。

用户接收的 `test.txt` 和 `test.jpg` 仅存在于 `Download/localsend-redirect`，source 与 Pictures
同名文件均不存在。SHA-256 分别为：

- TXT: `49c7f5a5d08115218ace4b9da465b8c2a00886c39fc68578c634aeb811d6d35a`
- JPG: `f2b5bb6a1851eb6db3fbd610df6f0d73610642739c7de3c6fc1b37cfeb205357`

## 状态保留与冷启动

`0.1.23-dev` 在 10 次冷启动和 Provider 重启后累积 26 个 PID 状态文件，确认原实现会保留死亡
PID 历史。`0.1.24-dev` 每次成功发布后仅删除 `/proc/<pid>` 已不存在的状态，不删除任何存活 PID
记录。设备结果为：

| 检查点 | `.status` 文件数 |
| --- | ---: |
| v0.1.23 修复前 | 26 |
| v0.1.24 重启后 | 3 |
| 10 个 LocalSend PID 轮换后 | 3 |
| 停止 app 并重启两个 Provider 后 | 2 |
| 再次启动 app 后 | 3 |

10 次冷启动全部为 `COLD`，10/10 回收上一 PID，10/10 为
`active/complete/fd_pinned/backend=1`，三条规则挂载每轮各一条。TotalTime
P50/P95/max 为 152/201/201 ms；RSS P50/P95/max 为 333664/345392/345392 KiB。
逐轮原始数据已受版本控制归档。

## Provider 重启窗口

双 Provider 重启结果：ExternalStorageProvider `14514 -> 24254`，MediaProvider
`10781 -> 23880`。两个新 PID 均重新发布两条 active admission。

在 Android 重建共享存储服务后立即启动 LocalSend，PID 22939 明确报告
`failed_preflight/preflight_failed/ENOENT(2)`，没有误报 active。存储服务稳定后重试，PID 22940
恢复 `active/complete/fd_pinned`，三条挂载全部存在。这是可观测 fail-open 恢复窗口；本报告不把
首次失败隐藏为通过，也不把一次稳定重试外推为零窗口承诺。

最终 force-stop 后 LocalSend PID 消失，Nagram、Screenshots、redirect source 三类全局 mountinfo
残留均为 0；两个 Provider 继续存活。Provider 状态提交失败、post-specialize errno 13 和崩溃标记
均为 0。

## 结论边界

本批在 myron 上完成 V-45 active 状态与真实文件结果对比、V-48 Provider restart 恢复，以及
V-60 的 v0.1.24 十轮冷启动补充。inactive/unsupported/collision/ambiguous/overflow、第二 ROM/root
framework/kernel tier、arm32 实际执行和 fanotify 相关场景仍未覆盖，不据此关闭完整设备矩阵。
