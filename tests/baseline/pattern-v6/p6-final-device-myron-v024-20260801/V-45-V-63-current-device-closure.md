# V-45～V-63 当前设备范围闭环

- Change ID: `p6-final-device-myron-v024-20260801-current-device-closure`
- Device: Xiaomi myron / Android 16 / kernel 6.12.23 / SukiSU Ultra / SELinux Enforcing / arm64-v8a
- Module: PathGuard Next `0.1.24-dev`
- Status: `complete`（available-device scope）
- Machine entry: `V-45-V-63-current-device-closure.json`

## 生产 smoke

补充检查使用已安装的 0.1.24 production 模块，LocalSend 一次冷启动结果为：

| 检查 | 结果 |
| --- | --- |
| LaunchState / TotalTime | `COLD` / 199 ms |
| runtime | `active/complete/fd_pinned`, backend=1 |
| 规则挂载 | Nagram=1、Screenshots=1、redirect=1 |

最终 `force-stop` 后 LocalSend PID 消失；全进程 mountinfo 扫描中 Nagram、Screenshots、redirect
三类残留均为 0。

## 已完成但此前未更新清单的证据

- policy race：5/5 `policy_changed/EAGAIN`，零挂载；
- invalid policy reload：旧 generation、hash 和 active 状态均保留，恢复后 hash 一致；
- topology race：5/5 `topology_changed/EAGAIN`，零残留；
- snapshot slot gate 与 10 秒 Android runtime soak 通过；
- 两个 Provider restart 后 TXT/JPG 接收及 redirect hash 正确；
- mount preflight cancel、verified rollback、owner death、rollback failure 各 5/5；
- 50 次 cold 功能循环和 50 次 warm 功能循环通过；Android 16 对 warm launch 始终返回 `UNKNOWN/0`，不生成虚假 latency。

## 跳过边界

以下项目在当前 production 配置下没有可用构造入口，按用户指示跳过并保留边界：

- Provider query/insert/reverse：bit 17 未通过复合 probe，保持 `unsupported`；
- fanotify/Export：内核 `CONFIG_FANOTIFY` disabled，bits 8～11 保持 `unsupported`；
- V-45 collision、ambiguous、overflow 生产状态：当前没有获批的注入入口；
- 第二设备、其他 ROM/root/kernel、arm32：用户授权范围豁免；
- warm latency：系统 launcher telemetry 不可用，仅保留功能与 RSS 结果。

这些跳过项不改变 C1～C6 已观察结果，也不宣称未覆盖组合可用。
