# Rules compiler D0 决议

> date: 2026-07-26  
> decision: cpp-tomlplusplus-arrow  
> status: Accepted

## 结论

PathGuard Rules format 1 选择：

- 唯一生产语言：C++20；
- 唯一 TOML parser：toml++ v3.4.0；
- TOML 版本：1.0；
- 用户 redirect 语法：保留 `"source" -> "target"`；
- 严格 TOML inline table 和映射子表只作为 D0 退出对照，不与箭头在 format 1 中并存；
- 不引入 Rust 规则编译工具链，不保留 Rust/C++ 双实现或隐藏标记 fallback。

这不是对 Rust 正确性的否定。Rust 候选通过了 span、TOML 1.0、207-byte golden、C ABI、panic containment 和 Android arm64 构建验证；最终选择 C++ 是因为在两个候选都正确的情况下，它以更少实体获得更低的完整编译耗时、峰值内存和二进制体积，并完全消除额外工具链与跨语言 ABI。

## 外部断言与共同语料

两个候选共享且不得按结果分叉的断言包括：

- 137 个 `toml-test` v2.2.0 / TOML 1.0 fixture：39 valid、98 invalid；
- 6 组 binder-neutral 文件、7 个 generated redirect；
- ASCII、Unicode、UTF-8 BOM、LF、CRLF、跨行规则和多次 rewrite；
- copied、synthetic-rule、synthetic-arrow 三类 parse error 映射；
- generated table/source/target byte span；
- 合法 redirect scope、错误 scope、手写 inline table、missing generated node；
- inline table 与映射子表均生成固定 207-byte policy v5；
- 当前 C++ `DecodePolicy()` 独立读取候选输出。

共同数据位于 `tests/golden/rules/d0/`、`tests/d0/results/` 和 `tests/fixtures/toml-test-v2.2.0/`。

## kill criterion 结论

| kill criterion | 结果 |
|---|---|
| generated table/value span 不稳定 | 两候选均通过 |
| Unicode、CRLF、BOM 或跨行映射不稳定 | 两候选均通过 |
| 正常路径必须依赖隐藏字段 | 不需要；C++ fallback 未启用 |
| 必须自建完整 CST 或解析 TOML 表路径 | 不需要 |
| Rust 必须转 `DocumentMut` 才能绑定 | 不需要；不可变 `Document` 可完成，且回归证明 `into_mut()` 会清除 span |
| TOML 1.0 语义无法冻结 | 两候选均冻结；Rust `toml_parser=1.0.4`，C++ unreleased features 关闭 |
| C ABI 必须暴露 AST/span/IR | Rust 极窄 ABI 可行，但选择 C++ 后整个 ABI 被删除 |
| 资源预算失败 | 两候选均未失败；C++ 有明显余量 |
| 原型复杂度超出最小边界 | C++ 不超出；Rust 会增加第二工具链和 ABI，收益不足 |

因此箭头技术门槛通过。继续保留箭头的理由不是“少写字符”，而是手工编辑时直接表达“应用可见路径 → 实际 backing 路径”。该收益在 C++ 单工具链方案下不再附带跨语言边界。若未来 Manager 成为绝大多数编辑入口，必须按 05 文档价值复审条款重新比较严格 TOML。

## 原始测量

Host Windows/MSVC Release，中位数单位为微秒；峰值为轮询进程 working set，包含进程运行时：

| 规则 | 候选 | parser | 完整编译 | 峰值内存 |
|---:|---|---:|---:|---:|
| 256 | C++ | 249 | 273 | 12,857,344 B |
| 256 | Rust | 162 | 317 | 13,815,808 B |
| 2,000 | C++ | 2,137 | 2,413 | 14,426,112 B |
| 2,000 | Rust | 1,446 | 2,764 | 18,825,216 B |
| 4,096 | C++ | 4,368 | 5,167 | 17,076,224 B |
| 4,096 | Rust | 3,948 | 6,960 | 24,608,768 B |

其他资源：

