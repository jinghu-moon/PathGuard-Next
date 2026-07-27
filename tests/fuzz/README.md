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

libFuzzer 的可写 corpus 和 artifact 必须放在 `build/` 下，不得直接把
`tests/fuzz/seeds` 作为可写 corpus 目录。

正式运行时先把固定 seed 复制到 `build/fuzz-corpus/<target>`，再将该目录
传给 libFuzzer，并设置 `-artifact_prefix=build/fuzz-artifacts/<target>/`。
