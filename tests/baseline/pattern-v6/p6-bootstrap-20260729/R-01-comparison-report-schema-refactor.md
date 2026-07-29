# R-01 前后对比报告 schema 重构

## 重构结果

- 新增 `tests/baseline/comparison_report_schema_v1.cmake` 作为唯一 schema 常量来源；
- validator 与 guard 都 include 该 manifest，不再重复 required fields 或 classifications；
- `valid-report.json` 同时作为 canonical template 和 positive fixture；
- 新增 fixture README，明确历史 Markdown 只读且不是 format 1 输入；
- 未批量改写或删除任何 V-01～V-10、rules device、R1 历史证据。

共享 manifest 分组定义 string/object/array fields、format version 和四个 classification。新增 Host、
rules device 或 R1 对比报告均使用同一个 JSON template/validator，不再各建字段清单。

## 验证

`pathguard_comparison_report_guard` 在重构后 `1/1` 通过，总耗时 1.03 秒。字段逐项删除、合法
fixture、非法 classification 和 Markdown 拒绝行为均未变化。

| 路径 | SHA-256 |
| --- | --- |
| `tests/baseline/comparison_report_schema_v1.cmake` | `104871F50E948430E493004B74FA8C4DE3D90ACC016AE1A1F49A300AA9304F27` |
| 重构后 `tests/baseline/validate_comparison_report.cmake` | `8115FF956F715F865DC8480830DC96EA3896600073DDC4A7E000C58624AC4FFC` |
| `build/pattern-v6-v02-release/r01-ctest.xml` | `09102BFBFE0E47ED6508F68FC296FB8431E5377F9C3F52B0678B2F7371F0CD4C` |

## 验收结论

- T-01 保持 green；
- 新报告 schema 只有一个字段定义来源；
- 历史证据无丢失、无改写；
- R-01 判定 `complete`。
