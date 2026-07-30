# V-12 rules schema 与 canonical policy 改造前基线

- Change ID: `p6-core-20260730`
- Task: `V-12`
- Before commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`
- Branch: `feature/pattern-redirect-v6`
- Build: Visual Studio 17 2022, x64, Release
- Observed at: `2026-07-30`（Asia/Shanghai）

## 自动测试

以下 Release CTest 全部通过（`6/6`）：

- `pathguard_rules_desugarer_golden_test`
- `pathguard_rules_parser_test`
- `pathguard_rules_document_test`
- `pathguard_rules_semantic_test`
- `pathguard_rules_conflict_test`
- `pathguard_rules_pipeline_test`

## 代表性 format 1 行为

| 输入 | SHA-256 | 实际结果 |
| --- | --- | --- |
| `tests/fixtures/rules/migrated-valid.toml` | `1d6488335811fd2791e5fc08166c0a0658b75f10b3e7fa7df01f506c729bc626` | lint 成功，无诊断 |
| `tests/golden/rules/policy-v5-strict-inline.toml` | `2ef538e615cb59f0a5e127adcc31b356aa3dd23ebc88952f9ff85d810f402bcc` | `PG-RULE-INVALID-VALUE`（旧 processes `*`）和 `PG-REDIRECT-SYNTAX` |
| `tests/fixtures/rules/invalid.toml` | `a038be282126093da1b7dafd96a32f6da7886c7c293b7a78faf3fc82aaf6d6ee` | `PG-ARROW-OPERAND rules.arrow_operand` |

v5 golden 文件 SHA-256 为
`4a0681f818141bc20b6e3ec466f6442ab4f57536f69bb88cf4390bf31a243d7b`；
固定 `expected_file_size=207`、`expected_content_generation=11078014328063549684`、
`expected_plan_generation=5918468725002442624`。

## 计划内破坏

- source schema 从 format 1 切换到 format 2；format 1 最终由新 parser 明确拒绝；
- `deny`/箭头 `redirect` 改为统一 action table，selector 使用 `root/glob/type`；
- `file_picker` 拆为 Provider intent/capability admission，不再是单一运行时承诺；
- policy binary 从 v5/schema 2 一次性切换为 v6/schema 3。

必须保持的核心语义：literal deny 返回拒绝、literal redirect 保持源到目标映射、相同语义输入
canonical 稳定、无效 policy 不发布。C1/C2 不允许发生非计划行为变化。
