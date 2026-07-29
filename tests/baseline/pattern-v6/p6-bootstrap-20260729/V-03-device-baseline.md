# V-03 C1～C5 真机行为基线

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-03` |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| After commit | N/A（实现前基线） |
| Branch | `feature/pattern-redirect-v6` |
| Device | alioth / Android 13 / API 33 / Linux 4.19.157 |
| Root | Magisk 30.6，SELinux Enforcing |
| Classification | `unchanged`（只记录当前事实） |
| Reviewer conclusion | V-03 基线已建立；C3 的 MediaStore 一致性存在已知缺口 |

## 模块与主体

| 字段 | 值 |
| --- | --- |
| Module | PathGuard Next `0.1.14-dev`, versionCode 15 |
| LocalSend | `org.localsend.localsend_app` 1.17.0, versionCode 583 |
| LocalSend UID | 10382 |
| Active PID | 24736 |
| ExternalStorageProvider PID/domain | 9210 / `u:r:platform_app:s0:c512,c768` |
| MediaProvider PID/domain | 4816 / `u:r:mediaprovider_app:s0:c222,c256,c512,c768` |
| Rules SHA-256 | `208e2f1d58a339bdd696e2548268f1e8dd15d62e09dbfba6664f2c3b8b2c9c89` |
| Policy SHA-256 | `1d1ec7d63ce491979acf521e5c28d0620acd2a0515e9adc914c44fff7612ff74` |
| arm64 Zygisk SHA-256 | `6caf75df2072c8ecbacf8710ab8e3c28a109caaa76531263aa9d9effd56ede90` |
| Content generation | 2504115471860318983 |
| Plan generation | 12692266494978381689 |
| Runtime topology generation | 9617772084435934573 |

当前 format 1 规则：

```toml
[apps."org.localsend.localsend_app"]
users = [0]
file_picker = true
deny = ["Pictures/Nagram", "DCIM/Screenshots"]
redirect = [
    "Download/localsend-source" -> "Download/localsend-redirect",
    "Pictures" -> "Download/localsend-redirect",
]
```

## 冷启动与 runtime status

对 LocalSend 执行一次 force-stop/launcher cold start。旧 PID 7957 的历史 status 为
`preflight_failed/rollback_complete`，不能代表当前运行。新 PID 24736 的实际状态为：

```text
enforcement=active
backend=2
transaction=complete
security=fd_pinned
reason=none
error=0
```

启动日志记录 4/4 MountOp 成功、`verification=syscall`、`committed=1` 和
`mi_snapshots=1`。backend 1 返回 ENOSYS 后选择 strict proc-fd backend 2。

LocalSend namespace 的关键挂载：

```text
/0/Download/localsend-redirect -> /storage/emulated/0/Pictures
/0/Download/localsend-redirect -> /storage/emulated/0/Download/localsend-source
/adb/modules/pathguard_next/run/deny-anchor -> /storage/emulated/0/DCIM/Screenshots
/adb/modules/pathguard_next/run/deny-anchor -> /storage/emulated/0/Pictures/Nagram
```

Settings PID 3291 的 namespace 没有上述业务挂载。

## C1：app-scoped literal deny

受限探针复用 LocalSend PID 24736 的 mount namespace，并设置相同 UID、primary group、
supplementary groups 且 drop capabilities。MagiskSU 不能切换到 LocalSend 的完整 SELinux MLS
category，因此该探针不是完整应用身份；这个限制必须保留在结论中。

结果：

| Path | Result |
| --- | --- |
| `Pictures/Nagram` | 失败，`No such file or directory` |
| `DCIM/Screenshots` | 失败，`Permission denied` |

另一个应用 namespace 不包含 deny anchor mount。C1 的 app-scoped namespace 隔离和拒绝行为
已观测；精确 errno 的统一化仍留给后续 Decision/adapter 测试。

## C2：app-scoped literal redirect

探针创建唯一临时文件 `.pathguard-v03-20260729-a.txt`：

1. 通过 LocalSend namespace 的 `Pictures` 写入成功，rc=0；
2. 物理文件出现在 `Download/localsend-redirect`；
3. root/global `Pictures` 下不存在该文件；
4. payload 为 `c2-via-pictures`；
5. 测试后临时文件数恢复为 0。

C2 当前行为通过。

## C3：LocalSend/Provider 代写

用户从另一台设备发送并在本机 LocalSend 接收：

- `PG_V03_PROVIDER_FILE.txt`；
- `PG_V03_PROVIDER_IMAGE.jpg`。

用户确认两项均显示接收成功，且可见于 `Download/localsend-redirect`。未手工移动文件。

文件证据：

| File | Size | SHA-256 |
| --- | ---: | --- |
| `PG_V03_PROVIDER_FILE.txt` | 75,494 | `49c7f5a5d08115218ace4b9da465b8c2a00886c39fc68578c634aeb811d6d35a` |
| `PG_V03_PROVIDER_IMAGE.jpg` | 125,478 | `f2b5bb6a1851eb6db3fbd610df6f0d73610642739c7de3c6fc1b37cfeb205357` |

物理检查确认：

- 两个文件都存在于 `Download/localsend-redirect`；
- `Download/localsend-source` 下均不存在；
- `Pictures/PG_V03_PROVIDER_IMAGE.jpg` 不存在。

日志链路：

1. LocalSend 记录目标 SAF tree 为 `primary:Download/localsend-source`；
2. ExternalStorageProvider 对普通文件以 `caller_uid=10382` 执行
   `realpath/access/stat64/open` source→redirect；
3. MediaProvider 对图片以 `caller_uid=10382` 执行 `realpath/access/stat64/open`，并记录
   `Open with FUSE` 的 source path；
4. LocalSend 分别记录 `Saved PG_V03_PROVIDER_FILE.txt` 和
   `Saved PG_V03_PROVIDER_IMAGE.jpg`。

重要缺口：ModernMediaScanner 随后尝试访问 source path 并得到
`NoSuchFileException`；对 `content://media/external/file` 和 images/media 的只读查询没有找到
这两个文件。因此当前 C3 应拆分为：

