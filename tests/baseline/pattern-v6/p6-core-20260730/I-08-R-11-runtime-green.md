# I-08～R-11 Pattern runtime 绿测与重构

- Change ID: `p6-core-20260730`
- Tasks: `I-08/R-08`, `I-09/R-09`, `I-10/R-10`, `I-11/R-11`
- Before commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`

## 实现结果

- `SelectorTable` 在单 package 内 canonical 去重，selector 不携带 package ownership；
  package/UID 授权只存在于 action 与 scope grant。
- `CandidateIndex` 按 identity、可信 package attribution、root、literal/extension/general
  分桶；scope miss 和 bucket miss 不调用 Pattern matcher。
- `ActionEvaluator` 在固定候选上按 deny、priority、specificity、RuleId 决策，不在热路径排序；
  observe/export 以 effect mask 与主处置分离。
- `OperationPlan` 统一处理一至两个 operand；完整决策后才接受，deny/collision/cross-domain
  分别映射 `EACCES`、`EEXIST`、`EXDEV`。
- RuleId 由 package、selector canonical bytes、action kind、target、priority 的语义哈希产生，
  不依赖源码声明顺序或 source line。

## 验证

Release 专项 `5/5` 通过：

- `pathguard_selector_builder_test`
- `pathguard_candidate_index_test`
- `pathguard_action_evaluator_test`
- `pathguard_operation_plan_test`
- `pathguard_pattern_runtime_guard`

## 设计原则

- KISS：0/1/multi 命中共用一个有界 evaluator，不引入第二套匹配语义。
- DRY：所有 adapter 后续只消费 `OperationPlan`，不复制 precedence/collision 逻辑。
- SOLID：selector 匹配、候选索引、action 决策和 syscall plan 保持单一职责。

