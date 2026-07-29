# V-11 前后对比报告安全网重放

## 输入

合法 format 1 JSON 从 V-02/V-03 只读复制以下元数据：

- before commit、Host/设备环境；
- rules、policy、arm64 module SHA-256；
- V-02 `52/52` Host 结果；
- V-03 C1～C5 结果与 C3 MediaStore 已知缺口；
- 两份原始 evidence path。

`classification=not_observed`，因为本任务只重放 schema，不把 validator 执行误报为设备行为重测。

## 重放矩阵

`replay_comparison_report_v11.cmake` 从唯一合法 JSON 在 ignored `build/pattern-v6-v11/` 中生成三个
损坏副本：

| Case | 预期 | 实际 |
| --- | --- | --- |
| valid | accepted | accepted |
| missing before commit | `missing_field:before_commit` | matched |
| invalid classification | `invalid_classification` | matched |
| empty evidence array | `empty_field:evidence_paths` | matched |

命令：

```powershell
cmake -DSOURCE_DIR="D:/100_Projects/110_Daily/PathGuard-Next" `
  -DOUTPUT_DIR="D:/100_Projects/110_Daily/PathGuard-Next/build/pattern-v6-v11" `
  -P "tests/baseline/replay_comparison_report_v11.cmake"
```

结果：exit 0，四个 case 全部满足预期 reason。

## 全量回归

重新配置后的 Release CTest 从 52 项增加到 53 项；原 52 项全部保留，新
`pathguard_comparison_report_guard` 通过：

```text
53/53 passed
0 failed
real time 11.60 seconds
```

本批次没有修改生产代码、规则、policy、模块或手机状态。C1～C5 行为沿用 V-02/V-03，
`unexpected_regression=0`。

## 证据哈希

| 路径 | SHA-256 |
| --- | --- |
| `V-11-valid-comparison-report.json` | `D4A7EB3F7581F27530A464C81111423E9AB6A8987DE43F07296951266F49DE42` |
| `tests/baseline/replay_comparison_report_v11.cmake` | `B7B73AD0299D68F39C56BF531438E43ECB875D81ED3279DBB0E6D12D158F452B` |
| `build/pattern-v6-v11/v11-results.txt` | `DA73C9B15D4DE3D35EB17EED8C83C86AD5DE3A73DCF8489A867D647A00FF5C04` |
| `build/pattern-v6-v02-release/v11-ctest.xml` | `5D3E501B778CA03588BD8F09EEE35B2C606772154D6270FABE073D13A417BDCD` |

## 验收结论

- 合法 V-02/V-03 元数据副本通过；
- 三份损坏副本均以精确 reason 失败；
- 原 52 项与新增 guard 全绿；
- 对现有核心行为没有运行时影响；
- V-11 判定 `complete`。
