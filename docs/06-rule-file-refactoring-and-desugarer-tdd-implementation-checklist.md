# PathGuard Next 配置文件重构与箭头脱糖器 · TDD 分阶段实施任务清单

> 状态：RF0～RF9 完成
>
> 文档版本：0.7
>
> 日期：2026-07-27
>
> 设计基线：`docs/04-rule-file-refactoring-design.md`、`docs/05-rule-arrow-desugarer-design.md`
>
> 二进制契约：`docs/adr/0002-policy-format-v4.md` 所定义的唯一 `policy.bin format v5`
>
> 实施范围：`rules.ini` → `rules.toml`、箭头语法决策与脱糖、完整规则编译链、控制面接入、迁移和删除旧解析路径

---

## 1. 文档目的

本文把规则文件重构和箭头脱糖器设计转换为可以逐项执行、逐项验证、按依赖解锁的 TDD 任务清单。

它不是新的架构设计，也不重新讨论 04/05 已冻结的语义。实施时以以下顺序为唯一主线：

```text
现状 characterization
    -> D0 联合 Go/No-Go
    -> Source/Format/词法基础
    -> 局部箭头校验与 RewriteMap
    -> 唯一 TOML parser + generated node 绑定
    -> RulesDocument / 语义编译 / policy.bin
    -> 构建与语言边界
    -> daemon / CLI / 发布事务切换
    -> 健壮性、性能、迁移与删除旧路径
```

核心原则：

- 每项生产实现前必须先有失败测试，即 `RED -> GREEN -> REFACTOR -> VERIFY`。
- D0 未形成唯一结论前，不得开始箭头生产实现。
- 不长期保留 Rust/C++ 双编译器，不让 daemon/CLI 使用不同 parser。
- 不以测试成熟第三方 TOML parser 为项目目标；测试重点是 PathGuard 自有边界、版本锁定和集成契约。
- 不因为流程图存在一个方框就创建一个类、crate 或静态库。
- 不为 formatter、Manager、未来高级 redirect 或通用 DSL 提前建立空接口。

---

## 2. 使用方法

### 2.1 状态标记

- `[ ]`：未开始。
- `[~]`：进行中，尚未满足任务完成证据。
- `[x]`：RED、GREEN、REFACTOR/VERIFY 和完成证据均已满足。
- `[!]`：阻塞；必须记录阻塞原因、复现命令和解除条件。

### 2.2 优先级

- `P0`：核心发布阻塞项。未完成不得切换 `rules.toml`。
- `P1`：核心切换后应完成，但不阻塞最小规则编译链。
- `P2`：明确延期或独立项目，不得提前创建生产抽象。

### 2.3 TDD 执行规则

每个任务按以下顺序执行：

1. `RED`：先写最小失败测试，确认失败原因正是缺失行为。
2. `GREEN`：只写使当前测试通过的最小生产代码。
3. `REFACTOR`：在测试保护下消除重复、收紧职责和命名。
4. `VERIFY`：运行任务级测试、阶段回归和必要的静态/构建检查。

禁止：

- 先实现整阶段，再集中补测试。
- 用人工查看日志代替结构化断言。
- 为让候选通过而修改已经冻结的 D0 golden。
- 在错误路径测试中接受“崩溃也算拒绝”。
- 只验证 Host 成功而跳过 Android ABI、链接或发布边界。

每个任务的最小完成证据统一包括：

- RED 阶段的失败测试名称和失败原因。
- GREEN 后可直接复跑的任务级命令与通过结果。
- REFACTOR 后的阶段回归结果。
- 涉及 golden、fixture、ABI、性能或 ELF 时，对应产物路径和摘要。

任务正文另列“完成证据”时，以更具体要求为准；未单列时也不能省略上述四项。

### 2.4 Characterization 例外

RF0 允许先记录当前行为再冻结 characterization test，因为它描述的是已经存在的 `ParseRulesIni()`、`PolicyDocument`、`EncodePolicy()`、daemon 编译和发布行为。

该例外只用于防止重构期间无意改变现有可执行语义，不允许把明显错误的旧行为永久定为新格式规范。每个 characterization 必须标记为：

- `preserve`：新编译链必须保持。
- `replace`：新设计明确替换，测试在对应切换任务完成后删除或改写。
- `compile-gated`：当前 executor 未完成，继续在编译期拒绝。

### 2.5 测试层次

| 层次 | 目标 | 默认位置 |
|---|---|---|
| 纯单元测试 | span、词法、状态机、路径、冲突、canonicalization | `tests/unit` 或选定 Rust crate 的 `src/*_test.rs` / `tests` |
| Golden | generated span、诊断、Canonical Policy、207-byte blob | `tests/golden/rules` |
| Fixture | TOML 1.0 字符串、注释、数组和非法编码素材 | `tests/fixtures/toml-test-v2.2.0` |
| 属性测试 | 恒等、字节保持、一一绑定、确定性 | Rust `proptest` 或 C++ 等价属性 harness |
| Fuzz | scanner、frame、RewriteMap、完整编译入口 | `fuzz` 或选定工具目录 |
| Host 集成 | CLI、daemon reconcile、发布失败、旧快照保留 | `tests/integration` |
| Android 集成 | C ABI、静态链接、保存方式、性能和发布 | `tests/device` |
| 架构测试 | Zygisk 零 parser/Rust/compiler 依赖 | 构建脚本与 ELF/link-map 检查 |

不得把所有断言堆入单个巨型测试程序。测试文件按职责拆分，但不为每个 production 函数建立一个文件。

### 2.6 测试命名与可重复性

- 测试名采用 `Given_When_Then` 或清晰的行为描述。
- 属性测试和 fuzz 回归必须记录固定 seed；失败输入保存为最小 corpus。
- Golden 更新必须说明规范变化，禁止以“实现输出变了”为理由直接覆盖。
- 性能测试记录设备、ABI、API、NDK、编译器、parser、构建模式、输入摘要和预热策略。
- 时间预算只使用 release/optimized 构建数据；debug 数据只用于诊断。

### 2.7 现有验证命令

当前 Host 基线：

```powershell
cmake -S . -B build -DPATHGUARD_BUILD_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

当前 Native 基线：

```powershell
./scripts/build-native.ps1 -Abi arm64-v8a
```

RF1 选定 C++20 + toml++ 后的 D0/隔离验证：

```powershell
./scripts/build-rules-compiler-d0.ps1
```

Rust 候选的历史命令和原始测量只保留在 D0 决议与结果记录中；生产工作区不保留不可执行的 Cargo 入口。后续 CTest 名称和 fuzz 命令在对应任务创建后必须回填本节。

---

## 3. 范围与非范围

### 3.1 本清单覆盖

- `rules.toml format = 1` 的格式冻结和唯一事实来源。
- 箭头是否保留的最终投入产出决策。
- Rust/`toml_edit`、C++/toml++、严格 TOML 三选一。
- `RulesFormatProbe`、流式字符串边界扫描、浅层容器 frame、局部数组元素校验。
- 全有或全无 rewrite、`RewriteMap`、`GeneratedRedirect`。
- 唯一 strict TOML parser、generated node 一一绑定和 scope 校验。
- `RulesDocument -> ResolvedPolicy -> CanonicalPolicy + PolicyRequirements -> PolicyBlob`。
- `PolicyAdmission`、`PolicyEncoder`、`PolicyVerifier` 和现有 C++ `DecodePolicy()` 独立验证。
- Rust 被选中时的窄 C ABI、cargo-ndk、Android.mk 和 Zygisk 零依赖证明。
- daemon/CLI/module 配置切换、原子发布、状态与结构化诊断。
- 删除 `ParseRulesIni()` 和运行时 `rules.ini` 双格式分支。

### 3.2 本清单不覆盖

- 重写 daemon、CLI 或 companion 为 Rust。
- 把 Rust 链接进 `libpathguard_zygisk.so`。
- Manager UI 的完整实现。
- 通用 TOML formatter、完整无损 CST 或通用 token 编辑器。
- `redirect_rules` 高级属性、条件、优先级、通配符或新箭头运算符。
- deny、isolate、event 等尚无 executor 能力的解禁；它们继续遵守 compile gate。
- policy format v6、trie、增量 AST、跨编译缓存或并行模块调度。

---

## 4. 当前项目基线

本节是实施输入，不代表最终架构。

### 4.1 已验证事实

| 项目 | 当前状态 |
|---|---|
| Host 构建 | CMake + C++20 |
| Host 测试 | 2026-07-26 RF1 Release 19/19 通过；原始 10/10 基线由守卫持续保护 |
| 规则源 | `module/config/rules.ini` |
| 解析入口 | `core/src/policy.cpp::ParseRulesIni()` |
| 源模型 | `PolicyDocument` 同时承载源语义、源码行号和二进制输入 |
| daemon 编译 | `CompileText()` 内串联 parse、validate、encode、DecodePolicy 和写文件 |
| daemon 发布 | 固定 `policy.bin.tmp`，尚未满足 04 文档完整发布事务 |
| 变化检测 | 原始文本比较 + 150ms sleep；已有目录 inotify，但尚未形成单 worker reconcile 状态机 |
| CLI | 只接受 `rules.ini`，`validate/compile` 直接调用旧解析器 |
| 二进制 | 唯一 `policy.bin format v5` |
| Golden | `tests/unit/binary_test.cpp` 固定 207 bytes |
| 当前研发阶段 | Redirect R1，Host 主干存在，真机矩阵尚未完成 |

### 4.2 已具备工具链

| 工具 | 当前值 |
|---|---|
| Rust | `rustc/cargo 1.97.1` |
| cargo-ndk | `4.1.2` |
| Android Rust targets | 已安装 `aarch64-linux-android`、`armv7-linux-androideabi` 等 |
| NDK | r27d，`27.3.13750724` |
| Native API | 31 |
| 首个 Android 目标 | `arm64-v8a` |

`ANDROID_NDK_HOME` 和 `ANDROID_NDK_ROOT` 当前未设置；现有脚本使用 `C:/A_Softwares/android-ndk-r27d/ndk-build.cmd` fallback。D0 构建必须打印并校验 NDK revision，避免 cargo-ndk 选择另一套 SDK NDK。

### 4.3 当前技术债务必须显式替换

- `ParseRulesIni()` 通过行扫描、前缀判断和 `SplitArrow()` 解析混合 DSL。
- `PolicyDocument` 不是 04 文档要求的分层模型。
- daemon 通过固定临时文件名发布，不具备完整 owner/mode/context、file fsync、directory fsync 和恢复契约。
- CLI、daemon、模块脚本和测试仍引用 `rules.ini`。
- 活动策略发布尚未由稳定结构化诊断和固定状态文件统一描述。
- 当前 native 构建把 `CORE_SOURCES` 直接编入 daemon/CLI；规则编译器尚无独立依赖边界。

---

## 5. 冻结决策与条件分支

### 5.1 D0 只能输出三种结果

1. Rust + `toml_edit 0.23.7` 完整规则编译器。
2. C++ + toml++ v3.4.0 完整规则编译器。
3. 两者或箭头投入产出不满足门槛，退回严格 TOML。

禁止：

- Rust 只做脱糖器、C++ 再 parse。
- Host 与 Android 使用不同 parser。
- daemon 与 CLI 使用不同 parser。
- 长期保留两个完整编译器作为 fallback。
- 正常路径依赖隐藏字段但不触发架构复审。

### 5.2 条件任务规则

- D0 选择 Rust：执行 RF6 的 Rust C ABI 任务，删除 C++ parser 原型。
- D0 选择 C++：跳过 Rust C ABI 生产任务，删除 Rust crate 原型和 lockfile artifact。
- D0 选择严格 TOML：RF2/RF3 的箭头生产任务标记为“不适用”，RF4 从原始 strict TOML 直接进入 parser；配置重构、语义编译、发布和迁移任务继续执行。
- 任一分支都必须保留同一 `RulesDocument`、Canonical Policy、207-byte golden 和 Zygisk 二进制读取契约。

### 5.3 Rust 候选冻结项

```toml
toml_edit = { version = "=0.23.7", default-features = false, features = ["parse"] }
toml_parser = "=1.0.4"
```

- 所有 Cargo build/test/ndk 使用 `--locked`。
- generated binding 前使用不可变 `toml_edit::Document::parse(...)`。
- 禁止在 binding/scope 前调用 `into_mut()`，因为它会 `despan()`。
- 控制面 compiler 使用 `panic = "unwind"` 和 `overflow-checks = true`。
- PathGuard 自有 scanner/rewrite/encoder 禁止 `unsafe`。

### 5.4 C++ 候选冻结项

- toml++ v3.4.0。
- `TOML_EXCEPTIONS=0`。
- `TOML_ENABLE_FORMATTERS=0`。
- `TOML_ENABLE_UNRELEASED_FEATURES=0`。
- 隐藏字段只允许作为 D0 受控原型，不是默认生产方案。

---

## 6. 总体依赖图

```text
RF0 当前行为与测试基线
 |
 v
RF1 D0：箭头价值 + Rust/C++/严格 TOML 联合决策
 |
 +-- strict TOML -----------------------------------+
 |                                                  |
 +-- arrow Go --> RF2 词法与局部上下文 --> RF3 Rewrite/SourceMap
                                                    |
                                                    v
RF4 唯一 parser、generated binding、scope、RulesDocument decode
 |
 v
RF5 normalize、validate、canonicalize、admission、encode、verify
 |
 v
RF6 唯一语言构建边界与 Android/Zygisk 隔离
 |
 v
RF7 daemon、CLI、发布事务和 rules.toml 迁移
 |
 v
