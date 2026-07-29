# I-01 前后对比报告校验器绿灯

## 实现范围

- 新增 `tests/baseline/validate_comparison_report.cmake`；
- 只接受 `.json` 与 `comparison_report_format=1`；
- 要求 T-01 冻结的全部字段非空且类型正确；
- classification 只接受 `unchanged`、`planned_break`、`unexpected_regression`、
  `not_observed`；
- 历史 Markdown 返回 `unsupported_report_format`，不批量改写或静默升级。

校验器使用 CMake 3.22+ 内置 JSON parser，可由 Windows Host CI 的 CTest 或本地 PowerShell 调用：

```powershell
cmake -DREPORT="tests/fixtures/comparison-reports/valid-report.json" `
  -P "tests/baseline/validate_comparison_report.cmake"
```

## 绿灯结果

`pathguard_comparison_report_guard`：`1/1` 通过，总耗时 1.02 秒。T-01 的 16 个字段删除、非法
classification、合法 report 与旧 Markdown case 全部满足预期。

## 证据哈希

| 路径 | SHA-256 |
| --- | --- |
| I-01 绿灯时 `tests/baseline/validate_comparison_report.cmake` | `6C46C29F03059E2F796DDE760B217138159D4DFF89788A8582B6777EDD1AACF5` |
| `build/pattern-v6-v02-release/i01-ctest.xml` | `5006F6B47D72D3D96AD5DF019D6A62C6EA5BC6DEE6ED08ED6CBDEB400BB4CAB0` |

后续 R-01 会在保持测试全绿的前提下抽取共享 schema manifest，因此当前 validator 文件哈希发生
计划内变化不代表本证据失效。

## 验收结论

- T-01 从 red 转为 green；
- validator 可在当前 Windows/CMake 工具链直接调用；
- 固定枚举和逐字段诊断已实现；
- I-01 判定 `complete`。
