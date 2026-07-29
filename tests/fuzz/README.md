# Rules compiler fuzz assets

RF0 只冻结 corpus 命名和 seed 规则；RF2/RF3/RF8 创建实际 fuzz target。

- seed 名称：`<target>-<sha256-prefix>.seed`
- regression 名称：`<target>-<issue-or-date>-<sha256-prefix>.case`
- 每个失败记录工具版本、固定 seed、最小输入和对应普通回归测试。

RF2 提供两个层次：

- `pathguard_rules_string_fuzz_smoke` 与 `pathguard_rules_candidate_fuzz_smoke`
  使用固定 PRNG seed 进入日常 CTest。
- `pathguard_rules_string_fuzzer` 与 `pathguard_rules_candidate_fuzzer`
  仅在 Clang 构建中启用 `PATHGUARD_BUILD_FUZZERS=ON`。
- `pathguard_rules_desugar_fuzz_smoke` 与 `pathguard_rules_desugar_fuzzer`
  覆盖 FormatProbe、scanner、单次 emitter、RewriteMap 和畸形前缀组合。
- `pathguard_rules_compile_fuzz_smoke` 与 `pathguard_rules_compile_fuzzer`
  覆盖 parser/scope、decoder、语义、encoder 和独立 verifier；成功结果必须可复验，
  任一 error 不得携带 blob。

固定工具链为 LLVM/Clang 18 compatible libFuzzer，日常 smoke 使用仓库固定
PRNG seed，长任务使用 `-max_total_time=3600 -timeout=10 -rss_limit_mb=512`。
发现崩溃后先用 `-minimize_crash=1` 最小化，再把输入登记为 `.case` 和普通
回归测试，不允许仅加入 ignore 列表。

libFuzzer 的可写 corpus 和 artifact 必须放在 `build/` 下，不得直接把
`tests/fuzz/seeds` 作为可写 corpus 目录。

正式运行时先把固定 seed 复制到 `build/fuzz-corpus/<target>`，再将该目录
传给 libFuzzer，并设置 `-artifact_prefix=build/fuzz-artifacts/<target>/`。

## Pattern v1 harness

P0 提供 `pathguard_pattern_tokenizer_fuzzer` 和
`pathguard_pattern_matcher_fuzzer` 两个独立 target。日常 CTest 运行
`pathguard_pattern_fuzz_smoke`，覆盖空输入和短输入；固定只读 seed 由
`tests/fuzz/seeds/pattern-v1/manifest.txt` 登记，随机变异统一使用 manifest 中的
`random_seed`。

运行 libFuzzer 时必须显式传入固定的 32-bit seed `1885434929`，例如：

```sh
pathguard_pattern_tokenizer_fuzzer build/fuzz-corpus/tokenizer \
  -seed=1885434929 -runs=1000
```

Linux Clang 可选 ASan/UBSan 构建命令：

```sh
cmake -S . -B build/pattern-fuzz -DCMAKE_CXX_COMPILER=clang++ \
  -DPATHGUARD_BUILD_TESTS=ON -DPATHGUARD_BUILD_FUZZERS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build/pattern-fuzz --target \
  pathguard_pattern_tokenizer_fuzzer pathguard_pattern_matcher_fuzzer
```

Release benchmark 输出为逐场景 JSON Lines 或带 header 的 TSV，二者均包含
`schema_version`、构建环境、场景、候选数、迭代数和耗时：

```sh
build/tests/pathguard_pattern_benchmark --format=jsonl
build/tests/pathguard_pattern_benchmark --format=tsv
```
