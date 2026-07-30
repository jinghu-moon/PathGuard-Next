# T-03～T-07 Pattern core P0 红测

- Change ID: `p6-core-20260730`
- Tasks: `T-03`, `T-04`, `T-05`, `T-06`, `T-07`
- Before commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`
- Build: Visual Studio 17 2022, x64, Release

## 重放命令

```powershell
cmake -S "." -B "build/pattern-v6-core-release" -G "Visual Studio 17 2022" -A x64 -DPATHGUARD_BUILD_TESTS=ON
ctest --test-dir "build/pattern-v6-core-release" -C Release -R "^pathguard_pattern_core_p0_guard$" --output-on-failure
```

## 实际结果

CTest 能发现并执行 `pathguard_pattern_core_p0_guard`。测试只因下列待实现契约缺失而失败：

- format 2 schema/parser source 与 `pathguard_rules_schema_v2_test`；
- Glob tokenizer/matcher source 与 `pathguard_rules_pattern_test`；
- brace/预算测试 source 与 `pathguard_rules_brace_test`。

构建系统、测试 runner 和现有依赖均正常。guard 已冻结 format 1 拒绝、selector/action 字段、
STAR/ONE/GLOBSTAR/CHAR_CLASS、invalid UTF-8、budget 以及 32/64 KiB brace 边界的测试入口。

## 结论

- Classification: `planned_break`
- Red gate: reproducible
- 失败原因：目标能力尚未实现，不是测试脚本或环境损坏。
