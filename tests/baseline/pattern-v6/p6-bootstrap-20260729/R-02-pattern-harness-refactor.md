# R-02 Pattern corpus 与 limits profile 重构

- Change ID: `p6-pattern-harness-20260730`
- Task: `R-02`
- Baseline commit: `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79`
- Branch: `feature/pattern-redirect-v6`
- Observed at: `2026-07-30`（Asia/Shanghai）

## 重构结果

- `core/include/pathguard/pattern_limits.h` 成为唯一生产
  `PatternLimitsProfile` 定义，冻结设计 5.4/ADR-0014/0015 的 P0 ceiling。
- `tests/fuzz/pattern_corpus.h` 成为 unit/property/fuzz smoke/benchmark 的唯一只读
  manifest/seed loader。
- libFuzzer 使用同一 `pattern-v1` manifest 所登记的外部 corpus；target 自身只消费
  libFuzzer 提供的 bytes，不在热入口读取文件。
- 构建门验证 seed 文件存在且 SHA-256 与 manifest 一致，并验证 production limits 只有一个
  定义。
- 增加确定性 property smoke，为后续 tokenizer/matcher 语义留出同一 TDD 入口。

## 实际结果

```text
pathguard_pattern_fuzz_smoke ......... Passed
pathguard_pattern_harness_contract ... Passed
pathguard_pattern_property_smoke ..... Passed
pathguard_pattern_benchmark_jsonl .... Passed
pathguard_pattern_benchmark_tsv ...... Passed
pathguard_pattern_harness_guard ...... Passed
6/6 passed
```

额外使用 Clang 20.1.7 + Ninja + `PATHGUARD_BUILD_FUZZERS=ON` 编译两个 libFuzzer/UBSan
target，并从 `build/pattern-v6-v02-fuzz/corpus/` 各执行 `-runs=1`。tokenizer 与 matcher
均先消费空初始化输入，再消费 manifest 固定 seed，进程正常退出且无 sanitizer 报告。

## 对比结论

- 计划内变化：新增 P0 测试目标、结构化 benchmark 协议和唯一 limits 定义。
- 非预期核心行为变化：未发现；本阶段没有接入生产 reader、Provider 或 app-path 热路径。
- Classification: `unchanged`（相对 C1～C5 运行时行为）。