| 项目 | C++ | Rust |
|---|---:|---:|
| Host 测量可执行文件 | 153,600 B | 433,664 B |
| Host 冷构建 | configure 4,442 ms + build 7,627 ms | 6,247 ms |
| Android arm64 冷构建 | 6,135 ms | 4,830 ms |
| Android stripped 控制面 `.so` | 279,016 B | 1,504,288 B |
| Android Rust staticlib | 不适用 | 25,092,372 B（archive，不作为最终体积） |

构建时间两边均可接受，未作为排除条件。C++ 的决定性优势是完整编译、内存、最终体积和不存在 FFI。

## 依赖冻结

### 选定依赖

- toml++ v3.4.0；
- `third_party/tomlplusplus/toml.hpp`；
- SHA-256：`2089217190195E12E9A4A454BC94CFB95B58A07FF927F1505D068188C2F864DF`；
- MIT；
- `TOML_EXCEPTIONS=0`；
- `TOML_ENABLE_FORMATTERS=0`；
- `TOML_ENABLE_UNRELEASED_FEATURES=0`。

### 已验证但未选择

- rustc/cargo 1.97.1；
- cargo-ndk 4.1.2；
- `toml_edit=0.23.7`，MSRV 1.76，MIT OR Apache-2.0；
- `toml_parser=1.0.4`；
- Cargo.lock SHA-256：`143F97E1ECEC9C3DFF409B0BB8698DF26B49619ABDD94D7A8853071DB067615B`；
- 依赖树：`toml_edit -> indexmap/equivalent/hashbrown + toml_datetime + toml_parser/winnow`。

## RulesLimits 与错误码

唯一冻结定义位于 `rules/include/pathguard/rules_contract.h`。每个 limit 都有 boundary-1、boundary、boundary+1 测试，覆盖：源字节、容器深度、token/node、应用数、每应用规则、展开规则、路径字节/组件、字符串 token、rewrite/segment、诊断/关联 span 和生成文本。

错误码冻结：全部 `PG-ARROW-*`、`PG-RULE-ARROW-SCOPE`、`PG-REDIRECT-SYNTAX`、`PG-DESUGAR-INTERNAL`、`PG-COMPILER-INTERNAL`、`PG-RESOURCE-LIMIT`。

## Android 与 Zygisk 隔离

- NDK r27d：27.3.13750724；
- API 31；
- 首个 ABI：arm64-v8a；
- C++ 候选：`-std=c++20 -O3 -fno-exceptions -fno-rtti`；
- `module/zygisk/arm64-v8a.so` 的 dynamic symbol 和 strings 扫描均未发现 toml++、Rust、`pg_rules_compile` 或规则编译器符号；
- parser 和编译器仍只属于 daemon/CLI 控制面。

## 删除清单

RF1 完成前删除：

- 隔离 Rust crate、Cargo.toml、Cargo.lock 和全部 Rust D0 源码；
- Rust C ABI 头、layout harness、policy blob integration 和 Android probe；
- Rust target/build artifact；
- 任何隐藏标记 fallback；
- 对 `refer/toml++.hpp` 的构建依赖，改为带许可证和哈希的正式 vendored header。

保留 `tests/d0/results/rust-toml-edit.txt` 作为不可执行的历史测量证据，不保留 Rust parser wrapper 或生产构建入口。

## 完整命令

```powershell
cargo test --manifest-path rules-compiler/Cargo.toml --locked
cargo build --manifest-path rules-compiler/Cargo.toml --locked --release --example d0_benchmark
cargo ndk -t arm64-v8a --platform 31 build --release --locked
cmake -S . -B build -DPATHGUARD_BUILD_TESTS=ON
cmake --build build --config Release --target pathguard_cpp_toml_adapter_spike
ctest --test-dir build -C Release -R pathguard_cpp_toml_adapter_spike --output-on-failure
./scripts/build-native.ps1 -Abi arm64-v8a
```

Android candidate probe 使用同一 NDK clang、API 31、相同输入和 Release 优化；完整参数与原始数值固定在本决议及 `tests/d0/results/` 中。
