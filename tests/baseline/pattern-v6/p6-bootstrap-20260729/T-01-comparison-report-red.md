# T-01 前后对比报告校验红测

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-comparison-report-20260729` |
| Task | `T-01` |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| Branch | `feature/pattern-redirect-v6` |
| Result | expected red |

## 红测范围

新增 `pathguard_comparison_report_guard`，固定 JSON format 1 的最小字段，并对合法 fixture 逐项删除：

```text
comparison_report_format, change_id, before_commit, after_commit,
environment, rules_source_hash, policy_hash, module_abi_hashes,
scenario_id, steps, expected_result, before_actual, after_actual,
classification, evidence_paths, reviewer_conclusion
```

测试还要求：

- `classification` 只接受后续 I-01 冻结的枚举；
- 既有 Markdown 历史报告不能被静默当作 format 1 JSON；
- 每个缺字段返回稳定 `missing_field:<name>` 诊断。

## 执行结果

命令：

```powershell
ctest --test-dir "build/pattern-v6-v02-release" -C Release `
  -R "^pathguard_comparison_report_guard$" --output-on-failure
```

结果：`0/1` 通过，专项测试按预期失败。输出完整列出 16 个缺字段诊断期望，并包含：

```text
valid report was rejected
classification: expected diagnostic invalid_classification
historical report: expected diagnostic unsupported_report_format
```

失败原因仅为 `tests/baseline/validate_comparison_report.cmake` 尚未实现，不依赖随机时间、设备或网络。

## 证据哈希

| 路径 | SHA-256 |
| --- | --- |
| `tests/baseline/comparison_report_guard_test.cmake` | `AFA3F6AE76413229E0F5500139B3F41084BBEACA9621F04781F3B490B5B96633` |
| `tests/fixtures/comparison-reports/valid-report.json` | `0A835202B27E7A336E4B1E2E23A2E1285F5670F700625C5B22CF665CC3A6BF1F` |

## 验收结论

- 红测已注册到 CTest；
- 缺失 commit、环境、hash、场景、步骤、actual、classification、evidence 和 reviewer 字段均有
  独立失败用例；
- 历史 Markdown 不会被新 schema 静默接受；
- 测试稳定红，进入 I-01。
