# V-24～V-31 app-path / Provider 对比

- Change ID: `p6-core-20260730`
- Before commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`
- After commit: `working_tree`
- Rules SHA-256: `5259875401187DCDB1928E92E3567402B74E21DE0C70E2EE532960B0F37E67F3`
- arm64 Zygisk SHA-256: `6EBC641E6F6691D92AC238095F86D398AB4300640C7B49F391D013D57CF45AE4`
- armeabi-v7a Zygisk SHA-256: `78DADAC07A7074F35A3843B84363C7BE8793B3F02CC23B3669386C06F408A44C`

## After 行为

- Provider 与 app-path Hook 都消费同一 format 6 `PolicyV6View`、scope、Pattern runtime 和
  `Decision -> syscall` 契约；旧 prefix mapper 不再位于生产 Hook 热路径。
- app-path 使用 specialize 时已验证的进程 UID/package；Provider 使用 Binder raw calling UID，
  unknown caller 完整透传。
- open/stat/access/opendir/mkdir/unlink/rmdir/rename/link/realpath/readlink/metadata/truncate/watch
  均发布独立 operation bit；deny=`EACCES`，双 operand 跨域=`EXDEV`。
- Provider bit 17 只允许由完整 composite probe 生成；当前没有生产 query/insert/reverse ABI
  adapter，因此保持清零/unsupported，不能因 PLT commit 或 ROM 名称单独设置。
- Hook commit 后只允许 active ↔ passthrough，不恢复 unloadable。
- root/relative path 在 matcher 前拒绝空组件、`.`、`..` 和超长组件。

## 对比结论

| 场景 | 分类 | 证据 |
| --- | --- | --- |
| 旧 literal prefix | unchanged | `pathguard_policy_action_router_test`、`pathguard_path_hook_contract_test` |
| glob deny/redirect | planned extension | `pathguard_policy_pattern_runtime_test`、`pathguard_policy_action_router_test` |
| 其他 UID 同路径透传 | unchanged | scope miss 单元测试 |
| Provider lifecycle | unchanged/strengthened | `pathguard_provider_redirect_lifecycle_test` |
| 生产 hazard snapshot | unchanged/strengthened | `pathguard_policy_snapshot_domain_test` |
| LocalSend 图片真实接收 | not_observed | 需要安装本批次模块并重启后人工操作 |
| Provider 50 次冷启动/重启 | not_observed | 未执行真机 soak |

自动化范围内 unexpected regression 为 0。真机项未观察，不伪报通过。
