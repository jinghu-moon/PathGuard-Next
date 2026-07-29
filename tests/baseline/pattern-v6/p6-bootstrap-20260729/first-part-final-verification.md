# 第一部分最终验证

- Change ID: `p6-pattern-harness-20260730`
- Scope: `V-01～V-11, T-01/I-01/R-01, T-02/I-02/R-02`
- Baseline commit: `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79`
- Branch: `feature/pattern-redirect-v6`
- Observed at: `2026-07-30`（Asia/Shanghai）
- Build: Visual Studio 17 2022, x64, Release

## 最终命令

```powershell
cmake -S "." -B "build/pattern-v6-v02-release" -G "Visual Studio 17 2022" -A x64 -DPATHGUARD_BUILD_TESTS=ON
cmake --build "build/pattern-v6-v02-release" --config Release --parallel 2
ctest --test-dir "build/pattern-v6-v02-release" -C Release --output-on-failure
git diff --check
```

## 结果

- Release configure/build：通过。
- CTest：`59/59` 通过，总耗时约 11.70 秒。
- Pattern 专项：`6/6` 通过。
- Clang 20.1.7 libFuzzer/UBSan：两个 target 编译通过，固定 corpus 短回归通过。
- `git diff --check`：通过。
- 生产 `kPatternLimitsProfileV1` 定义数：1。
- Pattern corpus：tokenizer/matcher seed 均存在且 SHA-256 与 manifest 一致。

全量首次重放曾发现 `pathguard_test_asset_guard` 把新的 `pattern-v1` 子目录误当成旧 rules
seed。根因修复为旧 validator 只比较其根目录普通文件；Pattern 子目录由
`pathguard_pattern_harness_guard` 独立验证。修复后旧 guard 的真实资产、坏 hash 注入、缺失资产
与 golden 泄漏测试仍通过。

## 前后对比

- 计划内变化：新增 6 个 Pattern Host 测试、两个可选 libFuzzer target、结构化 benchmark、
  corpus/limits 单一来源和 format/provenance ADR。
- 核心 C1～C5 运行时路径：本批次未接入生产 reader/adapter/hook，行为保持不变。
- 非预期核心回归：无。
- Classification: `unchanged`。
