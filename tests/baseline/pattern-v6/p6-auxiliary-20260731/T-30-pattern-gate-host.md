# T-30～R-30 Host matcher 性能门

- Change ID: `p6-pattern-gate-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Profile: Release / MSVC / x86_64
- Seed: `1885434929`
- Iterations: 1000 + 128 warmup
- Status: `complete`（runtime matrix 见同目录独立证据）

## 结构门

| 场景 | P99 ns | matcher calls | hot-loop allocations |
| --- | ---: | ---: | ---: |
| no_rules | 100 | 0 | 0 |
| scope_miss | 100 | 0 | 0 |
| zero_candidate | 100 | 0 | 0 |
| one_candidate | 700 | 1000 | 0 |
| one_candidate_except_2 | 1100 | 3000 | 0 |
| multi_candidate | 4300 | 8000 | 0 |
| multi_candidate_except_8 | 26700 | 72000 | 0 |
| max_bucket | 41100 | 64000 | 0 |

matcher calls 必须精确等于 `iterations * candidates * (1 + except)`；三个 bypass 场景必须为
0。allocation 计数在 warmup 完成、samples 预留容量之后开始，只覆盖 production match 热循环。

`reject_1000_patterns` 在 CandidateIndex 构造阶段用 `candidate/bucket limit` 拒绝，约
197300 ns，matcher calls/allocation 均报告 0，不允许 1000 条输入进入运行时扫描。

## 相对阈值

benchmark schema 升为 `pathguard.pattern-benchmark.v2`。每次执行使用同一进程、compiler 和
architecture 下的 `one_candidate.p99_ns` 作为 reference；candidate 场景按
`candidates * (1 + except) * noise_tolerance` 计算预算，bypass 场景使用同 reference 的噪声
容忍。CI 超阈值会返回非零，不再依赖跨机器固定纳秒值。

```text
pathguard_pattern_benchmark_jsonl  passed
pathguard_pattern_benchmark_tsv    passed
```

## Runtime 补充与 V-48 边界

Provider、provenance、reload、slot exhaustion、RSS 和 machine profile 已由
`T-30-runtime-performance-gate.md` 的独立 production-interface benchmark 覆盖。T-30～R-30 Host CI
状态为 complete；30 分钟 Host soak 和设备 Provider restart/profile 属于 V-48，保持独立状态。
