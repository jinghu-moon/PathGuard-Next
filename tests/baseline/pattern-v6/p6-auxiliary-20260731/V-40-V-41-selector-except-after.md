# V-40～V-41 有界 selector 差集审计与对比

- Change ID: `p6-selector-except-audit-20260731`
- Tasks: `V-40`, `T-25`, `I-25`, `R-25`, `V-41`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Build: Release, MSVC x86_64
- Rules SHA-256: `dea93c4b7a3fff4853f552f76ec581ebc3aa674bb70ffdd0452b00d6fe869419`
- Benchmark SHA-256: `39c6c36b358182b66a908b138edf69b22e53f649db647cc08b2dcd738ec805f9`

## V-40 决策门

ADR-0015 和 ADR-0016 均为 Accepted。format 6 已包含 canonical
`SelectorExceptRefTable`，runtime 不包含 `!` token。审计确认 base candidate 命中后才执行
except，except 与 base 复用同一个 PatternProgram evaluator。

审计发现一个验收偏差：schema 曾静默接受 `except = []`，与 ADR-0015 要求“空数组必须失败”
冲突。现已以 `PG-INVALID-VALUE/rules.selector_except_empty` 原子拒绝；已有活动 snapshot 的发布
语义由 rules control 的 invalid-source 回归测试继续保护。

## T-25/I-25/R-25 验证

- leading `!pattern` 保持 `UnsupportedSyntax`，字面量 `!` 使用 `\!`。
- full except 只让 selector 变为 NoMatch，不生成 allow/deny 等负动作。
- base 未命中时 matcher invocation 为 1，不执行 except；base 命中且 full except 时为 2。
- canonical except 排序、去重、每 selector 8 个/每 app 256 refs 和 transition budget 使用共享生产常量。
- CLI `explain` 现在输出每个 canonical `except=`，不再隐藏有效匹配集合。

专项 Release CTest：

```text
pathguard_rules_schema_v2_test        passed
pathguard_rules_pattern_test          passed
pathguard_policy_action_router_test   passed
pathguard_cli_v6_integration          passed
pathguard_pattern_benchmark_jsonl     passed
pathguard_pattern_benchmark_tsv       passed
```

## V-41 性能与候选路径

固定 seed `1885434929`，1000 iterations：

| 场景 | candidates | except/selector | P99 ns | matcher calls |
| --- | ---: | ---: | ---: | ---: |
| zero_candidate | 0 | 0 | 100 | 0 |
| one_candidate | 1 | 0 | 700 | 1000 |
| one_candidate_except_2 | 1 | 2 | 1100 | 3000 |
| multi_candidate | 8 | 0 | 4100 | 8000 |
| multi_candidate_except_8 | 8 | 8 | 22700 | 72000 |
| max_bucket | 64 | 0 | 37800 | 64000 |

matcher calls 严格等于 `iterations * candidates * (1 + except)`；zero candidate 保持 0，证明实现
没有退化为全局负规则扫描。

## 结论

| 差异 | 分类 |
| --- | --- |
| `except=[]` 从静默接受改为拒绝 | unexpected regression resolved |
| CLI 展示 canonical except | planned extension |
| 无 except 的 C1～C6 行为 | unchanged |
| except matcher 成本 | bounded / within recorded Release gate |

自动化范围内 `unexpected_regression=0`。本任务是纯 host selector 语义，不新增设备能力声明。
