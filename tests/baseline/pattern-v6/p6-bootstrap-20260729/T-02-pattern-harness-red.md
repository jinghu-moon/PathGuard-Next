# T-02 Pattern benchmark/fuzz 构建门红测

- Change ID: `p6-pattern-harness-20260730`
- Task: `T-02`
- Baseline commit: `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79`
- Branch: `feature/pattern-redirect-v6`
- Observed at: `2026-07-30`（Asia/Shanghai）
- Build profile: Visual Studio 17 2022, x64, Release

## 重放命令

```powershell
cmake -S "." -B "build/pattern-v6-v02-release" -G "Visual Studio 17 2022" -A x64 -DPATHGUARD_BUILD_TESTS=ON
ctest --test-dir "build/pattern-v6-v02-release" -C Release -R "^pathguard_pattern_harness_guard$" --output-on-failure
```

## 实际结果

`pathguard_pattern_harness_guard` 被发现并执行，结果为失败。诊断仅包含尚未实现的
Pattern harness 契约：

- 缺少 tokenizer/matcher libFuzzer source 与 CMake target；
- 缺少日常 fuzz smoke target；
- 缺少固定 `pattern-v1` corpus manifest；
- 缺少含 `zero_candidate`、`one_candidate`、`multi_candidate` 场景的 benchmark；
- 缺少 ASan/UBSan 与 JSONL/TSV 输出运行说明。

CTest 配置、生成和测试脚本本身均正常；不存在编译失败、依赖缺失或环境损坏。

## 结论

- Classification: `planned_break`
- Red gate: reproducible
- 下一步：I-02 只补齐可编译、可运行的空 harness，不实现 tokenizer 或 matcher 语义。