- SAF/Provider FD 写入与物理重定向：通过；
- LocalSend 用户侧接收：通过；
- MediaStore query/scan 与实际 FD 一致：**未通过/当前缺口**。

该缺口正是设计文档 P3 与任务 V-28～V-29/T-19 所保护的 before 行为，后续不得把它
改写成既有成功能力。

## C4：多源到同一目标

临时探针分别通过 `Pictures` 和 `Download/localsend-source` 写入不同文件，两次均成功，
物理文件均落到 `Download/localsend-redirect`，payload 分别为
`c2-via-pictures` 和 `c4-via-download`。

随后从两个 source 对同一个目录名执行 `mkdir`：第一次 rc=0，第二次 rc=1，证明同 target
同名碰撞被底层确定性拒绝。测试后目录和文件全部清理。

现有 v5 的 canonical reverse 并未在本任务重新宣称正确；反向唯一性仍以 V-10 和 route
provenance ADR 为实施前置。

## C5：故障隔离

- Settings namespace 看不到通过虚拟 source 创建的临时文件；
- Settings/global view 可以看到真实 backing 文件；
- 未命中应用没有 LocalSend 的 source/deny mounts；
- 当前 LocalSend runtime 为 active/complete，非故障 fail-open；
- 旧 PID 的 preflight failure 已 rollback_complete，不存在当前 namespace mutation。

故障注入、owner death 和 rollback failure已有 `tests/device/r1-safety-validation.md` 历史证据，
本任务没有重新运行破坏性 fault build。完整故障重放留给 V-39/V-59。

## 清理与保留

- 设备临时脚本 `/data/local/tmp/pathguard-v03-probe.sh` 已删除；
- `.pathguard-v03-*` 临时文件/目录前后数量均为 0；
- 用户实际接收的两个 `PG_V03_PROVIDER_*` 文件按用户要求保留在 redirect 目录；
- 本任务未修改规则、policy、模块、权限或系统配置。

## 验收结论

- C1、C2、C4 和 app namespace 隔离已建立可重放基线；
- C3 的真实 Binder caller UID、SAF/Provider 写入和物理重定向已建立基线；
- MediaStore query/scan 不一致被明确记录为当前实际缺口；
- 未把未观测或部分能力伪装成通过；
- V-03 的目标是记录实际 before 行为，证据已满足，因此判定 `complete`。
