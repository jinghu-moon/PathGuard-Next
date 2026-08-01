# V-48 Host 性能、稳定性与资源上限 after

- Change ID: `p6-v48-host-after-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Host: Windows x86_64 / MSVC Release / 20 hardware threads
- Device portion: `not_observed`（Provider restart/profile 需要真机）

## 30 分钟 soak

执行：`pathguard_runtime_benchmark.exe --soak-seconds=1800`。

```json
{"schema":"pathguard.runtime-soak.v1","duration_seconds":1800,"matches":29887370378,"reloads":402536774,"rss_before_bytes":11460608,"rss_after_bytes":11681792,"slot_high_watermark":4,"retired_high_watermark":5,"reload_rejected":0,"result":"passed"}
```

进程正常退出，stderr 为空；RSS 增长 221,184 bytes，未观察到 slot acquire failure 或 retire
reject。2 秒 CTest smoke 也通过（35,994,575 matches / 430,430 reloads）。

## Host 门结果

- Release CTest：`82/82 passed`；
- Provider route/provenance/reload/slot/RSS benchmark：通过；
- matcher benchmark JSONL/TSV：通过；
- `git diff --check`：通过；
- Android NDK `arm64-v8a,armeabi-v7a`：通过；
- Zygisk `APP_STL=none` / ELF isolation：通过；
- artifact SHA-256：
  - arm64 `0F9DFD64A83E46ED9B09A6530C4AF4AD341C080219E289D331BC9ED9A203DC8A`
  - armeabi-v7a `FEF926B0CBD8FDC560F977B8826850329298687E30FBE83C8A8293C11EE123B2`

## 未完成边界

V-43 的 fanotify normal/overflow/crash/cross-FS、V-45 的 active/inactive/unsupported 与真实文件
结果、V-48 的 Provider restart/profile 仍需设备。当前 Xiaomi Android 13 kernel 未启用
`CONFIG_FANOTIFY`，bit 8～11 必须保持 `unsupported`；这些限制不影响 Host 门结论，也不能宣称
event/Export 已在设备 active。
