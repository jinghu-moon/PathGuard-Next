# V-59 myron mount 故障注入报告

## 环境与身份

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-final-device-myron-v59-20260801` |
| Device | Xiaomi 25102RKBEC (`myron`)，Android 16/API 36，arm64-v8a only |
| Kernel/root | `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`；SukiSU Ultra/KernelSU `4.1.2-2-gf39c001e` |
| SELinux | Enforcing |
| Rules SHA-256 | `dea93c4b7a3fff4853f552f76ec581ebc3aa674bb70ffdd0452b00d6fe869419` |
| Device policy SHA-256 | `e2b2e771b4daccedceb49b2e6064f9c833b44ae942a69832352cc3baf6aef9af` |
| Production ZIP SHA-256 | `2142fd4022f890f832d2b7d3b8e0f50089b1b359c43ee9392dbca5a02198b51c` |
| Classification | `planned_break`（故障 profile 从失效的 450 ms 修正为 1000 ms） |

机器入口为同目录 `V-59-myron-mount-fault-injection.json`。

## 故障 profile 结果

| Profile | Zygisk SHA-256 | 样本 | 结果 |
| --- | --- | ---: | --- |
| prelease450 | `b1bb36c3c5291b20e4743a22101daaccc0fe5bfbd615c19128eb6dccdc613727` | 5 | 5/5 complete、三挂载；取消 0/5，profile 失效 |
| prelease1000 | `91bb8f78f894acf75d46015aa4a81b882c41d8c843c77364ef759a180b6ea063` | 5 | 5/5 `failed_preflight/preflight_failed/ECANCELED(125)`，挂载 0 |
| mountdelay1000 | `92d6ef11dbe59b75a7e7ea598eada0feed9fd52c8cd5a7293ca1247295443b7d` | 5 | 5/5 `rollback_complete/ECANCELED(125)`，回滚后挂载 0 |
| crash-after-mount | `8b658672f905099e34dae0b17cf08fe4207cc821e32695975e0d0dedb89fe9f3` | 5 | 5/5 `namespace_tainted/owner_death/EOWNERDEAD(130)`，成员终止 |
| mountdelay1000 + rollbackfailure | `8505bb4bf73e1120c98aec990fe106d06adc615bd6e6197f1f32bd55e588b3e7` | 5 | 5/5 `namespace_tainted/rollback_failed/EIO(5)`，`rollback_complete` 0/5 |

当前等待模型为基础 300 ms，进入 preflighting/applying 后各允许一次 500 ms 有界完成宽限。
因此 ADR-0003 的历史 450 ms 样本仍是当时实现的有效历史证据，但不再能触发当前实现的取消。
本批没有修改生产等待预算，只把当前故障 profile 调整到明确超过 800 ms 的 1000 ms。

prelease1000 的公开 runtime 使用 `transaction=failed_preflight`；mountdelay1000 使用
`transaction=rollback_complete`。两者公开 reason 均为当前 v2 reason 集合中的
`preflight_failed`，由 transaction 与 `error=125` 区分 mutation 前取消和验证回滚。

## 安全断言

| 断言 | 结果 |
| --- | --- |
| prelease1000 零 mutation | 5/5 |
| mountdelay1000 身份验证回滚后零挂载 | 5/5 |
| owner death `matched=1 signaled=1 remaining=0` | 5/5 |
| rollback failure identity confirmed | 5/5 |
| rollback failure `committed=0` | 5/5 |
| rollback failure 错报 `rollback_complete` | 0/5 |
| 各 profile 最终宿主 namespace 规则挂载 | 0 |

## 生产恢复

恢复包与安装产物逐项匹配：

| Artifact | SHA-256 |
| --- | --- |
| ZIP | `2142fd4022f890f832d2b7d3b8e0f50089b1b359c43ee9392dbca5a02198b51c` |
| arm64 Zygisk | `ce1423947cbfeed572e54b6b3a3e4fc295f1876a067637f52da78ba7eb2d8950` |
| pathguardd | `e0b8e60fda347a0dfa50f68e39ea147cf0cfff5e2284461c83c8f41cb4e3541f` |
| pathguardctl | `8e84e08aa87712d17fc52f3cc6b9b66c4907322c51968971c5600490a0b7d61f` |

生产恢复冷启动 10/10 为 `active/complete/fd_pinned`、backend 1、三挂载，故障注入日志 0；
TotalTime P50/P95/max 为 159/216/216 ms。

恢复后同一 LocalSend v2 会话接收 131-byte TXT 与 199241-byte JPG，均 HTTP 200；物理文件
只存在于 `Download/localsend-redirect`，SHA-256 分别为
`31386cc1fa83bb750a0873b8b254ed5011b18f567e2158a40d6109ba88778246` 与
`8ff9d6fbd9416c3cfc2466135a8dcdec496e74952ba5b26bbc857dc22860f1ce`。
ExternalStorageProvider 与 MediaProvider 存活，crash 匹配 0。最终 force-stop 后 PID 消失、
宿主 namespace 规则挂载 0。

## 边界

本批完成 V-59 的 mount cancellation/rollback/owner-death/rollback-failure 子矩阵。myron
`CONFIG_FANOTIFY` disabled，无法执行 fanotify overflow；Provider restart、policy corruption、
topology change、snapshot exhaustion 与第二 ROM/root framework 仍未观察，因此 V-59 保持 partial。
