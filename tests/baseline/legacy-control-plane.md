# RF0 旧控制面行为矩阵

> 状态：Characterization only
>
> 删除阶段：RF7-08

本文件记录 `rules.toml` 切换前 daemon/CLI 的实际行为。它不是新架构规范。

| 当前行为 | 自动化证据 | 分类 | RF7 替换任务 |
|---|---|---|---|
| CLI usage、缺文件错误和输入路径使用 `rules.ini` | `pathguard_legacy_control_plane_integration` | replace | RF7-05、RF7-06 |
| CLI `validate` 调用 `ParseRulesIni -> ValidatePolicy` | 同上 | replace | RF7-05 |
| CLI `compile` 直接写显式输出 | 同上 | preserve（仅离线输出语义） | RF7-05 |
| daemon 固定读取 `config/rules.ini` | 同上、`kRulesFileName` 单测 | replace | RF7-01、RF7-06 |
| daemon 顺序执行 parse、validate、encode、`DecodePolicy` 自检 | `pathguard_legacy_rules_control_test` | preserve（职责后续拆分） | RF5-09、RF7-03 |
| `DecodePolicy` 自检失败不写候选 | 注入损坏 bytes 的单元测试 | preserve | RF5-08、RF7-03 |
| 当前 policy bytes 完全相同时标记 `unchanged` 并跳过写入 | 单元与 daemon 集成测试 | replace 为 Canonical content generation 比较 | RF8-04 |
| 临时文件固定为 `policy.bin.tmp` | 临时目录碰撞测试 | replace | RF7-03 |
| 原子替换失败返回错误且不报告成功 | 目标路径为目录的故障注入 | preserve，并补 file/dir fsync 与恢复 | RF7-03 |
| reload 使用 150ms 固定 sleep | `kReloadDebounce == 150ms` 单测 | replace | RF7-02 |
| candidate 通过原始文本与 active/rejected 文本比较 | `IsCandidateNew` 单测 | replace 为 `source_digest` | RF7-02、RF7-04 |
| inotify 监听目录并处理 close-write/move/create/attrib/delete | 现有 daemon 源码与后续 RF7 集成基线 | preserve 并收敛为单 worker | RF7-02 |

`pathguard::legacy_rules` 只为 RF0 提供无全局状态的 characterization 测试缝。
它不属于目标规则编译架构，并随旧 parser 在 RF7-08 删除。
