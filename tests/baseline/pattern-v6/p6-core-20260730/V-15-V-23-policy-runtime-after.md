# V-15～V-23 policy/runtime 核心对比

- Change ID: `p6-core-20260730`
- Before commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`
- After commit: `working_tree`
- Host: Windows 11 x64；CMake 4.0.2；MSVC 19.44
- Android: NDK r27d/API 31；arm64-v8a + armeabi-v7a

## 结果

| 主题 | After 证据 | 分类 |
| --- | --- | --- |
| format v5 → format 6/schema 3 | golden/CRC/table/reference/UTF-8 reader 测试；旧 v5 稳定拒绝 | planned_break |
| capability/admission | bit 16～19 与 operation mask v1 逐 action 准入测试 | planned_break |
| snapshot reload/fork | 128/256 hazard slots、retire cap、atfork dirty/rebuild 测试 | unchanged + planned extension |
| literal mount | format 6 `MountPlanAdapter` 复用既有 mount transaction | unchanged |

Host Release CTest `75/75` 通过；Android 双 ABI、Zygisk `APP_STL=none`、ELF isolation 与
Host/Android rules compiler parity 通过。
真机 mountinfo/rollback 重放未在本批次执行，状态为 `not_observed`，不作为通过证据。

结论：format/schema/API 变化均为计划内破坏；自动化范围内未发现核心回归。
