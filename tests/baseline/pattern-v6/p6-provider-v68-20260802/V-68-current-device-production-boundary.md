# V-68 current-device production composite boundary

## 环境

- Device: Xiaomi M2012K11AC (`alioth`), Android 13 / API 33 / MIUI
  `V14.0.8.0.TKHCNXM`.
- Kernel: `4.19.157-perf-g9607d8651312`, SELinux enforcing, Magisk `30600`.
- Module: `0.1.44-dev` / versionCode `45`.
- ZIP SHA-256:
  `7ff47a3f60e30495c35cc654daa83e8c7f4d410db3cad1eeebd04c5843d47c67`.
- LocalSend: `org.localsend.localsend_app`, UID `10382`.

## 结果

| 子项 | 结果 | 证据 |
| --- | --- | --- |
| MediaProvider LSPlant | `2044/2044`, errno 0 | `build/device-evidence/provider-lsplant-v1/20260802-204858/provider-status.txt` |
| ExternalStorageProvider LSPlant | `3/3`, errno 0 | 同上 |
| Provider admission | 两端 `action_total=2`，两条 action 均 active | 同上 |
| LocalSend TXT/JPG forward | complete；只落在 `Download/localsend-redirect` | `post-receive-files.txt` |
| source 同名残留 | 0 | `post-receive-source-residual.txt` |
| target SHA-256 | TXT `49c7f5...d6d35a`；JPG `f2b5bb...205357` | `post-receive-files.txt` |
| authoritative bootstrap | 明确报告 `stage=strong_identity_unavailable`；不提交 weak identity | `post-receive-logcat.txt` |
| provenance WAL | `WAL_MISSING`，符合当前设备 fail-open 边界 | `post-receive-wal.txt` |
| LocalSend runtime | `backend=2`, `transaction=complete`, `security=fd_pinned` | `build/device-evidence/provider-lsplant-v1/20260802-205241/provider-status.txt` |
| queue/publication metrics | overflow、drop、hazard/retire 指标均为 0 | 同上 |
| bit 17 | 清零；`observed_capabilities=65536` | 同上 |

同一 Provider 回调可能同时上报不属于 source route 的 target document fact；该 fact 报告
`route_unavailable` 并透传。后续权威 source fact进入 strong-identity gate，报告
`strong_identity_unavailable`。未发生 prepare/materialize/commit，也未伪造 route。

## Strong identity probe

对 FUSE 可见路径和 `/data/media/0` backing 路径分别执行 arm64 probe：

- 两侧 `statx` mask 均为 `0x7ff`，不含 `STATX_BTIME (0x800)`；
- 两侧 `name_to_handle_at` 均返回 `ENOSYS (38)`；
- inode-only/ctime 不符合 ADR-0017，不得用于 durable owner。

原始输出位于：

- `build/device-evidence/provider-production-v68/20260802-185411/identity-probe-visible.txt`
- `build/device-evidence/provider-production-v68/20260802-185411/identity-probe-backing.txt`

## 结论

当前设备支持的 production forward、双 Provider LSPlant、admission、fail-open 和有界诊断子项
均完成。FILE_HANDLE protocol/snapshot/PFD 生产链路已通过 Host 和双 ABI gate，但 alioth 内核
不提供 ADR-0017 接受的任何 strong identity，因此 reverse、真实 PFD identity、committed live
publication、mutation/recovery 子矩阵按用户规则记为 `unsupported/not_observed` 并跳过。

bit 17 必须继续清零。后续仅能在提供可连接 file handle 或稳定 `STATX_BTIME` 的设备上继续
完整 V-68 composite 验收；不得在本机用裸 inode、ctime、URI 尾段或 display name 绕过该边界。