RF8 全量健壮性、性能、真机和删除旧路径
 |
 +--> RF9 P1 工具与 Manager 接入
```

只有当前阶段闸门通过，才能解锁其下游阶段。RF2 与 RF3 不得在 RF1 决议前作为生产代码落地。

---

## 7. 全局 Definition of Done

- [x] 运行时不再读取 `rules.ini`，`ParseRulesIni()` 已删除。
- [x] D0 有可复跑报告，并冻结唯一语言、parser、TOML 版本和箭头决策。
- [x] 没有 Rust desugarer + C++ AST 的中间 FFI。
- [x] 若保留箭头，字符串和注释内 `->` 永不误判，局部错误先于 TOML parse。
- [x] 若保留箭头，generated node 与 `GeneratedRedirect` 双向一一消费。
- [x] 若保留箭头，手写 `{ from, to }` 按 format 1 决策稳定拒绝；若选择严格 TOML，则只接受 D0 冻结的那一种标准语法。
- [x] 所有诊断落在原始 `rules.toml` byte span，并可渲染 Unicode code point 列。
- [x] TOML 1.0 接受/拒绝边界被锁定，format 1 不因依赖升级接受 TOML 1.1-only 语法。
- [x] 等价源文本生成相同 Canonical Policy 和 content generation。
- [x] 固定输入生成完全相同的 207-byte `policy.bin`。
- [x] 生产发布前由现有 C++ `DecodePolicy()` 独立验证候选字节。
- [x] 编译、准入、编码或发布失败均保留上一份有效策略。
- [x] `source_digest`、`candidate_sequence`、`content_generation`、`deployment_epoch`、capability/topology generation 含义不混用。
- [x] 所有阶段共享一份冻结的 `RulesLimits`。
- [x] parser、Rust runtime（如采用）、编译器和诊断代码没有链接进 Zygisk ELF。
- [x] Host 单元、golden、属性、fuzz 回归、集成和 Android arm64 验证全部通过。
- [x] 典型/大型/极限输入满足冻结的 CPU 与峰值内存预算。
- [x] 注释或空白变化不重写 `policy.bin`、不触发运行时 reload。
- [x] 未提前实现 formatter、完整 CST、高级 redirect、通用 DSL 或多 crate workspace。

---

## 8. RF0 阶段：当前行为与测试基础设施

### RF0-01 `[x] P0` 固化 Host 构建与测试基线

**依赖**

- 无。

**RED**

- [x] 增加一个基线检查，能在测试数量减少、`pathguard_binary_test` 缺失或 Release 测试未运行时失败。
- [x] 记录当前 10 个 CTest 名称及结果；证明检查在故意排除一个测试时会失败。

**GREEN**

- [x] 建立 `tests/baseline/host-tests.md` 或机器可读等价记录。
- [x] 固定现有 CMake/CTest 命令，不改变生产代码。

**REFACTOR / VERIFY**

- [x] 执行 Release 构建和全量 CTest。
- [x] 保存执行日期、编译器和命令，不把本机绝对路径写入 golden。

**完成证据**

- [x] 10/10 Host 测试通过的日志。
- [x] 基线检查能发现测试目标丢失。

### RF0-02 `[x] P0` 建立旧规则 parser characterization

**依赖**

- RF0-01。

**RED**

- [x] 为当前有效样例、非法 schema、failure closed、绝对路径、legacy 布尔、provider gate 建立独立 fixture。
- [x] 每个 fixture 标记 `preserve`、`replace` 或 `compile-gated`。
- [x] 先让 fixture harness 因未登记期望而失败。

**GREEN**

- [x] 复用当前 `ParseRulesIni()` 运行 fixture，不修改旧 parser 语义。
- [x] 记录旧 `PolicyDocument` 输出、错误行和二进制结果。

**REFACTOR / VERIFY**

- [x] 将 `tests/unit/policy_test.cpp` 中的大字符串拆为可复用 fixture 或 helper。
- [x] 不把旧文本错误消息当成未来稳定协议；只冻结必要的语义和错误位置。

**完成证据**

- [x] 旧格式到预期语义的 characterization 表。
- [x] 明确列出 format 1 不再支持的 `{user}`、`{package}`、isolate/event 旧写法。

### RF0-03 `[x] P0` 冻结 policy format v5 与 207-byte golden

**依赖**

- RF0-01。

**RED**

- [x] 新增测试，若文件大小不是 207、checksum/generation/排序任一变化则失败。
- [x] 新增 C++ reader 对 golden 中每个字段的逐项断言，而不只逐字节比较。

**GREEN**

- [x] 复用 `tests/unit/binary_test.cpp` 和 ADR-0002 常量。
- [x] 提取可供未来 Rust vertical slice 复用的固定语义输入描述。

**REFACTOR / VERIFY**

- [x] Golden 只保留一个事实来源，避免 ADR、C++ 测试和未来 Rust 测试手工维护三份不一致字节。
- [x] `DecodePolicy()` 仍是 Zygisk 字节契约的独立 C++ verifier。

**完成证据**

- [x] 207-byte、content generation `11078014328063549684`、plan generation `5918468725002442624`、checksum `484501896` 全部通过。

### RF0-04 `[x] P0` Characterize daemon/CLI 当前编译与发布行为

**依赖**

- RF0-01、RF0-02、RF0-03。

**RED**

- [x] Host 集成测试覆盖：成功编译、非法配置、输出未变化、DecodePolicy 自检失败、临时文件替换失败。
- [x] 测试明确暴露当前固定 `policy.bin.tmp`、150ms sleep、文本比较和 `rules.ini` 路径。

**GREEN**

- [x] 只增加测试缝或最小文件系统 adapter，不提前实现新 Publisher/Reconciler。
- [x] 记录哪些行为必须被 RF7 替换。

**REFACTOR / VERIFY**

- [x] 不为测试引入全局可变单例。
- [x] 文件系统、时钟和事件源只抽象到当前测试所需最小边界。

**完成证据**

- [x] 当前 daemon/CLI 行为矩阵。
- [x] 所有待替换行为都有对应 RF7 任务引用。

### RF0-05 `[x] P0` 建立新编译链测试目录、fixture 规范和 seed 管理

**依赖**

- RF0-01。

**RED**

- [x] 检查缺少 fixture provenance、license、manifest 或 seed 时失败。
- [x] 检查 golden 中出现生成文本内部 `{ from, to }` 诊断时失败。

**GREEN**

- [x] 创建建议目录：

```text
tests/
  fixtures/toml-test-v2.2.0/
  golden/rules/
  integration/rules/
  device/rules/
