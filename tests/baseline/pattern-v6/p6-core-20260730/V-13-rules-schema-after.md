# V-13 rules 模型改造后对比

- Change ID: `p6-core-20260730`
- Task: `V-13`
- Baseline: `V-12-rules-schema-before.md`

## 对比结果

| 场景 | Before | After | 分类 |
| --- | --- | --- | --- |
| 现有 format 1 parser/compiler | 6/6 rules 基线通过 | 全部旧 rules 专项仍通过 | unchanged |
| format 2 literal selector | 不存在 | canonical 标记 `Literal`，root/type/action 保留 | planned_break |
| format 2 glob selector | 不存在 | canonical PatternProgram，具固定 specificity | planned_break |
| format 1 输入交给 v2 parser | 不适用 | `PG-FORMAT-UNSUPPORTED` | planned_break |
| 无效 root/target | 旧 semantic 拒绝 | v2 canonical build 以 `PG-PATH-INVALID` 原子拒绝 | unchanged |

本工作包没有把 v2 接到生产 `CompileRules` 或 reader，因此 C1/C2 的现行执行路径未改变。
unexpected regression 为 0。后续只允许在 V-14/T-12/I-12 的一次性 v6 切换中改变生产版本。
