# I-02 Pattern benchmark/fuzz 空骨架绿测

- Change ID: `p6-pattern-harness-20260730`
- Task: `I-02`
- Baseline commit: `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79`
- Branch: `feature/pattern-redirect-v6`
- Observed at: `2026-07-30`（Asia/Shanghai）

## 最小实现

- 增加 tokenizer/matcher libFuzzer target；仅在
  `PATHGUARD_BUILD_FUZZERS=ON` 且 Clang 可用时构建。
- 增加日常 `pathguard_pattern_fuzz_smoke`，消费空输入和短输入。
- 增加固定 `pattern-v1` manifest、两个只读 seed 和统一随机种子。
- 增加 Release benchmark；输出 schema
  `pathguard.pattern-benchmark.v1`，覆盖 `zero_candidate`、`one_candidate` 和
  `multi_candidate`，支持 `--format=jsonl` 与 `--format=tsv`。
- 文档给出 Clang ASan/UBSan 可选命令。

此阶段的消费函数只执行确定性、有界的字节摘要；没有实现 glob tokenization 或 matching
语义，符合绿测最小实现边界。

## 实际结果

```text
pathguard_pattern_fuzz_smoke ........ Passed
pathguard_pattern_benchmark_jsonl ... Passed
pathguard_pattern_benchmark_tsv ..... Passed
pathguard_pattern_harness_guard ..... Passed
4/4 passed
```

JSONL/TSV 均实际输出 schema、Release/MSVC/x86_64 环境和三个候选场景。

## 结论

- Classification: `planned_break`
- T-02 green: yes
- Pattern runtime semantics changed: no
