# V-02 Host Release 行为基线

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-02` |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| After commit | N/A（实现前基线） |
| Branch | `feature/pattern-redirect-v6` |
| Scenario | C1～C5 Host contracts、rules/compiler、policy v5、mount、Provider、status |
| Classification | `unchanged` |
| Reviewer conclusion | V-02 通过，可以进入 V-03/V-04 |

## 构建

使用 V-01 冻结的新目录和命令：

```powershell
cmake -S . -B "build/pattern-v6-v02-release" -G "Visual Studio 17 2022" -A x64 -DPATHGUARD_BUILD_TESTS=ON
cmake --build "build/pattern-v6-v02-release" --config Release --parallel 2
```

实际结果：

- configure 成功，耗时 6.3 秒；
- build 成功，耗时 30.5 秒；
- Windows SDK：10.0.22000.0；
- C++ compiler：MSVC 19.44.35209.0；
- MSVC tools：14.44.35207；
- generator/platform：Visual Studio 17 2022 / x64；
- `PATHGUARD_BUILD_TESTS=ON`。

## CTest

执行命令：

```powershell
ctest --test-dir "build/pattern-v6-v02-release" -C Release --output-on-failure --output-junit "v02-ctest.xml"
```

结果：

| 指标 | 值 |
| --- | --- |
| Tests | 52 |
| Passed | 52 |
| Failed | 0 |
| Disabled | 0 |
| CTest real time | 10.73 秒 |
| JUnit reported time | 10 秒 |

耗时最高的五项：

| Test | Time |
| --- | --- |
| `pathguard_rules_hot_reload_integration` | 3.6272 秒 |
| `pathguard_host_baseline_guard` | 1.39935 秒 |
| `pathguard_native_toolchain_guard` | 1.0971 秒 |
| `pathguard_mount_transaction_test` | 0.841842 秒 |
| `pathguard_test_asset_guard` | 0.651886 秒 |

RF0 的 10 个原始 Host 测试仍全部存在并由 `pathguard_host_baseline_guard` 独立验证。
当前 52 项还覆盖 Provider caller/path/lifecycle、Media query、rules parser/semantic/conflict、
fuzz smoke、format cutover、release audit、native toolchain、control plane 和 hot reload。

## 原始证据

| 文件 | SHA-256 |
| --- | --- |
| `build/pattern-v6-v02-release/v02-ctest.xml` | `E64A374323A709C6DACB91927F62364F01F629035FBD652B8101B3FCA9B29AAC` |
| `build/pattern-v6-v02-release/CMakeCache.txt` | `4193F1E0F0DDE9B9B34B9C8F6F3BB3DD42AAB2FD93467077089956115E0BD33C` |
| `tests/baseline/expected-host-tests.txt` | `294A551DBA12055752B724B0FACD7255C1F29BF78677D26030CE33404DC7C974` |

JUnit 文件长度为 17,832 bytes，timestamp 为 `2026-07-29T22:45:27`。原始 build 目录受
`.gitignore` 保护，不提交；本报告保留命令、结果、hash 和证据路径。

## 对比结论

- 当前观测结果与任务清单记录的 `52/52` 一致；
- RF0 原始 10 项没有消失或改名；
- 没有计划内破坏，因为本批次尚未修改生产行为；
- `unexpected_regression=0`；
- V-02 判定 `complete`。
