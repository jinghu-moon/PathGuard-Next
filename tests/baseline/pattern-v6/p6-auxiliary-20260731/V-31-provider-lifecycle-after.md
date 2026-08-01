# V-31 Provider 生命周期修复后对比

- Change ID: `p6-provider-lifecycle-20260731`
- Task: `V-31` follow-up
- Before commit: `76376ac`
- After commit: `1fcb35b`
- Device: Xiaomi M2012K11AC, Android 13 / API 33
- Kernel: `4.19.157-perf-g9607d8651312`, arm64
- Root framework: Magisk `30.6:MAGISK:R` (`30600`)
- SELinux: Enforcing
- Module: `0.1.17-dev`
- Rules SHA-256: `dea93c4b7a3fff4853f552f76ec581ebc3aa674bb70ffdd0452b00d6fe869419`
- Policy SHA-256: `e2b2e771b4daccedceb49b2e6064f9c833b44ae942a69832352cc3baf6aef9af`
- Zygisk arm64 SHA-256: `b6eb7c903ece8c127a74dce6b85840ce0e1b9791c315a6e453927d6bdd05ea0e`

## 步骤与实际结果

1. 安装 `pathguard-next-v0.1.17-dev-universal.zip` 并重启。
2. daemon 在 policy 发布后检查支持的 storage Provider。
3. 同一 stale PID 连续两次未映射活动 `policy.bin` 后终止一次：

```text
attempt=21 targets=1 ready=0 stale=1 pending=1 terminated=0
attempt=22 targets=1 ready=0 stale=1 pending=0 terminated=1
attempt=27 targets=1 ready=1 stale=0 pending=0 terminated=0
```

4. 通过 LocalSend v2 API 同一会话上传 `test-final.txt` 和 `test-final.jpg`，两个 upload 请求均返回 HTTP 200。
5. 实际文件：

```text
/data/media/0/Download/localsend-redirect/test-final.txt  2797 bytes
/data/media/0/Download/localsend-redirect/test-final.jpg  199241 bytes
```

`localsend-source` 中不存在两个新文件。SHA-256 分别为：

```text
1d65f48b276df3652691cb277094973e8cd1c190bcfed5dd7fe90deede269aff  test-final.txt
8ff9d6fbd9416c3cfc2466135a8dcdec496e74952ba5b26bbc857dc22860f1ce  test-final.jpg
```

关键日志同时覆盖 SAF ExternalStorageProvider 与 MediaProvider：

```text
from=/storage/emulated/0/Download/localsend-source/test-final.txt
to=/storage/emulated/0/Download/localsend-redirect/test-final.txt

from=/storage/emulated/0/Pictures/test-final.jpg
to=/storage/emulated/0/Download/localsend-redirect/test-final.jpg
```

## 结论

| 场景 | 分类 | 结论 |
| --- | --- | --- |
| Provider 启动早于 Zygisk 可用 | planned extension | 双观察后单次重启，随后 ready |
| LocalSend TXT SAF 路由 | unexpected regression resolved | 嵌套 selector root 正确重定向 |
| LocalSend JPG MediaProvider 路由 | unchanged | 继续正确重定向 |
| 旧文件自动迁移 | not_observed / out of scope | 本变更只影响新操作，不迁移历史文件 |

本设备批次的两个 LocalSend 新写入均通过；Provider 50 次冷启动和第二 ROM 仍未执行，继续保持 `not_observed`。
