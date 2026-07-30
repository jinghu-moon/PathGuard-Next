# T-08～T-11 Pattern runtime 红测

- Change ID: `p6-core-20260730`
- Tasks: `T-08`, `T-09`, `T-10`, `T-11`
- Before commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`

`pathguard_pattern_runtime_guard` 可重复失败，且只报告 selector builder、CandidateIndex、
ActionEvaluator、OperationPlan 的生产文件和四个 CTest target 缺失。配置、生成和 runner 正常。

红门已冻结 `IdentityKey`、matcher invocation counter、DecisionReason、无 owner selector 和双
operand 固定计划入口；后续绿测不得通过 Provider/app-path 私有扫描器绕开这些抽象。
