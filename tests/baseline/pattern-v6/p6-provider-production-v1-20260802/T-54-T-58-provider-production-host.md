# T-54～R-58 Provider production implementation

## 范围

- 将 immutable route snapshot registry 接入 Zygisk/LSPlant production runtime。
- 完成 live publication、外部 Provider 身份持久化、Java 结果工厂、Cursor/PFD adapter、
  mutation/reverse 和 companion recovery 闭环。
- 本报告只证明 Host/ABI/offline 实现；真实 Android composite 行为归 V-68。

## 生产合同

- callback 热路径只读取 immutable snapshot；不访问 daemon/store、不持有全局锁、不复制
  route/provenance 对象。
- Zygisk 与 LSPlant bridge 各使用 256 个固定 reader slots，最多保留 8 个 retired snapshots；
  发布失败保持旧 generation 或 fail-open，不允许 retired storage 无界增长。
- route 只从已提交 provenance 和匹配的 policy/scope/rule/plan generation 构建；caller scope
  使用 Binder caller UID。generation、scope、identity 或 result type 不一致均透传。
- Provider URI/document ID 通过 provenance protocol v4、WAL format 3 持久化；WAL format 1/2
  继续可恢复。protocol、WAL 与 snapshot 均传输 file-handle type/bytes；禁止从 URI 尾段、
  单独 display name、MIME 或 count 猜 route。
- insert 只接受权威 `_data` 或 `relative_path + _display_name`；Cursor 只使用真实 `_data` 或
  document ID 逐行解析；durable identity 按 `FILE_HANDLE -> STATX_BTIME` 顺序采集。PFD
  仅在 file-handle type/bytes/volume/object type，或 statx volume/inode/btime/object type 全部
  匹配时承认 strong identity。
- uniquely-bound item URI 才可进入 update/delete observation；实际 rename/unlink/rmdir 继续
  复用统一 path hook provenance 事务。collection/selection delete 无唯一 binding 时 fail-open。
- 外部身份上报使用 32-slot 固定无锁队列，由后台 publisher 执行 daemon I/O；重试有界，
  companion 不可用时 fail-open，恢复后重新发布并置 runtime available。

## 实现结果

- LSPlant C ABI 新增 `pathguard_lsplant_publish_mapping_v1`，Provider 后台线程按 provenance
  generation 发布新 snapshot；新 route 不要求 Provider restart。
- Java result factory 覆盖 `File`、document ID、`Uri`、`Cursor`、`ParcelFileDescriptor` 和
  boxed count，并在任何 JNI/shape/identity 异常下保留原返回值。
- ExternalStorageProvider File reverse 与 MediaDocuments document ID 共用 committed
  provenance record，不维护第二套 route 状态。
- authoritative-fact bootstrap 在后台 publisher 执行；strong identity 不可用时记录
  `strong_identity_unavailable` 并立即释放队列槽，不在 callback 热路径访问 daemon/store，
  也不把 inode-only/ctime 冒充 strong identity。
- bit 17 继续强制清零；只有 V-68 真机矩阵全部通过后才允许另行启用。

## 验证

- MSVC Release CTest：`83/83` 通过。
- Clang UBSan static runtime CTest：`83/83` 通过。
- ProviderHooker Java dispatcher Host：通过。
- NDK r27d Zygisk：`arm64-v8a`、`armeabi-v7a` 通过；ELF isolation guard 通过。
- NDK 29 LSPlant：`arm64-v8a`、`armeabi-v7a` 通过；ELF export/TLS guard 通过，未引入
  `PT_TLS`。
- production integration、comparison 和 ELF guards：通过。

## 未完成边界

生产代码已完成；V-68 的 forward 子场景已在 alioth 上观察。该设备的 FUSE 与 backing plane
均不提供 `STATX_BTIME`，`name_to_handle_at` 同时返回 `ENOSYS`，因此 strong identity、reverse、
PFD、live publication 与 mutation/recovery 子矩阵按设备不满足跳过。需要具备可连接 file handle
或稳定 statx btime 的设备才能继续该矩阵；在此之前不得宣称方案 B production composite active，
也不得设置 bit 17。
