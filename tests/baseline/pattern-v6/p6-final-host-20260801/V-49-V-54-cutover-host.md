# V-49～V-54 旧接口清理与最终 Host Release 回归

- Change ID: `p6-final-host-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Host: Windows x86_64 / MSVC Release
- Status: `complete`

## 删除与替代

- policy v5 reader/writer、format 1 parser/desugarer 及生产构建引用已删除；旧输入只由
  format/version mismatch 路径拒绝。
- Provider prefix mapper 与 `file_picker`/`provider_compat` 决策已删除；生产路径统一使用
  Selector/Action、PolicyV6、provenance 和 versioned runtime status。
- daemon、CLI、rules compiler、native build 只生产 format 2 / policy v6。
- 历史报告和 baseline fixture 保持只读，不作为生产兼容分支。

`pathguard_legacy_cutover_guard` 对 `core/rules/daemon/cli/zygisk/native/module` 做生产扫描，结果通过；
仅剩 `canonical source` 一处注释用于说明禁止猜测来源，不是旧 fallback。

## 最终 Host 门

从新目录 `build-v54-release` 执行 configure、全量 Release build 和 CTest：`77/77 passed`。
补注册此前只构建未进入 CTest 的 `pathguard_rules_migration_test`，并将其 PolicyV6 期望更新为
371 bytes、content generation `2028246498201077069`。原 RF0 baseline guard 与旧测试 manifest
守卫均通过，未发现无故消失的历史测试。

结果分类：旧接口删除为 `planned_break`；format 2 / policy v6、C1～C6 Host 契约与 status
结果为 `unchanged`；`unexpected_regression=0`。
