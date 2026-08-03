# V-69 Namespace Projection 当前设备进行中

## 环境与证据

- 设备：Xiaomi alioth，Android 13/API 33，当前生产规则为两个 Provider redirect action。
- 第一阶段版本：`0.1.45-dev` / `versionCode=46`。
- 当前候选：`0.1.46-dev` / `versionCode=47`。
- Provider/LSPlant 证据：`build/device-evidence/provider-lsplant-v1/20260802-235131/`。
- Namespace 证据：`build/device-evidence/provider-namespace-v1/20260802-235239/`。
- 修正采集器后的复采：`build/device-evidence/provider-namespace-v1/20260803-000719/`。
- `0.1.46-dev` 接收后证据：`build/device-evidence/provider-namespace-v1/20260803-113633/`。
- Provider 重启后证据：`build/device-evidence/provider-namespace-v1/20260803-113951/`。
- 删除重建后证据：`build/device-evidence/provider-namespace-v1/20260803-120326/`。
- 卸载残留证据：`V-69-uninstall-residual.txt`。
- 碰撞/未知 Namespace 证据：`V-69-collision-unknown-namespace.txt`。
- 重新安装后的复采：`build/device-evidence/provider-namespace-v1/20260803-181921/`、
  `build/device-evidence/provider-lsplant-v1/20260803-181924/`。

## 真实操作结果

LocalSend 在 `0.1.46-dev` 按三步执行：开启保存到相册接收 `test.txt`、`test.jpg`；开启保存到相册接收
`test1.jpg`；关闭保存到相册再次接收同名 `test1.jpg`。

| 逻辑来源 | Namespace | 物理文件 | 结果 |
| --- | --- | --- | --- |
| `Download/localsend-source` | `ns_57xfxvj54rskidat5ak4krdeye` | `.../ns_57x.../test.txt`、`test1.jpg` | 通过 |
| `Pictures` | `ns_vzn4kspwdxed2tgosk2o4z6bpu` | `.../ns_vzn.../test.jpg`、`test1.jpg` | 通过 |

两个同名 `test1.jpg` 同时存在于不同 Namespace，各 `125478` bytes；四个文件均不在
target 顶层扁平路径。`test1.jpg` 的两份 SHA-256 均为
`f2b5bb6a1851eb6db3fbd610df6f0d73610642739c7de3c6fc1b37cfeb205357`。

LocalSend 进程 mountinfo 显示：

```text
/storage/emulated/0/Download/localsend-source
  -> /0/Download/localsend-redirect/_pg/v1/ns_57xfxvj54rskidat5ak4krdeye
```

这证明 mount 与 Provider 使用同一 Namespace；它不是历史来源证明。

## Provider 状态

`com.android.providers.media.module` 与 `com.android.externalstorage` 均为 `action_total=2`、
两条 admission `active`，`provider_bridge_errno=0`，LSPlant 初始化、Hook 安装和 self-test
均成功。过滤 logcat 没有 Provider FATAL/JNI/LSPlant fault。

重启 Provider 后，MediaProvider PID 从 `4981` 轮换为 `29994`，ExternalStorageProvider PID
从 `9169` 轮换为 `30195`。两端重新发布两条 active admission，LSPlant hook/self-test 数量恢复，
`provider_bridge_errno=0`；四个物理文件路径与 SHA-256 均未变化。因此 Provider restart 后的
Namespace 静态恢复已通过。

随后精确删除两个 Namespace 中的 `test1.jpg`，保留 `test.txt`、`test.jpg` 和 Namespace 目录；
LocalSend 再次按保存到相册开启/关闭各接收一次同名 `test1.jpg`。两个新对象的时间戳均为
`2026-08-03 12:03`，仍分别进入原 Namespace，大小均为 125478 bytes，SHA-256 均为
`f2b5bb6a1851eb6db3fbd610df6f0d73610642739c7de3c6fc1b37cfeb205357`。删除重建没有继承
数据库 owner，也没有落入错误 Namespace，因此该子项通过。

## 当前设备跳过项

采集器中的 shell MediaStore 查询和 root DocumentsProvider 查询只代表诊断 caller，不能替代
LocalSend UID 的 caller-scoped Cursor 视图；因此 `_pg` 不泄露给 LocalSend 尚未由这批 shell
证据证明。LocalSend 当前 UID 为 `10382`；分别以 `su 10382` 执行 `content query` 和
`content read` 时，Android 在取得 Provider 前统一拒绝，并返回
`requires android.permission.ACCESS_CONTENT_PROVIDERS_EXTERNALLY`。切换 Linux UID 不能建立真实
LocalSend 的应用进程、package attribution 与 ContentResolver 调用上下文，因此该结果不能作为
caller-scoped query/open 的正向证据，也不能通过 shell 绕过。按当前设备不满足则跳过的规则，
`Provider query` 与 `Provider open` 均记为 `unsupported/not_observed`。

## 卸载残留

卸载 `pathguard_next` 并重启后，设备 `sys.boot_completed=1`。模块目录和 `pathguardd` 均不存在，
LocalSend PID 不存在；全局及所有 `/proc/*/mountinfo` 中的 `localsend-source`、`/_pg/v1/ns_`
和 PathGuard 挂载匹配数均为 0。重启后的 MediaProvider PID `4847`、ExternalStorageProvider PID
`9426` 中，PathGuard/LSPlant/ProviderHooker maps 匹配数也均为 0。因此 mount、daemon 和 Provider
Hook 三类运行时残留检查通过。

`_pg/v1` 下四个物理测试文件仍然存在，这是卸载不删除用户数据的预期行为，不属于运行时残留。

## 碰撞与未知 Namespace

LocalSend 向 `Download/localsend-source` 连续接收两个 `collision.txt` 时，在第二次 Provider create
前已把名称改为 `collision (1).txt`；Provider 日志分别观察到两个不同目标路径。因此没有构造出
同 Namespace、同 basename 的 exact-target create，`collision=reject` 记为 `not_observed`，不能
标记通过或失败。

使用不属于 route snapshot 的合法形状 ID `ns_aaaaaaaaaaaaaaaaaaaaaaaaaa` 创建隔离 fixture。刷新
LocalSend 进程后，已知 `collision.txt` 在其 mount namespace 的逻辑 source 中可见，物理 unknown
fixture 存在，但逻辑 `Download/localsend-source/unknown-namespace.txt` 返回 `ENOENT`。因此 unknown
Namespace 不进入已知 source 的 mount 投影通过。caller-scoped Provider query/open 仍受上一节所述
设备构造边界限制，保持 `unsupported/not_observed`。fixture 文件和目录已精确清理，现有 collision
文件未改变。

测试中还观察到一个独立生命周期边界：LocalSend PID `12080` 建立 mount 后，用户通过 Solid
Explorer 直接删除物理 `_pg/v1/ns_*` 目录，存量 mountinfo 的 backing 变为 `//deleted`；Provider
随后可重建物理目录，但旧进程不会热重绑定。force-stop 并重启为 PID `16140` 后 mount 恢复且不再
带 `//deleted`。该事实不是 PathGuard 自动数据删除，logcat 明确记录删除方为
`pl.solidexplorer2`；当前实现只证明进程重启恢复，不宣称活动 mount 热恢复。

至此 V-69 当前设备可构造范围完成；无法构造的 exact collision 和 caller-scoped Provider
query/open 按规则记为 `unsupported/not_observed` 后跳过。

FILE_HANDLE/STATX_BTIME 相关 provenance 场景继续按当前设备能力标记 `unsupported/not_observed`。
