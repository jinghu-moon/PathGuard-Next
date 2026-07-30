# V-14 policy v5 切换前基线

- Change ID: `p6-core-20260730`
- Task: `V-14`
- Baseline commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`
- Status: `complete`

## 当前协议事实

- rules source：`format = 1`。
- binary policy：format 5 / schema 2。
- golden size：207 bytes。
- content generation：`11078014328063549684`。
- plan generation：`5918468725002442624`。
- Header、Package、MountRule、EventRule、String 共五类结构；无 Pattern/Selector/Action 表。

## 核心行为基线

| 场景 | 当前实际表现 | v6 预期差异分类 |
| --- | --- | --- |
| literal deny | format 1 编译并产生 mount deny | `unchanged`（输入 schema 为 planned break） |
| literal redirect | format 1 编译并产生 mount redirect | `unchanged`（输入 schema 为 planned break） |
| LocalSend Provider literal redirect | 已归档真机 C3/C4 基线成功 | `unchanged` |
| glob redirect | v5 无法表达 | `planned_break/new_feature` |
| format 1 input | 接受 | `planned_break`：切换后稳定拒绝 |
| v5 bytes | reader 接受 | `planned_break`：version mismatch/fail-open |
| corrupt checksum/offset/generation | reader 拒绝 | `unchanged` |
| failed deployment | 保留旧 active policy | `unchanged` |

V-15 将用语义等价 format 2/v6 fixture 重放自动化场景。需要设备参与的 C1～C5 本阶段按
用户授权可记为 `not_observed`，不得据此宣称真机通过。

