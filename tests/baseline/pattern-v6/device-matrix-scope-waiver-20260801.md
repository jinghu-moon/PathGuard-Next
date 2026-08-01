# 第二设备矩阵范围豁免

- 日期：2026-08-01
- 决策：当前没有可用的第二台设备，用户明确授权跳过第二设备验证。
- 状态：`not_observed`（scope waiver）

## 适用范围

V-48、V-57～V-60 中要求额外 ROM、root framework、kernel tier 或 arm32 设备重复执行的部分，
不再作为当前交付的待执行项。现有 myron/Android 16/SukiSU Ultra/arm64 证据仍是唯一可声明的
设备验证范围，不得据此推断其他设备组合兼容。

## 当前设备能力豁免

用户同时授权对当前设备无法构造的状态/能力场景直接跳过，并保留 `unsupported` 或
`not_observed` 结论：

- Provider bit 17 的 query/insert/reverse 复合能力；
- fanotify Export（`CONFIG_FANOTIFY` disabled）；
- V-45 collision、ambiguous、overflow 的生产注入状态；
- Android 16 无有效 warm latency 的 launcher 遥测。

## 不受本豁免影响的任务

- 当前设备尚未覆盖的 V-45 inactive/unsupported/collision/ambiguous/overflow 状态矩阵；
- T-27～R-27 生产 fanotify adapter，以及依赖支持设备的 V-43/V-59/V-60 Export 场景；
- V-46 `adapter-only` 决策合法阻断的 T-29～R-29/V-47；
- 完成剩余非豁免项后才能执行的 V-61～V-63 最终审计与完成判定。

上述范围不再阻断 V-61～V-63，但最终报告必须保留所有 `not_observed/unsupported` 能力边界。
