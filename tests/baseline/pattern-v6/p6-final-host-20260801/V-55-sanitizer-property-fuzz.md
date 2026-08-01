# V-55 Sanitizer、property 与 fuzz Host 回归

- Change ID: `p6-final-host-20260801`
- Before commit: `1fcb35b`
- After commit: `working_tree`
- Host: Windows x86_64 / Clang 20.1.7
- Fixed fuzz seed: `1885434929`
- Status: `complete`（可用工具链范围）

## UBSan 与自动化 corpus

使用 `RelWithDebInfo`、`-fsanitize=undefined -fno-omit-frame-pointer` 和
`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` 构建全项目。完整 CTest 最终为 `77/77 passed`，覆盖：

- format 2 parser/compiler/limits/conflict 与 compile fuzz smoke；
- tokenizer/matcher fuzz smoke、property smoke 和固定 seed manifest；
- PolicyV6 reader/corruption、route provenance/WAL、Export recovery corruption；
- snapshot/concurrency、slot/retire、2 秒 reload/match soak 与性能预算。

首次 CTest 因前一个超时调用留下并行 CTest、共同写临时目录而出现两项准备阶段失败；残留进程
退出后两项单独通过，随后完整 `77/77` 重放通过。未出现 UBSan 报告、crash、OOB、UAF、hang。

## libFuzzer

定向构建并并行运行约 11 秒：

- `pathguard_rules_compile_fuzzer`
- `pathguard_pattern_tokenizer_fuzzer`
- `pathguard_pattern_matcher_fuzzer`

三个进程均 exit 0，artifact 目录为空，无 crash/hang/UBSan 报告；运行 corpus 保留在
`build-v55-fuzz/fuzz-corpus`，受版本控制的历史 seed 与 manifest guard 全部通过。

## 工具链限制

- ASan configure 成功但链接失败：当前 Visual Studio STL ASan 支持库 `stl_asan.lib` 未安装或
  不可见。该失败发生在任何项目测试运行前，不是项目缺陷。
- Windows Clang 20 安装不包含 TSan runtime；因此 TSan 不可用。并发行为由
  `pathguard_snapshot_publisher_test`、`pathguard_policy_snapshot_domain_test` 和
  `pathguard_runtime_soak_smoke` 覆盖。
- Debug UBSan 的 `_ITERATOR_DEBUG_LEVEL=2` 与 Clang runtime 不兼容；改用带调试信息的
  RelWithDebInfo/static CRT 后成功运行完整门。

结论：所有当前 Host 工具链可执行的 sanitizer/property/fuzz 工作完成；ASan/TSan 环境限制已
显式记录，不推断为通过。