```

- [x] 定义 golden 元数据：输入 SHA-256、TOML 版本、parser/backend、原始 byte span、错误码。
- [x] 定义 fuzz regression corpus 的命名和 seed 保存方式。

**REFACTOR / VERIFY**

- [x] 测试数据与生产代码分离。
- [x] 不复制无关参考项目源码。

### RF0 阶段闸门

- [x] Release Host 10/10 基线仍通过。
- [x] 旧规则语义、207-byte golden、daemon/CLI 当前行为均有 characterization。
- [x] 新测试目录和 fixture/golden 规范可被 CI 检查。
- [x] 尚未引入生产 TOML parser 或箭头实现。

### RF0 验收记录（2026-07-26）

- RED 证据：在测试目标和 CMake 接线先落地、实现尚不存在时，配置阶段按预期因缺少 `daemon/src/legacy_rules_control.cpp` 失败；基线守卫和资产守卫也分别以注入缺失测试、缺失资产及非法诊断 golden 自证失败能力。
- GREEN 证据：Release Host 全量 CTest 为 15/15；其中 RF0 前的原始 10/10 基线由 `pathguard_host_baseline_guard` 持续保护。
- policy v5 证据：207 bytes、content generation `11078014328063549684`、plan generation `5918468725002442624`、checksum `484501896` 和完整字节 golden 全部通过。
- Android 证据：`./scripts/build-native.ps1 -Abi arm64-v8a` 构建通过，产出 `pathguardd`、`pathguardctl` 和 `libpathguard_zygisk.so`。
- 范围审计：仅新增旧行为 characterization、测试缝和测试资产契约；未引入 TOML parser、Rust crate 或生产箭头脱糖实现，未开始 RF1。

---

## 9. RF1 阶段：D0 联合 Go/No-Go 与格式冻结

### RF1-01 `[x] P0` 冻结 D0 外部断言和严格 TOML 对照组

**依赖**

- RF0 阶段闸门。

**RED**

- [x] 先建立候选无关的测试接口，缺少任一候选输出时测试失败。
- [x] 同一语料同时描述箭头、标准 inline table、标准映射子表三种用户方案的可读性和实现成本。
- [x] 决策模板缺少 kill criterion、资源数据或删除计划时检查失败。

**GREEN**

- [x] 冻结 D0 只比较完整规则编译边界，不只比较 parser API。
- [x] 冻结三种唯一结论及条件分支。
- [x] 冻结 binder-neutral 结果结构：AST node span、value span、parse error 原始 span、scope 结果、PolicyBlob。

**REFACTOR / VERIFY**

- [x] 所有候选共享同一断言，不根据结果调整 golden。
- [x] 严格 TOML 是退出路径，不作为同 format 并存语法。

### RF1-02 `[x] P0` 导入并冻结 toml-test v2.2.0 的 TOML 1.0 fixture 子集

**依赖**

- RF0-05、RF1-01。

**RED**

- [x] 测试证明直接扫描整个 `refer/toml-test-main/tests` 会错误包含 TOML 1.1-only 用例。
- [x] 测试在 fixture 不属于 `tests/files-toml-1.0.0` 时失败。
- [x] 测试在上游版本、许可证或来源摘要缺失时失败。

**GREEN**

- [x] 明确使用 `toml-test copy -toml=1.0` 或本地 `tests/files-toml-1.0.0` 过滤。
- [x] 第一批至少选择：
  - `valid/string/*.toml`；
  - `invalid/string/*.toml`；
  - `invalid/encoding/*.toml`；
  - `valid/comment/*.toml`；
  - `valid/array/*.toml` 与 `invalid/array/*.toml` 中和 frame/分隔符有关的用例；
  - UTF-8 BOM、LF、CRLF 代表用例。
- [x] 复制 MIT LICENSE、上游版本 `v2.2.0`、来源路径和筛选脚本/manifest。

**REFACTOR / VERIFY**

- [x] 不接入 Go tagged-JSON 协议，不为 PathGuard 重测 `toml_edit` 全部语义。
- [x] parser 版本门只保留少量 TOML 1.0/1.1 边界用例；scanner fixture 专注不越界、边界稳定和不误判。
- [x] invalid fixture 的成功条件是安全终止和正确失败归属，不是 scanner 必须替代 strict parser 给出完整 TOML 错误。

**完成证据**

- [x] 生成后的 fixture manifest 只包含 TOML 1.0 清单成员。
- [x] fixture 数量和 SHA-256 可重复生成。

### RF1-03 `[x] P0` 冻结 binder-neutral source-map/generated-node golden

**依赖**

- RF1-01、RF1-02。

**RED**

- [x] 覆盖 ASCII、Unicode、BOM、LF、CRLF、同行/跨行箭头、多次 rewrite。
- [x] 覆盖 parse error 落在 copied、synthetic-rule、synthetic-arrow 三类 segment。
- [x] 覆盖合法 scope、错误 scope、手写 inline table 和 generated node 重复/缺失。
- [x] 任一 candidate adapter 返回不同原始 byte span 时测试失败。

**GREEN**

- [x] Golden 只记录外部结果，不记录 toml++ 或 `toml_edit` 私有对象布局。
- [x] 每条记录固定原始/生成 table、source、target span 和期望错误码。

**REFACTOR / VERIFY**

- [x] 语料不可在候选结果已知后分叉。
- [x] 默认诊断不得展示内部 `from`、`to` 合成字段。

### RF1-04 `[x] P0` 实现最小 C++/toml++ v3.4.0 adapter spike

**依赖**

- RF1-03。

**RED**

- [x] 在未实现 adapter 时 binder-neutral tests 失败。
- [x] 添加 Unicode/CRLF/BOM、无异常 parse error、table/value source region 测试。
- [x] 添加 TOML 1.1-only 输入拒绝测试。

**GREEN**

- [x] 仅实现 D0 所需 parse/bind adapter。
- [x] 使用冻结宏关闭异常、formatter 和 unreleased features。
- [x] 若 source region 不足，只实现一次受控隐藏标记原型。（source region 充足，未启用 fallback）

**REFACTOR / VERIFY**

- [x] 隐藏标记原型使用相同 golden，不建立第二套宽松断言。（未触发）
- [x] 记录 toml++ 头文件 SHA-256、许可证、Host/NDK 构建方式。
- [x] 不把 adapter 接入生产 daemon。

### RF1-05 `[x] P0` 实现最小 Rust/`toml_edit` adapter spike

**依赖**

- RF1-03。

**RED**

- [x] `Document::parse` 的 table/value/error span 未映射到 golden 时失败。
- [x] 添加 `DocumentMut`/`into_mut()` 清除 span 的回归测试。
- [x] 添加 Cargo.lock 变化导致 TOML 1.1-only 输入被接受时失败的版本门。

**GREEN**

- [x] 创建隔离 D0 manifest，固定 `toml_edit = 0.23.7` 和 `toml_parser = 1.0.4`。
- [x] 使用不可变 `Document` 完成 parse、node span 和最小 scope 遍历。
- [x] 所有命令使用 `--locked`。

**REFACTOR / VERIFY**

- [x] 记录实际依赖树、许可证、Rust MSRV 和 crate 体积。
- [x] 不增加隐藏字段、`DocumentMut`、自建 CST 或中间 FFI。
- [x] D0 artifact 尚不提升为生产 workspace，并在未选中后删除。

### RF1-06 `[x] P0` 建立两候选完整编译 vertical slice

**依赖**

- RF0-03、RF1-05。

**RED**

- [x] Rust 或 C++ 任一候选不能从各自的 TOML 输入生成 207-byte blob 时失败。
- [x] 逐字节差异、checksum/generation/排序差异任一导致失败。
- [x] 现有 C++ `DecodePolicy()` 无法读取任一候选输出时失败。
- [x] 严格 TOML 对照输入不能生成同一语义 blob 时失败。

**GREEN**

- [x] 分别实现最小 C++ 和 Rust `source -> parse -> decode -> canonicalize -> encode -> verify` vertical slice。
- [x] 两个候选只实现 golden 所需字段，不提前实现全部模块。
- [x] C++ 候选可复用现有 format v5 编码器；Rust 候选实现独立最小 encoder。
- [x] 两个输出都交给现有 C++ reader 独立验证。

**REFACTOR / VERIFY**

- [x] 编码常量来自冻结 policy format 契约，不复制魔法 offset。
- [x] 比较完整编译路径，不用 C++ parser-only 对比 Rust end-to-end。
- [x] Rust encoder 自检与 C++ reader 验证职责分离。

### RF1-07 `[x] P0` 建立最小 C ABI harness

**依赖**

- RF1-06。

**RED**

- [x] 覆盖 null/length 组合、非法 UTF-8、超限输入、未知 ABI version、未知枚举。
- [x] 覆盖 opaque result accessor、null free、唯一释放和重复调用。
- [x] 注入 unwind panic，要求返回 `PG-COMPILER-INTERNAL` 且无 policy bytes。
- [x] C/Rust `sizeof`、`alignof`、`offsetof` 或枚举宽度不一致时失败。

**GREEN**

- [x] 手写极窄版本化 C 头。
- [x] 每个 `extern "C"` 入口内部 `catch_unwind`。
- [x] Rust 分配只由 `pg_rules_result_free()` 释放。

**REFACTOR / VERIFY**

- [x] ABI 不暴露 AST、RewriteMap、RulesDocument、Canonical Policy、Rust String/Vec 或回调。
- [x] 不为 double-free 建立全局 handle registry。
- [x] 明确记录 `catch_unwind` 无法处理 OOM abort、stack overflow、signal 和 `panic=abort`。

### RF1-08 `[x] P0` 验证 Host/Android arm64 构建与 Zygisk 隔离

**依赖**

- RF1-04、RF1-05；Rust ABI 测量还依赖 RF1-07。

**RED**

- [x] 构建脚本选择非 NDK r27d 或 API 31 时失败。
- [x] `pathguard_zygisk` ELF/link map 出现 parser、Rust runtime 或 compiler symbol 时失败。
- [x] candidate 无法生成 Android arm64 release artifact 时失败。

**GREEN**

- [x] 两候选使用同一 NDK revision、ABI、优化级别和输入。
- [x] 构建日志打印 rustc/clang/parser/cargo-ndk/NDK 版本。
- [x] Rust candidate 生成最小 staticlib；C++ candidate 生成等价控制面目标。

**REFACTOR / VERIFY**

- [x] 不修改 Zygisk 生产依赖图来迁就 candidate。
- [x] 不在 D0 默认构建 armv7/x86，先完成 arm64 证据。

### RF1-09 `[x] P0` 运行候选资源与性能对比

**依赖**

- RF1-04、RF1-05、RF1-06、RF1-08。

**RED**

- [x] 报告缺少完整编译时间、峰值内存、stripped 增量体积或构建时间时失败。
- [x] parser-only 与端到端数据混用时报告无效。

**GREEN**

- [x] 使用同一典型、大型、极限输入、预热策略和 release 构建。
- [x] 分别记录 parser adapter 和完整 vertical slice。
- [x] 记录 `toml_edit` trivia/span 共存峰值和 C++ source-region 转换成本。

**REFACTOR / VERIFY**

- [x] CPU 编译预算与 fsync 发布预算分开。
- [x] 不预设 Rust 或 C++ 必然更快。

### RF1-10 `[x] P0` 冻结 RulesLimits、错误码和唯一 D0 决议

**依赖**

- RF1-01～RF1-09。

**RED**

- [x] 对每个 limit 写 boundary-1、boundary、boundary+1 测试；未冻结具体数值时测试配置失败。
- [x] 决议缺少未选实现删除清单、严格 TOML比较或 kill criterion 结论时失败。

**GREEN**

- [x] 冻结至少：源字节、嵌套深度、token/节点、应用数、每应用规则数、展开后规则数、路径字节/组件、rewrite/segment、诊断/关联位置、生成文本。
- [x] 冻结 `PG-ARROW-*`、`PG-RULE-ARROW-SCOPE`、`PG-REDIRECT-SYNTAX`、`PG-DESUGAR-INTERNAL`、`PG-COMPILER-INTERNAL` 和资源超限错误码。
- [x] 输出三选一决议。

**REFACTOR / VERIFY**

- [x] 若选择 Rust，删除 C++ parser/fallback 原型并提升单一 compiler crate。（未选择 Rust）
- [x] 若选择 C++，删除 Rust D0 artifact，不引入 Rust 生产工具链。
- [x] 若选择严格 TOML，删除两个箭头 adapter 原型并更新 format 1 语法。（未选择严格 TOML）
- [x] 不保留“以后也许用”的未选 parser 包装层。

**完成证据**

- [x] `docs/decisions/rules-compiler-d0.md` 或等价决议文件。
- [x] 完整命令、版本、输入摘要、原始测量和 Go/No-Go 结论。

### RF1 阶段闸门

- [x] D0 外部断言、toml-test TOML 1.0 fixture 和 binder-neutral golden 已冻结。
- [x] C++、Rust 和严格 TOML 在同一标准下完成比较。
- [x] 已冻结唯一生产语言/parser/语法。
- [x] 未选 adapter、parser、fallback 和构建 artifact 已删除。
- [x] RulesLimits 与错误码有边界测试。
- [x] 未通过时 RF2/RF3 不开始；严格 TOML 分支直接转 RF4。（本次通过，后续按顺序进入 RF2）

### RF1 验收记录（2026-07-26）

- RED：C++ adapter 初始因未完成 parse overload/BOM 处理而编译失败；TOML fixture 守卫拒绝注入的 1.1-only inline-table newline；D0 守卫分别拒绝缺失候选报告、缺少 kill criterion 和残留 Rust artifact；Android 构建脚本拒绝 API 30。
- 共同语料：137 个 TOML 1.0 fixture（39 valid、98 invalid），6 组 source-map 文件、7 个 generated redirect、3 类 parse error segment 和 5 类 generated binding/scope 场景。
- 正确性：C++ 与 Rust vertical slice 均生成完全相同的 207-byte policy v5，且通过现有 C++ `DecodePolicy()`；严格 inline table 和映射子表得到同一 blob。
- 资源结论：C++ 在 4,096 条规则下完整编译中位数 5,167 µs、峰值 working set 17,076,224 B、Android stripped 样本 279,016 B；Rust 分别为 6,960 µs、24,608,768 B、1,504,288 B。
- 唯一决议：选择 C++20 + toml++ v3.4.0 + TOML 1.0 + 箭头语法；不需要隐藏标记。Rust 候选虽通过正确性验证，但因第二工具链、C ABI 和资源成本未选择，原型已删除。
- 隔离：NDK r27d（27.3.13750724）、API 31、arm64-v8a 构建通过；Zygisk symbol/string 扫描未发现 parser、Rust runtime 或 rules compiler 依赖。
- 回归：Release Host 全量 CTest 19/19；`build-native.ps1 -Abi arm64-v8a` 与 RF1 D0 Android 构建均通过。
- 范围：尚未实现 RF2 流式扫描器或 RF3 rewrite，不把 D0 adapter 接入 daemon。

---

## 10. RF2 阶段：Source、Format 与流式词法状态机

> 本阶段仅在 RF1 决定保留箭头时执行。

### RF2-01 `[x] P0` 实现 `SourceBuffer`、`ByteSpan` 与 `LineIndex`

**依赖**

- RF1 阶段闸门。

**RED**

- [x] 覆盖空文件、ASCII、Unicode、LF、CRLF、BOM、EOF 和越界 span。
- [x] byte offset 到 1-based 行/Unicode code point 列映射错误时失败。
- [x] 任一 span 超出 source 时失败而不是截断。

**GREEN**

- [x] 内部统一 UTF-8 byte half-open span。
- [x] JSON 诊断保留 byte offset；CLI 显示 code point 列。
- [x] 使用 checked arithmetic 和冻结 source size 上限。

**REFACTOR / VERIFY**

- [x] LSP UTF-16 转换不进入核心类型。
- [x] SourceBuffer 生命周期覆盖 parse/decode，不复制无关文本。

### RF2-02 `[x] P0` 实现 `RulesFormatProbe`

**依赖**

- RF2-01。

**RED**

- [x] 覆盖 BOM、前导空白/注释、`format = 1`、缺失、非 bare key、非十进制、未知版本。
- [x] `format` 不是首个有效声明时失败。
- [x] 后续重复 format 留给 strict parser/decoder 的测试。

**GREEN**

- [x] 只识别第一个有效声明，不扫描和解释其他字段。
- [x] 在主箭头扫描前完成版本选择。

**REFACTOR / VERIFY**

- [x] 不建立 key parser 或 dotted-key 逻辑。
- [x] 每个未来 format 版本拥有显式入口，不猜测语法。

### RF2-03 `[x] P0` 实现 TOML 1.0 单行字符串边界

**依赖**

- RF2-01。

**RED**

- [x] 基本字符串覆盖 `\"`、连续反斜杠奇偶性、字符串内 `->` 和 `#`。
- [x] 字面量字符串覆盖单引号、反斜杠普通字节和字符串内箭头。
- [x] 未终止字符串报告 `PG-ARROW-STRING-BOUNDARY`。

**GREEN**

- [x] scanner 只识别边界，不解码 escape。
- [x] 只有 Normal 状态中的连续 ASCII `-` `>` 产生 Arrow token。

**REFACTOR / VERIFY**

- [x] 边界逻辑不依赖正则表达式。
- [x] 不将非法 escape 抢先解释为 PathGuard 语义错误。

### RF2-04 `[x] P0` 实现 TOML 1.0 多行字符串与注释边界

**依赖**

- RF2-03。

**RED**

- [x] 覆盖三/四/五连续引号、multiline basic escape、multiline literal、CRLF。
- [x] 多行字符串中的 `->`、`#`、括号不影响 outer state。
- [x] 注释到 LF、CRLF 和 EOF 的边界稳定。

**GREEN**

- [x] 按 TOML 1.0 delimiter 规则实现有限状态机。
- [x] 多行字符串可存在于普通 TOML，但不得作为 redirect operand。

**REFACTOR / VERIFY**

- [x] 不使用递归。
- [x] 状态机对每个输入字节最多进行有界工作。

### RF2-05 `[x] P0` 接入 toml-test 字符串/注释/encoding fixture

**依赖**

- RF1-02、RF2-03、RF2-04。

**RED**

- [x] fixture harness 在扫描器越界、死循环、未消费输入或产生无界诊断时失败。
- [x] valid string/comment fixture 中的字符串内部 sentinel arrow 被误判时失败。
- [x] invalid encoding fixture 产生 policy candidate 时失败。

**GREEN**

- [x] 遍历 manifest 选中的 raw TOML 文件。
- [x] valid fixture 验证安全完成和边界稳定；invalid fixture 验证安全拒绝或交给 parser。

**REFACTOR / VERIFY**

- [x] 不解析配套 tagged JSON。
- [x] 不把 toml-test 未覆盖的嵌套深度/资源上限责任推给上游套件。

### RF2-06 `[x] P0` 实现显著 token 摘要与浅层容器 frame

**依赖**

- RF2-04。

**RED**

- [x] 覆盖 value array、嵌套 array、inline table、table header、array-of-tables header 和 unknown bracket。
- [x] 表头的 `[`、inline table 的逗号不能冒充 array element delimiter。
- [x] 超过最大深度时稳定报资源错误。

**GREEN**

- [x] 只维护 `at_statement_start`、`expecting_value`、有界 frame stack 和递增 frame id。
- [x] 无法可靠分类的 bracket 使用 unknown frame，并禁止箭头转换。

**REFACTOR / VERIFY**

- [x] 不保存完整 TokenStream。
- [x] 不解析 table key、table path 或 dotted key。

### RF2-07 `[x] P0` 实现流式箭头候选状态机

**依赖**

- RF2-03、RF2-06。

**RED**

- [x] 合法 basic/literal、无空格、多空格、跨行空白、尾逗号。
- [x] 普通键值、inline table 字段、table header 报 `PG-ARROW-CONTEXT`。
- [x] 缺左右 operand 报 `PG-ARROW-OPERAND`。
- [x] 内部注释报 `PG-ARROW-COMMENT-INSIDE`。
- [x] 链式箭头报 `PG-ARROW-CHAINED`。
- [x] 连续元素缺逗号报 `PG-ARROW-MISSING-COMMA`。

**GREEN**

- [x] 任一时刻最多一个 pending candidate。
- [x] 左右 delimiter 必须属于同一 value-array frame。
- [x] 右 operand 后 EOF 且 array 未闭合时允许候选完成，由 strict parser 报缺 `]`。

**REFACTOR / VERIFY**

- [x] 同一字符串不能参与两个箭头。
- [x] 同一根因只产生最早、最具体的箭头错误。

### RF2-08 `[x] P0` 实现 all-or-nothing 错误策略与有界诊断

**依赖**

- RF2-07、RF1-10。

**RED**

- [x] 任一 `PG-ARROW-*` 出现时 strict parser mock 不得被调用。
- [x] 多个独立错误只收集到 max diagnostics，并追加 omitted 诊断。
- [x] error 输出不得携带可发布 RulesDocument 或 PolicyBlob。

**GREEN**

- [x] 扫描可继续收集有界独立错误，但不应用任何 rewrite。
- [x] 统一结构化 Diagnostic，不在 scanner 拼多套文本。

**REFACTOR / VERIFY**

- [x] 诊断 span 均位于原始 SourceBuffer。
- [x] 资源超限在最早可判断位置失败。

### RF2-09 `[x] P0` 词法与 frame 状态机属性测试/fuzz

**依赖**

- RF2-05～RF2-08。

**RED**

- [x] seed corpus 至少含 toml-test 选集和所有手写边界。
- [x] 故意注入 offset 溢出、frame 不弹栈或 EOF 不推进时 fuzz 能发现。

**GREEN**

- [x] fuzz string boundary 和 container/candidate 两个目标。
- [x] 不变量：终止、无越界、checked arithmetic、bounded allocation、诊断在源范围内。

**REFACTOR / VERIFY**

- [x] 保存最小回归 corpus 和固定 seed。
- [x] 若选 Rust，项目自有模块保持无 `unsafe`。（RF1 选择 C++，本项不适用）

### RF2 阶段闸门

- [x] FormatProbe 在箭头扫描前稳定选择 format。
- [x] toml-test TOML 1.0 选集通过 scanner 安全性测试。
- [x] string/comment/frame/candidate 单测和 fuzz 通过。
- [x] 没有完整 TokenStream、TOML key parser 或 table-path parser。
- [x] 任一箭头错误都不会进入 strict parser。

### RF2 验收记录（2026-07-27）

- 实施提交/工作区版本：基于 `08321aa` 的未提交 RF2 工作区；未进入 RF3。
- RED：缺少 `SourceBuffer`/scanner 头文件时构建失败；fixture 数量初始断言失败；Windows libFuzzer 首次因 CRT 不一致、再次因 ASan runtime 不可用而链接失败。
- GREEN：新增 `pathguard_rules` 控制面静态库和 6 个 RF2 CTest；Host 全量 Release CTest `25/25` 通过。
- Fixture：`toml-test v2.2.0` TOML 1.0 清单共 137 个输入，其中 valid 39、invalid 98、invalid encoding 15；资产 SHA-256 守卫通过。
- Fuzz：Clang `20.1.7` 下 `pathguard_rules_string_fuzzer`、`pathguard_rules_candidate_fuzzer` 各运行 10,000 次，无崩溃或不变量失败；日常固定 seed smoke 纳入 CTest。
- 固定 seed：`rules-string-fc1e00f7fbc3a5b3.seed`、`rules-candidate-c69f97c7c2b378df.seed`，文件名、manifest 和 SHA-256 一致性由资产守卫验证。
- Android：`./scripts/build-native.ps1 -Abi arm64-v8a` 通过；`pathguardd`、`pathguardctl` 与 `libpathguard_zygisk.so` 均构建成功。
- 架构边界：daemon 和 Zygisk 未链接 `pathguard_rules`；没有完整 TokenStream、TOML key/table-path parser、rewrite、toml++ parser 或发布接入。
- 下一阶段：RF3 仅实现区间 rewrite、`RewriteMap` 与 `GeneratedRedirect`，不得提前进入 RF4 parser/AST decode。

---

## 11. RF3 阶段：Rewrite、RewriteMap 与 GeneratedRedirect

> 本阶段仅在 RF1 决定保留箭头时执行。

### RF3-01 `[x] P0` 实现 `ArrowRewrite` 收集和区间不变量

**依赖**

- RF2 阶段闸门。

**RED**

- [x] rewrite 乱序、重叠、空 operand span、RuleId 重复时失败。
- [x] 超过 rewrite 上限或生成大小溢出时失败。

**GREEN**

- [x] 只保存 rule/source/arrow/target span 和单次编译 RuleId。
- [x] 扫描无错误后按 rule begin 校验。

**REFACTOR / VERIFY**

- [x] RuleId 不做跨编辑持久化或哈希。
- [x] 内部不变量失败统一为 `PG-DESUGAR-INTERNAL`。

### RF3-02 `[x] P0` 实现单次严格 TOML 输出

**依赖**

- RF3-01。

**RED**

- [x] 输出不是 `{ from = raw-source, to = raw-target }` 时失败。
- [x] source/target raw token 字节发生改变时失败。
- [x] 跨行箭头生成多行 inline table 时失败。
- [x] 反复 insert/replace 的性能守卫或代码检查失败。

**GREEN**

- [x] 预计算增量、验证生成大小、一次 reserve、按 offset 单次复制。
- [x] 原始逗号、右括号和元素后注释不进入 rewrite span。

**REFACTOR / VERIFY**

- [x] 总复杂度 O(N + R)。
- [x] 不解码再编码字符串。

### RF3-03 `[x] P0` 实现无箭头零 rewrite 快路径

**依赖**

- RF3-02。

**RED**

- [x] 无外部箭头输入发生 generated buffer、rewrite 或 GeneratedRedirect 分配时失败。
- [x] parser input 不再指向原始 source 时失败。

**GREEN**

- [x] 允许 parser 直接借用 SourceBuffer。
- [x] RewriteMap 使用空/identity 约定。

**REFACTOR / VERIFY**

- [x] 快路径与普通路径共享后续 parser/decode API。
- [x] 不为快路径建立第二套语义编译器。

### RF3-04 `[x] P0` 实现分段 `RewriteMap`

**依赖**

- RF3-02、RF2-01。

**RED**

- [x] copied、synthetic-rule、synthetic-arrow segment 映射均有 boundary 测试。
- [x] segment 非递增、重叠、空洞处理不一致或查询越界时失败。
- [x] Unicode/CRLF/BOM generated position 映射错误时失败。

**GREEN**

- [x] segment 按 generated begin 严格递增。
- [x] 位置查询使用二分查找；copied 区间线性偏移映射。
- [x] 合成前后缀映射整条 rule，`, to =` 映射 arrow。

**REFACTOR / VERIFY**

- [x] 通用 generated-to-original 查询只用于诊断。
- [x] 成功路径测试断言 `MapGeneratedPosition()` 调用次数为 0。

### RF3-05 `[x] P0` 实现 `GeneratedRedirect` 最小来源记录

**依赖**

- RF3-02。

**RED**

- [x] generated table/source/target span 不精确时失败。
- [x] 原始 source/target 路径错误无法分别定位时失败。
- [x] 手写 inline table 被错误标记为 generated 时失败。

**GREEN**

- [x] 保存 RuleId、原始 rule/source/arrow/target 和 generated table/source/target span。
- [x] 不保存普通 TOML token 或 trivia。

**REFACTOR / VERIFY**

- [x] RewriteMap 与 GeneratedRedirect 职责不合并。
- [x] 不使用 AST path 代替来源证明。

### RF3-06 `[x] P0` 建立 source-map golden 与属性测试

**依赖**

- RF3-03～RF3-05。

**RED**

- [x] 覆盖多次 rewrite 后 parser error 位于各 segment 类型。
- [x] 无箭头恒等、operand 字节保持、rewrite 不重叠、生成文本无外部箭头属性。

**GREEN**

- [x] Golden 固定 byte begin/end、行列、错误码、message key 和 related spans。
- [x] 使用 RF1-03 binder-neutral 语料。

**REFACTOR / VERIFY**

- [x] 输出不依赖平台换行转换。
- [x] 错误输出不泄漏 generated source 细节。

### RF3-07 `[x] P0` 完整 desugar 管线 fuzz 与畸形前缀组合

**依赖**

- RF3-06。

**RED**

- [x] 生成 `MalformedTomlPrefix + ValidRedirectSuffix` corpus。
- [x] 故意制造 frame 漂移、重叠 rewrite 或 generated mismatch 时能失败。

**GREEN**

- [x] fuzz `FormatProbe -> lex -> validate -> emit -> source map`。
- [x] 不变量：错误不产生 parser candidate，成功不残留外部 arrow，所有 span 在源范围内。

**REFACTOR / VERIFY**

- [x] 未终止字符串吞没后缀时报告边界错误，不伪造后缀 arrow。
- [x] generated mismatch 终止为内部错误，绝不降级成功。

### RF3 阶段闸门

- [x] all-or-nothing rewrite、零 rewrite 快路径和 O(N + R) emitter 通过。
- [x] RewriteMap/GeneratedRedirect golden 与属性测试通过。
- [x] 畸形前缀组合不会产生静默错误策略。
- [x] 成功路径不调用通用 RewriteMap 查询。

### RF3 验收记录（2026-07-27）

- 实施提交/工作区版本：基于 `72c698d` 的 RF3 工作区；未进入 RF4 parser/AST decode。
- RED：`pathguard_rules_desugarer_test` 首次因缺少 `pathguard/rules/desugarer.h` 构建失败，证明测试先于生产接口。
- GREEN：新增单次 emitter、identity 快路径、`RewriteMap`、`GeneratedRedirect`、2 个单元/golden CTest 和 1 个完整管线 fuzz smoke；Host Release CTest `28/28` 通过。
- Golden：复用 RF1 binder-neutral 的 ASCII、Unicode、CRLF、BOM、跨行和多 rewrite 共 7 行来源记录，以及 copied/synthetic-rule/synthetic-arrow 三类 source-map 错误位置。
- Fuzz：Clang `20.1.7`、libFuzzer seed `2025751881`，`pathguard_rules_desugar_fuzzer` 运行 10,000 次通过；固定 seed SHA-256 为 `3b6c80b98784e2b9a7d42a2500104281ed2a24d512f551efa947013f40af2292`。
- Android：`./scripts/build-native.ps1 -Abi arm64-v8a` 通过。
- 架构边界：无 parser、AST、RulesDocument 或 daemon 接入；成功路径 `RewriteMap` 查询计数为 0；daemon/Zygisk 未链接 `pathguard_rules`。
- 闸门结论：通过，解锁 RF4。

---

## 12. RF4 阶段：唯一 TOML parser、来源绑定、Scope 与 RulesDocument

### RF4-01 `[x] P0` 接入 D0 选定的唯一 strict TOML parser

**依赖**

- RF1 阶段闸门；保留箭头时还依赖 RF3 阶段闸门。

**RED**

- [x] TOML 1.0 valid/reject subset、BOM/CRLF/Unicode、重复键和非法 escape 测试。
- [x] TOML 1.1-only 输入必须拒绝。
- [x] parser error 未映射到原始 source 时失败。

**GREEN**

- [x] Rust 路径使用不可变 `Document`；C++ 路径使用无异常 toml++。（RF1 选择 C++）
- [x] 严格 TOML 分支直接解析原始 SourceBuffer。

**REFACTOR / VERIFY**

- [x] parser adapter 只暴露编译器需要的最小访问接口。
- [x] 未选 parser 不存在于生产依赖。

### RF4-02 `[x] P0` 实现 generated node 双向一一绑定（仅箭头分支）

**依赖**

- RF4-01、RF3-05；严格 TOML 分支不适用。

**RED**

- [x] 每条 GeneratedRedirect 恰好匹配一个 inline table。
- [x] 每个被视为 generated 的 AST node 恰好消费一条记录。
- [x] 缺失、重复、多重匹配均返回 `PG-DESUGAR-INTERNAL`。

**GREEN**

- [x] 首选 generated table exact span lookup。
- [x] 按 D0 决议实现唯一 binder，不保留多个 fallback 分支。

**REFACTOR / VERIFY**

- [x] binding 不通过 RewriteMap 通用查询。
- [x] Rust binding 前不调用 `into_mut()`。（C++ 路径不适用）

### RF4-03 `[x] P0` 实现 generated node AST scope validator（仅箭头分支）

**依赖**

- RF4-02。

**RED**

- [x] 接受 `/apps/<package>/redirect/<index>`。
- [x] deny、processes、top-level redirect、嵌套数组报 `PG-RULE-ARROW-SCOPE`。
- [x] table header 和 dotted-key 表面写法只要 AST 路径等价均得到同一结果。

**GREEN**

- [x] parser 负责完整表路径，validator 只检查最终 path segments。
- [x] scope error 在普通字段类型错误前报告。

**REFACTOR / VERIFY**

- [x] 不把 apps/package/redirect 识别逻辑移回 scanner。
- [x] 已报 scope error 的节点标记为 decoder 跳过。

### RF4-04 `[x] P0` 拒绝手写 inline table 并抑制派生诊断（仅箭头分支）

**依赖**

- RF4-02、RF4-03。
- 严格 TOML 分支不执行本任务，由 RF1 决议冻结的标准 redirect 语法进入普通 decoder。

**RED**

- [x] 纯手写和 arrow/手写混合数组报告 `PG-REDIRECT-SYNTAX`。
- [x] generated scope error 不再产生同节点的 deny 类型错误。
- [x] 手写 table 不能伪造 generated span。

**GREEN**

- [x] redirect decoder 只接受已消费 GeneratedRedirect 的 inline table。
- [x] 同一根因只报告一次。

**REFACTOR / VERIFY**

- [x] 不保留“兼容接受 {from,to}”选项。
- [x] 高级 redirect 不在当前字段中预留未用 key。

### RF4-05 `[x] P0` 实现统一 parse/source-map 诊断适配

**依赖**

- RF4-01；保留箭头时依赖 RF3-04。

**RED**

- [x] parse error 位于 copied/synthetic segment 时映射回不同正确原始 span。
- [x] JSON 与文本诊断 code/span 不一致时失败。
- [x] 默认输出出现 `<desugared>` 或内部 from/to 时失败。

**GREEN**

- [x] 统一 Diagnostic：code、severity、phase、primary、related、field path、message key/args。
- [x] parser error 只在需要时构建详细上下文。

**REFACTOR / VERIFY**

- [x] 文本、JSON、状态文件和未来 Manager 共用同一模型。
- [x] 达诊断上限时追加 omitted 记录。

### RF4-06 `[x] P0` 定义分层源模型和 OriginMap

**依赖**

- RF4-01。

**RED**

- [x] 编译期类型测试/架构测试阻止 AST、parser node、comment、device snapshot 进入 RulesDocument。
- [x] RuleId/OriginMap 能分别定位 deny 和 redirect source/target。

**GREEN**

- [x] 定义 `RulesDocument`、`RuleId`、`OriginMap<RuleId, SourceSpan>`。
- [x] 源码位置不写入 Canonical Policy。

**REFACTOR / VERIFY**

- [x] 不继续扩充旧 `PolicyDocument`。
- [x] 不设计跨编辑持久化 RuleId。

### RF4-07 `[x] P0` 解码顶层、compatibility 与应用字段

**依赖**

- RF4-06。

**RED**

- [x] 覆盖 format、compatibility.allow_legacy_mount、apps、enabled、users、processes、file_picker。
- [x] 覆盖缺失默认值、类型错误、未知字段、重复应用、非法包名/user/process。
- [x] `enabled = false` 仍做 Host 静态校验但不进入策略 IR。

**GREEN**

- [x] 每个模块使用专一 decoder。
- [x] 未知字段和未知模块编译失败。

**REFACTOR / VERIFY**

- [x] 不建立通用动态 field registry。
- [x] 默认值只定义一次并被测试复用。

### RF4-08 `[x] P0` 解码 deny/redirect 并保持精确来源

**依赖**

- RF4-04、RF4-07；严格 TOML 分支按 D0 选定语法调整输入。

**RED**

- [x] deny 只接受字符串数组。
- [x] redirect source/target 分别保留 origin span。
- [x] 空数组、非法元素、多行 operand、混合元素和超限规则数。

**GREEN**

- [x] 构建强类型 RulesDocument，不执行路径规范化或设备准入。
- [x] generated marker/AST/trivia 在 decode 完成后释放。

**REFACTOR / VERIFY**

- [x] parser Document 只存在单次 parse/decode 生命周期。
- [x] RulesDocument 不持有 parser 引用。

### RF4 阶段闸门

- [x] 唯一 parser 的 TOML 1.0 gate 通过。
- [x] 若保留箭头，generated node 双向一一绑定、scope 优先和手写 table 拒绝通过。
- [x] parse、scope、type 诊断均指向原始 source。
- [x] RulesDocument/OriginMap 与 AST、设备状态和二进制布局隔离。

### RF4 验收记录（2026-07-27）

- 实施提交/工作区版本：基于 `6027c9a` 的 RF4 工作区；未进入 RF5 语义编译。
- RED：`pathguard_rules_parser_test` 首次因缺少 `pathguard/rules/compiler.h` 构建失败。
- GREEN：新增唯一无异常 toml++ adapter、generated exact-span binder、AST scope validator、强类型 decoder、OriginMap 和统一文本/JSON Diagnostic；Host Release CTest `30/30` 通过。
- Parser gate：toml-test v2.2.0 的 TOML 1.0 选集 valid 39、invalid 98 全部符合预期，并拒绝 TOML 1.1-only inline table 换行。
- 诊断：copied operand 与 synthetic prefix parse error 均映射到原始 `rules.toml`；默认文本/JSON 不出现 `<desugared>` 或内部 generated source。
- 解码：覆盖默认值、BOM/CRLF/Unicode、未知字段、类型/值错误、RuleId/OriginMap、规则上限、手写/混合 redirect 和 disabled app。
- Android：`./scripts/build-native.ps1 -Abi arm64-v8a` 通过。
- 架构边界：toml++ 为 `pathguard_rules` PRIVATE 依赖且只出现在实现文件；公开头无 toml AST，daemon/native/Zygisk 无 parser 链接。
- 闸门结论：通过，解锁 RF5。

---

## 13. RF5 阶段：语义编译、Canonical Policy、Admission 与 PolicyBlob

### RF5-01 `[x] P0` 建立 `ResolvedPolicy` 和不可变 `CanonicalPolicy` 边界

**依赖**

- RF4 阶段闸门。

**RED**

- [x] 架构测试阻止 source span/comment/AST 进入 Resolved/Canonical。
- [x] 设备 capability/topology 进入 Host canonicalization 时失败。

**GREEN**

- [x] RulesDocument 解析用户字段；ResolvedPolicy 保存规范化执行域；CanonicalPolicy 排序去表层差异。
- [x] PolicyRequirements 与 CanonicalPolicy 分离。

**REFACTOR / VERIFY**

- [x] 使用只读输入和显式输出，不原地改变同一文档含义。
- [x] 不为每个模型拆 crate/静态库。

### RF5-02 `[x] P0` 复用并补强 `PathNormalizer`

**依赖**

- RF5-01。

**RED**

- [x] 相对路径、分隔符、重复斜杠、`.`、`..`、NUL、绝对路径、空组件、UTF-8 长度和组件上限。
- [x] `{user}`、`{package}`、未知 `{...}` 均不展开。
- [x] 同一路径只规范化一次的计数测试。

**GREEN**

- [x] 产出可复用 `NormalizedPath` 和组件边界。
- [x] 后续冲突、排序和编码不重复切分原始字符串。

**REFACTOR / VERIFY**

- [x] 不把设备 symlink 解析放入 Host normalizer。
- [x] 不提前引入 trie。

### RF5-03 `[x] P0` 实现模块内 deny/redirect 校验

**依赖**

- RF5-02。

**RED**

- [x] deny 重复、父包含子、冗余 warning。
- [x] redirect 自映射、目标位于源内部、同源不同目标、源包含关系。
- [x] 未完成 executor 的 action 保持 compile gate。

**GREEN**

- [x] 按模块和执行域处理，产生稳定错误码和 related spans。
- [x] enabled=false 不进入 Canonical，但源仍完成静态校验。

**REFACTOR / VERIFY**

- [x] 不以规则书写顺序解决冲突。
- [x] warning 与 error 不共享含糊布尔结果。

### RF5-04 `[x] P0` 实现 `CrossModuleConflictValidator`

**依赖**

- RF5-03。

**RED**

- [x] deny/redirect 同路径、父子遮蔽、独立 mount domain、Provider 反向映射歧义。
- [x] 同 VFS 路径在不同执行域允许，在同域按矩阵处理。
- [x] 相关规则位置完整。

**GREEN**

- [x] 共享最小结构：按 app 执行域组合 `NormalizedPath` 与 `RuleId`；未引入仅承载标签的额外实体。
- [x] 分组、排序、相邻扫描/二分查找，避免无界 O(R²)。

**REFACTOR / VERIFY**

- [x] 冲突矩阵只有已存在能力，不建立通用规则代数。
- [x] 排序、哈希索引和二分查找的实现边界符合 O(R log R) 目标；性能实测统一留在 RF8。

### RF5-05 `[x] P0` 实现 redirect 图约束与循环检测

**依赖**

- RF5-03。

**RED**

- [x] A→B→A、自环、长链、共享目标、目标成为其他源。
- [x] Provider/file_picker 场景的多对一歧义。

**GREEN**

- [x] 使用哈希索引和颜色/DFS 等线性图遍历。
- [x] 不重复解析路径字符串。

**REFACTOR / VERIFY**

- [x] 图只服务已冻结 redirect 语义。
- [x] 不为未来条件规则建立通用图框架。

### RF5-06 `[x] P0` 生成 `PolicyRequirements` 与执行 device admission

**依赖**

- RF5-04、RF5-05。

**RED**

- [x] Host compile 在不同 capability/topology 下生成相同 CanonicalPolicy/Requirements。
- [x] fixed snapshot 上 strict/legacy 计划级选择、缺失能力和 topology generation 变化。
- [x] strict 运行时失败不得自动改写 policy 或逐规则 fallback。

**GREEN**

- [x] Requirements 使用既有 capability bitset/action mask 和少量固定约束。
- [x] AdmissionResult 只引用 content generation 和 snapshot generations，不复制策略树。

**REFACTOR / VERIFY**

- [x] Host compiler 不读设备瞬时状态。
- [x] Admission 不重新解释 TOML。

### RF5-07 `[x] P0` 实现确定性 Canonical Policy 与 content generation

**依赖**

- RF5-04～RF5-06。

**RED**

- [x] 注释、空白、键顺序、规则输入顺序和引号风格变化不改变输出。
- [x] 真实策略变化改变 content generation。
- [x] 禁用应用不进入 Canonical。

**GREEN**

- [x] 一次完成排序和去重；字符串表由既有 format v5 encoder 单次准备。
- [x] content generation 只来自 Canonical Policy。

**REFACTOR / VERIFY**

- [x] `source_digest` 不参与 content generation。
- [x] 编码器不执行第二套规则语义规范化。

### RF5-08 `[x] P0` 拆分 `PolicyEncoder` 与 `PolicyVerifier`

**依赖**

- RF5-07、RF0-03。

**RED**

- [x] 固定语义输入必须生成 207-byte golden。
- [x] checksum、offset、count、排序、reserved bits、generation 损坏均被 verifier 拒绝。
- [x] capability/topology 变化不得改变 blob。

**GREEN**

- [x] Encoder 纯确定性序列化 format v5。
- [x] Verifier 独立读取候选 bytes 并检查边界。
- [x] C++ 完整编译入口调用现有 `DecodePolicy()` 独立验证；RF7 Publisher 发布前再次验证。

**REFACTOR / VERIFY**

- [x] verifier 不重新 parse TOML。
- [x] policy binary 模块不依赖 parser、span 或 diagnostics。

### RF5-09 `[x] P0` 建立完整纯编译入口

**依赖**

- RF5-01～RF5-08。

**RED**

- [x] 从 source bytes 到 verified PolicyBlob 的端到端成功/失败矩阵。
- [x] 任一阶段错误不产生部分 blob。
- [x] structured diagnostics、requirements、admission 和 generation 字段一致。

**GREEN**

- [x] 实现单一 CompileRules 请求，内部按冻结阶段顺序执行。
- [x] 纯编译入口不执行文件写入，可直接用于 validate-only。

**REFACTOR / VERIFY**

- [x] 不创建巨型 `ParseRulesToml()`。
- [x] 简单阶段优先纯函数；RF5 未引入 I/O 或长期状态对象。

### RF5 阶段闸门

- [x] 模型分层、路径、模块内/跨模块冲突和循环检测测试通过。
- [x] Host compile 与 Device admission 明确分离。
- [x] CanonicalPolicy/PolicyRequirements/PolicyBlob 确定性测试通过。
- [x] 207-byte golden 和 C++ DecodePolicy 独立验证通过。
- [x] 完整编译入口任一错误均不产生候选策略。

### RF5 验收记录（2026-07-27）

- 实施提交/工作区版本：基于 `b744bfd` 的 RF5 工作区；尚未接入 daemon/CLI。
- RED：`pathguard_rules_semantic_test` 首次因缺少 `pathguard/rules/semantic.h` 构建失败。
- GREEN：新增 `NormalizedPath`、Resolved/Canonical/Requirements/Blob 分层、模块内与跨模块冲突、redirect cycle、Admission、format v5 编码与独立验证；RF5 定向测试 `3/3` 通过。
- 确定性：注释、空白、键顺序和引号变化保持相同 blob；语义变化改变 generation；禁用应用不进入 Canonical。
- Golden：固定输入逐字节生成 207-byte format v5，content generation 为 `11078014328063549684`，并由现有 `DecodePolicy()` 复核。
- 性能结构：规范化单次完成；冲突使用排序、相邻扫描/二分查找，cycle 使用哈希索引与颜色 DFS；未引入 trie 或通用规则代数。
- Host：Release 全量 CTest `33/33` 通过。
- Android：`./scripts/build-native.ps1 -Abi arm64-v8a` 通过。
- 架构边界：依赖仅为 `pathguard_rules -> pathguard_core`；公开模型不持有 AST/span/设备状态，Zygisk 未链接 rules/parser/compiler。
- 闸门结论：通过，解锁 RF6。

---

## 14. RF6 阶段：唯一语言边界、C ABI 与 Android 构建

### RF6-01 `[x] P0` 建立 `pathguard_rules_compiler` 唯一生产目标

**依赖**

- RF5 阶段闸门。

**RED**

- [x] 依赖图中 policy binary 反向依赖 parser/span 时失败。
- [x] format 1 compiler/parity 目标直接调用旧 parser 时失败；daemon/CLI 活动旧格式路径保持隔离并在 RF7 原子切换。
- [x] Zygisk 目标出现 compiler source/dependency 时失败。

**GREEN**

- [x] Rust 分支不适用：RF1 已冻结 C++，生产树无 Rust crate/runtime。
- [x] C++ 分支以命名空间/文件边界隔离，只有一个 `pathguard_rules_compiler` 静态库。

**REFACTOR / VERIFY**

- [x] 依赖方向为 `rules compiler -> core binary`，core 不反向依赖 parser/span。
- [x] 未创建空的多 crate workspace。

### RF6-02 `[x] P0` 固化版本化 C ABI（仅 Rust 分支，不适用）

**依赖**

- RF6-01；仅 Rust 分支。

**RED**

- [x] 不适用：RF1 选择 C++ 后已删除 Rust D0 artifact，生产调用不跨语言。
- [x] 不适用：C++ 调用直接使用值类型结果，无 opaque FFI handle。

**GREEN**

- [x] 不适用：未保留生产 C ABI。
- [x] 不适用：未暴露跨语言输入/输出布局。

**REFACTOR / VERIFY**

- [x] 未引入 cbindgen。
- [x] 无 Rust 回调或 Rust 生产依赖。

### RF6-03 `[x] P0` 固化 panic、overflow 与所有权安全（仅 Rust 分支，不适用）

**依赖**

- RF6-02。

**RED**

- [x] 不适用：生产无 Rust extern/panic 边界。
- [x] C++ scanner/rewrite 的 checked arithmetic、最大分配、非法 UTF-8 和 repeated compile 已由 RF1～RF3 覆盖。
- [x] 不适用：生产无 Rust panic 状态。

**GREEN**

- [x] 不适用：无 Cargo release profile。
- [x] C++ 完整编译入口错误时不返回 policy bytes。

**REFACTOR / VERIFY**

- [x] 文档和错误模型不宣称捕获 OOM/stack overflow/signal。
- [x] 不适用：生产树无 Rust `unsafe`。

### RF6-04 `[x] P0` 集成 cargo-ndk/NDK 与现有 native 构建

**依赖**

- RF6-01；Rust 分支还依赖 RF6-02。

**RED**

- [x] NDK revision、API、ABI 不匹配时脚本失败。
- [x] 不适用：C++ 分支无 Rust staticlib。
- [x] 不适用：生产无 Cargo/lockfile。

**GREEN**

- [x] Rust 分支不适用：未调用 cargo-ndk。
- [x] C++ 分支：parser/compiler 只构建为控制面静态库与 parity probe，不进入 companion/Zygisk；daemon/CLI 消费点由 RF7 原子切换。

**REFACTOR / VERIFY**

- [x] 不改变 companion/Zygisk 的进程和语言边界。
- [x] Host/Android parity probe 编译同一生产源和同一测试源，不复制规则实现。

### RF6-05 `[x] P0` 建立 Zygisk ELF/link-map 零依赖测试

**依赖**

- RF6-04。

**RED**

- [x] 测试注入一个 compiler symbol 到 Zygisk 时必须失败。
- [x] 检查 Rust runtime、`toml_edit`、toml++、Diagnostic、C ABI symbol。

**GREEN**

- [x] 对最终 stripped ELF、完整/动态符号、字符串和静态 link map 执行检查。
- [x] `scripts/build-native.ps1` 在复制打包产物后自动运行检查。

**REFACTOR / VERIFY**

- [x] 不只扫描源码 include；检查最终产物和 link map。
- [x] arm64-v8a 是首个强制门槛。

### RF6-06 `[x] P0` 验证 Host/Android 编译结果一致

**依赖**

- RF6-04、RF6-05。

**RED**

- [x] 同一 source 在 Host 与 Android compiler 输出 byte 不一致时失败。
- [x] 诊断 code/span、requirements、generation 不一致时失败。

**GREEN**

- [x] 使用同一 parity probe、生产源、RulesLimits 和 format v5 golden。
- [x] Android 侧只复测构建、端到端输出和隔离；完整语义测试留在 Host。

**REFACTOR / VERIFY**

- [x] 不为 Android 建第二套规则语义。
- [x] 平台差异只存在构建/I/O adapter。

### RF6 阶段闸门

- [x] format 1 生产构建只有一个 compiler/parser；旧 INI 活动路径隔离到 RF7 迁移边界。
- [x] C++ 分支无 Rust 生产依赖。
- [x] NDK r27d/API 31/arm64 构建可重复、版本明确；Cargo 不适用。
- [x] Host/Android 输出一致。
- [x] Zygisk 最终 ELF 零 parser/Rust/compiler/diagnostic 依赖。

### RF6 验收记录（2026-07-27）

- 实施提交/工作区版本：基于 `465ae3c` 的 RF6 工作区；未提前执行 RF7 的活动配置迁移。
- RED：`pathguard_rules_build_boundary_guard` 因缺少唯一 target 失败；`pathguard_zygisk_elf_guard` 因缺少最终产物检查器失败。
- GREEN：唯一 Host/NDK target 重命名为 `pathguard_rules_compiler`；NDK 编译同一组 C++20/toml++ 生产源，无 Rust/C ABI 分支。
- 工具链：`build-native.ps1` 强制 NDK r27d `27.3.13750724`、API 31 和 arm64 首个门槛；错误 NDK/API/ABI 注入测试通过。
- Parity：连接设备 `arm64-v8a` probe 与 Host probe 的 207-byte blob、content generation、requirements、错误码和 byte span 完全一致。
- 隔离：自动扫描最终 stripped Zygisk ELF 的完整/动态符号、字符串及 106652-byte link map；注入 compiler symbol 会失败，真实产物通过。
- Host：Release 全量 CTest `37/37` 通过。
- Android：`./scripts/build-native.ps1 -Abi arm64-v8a -HostParityProbe build/tests/Release/pathguard_rules_parity_probe.exe` 通过。
- 迁移边界：旧 `rules.ini` 控制面仍保持 RF0 characterization，避免 RF6 中途破坏配置；RF7 将 source loader、reconciler、publisher、CLI 与文件名一次切换。
- 闸门结论：通过，解锁 RF7。

---

## 15. RF7 阶段：控制面切换、发布事务与破坏性迁移

### RF7-01 `[x] P0` 实现安全 `RulesSourceLoader`

**依赖**

- RF6 阶段闸门。

**RED**

- [x] 普通文件、symlink、非普通文件、不安全 owner/mode、超限、BOM 非起始、截断写入。
- [x] LF/CRLF、rename 保存、目录 FD + `O_NOFOLLOW` 约束。
- [x] 同一 FD 两次稳定读取的元数据或字节不一致时不编译。

**GREEN**

- [x] 从固定目录 FD 安全打开 `rules.toml` 并生成 immutable source snapshot/SHA-256 source_digest。
- [x] 拒绝 owner 不匹配或 group/other 可写配置。

**REFACTOR / VERIFY**

- [x] Loader 不 parse TOML。
- [x] Loader 只使用固定文件名，不把用户路径拼进 shell。

### RF7-02 `[x] P0` 实现单 worker `ConfigReconciler`

**依赖**

- RF7-01、RF5-09。

**RED**

- [x] close-write、create、move、attrib、delete、inotify overflow。
- [x] 编译期间再次保存由事件队列保留，单 worker 下一轮读取最新 digest。
- [x] burst 事件只在同步 Reconciler 上串行 compile。
- [x] 固定 sleep 不是正确性前提。

**GREEN**

- [x] inotify/polling 事件只调用同一单 worker reconcile 入口；overflow 触发全量重读。
- [x] source_digest 与设备快照均未变化时跳过完整编译。

**REFACTOR / VERIFY**

- [x] 未使用固定防抖保证正确性，稳定读取独立完成。
- [x] polling fallback 与 inotify 共用 reconcile 语义。

### RF7-03 `[x] P0` 实现 `PolicyPublisher` 原子事务与恢复

**依赖**

- RF7-02、RF5-08。

**RED**

- [x] 不可预测临时文件、partial write、mode/owner/context、file fsync、rename、dir fsync 各阶段注入失败。
- [x] 任一失败不覆盖当前 policy；dir fsync 失败执行备份回滚。
- [x] 重启从可验证 policy.bin 重建 active generation，并只在 generation 匹配时恢复 deployment epoch。
- [x] 固定 `policy.bin.tmp` 使用检查失败。

**GREEN**

- [x] 目标目录下创建不可预测的唯一候选和备份文件。
- [x] 写入、设置 mode/继承 owner/context、重读、Verifier、C++ DecodePolicy、file fsync、原子 rename、dir fsync。
- [x] daemon Publisher 是活动路径唯一写者。

**REFACTOR / VERIFY**

- [x] Publisher 不解释 TOML/规则语义。
- [x] content generation 未变化时不重写 blob。

### RF7-04 `[x] P0` 实现状态模型与统一结构化诊断输出

**依赖**

- RF7-02、RF7-03、RF4-05。

**RED**

- [x] source_invalid、environment_unsupported、publish_failed 三类状态。
- [x] source_digest、candidate_sequence、active content generation、deployment epoch、capability/topology generations。
- [x] 文本、JSON、状态文件共享同一 ControlState 字段测试。

**GREEN**

- [x] 分别原子更新 `module/run/rules-status.txt` 和 `rules-status.json`。
- [x] 失败显示“新配置未生效，仍使用上一版本”。

**REFACTOR / VERIFY**

- [x] 状态文件不影响策略内容；只可在已验证 policy generation 匹配时恢复 deployment epoch。
- [x] 不使用含义不明的单一 `generation` 字段。

### RF7-05 `[x] P0` 切换 CLI 到统一编译入口

**依赖**

- RF7-04。

**RED**

- [x] `validate --host`、受控拒绝的 `validate --device`、离线 `compile`、daemon `reload`。
- [x] CLI 直接写 `run/policy.bin` 时拒绝并要求 reload。
- [x] 文本/JSON 诊断由同一 Diagnostic 渲染，错误码一致。

**GREEN**

- [x] CLI 不再调用 `ParseRulesIni()`。
- [x] compile 只写显式离线输出；reload 通过 `rules.toml` attrib 事件请求 daemon。

**REFACTOR / VERIFY**

- [x] CLI 与 daemon 复用同一 compiler/diagnostic 契约，device admission 只由 daemon snapshot 执行。
- [x] 不在 CLI 复制规则验证。

### RF7-06 `[x] P0` 切换模块模板、脚本和监控路径到 `rules.toml`

**依赖**

- RF7-02、RF7-05。

**RED**

- [x] package/installation test 检查 `rules.toml` 存在、`rules.ini` 不存在。
- [x] `module/action.sh`、daemon、CLI help、hot reload tests 任一仍引用旧路径时失败。
- [x] 默认模板可编译；RF5 已验证 `enabled=false` 不进入 Canonical。

**GREEN**

- [x] 使用 format 1 最小注释模板。
- [x] 更新模块安装、self-check 和热重载测试路径。

**REFACTOR / VERIFY**

- [x] 默认模板不启用未完成 deny/event executor。
- [x] 不自动重写用户 `rules.toml`。

### RF7-07 `[x] P0` 建立一次性旧样例迁移与语义 golden

**依赖**

- RF7-05、RF7-06、RF0-02。

**RED**

- [x] 不含占位符的旧样例与新样例生成相同 179-byte blob/content generation。
- [x] `{user}`/`{package}` 与未完成 action 在迁移文档中明确要求人工具体化/删除。
- [x] 迁移结果存在两套运行时格式时失败。

**GREEN**

- [x] 提供 `docs/rules-ini-migration.md` 离线一次性迁移步骤。
- [x] 运行时只读取 `rules.toml`。

**REFACTOR / VERIFY**

- [x] 不建立 `if toml else ini` 长期分支。
- [x] 迁移说明/fixture 不进入 daemon 热加载路径。

### RF7-08 `[x] P0` 删除旧 parser、旧模型耦合和固定发布路径

**依赖**

- RF7-01～RF7-07。

**RED**

- [x] 静态检查搜索 `ParseRulesIni`、`SplitArrow`、`rules.ini`、固定 `policy.bin.tmp` 和旧 CLI usage。
- [x] 任一生产引用存在时失败，并有注入旧引用的负向测试。

**GREEN**

- [x] 删除旧 parser、legacy control 及只为旧语法存在的 helper。
- [x] runtime reader 继续只使用明确的 `PolicyDocument`/format v5 binary model。

**REFACTOR / VERIFY**

- [x] 保留必要的 characterization fixture 作为迁移 golden，不保留旧生产代码。
- [x] 不使用 dead wrapper 假装完成删除。

### RF7-09 `[x] P0` 验证主流 Android 编辑器保存方式

**依赖**

- RF7-02、RF7-06。

**RED**

- [x] 原地 truncate/write、临时文件 rename、BOM 添加、LF↔CRLF、chmod 改变、连续保存。
- [x] 不稳定双读与不安全权限不发布。

**GREEN**

- [x] 使用 adb/shell 复现 Root 文件管理器/文本编辑器的覆盖、rename 和 shell 编辑代表流程。
- [x] 不安全 owner/mode 给出 `PG-SOURCE-UNSAFE` 可定位状态；context 继承可信目录。

**REFACTOR / VERIFY**

- [x] 保存方式差异只影响 SourceLoader/Reconciler，不侵入 compiler。

### RF7 阶段闸门

- [x] daemon/CLI/module 只使用 `rules.toml` 和统一编译入口。
- [x] Publisher 全故障注入通过，失败保留上一份策略。
- [x] 状态和诊断含义稳定。
- [x] `ParseRulesIni()`、运行时 `rules.ini` 和固定临时文件路径已删除。
- [x] 编辑器 rename/truncate/连续保存能够最终收敛。

### RF7 验收记录（2026-07-27）

- 实施提交/工作区版本：基于 `cd7963d` 的 RF7 工作区；旧 parser/legacy control 生产文件已删除。
- RED：`pathguard_rules_control_test` 首次因缺少 `pathguard/rules_control.h` 构建失败；稳定读取测试随后发现同长度覆盖不能只依赖 Windows mtime。
- Source：Windows 双读字节+元数据；Linux/Android 固定目录 FD、`openat(O_NOFOLLOW)`、同 FD `fstat`/双读；SHA-256 已用固定向量验证。
- Publisher：9 个故障点全部保留旧策略；候选写后重读并由 format v5 verifier/`DecodePolicy()` 验证；唯一 temp/backup、file/dir fsync 和 rollback 通过。
- Reconcile/状态：source_invalid、environment_unsupported、publish_failed、source_digest 快路径、device snapshot 重新准入、文本/JSON 状态和重启恢复通过。
- CLI/module：validate/compile/reload/status 使用统一 compiler；活动路径写入被拒绝；包内只有 `config/rules.toml`，默认模板可编译。
- 迁移：旧有效样例映射为相同 179-byte blob、content generation `18258669379964361373`；占位符/未完成 action 要求人工迁移，无双 parser。
- Host：Release 全量 CTest `39/39` 通过，包含后台 hot reload。
- Android：arm64 daemon/CLI/rules compiler 构建和 Host parity 通过；设备原地覆盖、rename、BOM/CRLF、unsafe mode、连续保存矩阵通过。
- 模块包：`pathguard-next-v0.1.11-dev-universal.zip` 仅含 `config/rules.toml`，不含 `rules.ini`。
- 闸门结论：通过，解锁 RF8。

---

## 16. RF8 阶段：全量健壮性、性能、真机与发布验收

### RF8-01 `[x] P0` 完整规则编译安全与资源回归

**依赖**

- RF7 阶段闸门。

**RED**

- [x] 每个 RulesLimits 边界的全管线测试。
- [x] 小输入产生超大展开、诊断放大、深层 TOML、超长字符串和路径。
- [x] 任一 error 进入 encoder/publisher 时失败。

**GREEN**

- [x] 最早阶段和展开/编码前双重验证。
- [x] 统一稳定资源错误码。

**REFACTOR / VERIFY**

- [x] toml-test 不覆盖的深度和内存限制由本项目测试承担。
- [x] 无无界递归或无界诊断。

### RF8-02 `[x] P0` 长时间 fuzz 与回归 corpus

**依赖**

- RF8-01；保留箭头时依赖 RF3-07。

**RED**

- [x] scanner、frame、RewriteMap、parser/scope、decoder 和完整 compile 目标均可运行。
- [x] corpus 中任何已知崩溃不再复现。

**GREEN**

- [x] Host CI 跑短 fuzz smoke；定期任务跑长 fuzz。
- [x] 崩溃最小化后进入普通回归测试。

**REFACTOR / VERIFY**

- [x] 固定 fuzz 工具版本和命令。
- [x] 不把第三方 parser 内部 bug 静默标为忽略；记录上游版本和缓解。

### RF8-03 `[x] P0` 建立编译性能 benchmark 和指标

**依赖**

- RF7 阶段闸门。

**RED**

- [x] 报告缺少阶段时间、source/generated bytes、arrow/rewrite count、peak memory、backend/parser version、blob bytes 时失败。
- [x] debug 或含 fsync 数据混入 CPU budget 时失败。

**GREEN**

- [x] 记录 format_probe、lex、rewrite、parse、scope、decode、normalize、conflict、canonicalize、admission、encode、verify。
- [x] 单独记录 publish/fsync。
- [x] 典型 `≤64 KiB/≤256`、大型 `≤256 KiB/≤2,000`、极限 `≤1 MiB/≤4,096`。

**REFACTOR / VERIFY**

- [x] 目标：典型 P95 <10ms/<16MiB；大型 <50ms/<24MiB；极限 <100ms/<32MiB，最终以目标设备数据冻结。
- [x] 无箭头快路径单独测量。
- [x] 未有证据前不引入 trie、增量 AST 或编译缓存。

### RF8-04 `[x] P0` 验证 content generation 不变时跳过发布

**依赖**

- RF7-03、RF5-07。

**RED**

- [x] 只改注释、空白、键顺序、箭头对齐、LF/CRLF 的案例。
- [x] source_digest 变化但 blob write/reload 被调用时失败。

**GREEN**

- [x] 更新候选验证状态和 source_digest。
- [x] 跳过 policy.bin 重写和 runtime reload。

**REFACTOR / VERIFY**

- [x] 不使用 source_digest 代替 content generation。
- [x] 不缓存 parser AST。

### RF8-05 `[x] P0` 编译/准入/发布失败恢复集成测试

**依赖**

- RF7-03、RF7-04。

**RED**

- [x] source_invalid、environment_unsupported、encoder/verifier failure、C++ reader failure、fsync/rename failure、daemon crash/restart。
- [x] 任一案例当前 policy 或状态被错误覆盖时失败。

**GREEN**

- [x] 恢复上一有效策略和最近诊断。
- [x] 状态缺失/过期/不一致时由验证后的 policy 重建。

**REFACTOR / VERIFY**

- [x] 不发布部分应用或部分规则。
- [x] Rust panic（未采用，不适用）与普通编译错误服从同一保留旧策略语义。

### RF8-06 `[x] P0` Android arm64 端到端与 Zygisk 回归

**依赖**

- RF8-03、RF8-05。

**RED**

- [x] daemon 编译 rules.toml、发布 policy.bin、Zygisk mmap/read、包索引和 redirect plan。
- [x] parser/compiler 被意外带入 app 进程时失败。
- [x] 新配置失败时运行中仍读取旧 generation。

**GREEN**

- [x] 至少在当前 Alioth/R1 环境完成控制面垂直链路。
- [x] 与现有 redirect 真机安全矩阵组合验证。

**REFACTOR / VERIFY**

- [x] 配置编译性能不混入 mount_step/应用启动热路径数据。
- [x] Zygisk 只消费冻结 binary。

### RF8-07 `[x] P0` 发布前全仓引用、文档和许可证审计

**依赖**

- RF8-01～RF8-06。

**RED**

- [x] 搜索旧规则名、旧 parser、未选 parser、隐藏 marker、D0 临时 artifact、TOML 1.1-only 配置。
- [x] 依赖许可证、版本、hash 或 lockfile 缺失时失败。

**GREEN**

- [x] 更新 README、00/02/03/04/05、CLI help、模块模板和测试说明。
- [x] 记录 toml-test fixture provenance/MIT license。

**REFACTOR / VERIFY**

- [x] 文档只描述唯一生产实现。
- [x] Proposed 状态仅在实际闸门通过后更新。

### RF8 阶段闸门

- [x] 完整规则编译资源边界、fuzz 和 failure recovery 通过。
- [x] CPU/内存/体积满足冻结预算。
- [x] Android arm64 端到端和 Zygisk 零依赖通过。
- [x] content generation 不变跳过发布。
- [x] 全仓不存在旧 parser/双格式/未选 parser/fallback 残留。
- [x] 全局 Definition of Done 全部满足。

### RF8 验收记录（2026-07-27）

- 实施提交/工作区版本：基于 `89b41d4` 的 RF8 工作区。
- RED：资源测试发现 `max_expanded_rules` 未消费、路径超限错误码不统一；性能门禁发现 generated span 定位 O(R²)；完整 fuzz 首次发现 Windows CRT 配置不一致。
- GREEN：补齐全 `RulesLimits` 管线门禁、O(1) generated span 索引、生成文本行索引、完整 compile fuzz、阶段 benchmark、等价源跳过发布和重启恢复。
- Fuzz：Clang/libFuzzer 20.1.7 + UBSan；固定 50,000 次和 60 秒（>242 万次、RSS 44 MiB）均无 crash/artifact。
- Host 性能：典型 1.70 ms/11.7 MiB，大型 14.63 ms/14.6 MiB，极限 29.15 ms/18.0 MiB；publish/fsync 3.60～7.64 ms。
- Android 性能：Alioth/R1、arm64-v8a/API 31/NDK r27d；典型 2.65 ms/8.0 MiB，大型 17.69 ms/8.0 MiB，极限 36.92 ms/8.4 MiB；publish/fsync 7.21～18.16 ms。
- Android 垂直链路：daemon 发布 generation `5668809052303656865`，数据面 mmap/index/plan 得到 `Source -> Target`；失败候选后 SHA-256 与旧 plan 不变。
- 审计：toml++ v3.4.0 SHA-256/MIT、toml-test v2.2.0 TOML 1.0 fixture/MIT、旧 parser/隐藏 marker/未选 parser、Zygisk ELF/link map 均由自动负向门禁锁定。
- 完整报告：`docs/rules-compiler-rf8-verification.md`。
- 闸门结论：通过，解锁 RF9。

---

## 17. RF9 阶段：P1 辅助工具与 Manager 接入

本阶段不阻塞核心 `rules.toml` 切换。只有 RF8 通过后开始。

### RF9-01 `[x] P1` 实现 `lint`、`plan` 和 `explain --path`

**依赖**

- RF8 阶段闸门。

**RED**

- [x] lint 冗余/遮蔽/Unicode 近似/legacy 风险。
- [x] plan 稳定报告新增、删除、修改。
- [x] explain 展示最长前缀和被遮蔽父规则。

**GREEN**

- [x] 复用 Canonical Policy、OriginMap 和现有 validator，不复制规则语义。

**REFACTOR / VERIFY**

- [x] 工具不写活动 policy。

### RF9-02 `[x] P1` PathGuard 专用 `fmt` 需求门禁（N/A）

> 结论：真实用户无损格式化需求尚未确认，本项按 YAGNI 判定 N/A；不实现 formatter，不建立完整 CST。以下测试条件保留为未来独立任务的启动门禁，不表示当前已实现格式化。

**依赖**

- RF8 阶段闸门、RF9-01。

**RED**

- [x] N/A：仅在真实 formatter 需求确认后建立上述 RED 用例。
- [x] N/A：当前没有格式化写回入口，因而不会发生有损覆盖。

**GREEN**

- [x] 真实用户需求启动条件未满足，不创建生产实现。
- [x] N/A：未提供 stdout、新文件或原地格式化命令。

**REFACTOR / VERIFY**

- [x] 未调用严格 TOML formatter 改写扩展源。
- [x] 核心 compiler 不持久化完整 CST。

### RF9-03 `[x] P1` Manager 复用 daemon 编译与乐观并发

**依赖**

- RF8 阶段闸门；不依赖 RF9-02。

**RED**

- [x] Manager 使用过期 source_digest 保存时被拒绝。
- [x] UI/协议模型分别暴露 source/candidate/active/deployment/capability/topology。
- [x] Manager 保存入口不复制 parser/validator。

**GREEN**

- [x] 通过 daemon 统一 validate/admission/diagnostic。
- [x] 局部编辑真实需求尚未出现，不建立 rules_editor。

**REFACTOR / VERIFY**

- [x] 保存 API 只接受调用方提交的完整替换文本；daemon 不重排或重建 TOML，失败候选不覆盖源文件。

### RF9 阶段闸门

- [x] 辅助工具只复用已验证编译链。
- [x] fmt/Manager 没有成为第二输入语义或第二发布者。

### RF9 验收记录（2026-07-27）

- RED：`pathguard_rules_tools_test`、`pathguard_rules_tools_cli_integration` 和 `pathguard_rules_manager_save_test` 分别先因缺失工具行为、CLI 命令与 `Reconciler::SaveRules` 失败。
- GREEN：实现 `lint`、稳定 `plan`、最长前缀 `explain --path`；Manager 保存执行两次 source digest CAS、共享编译/准入、耐久原子源码替换，并回到唯一 Reconciler/Publisher。
- YAGNI：`fmt` 的真实需求门禁未触发，未加入完整 CST、严格 TOML formatter 写回或 rules_editor。
- Host Release：47/47 通过；RF9 新增的工具单测、CLI 集成和 Manager CAS 单测全部通过。
- Android arm64、Host/Android parity、Zygisk ELF 隔离和 Alioth/R1 垂直链路全部通过。
- 完整报告：`docs/rules-compiler-rf9-verification.md`。
- 闸门结论：通过；RF0～RF9 实施主线完成。

---

## 18. 可并行工作与禁止偷跑项

### 18.1 可并行

- [ ] RF1-04 C++ adapter 与 RF1-05 Rust adapter 可并行，但必须先完成 RF1-03。
- [ ] RF1-06 Rust vertical slice 与 RF1-04 C++ source-region spike 可并行。
- [ ] RF2 的 Source/LineIndex 与 fixture harness 可并行，FormatProbe 和 scanner 仍按依赖合并。
- [ ] RF5 的 graph cycle 测试和 cross-module matrix fixture 可在模型边界冻结后并行准备。
- [ ] RF7 的 Publisher failure fixture 可在 Reconciler 实现前准备，但不得提前切换活动路径。
- [ ] 文档、license 和 fixture provenance 可持续维护，不得修改未冻结生产语义。

### 18.2 P2/独立项目，不得偷跑

- [ ] daemon/CLI/companion 整体 Rust 迁移。
- [ ] Zygisk Rust 化。
- [ ] `redirect_rules` 高级属性。
- [ ] 通用操作符注册、宏、表达式、模板或占位符系统。
- [ ] 完整无损 TOML CST、通用 formatter。
- [ ] trie、增量 parser、跨编译缓存、并行规则编译。
- [ ] policy format v6。
- [ ] Manager 完整编辑器。

上述项若进入开发，必须另建设计和 TDD 清单，不得提前放入 enum、ABI、数据库或配置字段。

---

## 19. Kill Criteria 与风险清单

### 19.1 箭头方案停止条件

- [ ] 两个候选 generated span 在 Unicode/CRLF/BOM 下均不稳定。
- [ ] 精确映射需要完整 CST 或解析 TOML table path。
- [ ] 正常路径必须全面依赖隐藏 nonce 字段。
- [ ] Rust 必须使用 `DocumentMut` 才能完成 binding。
- [ ] 唯一 C ABI 必须暴露 AST/span/IR 或跨语言长期借用。
- [ ] 资源、体积或构建复杂度明显超过严格 TOML收益。

满足任一项时返回严格 TOML比较，不进入 RF2。

### 19.2 Rust 候选停止条件

- [ ] TOML 1.0 语义无法通过 lockfile 和 conformance gate 冻结。
- [ ] Android arm64 staticlib/C ABI 所有权/panic containment 不可靠。
- [ ] 207-byte golden 或 C++ reader 验证无法一致。
- [ ] Rust/parser/runtime 无法从 Zygisk 最终 ELF 隔离。

### 19.3 C++ 候选停止条件

- [ ] source region 或 line/column 到 byte offset 无法稳定恢复。
- [ ] 无异常模式丢失必要错误位置。
- [ ] 生产必须依赖隐藏 marker，但未获得独立架构批准。

### 19.4 发布停止条件

- [ ] 任一错误可能覆盖活动 policy。
- [ ] daemon 之外仍可直接写活动路径。
- [ ] source_digest/content generation/deployment epoch 混用。
- [ ] parser/compiler 链接进入 Zygisk。
- [ ] 旧 `rules.ini` 分支仍存在。

---

## 20. 设计契约追踪矩阵

| 设计契约 | 本文任务 |
|---|---|
| 04 `5 单一事实来源 | RF7-03、RF7-06～RF7-08 |
| 04 `7 format 1 字段 | RF4-07～RF4-08 |
| 04 `8 / 05 `7～`10 脱糖 | RF2、RF3 |
| 04 `9 路径模型 | RF5-02 |
| 04 `10 冲突规则 | RF5-03～RF5-05 |
| 04 `12 编译器架构 | RF4～RF6 |
| 04 `12.4 语言/parser | RF1 |
| 04 `12.6 构建边界 | RF6 |
| 04 `13 版本模型 | RF5-07、RF7-04、RF8-04 |
| 04 `14 发布失败 | RF7-01～RF7-04、RF8-05 |
| 04 `15 诊断 | RF2-08、RF4-05、RF7-04 |
| 04 `16 迁移 | RF7-06～RF7-08 |
| 04 `17/`18 测试与限制 | RF1-02、RF8 |
| 05 `6 FormatProbe | RF2-02 |
| 05 `11 RewriteMap | RF3-04、RF3-06 |
| 05 `12 GeneratedRedirect | RF3-05、RF4-02 |
| 05 `14 Scope | RF4-03～RF4-04 |
| 05 `15 诊断优先级 | RF2-08、RF4-05 |
| 05 `19 性能 | RF1-09、RF8-03～RF8-04 |
| 05 `20 fuzz/golden | RF2-09、RF3-06～RF3-07、RF8-02 |
| ADR-0002 207-byte format v5 | RF0-03、RF1-06、RF5-08 |

---

## 21. 发布前回归清单

### 21.1 格式与 parser

- [ ] format 首声明、缺失、重复、未知版本。
- [ ] TOML 1.0 valid/reject gate。
- [ ] TOML 1.1-only 输入拒绝。
- [ ] BOM、LF、CRLF、Unicode、非法 UTF-8。
- [ ] 未知字段、重复 key/table、非法 package/user/process。

### 21.2 箭头（若采用）

- [ ] 字符串/注释/多行字符串内箭头不误判。
- [ ] basic/literal、同行/跨行、尾逗号。
- [ ] context、operand、comment-inside、chained、missing-comma。
- [ ] generated node 一一绑定和 scope 优先。
- [ ] 手写 inline table 拒绝。
- [ ] source-map golden 和无箭头快路径。

### 21.3 语义与 binary

- [ ] 路径规范化和禁止占位符。
- [ ] deny/redirect/Provider 冲突和 cycle。
- [ ] enabled=false、compile gate、Requirements/Admission。
- [ ] Canonical determinism/content generation。
- [ ] 207-byte golden、checksum、C++ DecodePolicy。

### 21.4 控制面

- [ ] SourceLoader owner/mode/symlink/size。
- [ ] rename/truncate/连续保存/inotify overflow。
- [ ] 单 worker 最终收敛。
- [ ] Publisher 全阶段故障注入。
- [ ] source_invalid/environment_unsupported/publish_failed。
- [ ] 注释变化跳过 blob write/reload。
- [ ] daemon 唯一写者。

### 21.5 构建与隔离

- [ ] 依赖/lockfile/license/hash。
- [ ] Host/Android arm64 输出一致。
- [ ] Rust ABI/panic/ownership（若采用）。
- [ ] Zygisk ELF/link map 零 parser/Rust/compiler。
- [ ] 包内只有 `rules.toml`。

### 21.6 范围治理

- [ ] 无双 parser、双格式、隐藏 marker 生产残留。
- [ ] 无完整 CST、通用 DSL、incremental AST/trie 空壳。
- [ ] 未重写 daemon/CLI/companion 为 Rust。
- [ ] 未提前开放未完成 executor。

---

## 22. 每阶段验收记录模板

```markdown
### RFx 验收记录

- 日期：
- 实施提交/工作区版本：
- 执行人：
- 选定 backend/parser（RF1 后）：
- RED 证据：
- 任务级测试命令与结果：
- 完整回归命令与结果：
- Fixture/golden/seed 版本：
- Host compiler 与构建模式：
- Android 设备/API/ABI/NDK：
- Benchmark 报告路径：
- ELF/link-map 报告路径：
- 已知风险与延期项：
- 闸门结论：通过 / 不通过
```

只有“闸门结论：通过”的阶段才能解锁下游阶段。测试未运行、只在 debug 下观察、只验证 parser 单个 API 或只比较主观代码量，都不能判定通过。

---

## 23. 计划测试产物索引

文件名可按最终 Rust/C++ 选择微调；职责不得合并成单个巨型测试。

### 23.1 Source、desugar 与 parser

| 建议测试 | 覆盖任务 |
|---|---|
| `RulesFormatProbeTest` | RF2-02 |
| `SourceBufferLineIndexTest` | RF2-01 |
| `TomlStringBoundaryTest` | RF2-03～RF2-05 |
| `ArrowContainerFrameTest` | RF2-06 |
| `ArrowCandidateStateMachineTest` | RF2-07～RF2-08 |
| `ArrowRewriteEmitterTest` | RF3-01～RF3-03 |
| `RewriteMapTest` | RF3-04、RF3-06 |
| `GeneratedRedirectTest` | RF3-05、RF4-02 |
| `GeneratedNodeScopeTest` | RF4-03～RF4-04 |
| `ParserDiagnosticSourceMapGoldenTest` | RF4-01、RF4-05 |
| `Toml10VersionGateTest` | RF1-02、RF4-01 |

### 23.2 RulesDocument 与语义

| 建议测试 | 覆盖任务 |
|---|---|
| `RulesDocumentDecoderTest` | RF4-06～RF4-08 |
| `RulesModelBoundaryTest` | RF4-06、RF5-01 |
| `PathNormalizerTest` | RF5-02 |
| `DenyRedirectValidatorTest` | RF5-03 |
| `CrossModuleConflictValidatorTest` | RF5-04 |
| `RedirectGraphValidatorTest` | RF5-05 |
| `PolicyRequirementsAdmissionTest` | RF5-06 |
| `CanonicalPolicyDeterminismTest` | RF5-07 |
| `PolicyV5GoldenTest` | RF0-03、RF1-06、RF5-08 |
| `CompleteRulesCompilerTest` | RF5-09 |

### 23.3 ABI、控制面与发布

| 建议测试 | 覆盖任务 |
|---|---|
| `RulesCompilerAbiLayoutTest` | RF1-07、RF6-02 |
| `RulesCompilerPanicBoundaryTest` | RF1-07、RF6-03 |
| `RulesCompilerAndroidParityTest` | RF6-06 |
| `ZygiskCompilerDependencyTest` | RF1-08、RF6-05 |
| `RulesSourceLoaderTest` | RF7-01 |
| `ConfigReconcilerTest` | RF7-02 |
| `PolicyPublisherFailureInjectionTest` | RF7-03 |
| `RulesStatusDiagnosticTest` | RF7-04 |
| `RulesCliIntegrationTest` | RF7-05 |
| `RulesMigrationGoldenTest` | RF7-07 |
| `RulesEditorSaveModeDeviceTest` | RF7-09 |

### 23.4 Fuzz 与性能

| 建议目标 | 覆盖任务 |
|---|---|
| `fuzz_toml_string_boundary` | RF2-09 |
| `fuzz_arrow_frame_candidate` | RF2-09 |
| `fuzz_rewrite_map` | RF3-07 |
| `fuzz_desugar_parse_scope` | RF3-07、RF8-02 |
| `fuzz_complete_rules_compile` | RF8-02 |
| `RulesCompilerBenchmark` | RF1-09、RF8-03 |
| `RulesPublisherBenchmark` | RF8-03～RF8-04 |

---

## 24. 命令回填清单

以下命令在对应目标创建后必须实际可执行并回填真实路径：

### 24.1 Host C++

```powershell
cmake -S . -B build -DPATHGUARD_BUILD_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Release -R "rules|policy" --output-on-failure
```

### 24.2 Rust（若选择）

```powershell
cargo test --manifest-path rules-compiler/Cargo.toml --locked
cargo test --manifest-path rules-compiler/Cargo.toml --release --locked
cargo build --manifest-path rules-compiler/Cargo.toml --release --locked
```

### 24.3 Android

```powershell
./scripts/build-native.ps1 -Abi arm64-v8a
```

### 24.4 toml-test fixture 校验

必须提供一个仓库内脚本，完成：

- 验证上游版本 `v2.2.0`。
- 验证所有 fixture 属于 `files-toml-1.0.0`。
- 验证 LICENSE、manifest 和 SHA-256。
- 禁止 TOML 1.1-only fixture 混入 format 1。

### 24.5 ELF/link-map

必须把具体 `llvm-readelf`、`nm` 或 NDK 等价命令写入 RF6-05 的测试脚本，并在包构建前自动执行；不能只保留人工检查说明。

---

## 25. 最终执行顺序

1. 完成 RF0，不改变生产规则语义。
2. 完成 RF1，以数据决定 Rust/C++/严格 TOML，并删除未选原型。
3. 若保留箭头，依次完成 RF2、RF3；若不保留，直接进入 RF4。
4. 完成 RF4 的唯一 parser、来源/作用域和 RulesDocument。
5. 完成 RF5 的语义、admission、207-byte encoder/verifier。
6. 完成 RF6 的唯一构建和语言边界。
7. 完成 RF7 的 daemon/CLI/Publisher 和破坏性格式切换。
8. 完成 RF8 的 fuzz、性能、真机、恢复和全仓清理。
9. RF9 只在核心交付后按真实需求实施。

这条顺序保证最不确定、最可能推翻后续工作的决策最先验证；同时以 Characterization、Golden、TOML 1.0 fixture、属性测试、Fuzz、Host/Android 集成和最终 ELF 依赖形成逐层证据。整个实现只增加当前配置重构和箭头决策真正需要的实体，避免把有限语法糖演化成第二套 TOML parser 或通用 DSL。

---

## 26. 依据与参考资料

### 26.1 本项目设计与现状

- `docs/00-architecture-design.md`：控制面/数据面、规则语言和 Rust 边界。
- `docs/02-performance-audit-and-optimization-plan.md`：daemon 指标和数据面性能隔离。
- `docs/03-redirect-subsystem-design.md`：当前 R1 状态、执行域、冲突与真机矩阵。
- `docs/04-rule-file-refactoring-design.md`：规则格式、编译管线、模型、发布和迁移。
- `docs/05-rule-arrow-desugarer-design.md`：D0、词法、RewriteMap、generated binding 和诊断。
- `docs/adr/0002-policy-format-v4.md`：唯一 format v5 与 207-byte golden。
- `docs/adr/0003-mount-timeout-and-mutation-lease.md`、`0004-capability-bitset.md`、`0005-dual-mount-backends.md`、`0006-saf-provider-virtualization.md`：admission、失败和 capability 边界。

### 26.2 审核与测试资料

- `refer/脱糖器审核报告.md`：局部数组元素检查、错误优先级、source map、性能与 fuzz 审核意见；已落实到 RF1～RF4、RF8。
- `refer/toml-test-main`：toml-lang 官方生态的 language-agnostic 测试套件，本地版本 `v2.2.0`。
- `refer/toml-test-main/tests/files-toml-1.0.0`：format 1 fixture 的唯一筛选清单。
- `refer/toml-test-main/README.md`：不直接复制全部 tests、实现自定行为和嵌套深度免责声明。

### 26.3 参考项目取舍

- `refer/pathguard-reference-repos/ZygiskNext-master` 证明 Rust daemon 与独立 Zygisk loader 可以保持进程/产物隔离，但不构成把 Rust 链入 PathGuard Zygisk 的理由。
- `refer/Storage-redirection-X-Public-main` 等项目只用于验证 Android Root 工程中 Rust/Native 工具链的可行性和部署形态，不复制其规则语义或安全声明。
- 参考项目不能替代 PathGuard 自己的 207-byte golden、C ABI、ELF 隔离、真机和故障注入测试。
