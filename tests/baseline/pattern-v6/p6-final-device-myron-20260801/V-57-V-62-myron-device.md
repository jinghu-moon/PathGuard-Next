# V-57～V-62 myron 真机报告

## 报告信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-final-device-myron-20260801` |
| Before | `0.1.18-dev`，基于 `1fcb35ba327dd7e24e42286ad1ecf734326d07bd` 的 working tree 构建 |
| After | `0.1.19-dev`，versionCode 20，working tree |
| 设备 | Xiaomi 25102RKBEC (`myron`)，Android 16/API 36，arm64-v8a only |
| Kernel/root | `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`；SukiSU Ultra/KernelSU `4.1.2-2-gf39c001e` |
| SELinux | Enforcing |
| LocalSend | 1.17.0，UID 10358 |
| Rules SHA-256 | `dea93c4b7a3fff4853f552f76ec581ebc3aa674bb70ffdd0452b00d6fe869419` |
| Device policy SHA-256 | `e2b2e771b4daccedceb49b2e6064f9c833b44ae942a69832352cc3baf6aef9af` |
| Module ZIP SHA-256 | `2142fd4022f890f832d2b7d3b8e0f50089b1b359c43ee9392dbca5a02198b51c` |
| Installed Zygisk SHA-256 | `ce1423947cbfeed572e54b6b3a3e4fc295f1876a067637f52da78ba7eb2d8950` |
| Classification | `planned_break` |

机器可验证入口为同目录 `V-57-V-62-myron-device.json`。设备与 artifact 完整快照位于
`build/device-evidence/p6-final-device-myron-v019-20260801/device-snapshot.json`。

## Before 与修复边界

`0.1.18-dev` 的 TXT/JPG upload 均返回 HTTP 500，响应为
`Could not save file. Check receiving device for more information.`。交互诊断同时观察到
ExternalStorageProvider 与 MediaProvider 在 hook 后触发 syscall 437 (`openat2`) 并被 Provider
seccomp 以 `SIGSYS/SYS_SECCOMP` 终止。模块重启后旧 crash buffer 已不可恢复，因此受控 before
原始证据只宣称两份 HTTP 500 响应，不把交互日志伪装成仍可读取的文件。

`0.1.19-dev` 在 Provider hook 安装阶段固定选择已有的逐组件
`openat(O_PATH|O_NOFOLLOW)` FD resolver；daemon 的独立 capability probe 不变。

## C1～C6 单设备结果

| 范围 | 本机结果 | 状态 |
| --- | --- | --- |
| C1 deny | 复制 LocalSend UID/GID/Groups 后枚举 `Pictures/Nagram` 与 `DCIM/Screenshots`，均 rc=1、`Permission denied`；挂载点自身仍可见为 mode 000 | observed |
| C2 app redirect | 从 LocalSend namespace 的 `Download/localsend-source` 创建唯一文件，物理文件只出现在 `Download/localsend-redirect` | observed |
| C3 Provider | 同一 LocalSend v2 会话上传 TXT/JPG 均 HTTP 200；ExternalStorageProvider 与 MediaProvider 路由日志覆盖 `access/open/stat64` | observed |
| C4 多源 | TXT 从 Download source、JPG 从 Pictures source 写入同一 redirect target，不同名均成功 | observed |
| C5 isolation/lifecycle | force-stop 后目标 PID 和 `/proc/<pid>` 消失，全局 namespace 规则挂载数为 0；诊断 status 快照保留 | observed |
| C6 Glob v1 | Provider 的 `Download/localsend-source/**` 与 `Pictures/**` 文件匹配由 TXT/JPG 实际写入覆盖 | partial observed |

同名碰撞、reverse provenance、query/insert、rename/link 多 operand 与故障 capability fail-open
没有在本设备批次执行，不能从上述结果外推。

## 50 次启动与 soak 后接收

50 轮均执行 `am force-stop` 后启动新 PID，并逐轮记录上一 PID 回收、runtime status、三条挂载和 RSS：

| 指标 | 结果 |
| --- | ---: |
| 功能断言 | 50/50 |
| `am LaunchState=COLD` | 48/50 |
| `am LaunchState=UNKNOWN` | 2/50 |
| TotalTime 样本 | 48 |
| TotalTime P50/P95/P99/max | 173/274/294/294 ms |
| RSS P50/P95/P99/max | 336784/343852/348808/348808 KiB |

第 5、13 轮的 `am` 没有提供 `TotalTime`，并返回 `LaunchState=UNKNOWN`；两轮均确认上一 PID
已回收，新 PID 的 runtime 为 `active/complete/fd_pinned`、backend 1，三条规则挂载各 1 条。
因此保留为 launcher 遥测异常，不将其计入 48 个 TotalTime 样本，也不误报为 PathGuard 失败。

随后执行 50 次保持进程存活的 home-to-foreground warm 操作。PID 8624 在 50/50 轮中保持
不变，runtime 与三条挂载全部通过；RSS P50/P95/P99/max 为
271692/274008/307200/307200 KiB（49 个有效样本；第 42 轮无可解析 TOTAL 行；首末差值
-33212 KiB）。Android 16 每轮均返回
`Activity not started, intent has been delivered to currently running top-most instance`、
`LaunchState=UNKNOWN` 和 `TotalTime=0`，因此该批没有有效 warm latency 样本，不生成虚假的
P50/P95/P99。

soak 后上传结果：

| 文件 | HTTP | 字节 | SHA-256 | 物理结果 |
| --- | ---: | ---: | --- | --- |
| `pg-myron-v019-post-soak.txt` | 200 | 131 | `31386cc1fa83bb750a0873b8b254ed5011b18f567e2158a40d6109ba88778246` | redirect 存在，source/Pictures 不存在 |
| `pg-myron-v019-post-soak.jpg` | 200 | 199241 | `8ff9d6fbd9416c3cfc2466135a8dcdec496e74952ba5b26bbc857dc22860f1ce` | redirect 存在，source/Pictures 不存在 |

ExternalStorageProvider PID 22082、MediaProvider PID 10327 在上传后均存活。crash buffer 对
`SIGSYS`、`SYS_SECCOMP`、`syscall 437` 和两个 Provider 进程名的匹配数为 0。

## V-57～V-62 边界

| Task | 本批结论 | 未覆盖项 |
| --- | --- | --- |
| V-57 | partial observed | 第二 ROM/root framework/kernel tier、arm32、完整 C1～C6 operation matrix |
| V-58 | partial observed | shared UID、identity clear、query/insert、Provider restart、FUSE unavailable |
| V-59 | partial observed | mount cancellation/rollback/owner-death/rollback-failure 子矩阵见 `V-59-myron-mount-fault-injection.json`；fanotify、Provider restart、policy/topology 等仍未观察 |
| V-60 | partial observed | 50 cold + 50 warm 功能循环与接收通过；warm latency、PathGuard 分阶段 timing/counters、Provider restart profile 和可选 Export 未观察 |
| V-61 | partial audited | 本报告无 `unexpected_regression`；最终结论仍依赖剩余设备报告 |
| V-62 | partial audited | 本设备证据路径闭环；完整 ROM/framework/operation 追踪未闭环 |

## Reviewer conclusion

Provider resolver 变更是修复 Android 16 seccomp 兼容性的计划内行为变化。本设备已执行范围内没有
残留 unexpected regression，0.1.19 的 Provider 接收、app mount、deny、生命周期与短时 soak
均符合预期。该结果只代表一台 arm64 Xiaomi Android 16/SukiSU Ultra 设备，不满足 V-57～V-62
冻结的完整矩阵，相关任务继续保持 partial/pending。
