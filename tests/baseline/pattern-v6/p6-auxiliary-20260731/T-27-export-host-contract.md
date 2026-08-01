# T-27～R-27 Export Host 合同

- Change ID: `p6-export-host-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Scope: Host worker、durable store 与 filesystem executor
- Status: `in_progress`

## Red

新增独立 `pathguard_export_worker_test` 后，构建按预期因以下生产合同缺失而失败：

- `TaskKey::Fid` 与显式 probe 的 stat fallback；
- normalized `EventSource` / close-write / rename / overflow 摄取；
- 分阶段 `TransferResult`；
- versioned `RecoverySnapshot` / `RecoveryStore`。

该失败是计划内 T-27 red，不是既有测试回归。

## Green / refactor

实现后，事件摄取、任务状态机、transfer callback 和 recovery store 为独立接口。Host fake 覆盖：

1. close-write 与 rename 乱序输入、重复事件和 source failure；
2. FID `(fsid, opaque handle, mount identity)` 优先，stat fallback 需要显式 probe 结果；
3. generation、event kind 和 event window 参与幂等 key，避免永久吞掉同文件后续写入；
4. queue full 与 source overflow 都设置 `rescan_required`；
5. copy/sync/rename 阶段失败不能进入 `complete`；
6. queued/failed/complete snapshot 恢复，crash 中的 running 恢复为 queued；
7. duplicate、错误 version、corrupt store 和超容量 snapshot 原子拒绝，不污染现有状态。
8. recovery 文件使用版本头、CRC32、记录/路径/handle 上限，并以临时文件 sync 后原子替换；
9. filesystem executor 始终向目标目录临时文件 copy+fsync，再 no-replace 原子安装；copy 保留源，
   move/trash 只在安装成功后删除源，目标冲突和源缺失返回精确 failure stage。
10. worker 的有效容量同时约束 queue 与全部持久化记录，且不超过恢复格式的 4096 条硬上限；
    新任务只淘汰已完成记录，失败记录保留显式重试语义，持续 event window 不产生隐性增长。

专项验证：

```text
pathguard_export_worker_test       passed
pathguard_auxiliary_adapters_test  passed
MSVC Release CTest                77/77 passed
Clang UBSan CTest                  77/77 passed
Android NDK arm64-v8a             passed
Android NDK armeabi-v7a           passed
Zygisk APP_STL=none / ELF guard   passed
Host/Android rules parity         passed
```

## 未完成实现边界

以下生产实现不能由 Host fake 代替，任务保持 `in_progress`；它们不是 V-43 的纯设备验证项：

- 生产 fanotify capability probe、FID/DFID_NAME/pidfd/rename-target adapter；
- 支持 fanotify 设备上的 V-43 normal/overflow/crash/cross-FS 对比。

任何上述缺口都不得被报告为同步 Redirect 成功或 event domain active。
