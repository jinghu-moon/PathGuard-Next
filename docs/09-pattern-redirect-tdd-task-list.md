# PathGuard Next Pattern Redirect TDD 执行清单

> 状态：原 pattern v6 当前设备范围已闭环；2026-08-01 根据用户决策启动 Provider contract
> adapter（方案 B）后续工作，T-34～R-58 的生产实现及 V-64 alioth/Android 13 公共操作基线已完成，
> bit 17 在真实 adapter profile、虚拟映射、FD identity、reverse 和 restart 全部通过前保持
> `unsupported`。2026-08-02 已启动并完成 Shared Target Namespace Projection 的 Host/双 ABI
> 实现：多源共享 target root 编译为 `_pg/v1/ns_<id>`，mount/Provider 同投影共享 Namespace，
> static reverse 不再依赖 strong identity；真机 V-69 已在 `0.1.45-dev`/`0.1.46-dev` 完成当前设备
> 可构造范围，caller-scoped query/open 与 exact collision 按设备/应用入口不满足记为
> `unsupported/not_observed`；活动 Namespace 被外部删除后只验证进程重启恢复，不宣称热恢复。
> 原 V-46 `adapter-only` 决策形成合法阻断，fanotify/Export
> 因当前设备 `CONFIG_FANOTIFY` disabled 按用户授权跳过并保持 unsupported/not_observed。第二部分已有单设备
> LocalSend/Provider 生命周期补充证据；2026-08-01 已增加一台 Android 16/myron/SukiSU Ultra
> 的 0.1.19 Provider 修复、50 次启动和接收批次；0.1.24 又补齐 Provider per-action 状态、
> Provider restart、死亡 PID 状态回收与 10 次冷启动。上述批次仅为 partial observed；用户已于
> 2026-08-01 明确跳过第二设备验证，其余 ROM、root framework、kernel tier 和 arm32 保持
> `not_observed` 且不纳入当前待执行范围；未覆盖 operation/state 矩阵按当前设备能力标记
> `unsupported/not_observed`。2026-08-02 的 V-68 在 `0.1.44-dev` 上完成当前设备可执行的
> LocalSend TXT/JPG forward、双 Provider、admission 和 fail-open 子项；alioth 的 FUSE/backing
> 均无 `STATX_BTIME` 且 `name_to_handle_at=ENOSYS`，strong-identity 依赖矩阵按设备不满足跳过，
> bit 17 继续清零。
>
> 基准设计：`docs/08-pattern-redirect-design.md` v0.9
>
> 单任务时间盒：2～4 小时；超过 4 小时必须再次拆分

## 0. 执行约定

本清单保护的是设计文档 1.3 节定义的 C1～C6 核心场景，而不是 format 1、policy v5、旧类名或旧 CLI 参数。项目尚未发布，允许一次性切换 schema、二进制格式和内部接口；但任何影响既有模块或行为的工作包都必须按 `V-before -> T-red -> I-green -> R-refactor -> V-after` 执行。不得把同一主题的 after 验证推迟到数个工作包之后。

执行规则：

- `[红]` 任务结束时必须保留可重复的失败证据，失败原因只能是待实现能力缺失，不能是测试本身无法编译或环境损坏。
- `[绿]` 只实现使对应红测通过的最少行为，不顺带实现后续任务。
- `[重构]` 开始前全部相关测试必须为绿；重构不得改变已冻结的外部语义。
- `[验证]` 的 before/after 报告必须记录 change ID、before/after commit、设备/Android/kernel/root framework/SELinux、规则/policy/module hash、场景 ID、精确步骤、实际结果、证据路径和结论。
- 对比结果只能标记为 `unchanged`、`planned_break` 或 `unexpected_regression`。存在未解释的核心回归时立即停止后续主题。
- 每个 C++ 单元测试都进入 CTest；parser/matcher/reader 的边界输入进入 fuzz corpus；协议变更同步更新 host golden、NDK ABI gate、设备 probe、CLI/status 和文档。
- 真机任务默认至少覆盖 arm64、Android 11+、SELinux Enforcing；涉及兼容性声明时按任务指定扩展 ROM、kernel、ABI 和 root framework 矩阵。

建议证据目录：

```text
tests/baseline/pattern-v6/<change-id>/
build/device-evidence/<change-id>/
tests/golden/policy-v6/
tests/fuzz/seeds/pattern-v1/
```

## 第一部分：基础与准备

> 执行结果（2026-07-30）：本部分全部完成。受版本控制的环境、Host/真机基线、
> 决策记录、红绿重构证据及重放结果统一索引于
> `tests/baseline/pattern-v6/README.md`。最终 Release configure/build 通过，CTest `59/59`
> 通过，`git diff --check` 通过；未发现非预期核心行为回归。

| 已完成任务 | 状态 | 主要证据 |
| --- | --- | --- |
| V-01～V-07 | complete | 环境、Host/设备基线、追踪矩阵、当前差距、参考项目与官方约束 |
| V-08～V-10 | complete | ADR-0015 决策门、ADR-0016 format 6、ADR-0017 provenance |
| T-01/I-01/R-01/V-11 | complete | 对比报告红测、validator、共享 schema 与故障重放 |
| T-02/I-02/R-02 | complete | Pattern harness 红测、最小骨架、共享 corpus/limits |
| 第一部分最终验证 | complete | Release `59/59`、专项 `6/6`、diff check |

### V-01 [验证] 固化工作区、工具链与执行命令

- **任务描述**：记录当前 commit、dirty files、CMake/编译器/NDK/ADB 版本、可用 ABI 和测试命令；确认不把 `build-release/` 或用户已有改动纳入任务变更。
- **验收标准**：生成可复用的环境清单；另一名开发者能按清单完成 configure、Release build、CTest list 和设备连接检查。
- **关联需求**：设计 1.1、1.3、11.0；项目“前后对比测试”原则。
- **依赖/时间盒**：无；2 小时。

### V-02 [验证] 归档现有 Host 行为基线

- **任务描述**：在未改代码的提交上执行 Release configure/build/CTest，保存完整测试清单、总数、耗时和失败输出；特别标记 policy v5、mount、Provider、rules 和 status 测试。
- **验收标准**：基线报告记录当前观测的 `52/52` 结果或解释任何差异，附命令与日志 hash；`expected-host-tests.txt` 与实际测试名一致。
- **关联需求**：设计 1.3、2.1、11.0；`tests/baseline/host-tests.md`。
- **依赖/时间盒**：V-01；2 小时。

### V-03 [验证] 归档 C1～C5 真机基线

- **任务描述**：用 format 1/policy v5 重放 literal deny、literal redirect、LocalSend/Provider 代写、多源映射和故障隔离；记录 UID/user、最终路径、errno、runtime status、mountinfo 和关键日志。
- **验收标准**：C1～C5 每个场景都有可审计报告和原始证据；不能执行的设备层级明确标记为 `not_observed`，不得写成通过。
- **关联需求**：设计 1.3、4.1、11.0、11.3；ADR-0003、0005、0006。
- **依赖/时间盒**：V-01；4 小时。

### V-04 [验证] 建立核心场景需求追踪矩阵

- **任务描述**：把 C1～C6 分解为输入规则、执行主体、操作、预期位置/errno、能力位、operation mask、Host 测试、设备测试和证据字段。
- **验收标准**：每个核心结果至少映射一个自动测试和一个适用的真机步骤；每个后续任务可引用矩阵行 ID。
- **关联需求**：设计 1.3、7.1、11；ADR-0012、0013。
- **依赖/时间盒**：V-02、V-03；3 小时。

### V-05 [验证] 核对当前代码与目标设计差距

- **任务描述**：逐项盘点 format 1/v5、`RedirectRule`、`PathRule`、Provider prefix mapper、capability bit 0～4/8～11、policy load 与 status；标出要替换、复用和删除的边界。
- **验收标准**：差距表至少覆盖 rules、core、daemon、zygisk、native、CLI、protocol 和 tests；每项有 owner module 与后续任务 ID。
- **关联需求**：设计 2.1、5、7、8、10；ADR-0002、0004、0006。
- **依赖/时间盒**：V-04；3 小时。

### V-06 [验证] 归档参考项目证据与禁止照搬项

- **任务描述**：记录 KernelSU/Magisk 的 module/namespace 生命周期、MaterialCleaner 的 Provider/MediaStore 与观察器边界、cleanerhooks 的 Hook 常驻约束、NoMount 的 VFS lookup/readdir/reverse 机制；同时写明哪些能力不能直接移植。
- **验收标准**：证据表包含仓内文件路径/行号、可借鉴契约、PathGuard 不采用原因；不把 root framework fallback 当成 action admission fallback。
- **关联需求**：设计 3、7.4～7.8、13；ADR-0006、0010。
- **依赖/时间盒**：V-05；3 小时。

### V-07 [验证] 核对官方 API 与内核语义

- **任务描述**：以 Android scoped storage、FUSE passthrough、MediaProvider、DocumentsProvider，Linux `openat2`/`fanotify` 和 POSIX `fnmatch`/`pthread_atfork` 为准建立外部约束表。
- **验收标准**：每条外部结论附官方 URL、访问日期和对应测试任务；明确 MediaProvider ABI 不按 ROM 名称推断、FUSE passthrough 路径、fanotify overflow、`openat2` probe 及 atfork child 限制。
- **关联需求**：设计 3.4～3.6、5.5、7.4～7.7、9；ADR-0011～0014。
- **依赖/时间盒**：V-06；2 小时。

### V-08 [验证] 关闭 ADR-0015 决策门

- **任务描述**：用真实规则样本、候选桶退化样本和微基准评审 `select.except`；修正 ADR 中旧 `ScopeKey=(uid,user,package)` 表述，使其服从 UID/user 最低可信边界和可信 package attribution。
- **验收标准**：ADR-0015 明确变为 Accepted 或 Deferred/Rejected；若 Accepted，冻结 except 预算、canonical form、冲突语义和 format 6 table；若未接受，后续实现不得编码 except。
- **关联需求**：设计 4.5、6.1～6.2、8、10 P0；ADR-0015。
- **依赖/时间盒**：V-04、V-07；4 小时。

### V-09 [验证] 冻结 policy format 6 ADR

- **任务描述**：新增 supersede ADR-0002 的 format 6 决策，冻结 header、表布局、计数/偏移/对齐、硬上限、canonical encoding、未知字段和 v5 拒绝语义；按 V-08 结果决定是否包含 ExceptRefTable。
- **验收标准**：ADR 状态 Accepted；compiler/reader/CLI/device probe 共享的唯一常量和 golden vector 规则无悬空字段。
- **关联需求**：设计 4.1、5、8、10 P1；ADR-0002、0014、0015。
- **依赖/时间盒**：V-08；4 小时。

### V-10 [验证] 冻结 route provenance ADR

- **任务描述**：定义多源到同目标的 prepare/commit/abort、identity key、generation、持久化、rename/delete、崩溃恢复、GC 和 `AmbiguousReverse`；明确不能恢复 v5 canonical visible source fallback。
- **验收标准**：ADR 状态 Accepted，写清事务边界、存储格式和故障语义；P3 任务不存在依赖未定义行为。
- **关联需求**：设计 6.3、7.3、8、10 P3、11.2；C4。
- **依赖/时间盒**：V-09；4 小时。

### T-01 [红] 为前后对比报告建立失败校验测试

- **任务描述**：为缺失 commit、环境、hash、场景、步骤、actual、classification 或 evidence path 的报告编写失败测试，并加入 CTest。
- **验收标准**：新测试稳定失败，输出逐字段诊断；现有历史报告不被静默当作新格式有效报告。
- **关联需求**：设计 1.3、11.0；项目安全机制。
- **依赖/时间盒**：V-04；2 小时。

### I-01 [绿] 实现前后对比报告校验器

- **任务描述**：实现最小 schema/脚本，使完整报告通过、缺字段报告失败；支持 `unchanged/planned_break/unexpected_regression/not_observed` 固定枚举。
- **验收标准**：T-01 全绿；校验器可在 Windows Host CI 和本地 PowerShell 调用。
- **关联需求**：设计 1.3、11.0。
- **依赖/时间盒**：T-01；3 小时。

### R-01 [重构] 统一基线与设备报告元数据

- **任务描述**：抽取共享 manifest/fixture，消除 Host、rules device 和 R1 报告重复字段；保留历史报告只读，不批量改写旧证据。
- **验收标准**：T-01 保持全绿；新增报告模板只有一个字段定义来源；无历史证据丢失。
- **关联需求**：设计 1.3、11.0；DRY、单一职责。
- **依赖/时间盒**：I-01；2 小时。

### V-11 [验证] 重放报告校验安全网

- **任务描述**：用 V-02/V-03 的副本生成一份合法报告和三份故意损坏报告，运行校验器并归档结果。
- **验收标准**：合法报告通过，损坏报告均以预期 reason 失败；对现有核心行为没有任何运行时影响。
- **关联需求**：设计 11.0。
- **依赖/时间盒**：R-01；2 小时。

### T-02 [红] 建立 Pattern benchmark/fuzz 构建门

- **任务描述**：先添加会因目标缺失而失败的 CTest，要求存在 tokenizer/matcher fuzz target、固定 corpus manifest 和 0/1/多候选基准场景。
- **验收标准**：测试只因 harness 尚未实现而失败；定义 ASan/UBSan 可选运行命令和 Release benchmark 输出格式。
- **关联需求**：设计 3.7、5.3～5.4、11.1～11.4；ADR-0011、0014、0015。
- **依赖/时间盒**：V-09；2 小时。

### I-02 [绿] 实现 benchmark/fuzz 空骨架

- **任务描述**：增加最小可编译 target、seed manifest、统一随机种子与 JSON/TSV 指标输出，不实现匹配语义。
- **验收标准**：T-02 全绿；fuzzer 能消费空/短输入，benchmark 能输出 schema 版本和环境信息。
- **关联需求**：设计 11.1、11.4。
- **依赖/时间盒**：T-02；3 小时。

### R-02 [重构] 统一测试 corpus 与限制 profile

- **任务描述**：让 unit/property/fuzz/benchmark 共用 pattern limits 和 seed loader，避免测试中复制生产常量。
- **验收标准**：T-02 全绿；任何预算常量只有一个生产定义，测试只引用或显式验证该定义。
- **关联需求**：设计 5.3、5.4、8、11.1；KISS、DRY。
- **依赖/时间盒**：I-02；2 小时。

## 第二部分：核心功能开发

本部分直接交付 C1～C6。每个带 before/after 的主题都是独立合入门；不得在 after 失败时继续下一主题。

> 执行结果（2026-07-30）：T-03～T-24、I-03～I-24、R-03～R-24 的可自动化实现与
> 验证完成；Host Release CTest `75/75`、Android NDK arm64-v8a/armeabi-v7a、Zygisk
> `APP_STL=none` 构建通过，`git diff --check` 通过。V-12～V-39 的 Host/契约证据已归档。
> 2026-07-31 已在一台 Xiaomi Android 13/Magisk 设备补测 Provider early-start 恢复与 LocalSend
> TXT/JPG 实际接收；Provider 50 次冷启动、真实 strong identity 与多 ROM/kernel 兼容矩阵仍
> 记录为 `not_observed`，不伪报通过。自动化范围内
> unexpected regression 为 0。Provider query/insert/reverse 生产 ABI adapter 尚未准入，bit 17
> 明确保持 `unsupported`；C3 的前向 path-I/O 最低闭环完成，不宣称复合视图已在设备 active。

| 已完成任务 | 状态 | 主要证据 |
| --- | --- | --- |
| V-12～V-14、T-03～R-11 | complete | format 2、Glob v1、brace、selector/index/evaluator/OperationPlan |
| V-15～V-23、T-12～R-16 | complete | format 6、admission、hazard snapshot、MountPlan adapter |
| T-17～R-20 | complete（bit 17 runtime unsupported） | app-path/Provider path-I/O、operation mask、composite admission、Hook lifecycle；无伪 active |
| V-24～V-31 | complete + partial device observed | Host 契约通过；单设备 LocalSend/Provider 生命周期已观察；query/insert/reverse unsupported，Provider soak/多 ROM 未观察 |
| T-21～R-24 | complete | provenance create/rename/delete、WAL/CRC、resolver probe、failure policy |
| V-32～V-39 | complete + device not_observed | Host 故障矩阵通过；真实设备恢复/TOCTOU 未观察 |

### V-12 [验证] 记录 rules schema 与 canonical policy 改造前基线

- **任务描述**：重放 parser/desugar/semantic/conflict/golden 测试和三份代表性 format 1 配置；列出 format 2 切换中的计划内 schema 破坏，但本主题暂不切换生产 reader。
- **验收标准**：报告固定旧配置的 parse/canonical/error 文本和 policy v5 hash；核心 literal 语义标为必须保持。
- **关联需求**：设计 2.1、4.1～4.4、11.0～11.1；ADR-0002、0014。
- **依赖/时间盒**：V-09、V-11；3 小时。

### T-03 [红] 定义统一 Selector/Action 文档模型

- **任务描述**：为 format 2 的 `select.root/glob/type`、deny/redirect、priority/preserve/collision/enforcement/provider intent 编写 parser 与 semantic 失败测试；同时测试 format 1 被 format 2 parser 拒绝。
- **验收标准**：测试因缺少新 Document/Canonical IR 而失败；错误位置和 error code 预期已冻结。
- **关联需求**：设计 2.2、4.1～4.4、5.1；ADR-0012～0014。
- **依赖/时间盒**：V-12；4 小时。

### I-03 [绿] 实现最小 format 2 文档与 Canonical IR

- **任务描述**：新增值对象 `PathSelector`/`ActionRule` 输入模型和最小 parser/semantic 转换；仅支持当前红测字段，不编码 binary、不执行 pattern。
- **验收标准**：T-03 全绿；literal 和 glob selector 在 canonical policy 中可区分；非法路径/枚举值被拒绝。
- **关联需求**：设计 4、5.1、10 P0。
- **依赖/时间盒**：T-03；4 小时。

### R-03 [重构] 分离语法解析、语义校验与 IR 构建

- **任务描述**：拆开 TOML adapter、schema validation、canonicalization；让 deny/redirect 共用 selector 构建器，删除新增路径中的动作专用 pattern 解析。
- **验收标准**：T-03 和既有 rules 测试全绿；selector canonicalization 只有一个实现；模块依赖方向不指向 Zygisk/Android。
- **关联需求**：设计 5.1～5.2、7.1；SOLID、DRY。
- **依赖/时间盒**：I-03；3 小时。

### V-13 [验证] 对比 rules 模型改造结果

- **任务描述**：重放 V-12；用语义等价 format 2 fixture 比较 literal deny/redirect canonical 结果，检查计划内错误文本/schema 差异。
- **验收标准**：计划内破坏已列明；C1/C2 语义无意外差异；全部 unexpected regression 为零。
- **关联需求**：设计 1.3、4.1、11.0。
- **依赖/时间盒**：R-03；2 小时。

### T-04 [红] 冻结 Glob v1 literal、`*`、`?` 与转义语义

- **任务描述**：表驱动测试零长度 `*`、单 scalar `?`、不跨 `/`、dotfile、UTF-8 scalar、转义元字符、TOML 双重转义和尾随反斜杠失败。
- **验收标准**：测试因 tokenizer/matcher 未实现而失败；用例同时包含正例、反例和 expected token stream。
- **关联需求**：设计 4.2、5.3、11.1～11.2；ADR-0014。
- **依赖/时间盒**：R-03、R-02；3 小时。

### I-04 [绿] 实现无回溯组件内 Glob matcher

- **任务描述**：实现 UTF-8 校验、literal、`STAR_COMPONENT`、`ONE_COMPONENT_CHAR` 和 escape token；使用 caller 提供 scratch，不在热路径分配。
- **验收标准**：T-04 全绿；非法 runtime UTF-8 返回结构化 `InvalidPathEncoding` 而非普通 NoMatch。
- **关联需求**：设计 4.2、5.2～5.3、9.3。
- **依赖/时间盒**：T-04；4 小时。

### R-04 [重构] 规范化 tokenizer 与 matcher 状态机

- **任务描述**：合并连续 literal token，去除递归/回溯路径；建立只读 token span API，并把 UTF-8 解码封装为独立纯函数。
- **验收标准**：T-04 全绿；静态检查确认 matcher 无 heap allocation、无 regex 依赖、无动作语义。
- **关联需求**：设计 5.2～5.3、11.4；KISS、单一职责。
- **依赖/时间盒**：I-04；3 小时。

### T-05 [红] 冻结完整组件 `**` 语义

- **任务描述**：测试 `**/*.jpg` 匹配 root 直接项和任意深度后代、`*.jpg` 不跨层；测试 bare `**`、`a/**` 不匹配 root/`a` 本身，以及 `ab**`/`**x` 编译失败。
- **验收标准**：失败仅因 `GLOBSTAR_COMPONENT` 未实现；零层、多层和目录本身边界均有反例。
- **关联需求**：设计 4.2、5.3、11.1～11.2；ADR-0014。
- **依赖/时间盒**：R-04；2 小时。

### I-05 [绿] 实现 `GLOBSTAR_COMPONENT`

- **任务描述**：在组件状态机中实现零到多完整目录组件跳过，保持线性/有界状态推进，不引入递归枚举。
- **验收标准**：T-05 全绿；100 层路径在预算内稳定完成，`/` 边界不被普通 star 吞并。
- **关联需求**：设计 5.3、11.4。
- **依赖/时间盒**：T-05；4 小时。

### R-05 [重构] 合并 globstar 与普通组件转移逻辑

- **任务描述**：抽取组件边界游标和状态集合，减少特殊分支；保留明确的最大状态/深度检查。
- **验收标准**：T-04/T-05 全绿；benchmark 不出现超过已记录噪声阈值的退化。
- **关联需求**：设计 5.3、5.4、11.4。
- **依赖/时间盒**：I-05；3 小时。

### T-06 [红] 冻结字符类语法与 canonical 别名

- **任务描述**：测试 `[abc]`、range、多个 range、`[!abc]`、`[^abc]` 等价、转义 `]`/`-`、ASCII endpoint、补集不匹配 `/`；拒绝空类、反向 range、named/equivalence/collating class 和非 ASCII endpoint。
- **验收标准**：测试因 CHAR_CLASS 缺失而失败；别名必须生成相同 canonical tokens 和 selector ID。
- **关联需求**：设计 4.2、4.4、5.3、11.1；ADR-0014。
- **依赖/时间盒**：R-05；3 小时。

### I-06 [绿] 实现 128-bit ASCII CharacterClass

- **任务描述**：实现编译期 bitmap、negated flag 和 runtime scalar 检查；非 ASCII scalar 对正类不命中、对补类按冻结语义处理但永不匹配 `/`。
- **验收标准**：T-06 全绿；CharacterClassTable 可稳定 canonical 编码。
- **关联需求**：设计 4.2、5.3、8；ADR-0014。
- **依赖/时间盒**：T-06；3 小时。

### R-06 [重构] 统一字符类解析诊断

- **任务描述**：将 class lexer 与通用 escape/offset tracking 复用，确保 parser error 精确指向源字节且不泄漏 locale 行为。
- **验收标准**：T-06 全绿；fuzz 对任意 byte input 不崩溃、不越界，错误 code 稳定。
- **关联需求**：设计 11.1、12；ADR-0014。
- **依赖/时间盒**：I-06；2 小时。

### T-07 [红] 冻结 brace 展开与 pattern 预算

- **任务描述**：测试 `{jpg,jpeg,png}` 在 glob parser 前展开，拒绝嵌套、range、空项、slash、glob metacharacter；测试 32 结果、64 KiB、token/depth/path/selector 上限和原子失败。
- **验收标准**：测试因 host expander/limits 未实现而失败；任何超限用例要求整条规则失败、不得截断。
- **关联需求**：设计 4.2、5.3～5.4、8、11.1；ADR-0014。
- **依赖/时间盒**：R-06；3 小时。

### I-07 [绿] 实现宿主 brace 展开和统一预算校验

- **任务描述**：在 parser 前有界展开为普通 selector/action；实现 compiler/reader 共用上限常量，不向 Pattern IR 写 BRACE token。
- **验收标准**：T-07 全绿；展开后的 policy 与手写多规则 canonical 等价；超限不发布候选 policy。
- **关联需求**：设计 4.2、5.3、8；ADR-0014。
- **依赖/时间盒**：T-07；4 小时。

### R-07 [重构] 统一编译期资源预算报告

- **任务描述**：集中 selector/action/token/class/string/brace 预算计算和诊断，删除各 parser 的重复 magic number。
- **验收标准**：T-07 全绿；每个超限错误包含 limit、actual、rule ID，不暴露半成品 IR。
- **关联需求**：设计 5.3、8、11.1；DRY。
- **依赖/时间盒**：I-07；2 小时。

### T-08 [红] 冻结 selector canonicalization、去重和 specificity

- **任务描述**：测试 root/type/token 相同则去重，不同 app 可共享无归属 selector；测试 `[^]` 别名、brace 重复、literal score、depth 和固定后缀的确定性计算。
- **验收标准**：测试因 canonical selector table 缺失而失败；package scope 只能通过 PackageTable→ActionTable 引用体现。
- **关联需求**：设计 4.4、5.1、5.4、6.1、8；ADR-0014。
- **依赖/时间盒**：R-07；3 小时。

### I-08 [绿] 实现 canonical SelectorTable 构建器

- **任务描述**：按 canonical key 分配稳定 SelectorId，预计算 specificity/first literal/fixed extension 和 action range，不让 selector 携带 package ownership。
- **验收标准**：T-08 全绿；相同输入跨运行生成相同表顺序和 generation。
- **关联需求**：设计 5.1、5.4、8。
- **依赖/时间盒**：T-08；4 小时。

### R-08 [重构] 分离 selector identity 与 action ownership

- **任务描述**：收敛 PackageTable→ActionTable→SelectorTable 引用方向，删除任何用 selector 反推 package/UID 的代码。
- **验收标准**：T-08 全绿；加入 shared selector 跨 app 隔离回归用例；无跨 app 动作泄漏。
- **关联需求**：设计 5.1、8、9.2；SOLID。
- **依赖/时间盒**：I-08；3 小时。

### T-09 [红] 冻结候选索引与透传快路径

- **任务描述**：测试 `caller_uid -> user_id -> attribution/package -> root -> literal/extension/general bucket`；未命中 scope/root 不能调用 token matcher；测试 1000 条无前缀模式预算失败和 general bucket 上限。
- **验收标准**：测试能观测 matcher invocation count，当前因索引缺失而失败；错误 scope 必须为 O(1) 透传路径。
- **关联需求**：设计 5.4、7.6、9.2、11.2/11.4；ADR-0012、0013、0015。
- **依赖/时间盒**：R-08；4 小时。

### I-09 [绿] 实现不可变 CandidateIndex

- **任务描述**：构建按 UID/user/root/首 literal/extension 分桶的只读索引和有界 general bucket；预先过滤 execution-domain admission 未通过的 action。
- **验收标准**：T-09 全绿；索引构建只发生在 snapshot load；热路径无动态分桶和 heap allocation。
- **关联需求**：设计 5.4、5.5、7.6。
- **依赖/时间盒**：T-09；4 小时。

### R-09 [重构] 统一 literal 与 glob 候选入口

- **任务描述**：让 LiteralPrefix 和 Glob 共用 scope/root 索引框架，后端 plan 在其后分域；移除 Provider/app-path 各自候选扫描的新增重复路径。
- **验收标准**：T-09 全绿；literal 热路径与旧基准相比无显著回退；索引接口与 Android ABI 无关。
- **关联需求**：设计 5.4、7.1～7.3；DRY。
- **依赖/时间盒**：I-09；3 小时。

### T-10 [红] 冻结 ActionEvaluator 决策顺序和副作用模型

- **任务描述**：覆盖 scope→candidate→match→Deny→Redirect→Pass，priority/specificity/RuleId tie-break，0/1 命中快路径，2+ 命中有界扫描，以及 Observe/Export 不覆盖主处置。
- **验收标准**：测试因 evaluator 缺失而失败；每个预期含 reason、RuleId/SelectorId、冲突 ID 和 generations。
- **关联需求**：设计 5.2、5.6、6.1～6.2、11.2。
- **依赖/时间盒**：R-09；4 小时。

### I-10 [绿] 实现无排序 ActionEvaluator

- **任务描述**：扫描命中 selector 的连续 action range，比较 precedence/priority/specificity/RuleId；实现 0/1 命中快路径和 primary/effect mask。
- **验收标准**：T-10 全绿；运行时不反查 selector owner、不排序、不分配；Deny 不被 capability fail-open 覆盖。
- **关联需求**：设计 5.2、5.6、6.1、9.3。
- **依赖/时间盒**：T-10；4 小时。

### R-10 [重构] 统一 Decision 与诊断 ID

- **任务描述**：抽取后端无关 Decision/Reason/diagnostic payload，消除 mount/Provider/app-path 自定义布尔结果；保持执行适配器只翻译 errno/target。
- **验收标准**：T-10 全绿；所有核心 reason 可由 status/explain 消费；Pass 不产生审计 I/O。
- **关联需求**：设计 5.2、5.6、7.1、11.4。
- **依赖/时间盒**：I-10；3 小时。

### T-11 [红] 冻结双 operand 与碰撞语义

- **任务描述**：测试 rename/link 在同一 snapshot 匹配两端，拒绝跨 domain/不一致 scope；编译期证明的重叠冲突失败，运行时同 target path 返回 `EEXIST`，active deny 返回 `EACCES`。
- **验收标准**：测试因 multi-operand evaluator/collision guard 缺失而失败；不得出现一端已重写另一端透传的半操作。
- **关联需求**：设计 5.2、6.2～6.3、9.3、11.2；C4/C5。
- **依赖/时间盒**：R-10；4 小时。

### I-11 [绿] 实现 multi-operand 决策与 collision reject

- **任务描述**：一个 snapshot guard 内匹配全部 operand，再一次性决策；实现首版唯一 collision=`reject` 和 target identity/path 碰撞检查。
- **验收标准**：T-11 全绿；rename/link 原子失败，未发生部分文件系统变更；错误含双方 rule/selector ID。
- **关联需求**：设计 5.2、6.2～6.3。
- **依赖/时间盒**：T-11；4 小时。

### R-11 [重构] 提取 OperationPlan

- **任务描述**：把决策与实际 syscall 参数构造分开，以固定容量 `OperationPlan` 表示 1/2 operand、target 和 rollback/provenance 钩子。
- **验收标准**：T-11 全绿；ActionEvaluator 仍为纯函数；后端不能绕过统一冲突检查。
- **关联需求**：设计 5.2、7.1、9.3；单一职责。
- **依赖/时间盒**：I-11；3 小时。

### V-14 [验证] 记录 policy v5 切换前全链路基线

- **任务描述**：冻结 compiler 输出、daemon publish、Zygisk/Provider reader、CLI/status、默认配置和 device probe 的 v5 实际行为与 hashes。
- **验收标准**：C1～C5、损坏 v5、错误 version、旧 CLI 输出均有 before 证据；计划内删除项已列明。
- **关联需求**：设计 4.1、8、10 P1、11.0；ADR-0002。
- **依赖/时间盒**：V-13、V-09；4 小时。

### T-12 [红] 冻结 policy format 6 golden 与拒绝矩阵

- **任务描述**：按 format 6 ADR 编写 header/table/offset/alignment/generation golden；测试 truncated、overflow、overlap、unknown enum/flags、预算超限、v5 输入和 trailing bytes 拒绝。
- **验收标准**：encoder/reader 测试均因 v6 未实现而失败；golden 独立于生产 encoder 生成。
- **关联需求**：设计 8、11.1；format 6 ADR。
- **依赖/时间盒**：V-14、R-11；4 小时。

### I-12 [绿] 实现 format 6 encoder/reader 并一次性切换

- **任务描述**：同步更新 compiler、core reader、daemon、Zygisk/Provider、CLI/status、fixtures、共享头和 probes；新 reader 明确拒绝 v5，不保留双 reader/生产迁移器。
- **验收标准**：T-12 全绿；所有生产组件只读写 v6；旧格式返回 version mismatch 并 fail-open，不做部分解析。
- **关联需求**：设计 4.1、8、10 P1；ADR-0002 superseding ADR。
- **依赖/时间盒**：T-12；4 小时；若无法在 4 小时内同步全部组件，按组件拆成连续 I-12a/I-12b，但不得发布半套协议。

### R-12 [重构] 收敛唯一 policy v6 结构定义

- **任务描述**：消除 compiler/runtime 的重复 layout 常量，分离 validated view 与 owned builder；删除生产路径中的 v5 reader 分支，但暂留 baseline fixture。
- **验收标准**：T-12 和全部 policy tests 全绿；NDK reader 不链接 rules compiler；format 常量只有一个共享来源。
- **关联需求**：设计 8、10 P1；DRY、依赖倒置。
- **依赖/时间盒**：I-12；3 小时。

### V-15 [验证] 对比 v5→v6 一次性切换

- **任务描述**：重放 V-14，使用语义等价 format 2/v6 配置；逐项比较 C1～C5、fail-open、generation 和 status。
- **验收标准**：旧 TOML/v5 拒绝与 CLI 字段变化标为 planned_break；核心结果均 unchanged；无 unexpected regression。
- **关联需求**：设计 1.3、4.1、8、11.0。
- **依赖/时间盒**：R-12；4 小时。

### V-16 [验证] 记录 capability/admission 改造前基线

- **任务描述**：记录 bit 0～4/8～11、Provider hook flags、`provider_compat`、mount backend selection 和 status 的实际值；列出 bit 16～19 与 operation mask 的计划内协议变化。
- **验收标准**：Host/device capability snapshot、原始 probe 日志和 status JSON 已归档；“hook installed”与“action active”被分别记录。
- **关联需求**：设计 2.1、7.6、8、11.0；ADR-0004、0012、0013。
- **依赖/时间盒**：V-15；3 小时。

### T-13 [红] 冻结 capability bit 16～19 与 action admission

- **任务描述**：测试 provider caller UID/query-insert/FUSE complete/app-path 位、operation mask、execution domain 和 required subset；覆盖部分能力、stale generation、未知位和禁止隐式 fallback。
- **验收标准**：测试因共享协议/AdmissionEvaluator 缺失而失败；每个组合预期 `active/inactive/unsupported`、missing bits 与 reason。
- **关联需求**：设计 7.1、7.6、8；ADR-0004、0012、0013。
- **依赖/时间盒**：V-16；4 小时。

### I-13 [绿] 实现共享 capability snapshot 与准入器

- **任务描述**：在共享头、daemon probe、policy action、runtime reader/status 中落地 bit 16～19 和 operation mask；加载 snapshot 时一次性准入/剔除 action。
- **验收标准**：T-13 全绿；热路径不做 capability probe；缺能力只使对应 action fail-open，active deny/collision 不被降级。
- **关联需求**：设计 5.5、7.6、9.3；ADR-0012、0013。
- **依赖/时间盒**：T-13；4 小时。

### R-13 [重构] 统一 execution domain admission

- **任务描述**：用一个纯函数处理 `domain + required_capabilities + required_operations`，删除 mount/Provider/app-path 的隐式后端选择和重复状态拼装。
- **验收标准**：T-13 全绿；所有 adapter 只消费 admitted plan；状态同时带 plan/capability/topology generation。
- **关联需求**：设计 7.1、7.6；ADR-0005、0012、0013。
- **依赖/时间盒**：I-13；3 小时。

### V-17 [验证] 对比 capability/admission 行为

- **任务描述**：重放 V-16，注入完整、部分、缺失和 generation 过期 capability；比较每类 action 与核心场景。
- **验收标准**：新增 bit/status 字段为 planned_break；C1～C5 在能力等价时 unchanged；缺能力均明确 fail-open 且不宣称 active。
- **关联需求**：设计 1.3、7.6、11.3；C5。
- **依赖/时间盒**：R-13；3 小时。

### V-18 [验证] 记录 policy reload 与并发读取基线

- **任务描述**：测量当前 load/publish/read、Provider/app 线程数、reload 行为、fork 后状态和内存增长；记录锁/分配与旧 snapshot 生命周期。
- **验收标准**：报告包含并发模型、P50/P95/P99、RSS、reload failure 行为和可重现 stress 命令。
- **关联需求**：设计 5.5、11.4；ADR-0001、0011。
- **依赖/时间盒**：V-17；3 小时。

### T-14 [红] 冻结 hazard pointer 发布与回收不变量

- **任务描述**：测试 `load -> hazard.store -> second load`、ABA/reload race、128 app slots、256 Provider slots、slot exhaustion、retire cap、失败候选保留旧 snapshot 和 seq_cst 顺序。
- **验收标准**：确定性并发 harness/TSan 可运行，当前因 registry/publisher 缺失失败；不得以概率性 sleep 作为唯一同步。
- **关联需求**：设计 5.5、11.2/11.4；ADR-0011。
- **依赖/时间盒**：V-18；4 小时。

### I-14 [绿] 实现 immutable MatcherSnapshot 发布器

- **任务描述**：私有构建/校验/index/admission 后原子发布；实现固定 slot registry、retire list 和安全扫描，slot/retire 超限 fail-open 并计数。
- **验收标准**：T-14 全绿；reader 无锁、无引用计数、无 heap allocation；失败 load 不替换当前 snapshot。
- **关联需求**：设计 5.5、7.6；ADR-0011。
- **依赖/时间盒**：T-14；4 小时。

### R-14 [重构] 分离 snapshot 构建、发布和生命周期观测

- **任务描述**：将 builder、publisher、reader guard、retire collector 与 metrics 拆为专一组件；保持首版 seq_cst，不提前做 relaxed 优化。
- **验收标准**：T-14 全绿；API 明确 guard 生命周期；指标包含 slot exhausted、retire limit、reload rejected、active readers。
- **关联需求**：设计 5.5、5.6；ADR-0011；SOLID、YAGNI。
- **依赖/时间盒**：I-14；3 小时。

### V-19 [验证] 对比 reload 并发与资源使用

- **任务描述**：重放 V-18，执行高并发 match+reload、失败 policy、slot exhaustion 和长读者；比较结果一致性、延迟和 RSS。
- **验收标准**：无 UAF/数据竞争/死锁；结果只来自完整 old/new generation；计划外 P99/RSS 回退为零或有明确阻断结论。
- **关联需求**：设计 5.5、11.4；ADR-0011。
- **依赖/时间盒**：R-14；4 小时。

### V-20 [验证] 记录 Zygote fork 生命周期基线

- **任务描述**：记录普通 app zygote、app zygote/isolated process 可用路径，当前 child 状态、module unload 和 policy mmap 行为。
- **验收标准**：至少一个实际 fork trace 和 host 模拟证据；明确 child handler 可调用函数白名单。
- **关联需求**：设计 5.5；ADR-0001、0011；POSIX `pthread_atfork`。
- **依赖/时间盒**：V-18；2 小时。

### T-15 [红] 冻结 atfork dirty/rebuild 语义

- **任务描述**：测试 child handler 只设置原子 dirty flag，不分配/加锁；首个正常入口清空继承 owner/hazard/retire 并惰性重建，覆盖 isolated process。
- **验收标准**：测试因 atfork integration 缺失而失败；检测在 child handler 中调用非 async-signal-safe 路径会失败。
- **关联需求**：设计 5.5；ADR-0011；POSIX `pthread_atfork`。
- **依赖/时间盒**：V-20、R-14；3 小时。

### I-15 [绿] 实现 atfork child dirty 与惰性重建

- **任务描述**：注册 `pthread_atfork`；child handler 仅原子置位，正常 adapter 入口执行 registry/snapshot 重建；不依赖每个调用点手动清空。
- **验收标准**：T-15 全绿；fork 子进程不持有父进程 hazard owner，不长期继承 Zygote policy mmap。
- **关联需求**：设计 5.5；ADR-0001、0011。
- **依赖/时间盒**：T-15；3 小时。

### R-15 [重构] 统一进程生命周期入口

- **任务描述**：抽取 `EnsureProcessState()` 并由 app-path/Provider 正常入口共享；避免 adapter 自行实现 fork 检查。
- **验收标准**：T-15 全绿；新增 adapter 不需复制 atfork 清理逻辑。
- **关联需求**：设计 5.5、7.1；DRY。
- **依赖/时间盒**：I-15；2 小时。

### V-21 [验证] 对比 fork 后稳定性

- **任务描述**：重放 V-20，在 reload 前后启动普通/isolated 进程，检查 generation、hazard 指标、policy mapping 和透传/命中结果。
- **验收标准**：无继承脏 slot、UAF 或规则串用；无法覆盖的 app zygote 组合明确标 `not_observed`。
- **关联需求**：设计 11.3～11.4；ADR-0011。
- **依赖/时间盒**：R-15；3 小时。

### V-22 [验证] 记录 literal mount plan 改造前基线

- **任务描述**：重放 C1/C2、strict/legacy backend、mount transaction、rollback/taint 和 longest-prefix；保存 mountinfo 与 generation。
- **验收标准**：每个 backend 的实际结果和安全等级已归档；计划只替换 plan 来源，不改变 mount 事务安全语义。
- **关联需求**：设计 7.2、10 P2；ADR-0003、0005。
- **依赖/时间盒**：V-15、V-17；4 小时。

### T-16 [红] 冻结统一 IR 到 MountPlan 的物化

- **任务描述**：测试 LiteralPrefix deny/redirect 转成既有 mount operations，Glob 不生成 bind；测试 nested rule、source plane、backend capability 和 operation IDs。
- **验收标准**：测试因 v6 plan builder 未接入 mount executor 而失败；事务预检/回滚预期与旧基线一致。
- **关联需求**：设计 5.1、7.1～7.2、8；ADR-0005。
- **依赖/时间盒**：V-22；3 小时。

### I-16 [绿] 接入 Mount execution domain

- **任务描述**：从 admitted ActionTable 物化现有 strict/legacy MountPlan，复用 FD pin、lease、apply/verify/rollback；删除 v5 MountTable 生产入口。
- **验收标准**：T-16 全绿；Glob action 不触发 mount；literal C1/C2 Host integration 通过。
- **关联需求**：设计 7.1～7.2、10 P2。
- **依赖/时间盒**：T-16；4 小时。

### R-16 [重构] 隔离 MountPlan adapter

- **任务描述**：让 mount executor 只依赖稳定 MountOp/transaction 接口，不理解 Selector/Glob；统一 operation ID 与 Decision diagnostics。
- **验收标准**：T-16、mount transaction/backend 测试全绿；旧 v5 plan builder 无生产引用。
- **关联需求**：设计 7.1～7.2；SOLID。
- **依赖/时间盒**：I-16；3 小时。

### V-23 [验证] 对比 literal mount 核心闭环

- **任务描述**：重放 V-22，在等价 format 2/v6 规则下比较 C1/C2、backend、mountinfo、rollback 和 lifecycle cleanup。
- **验收标准**：C1/C2 unchanged；无额外 mount 泄漏；旧 plan 接口删除为 planned_break。
- **关联需求**：设计 1.3、7.2、11.3；C1/C2/C5。
- **依赖/时间盒**：R-16；4 小时。

### V-24 [验证] 记录 app-path Hook 改造前基线

- **任务描述**：对 open/stat/access/opendir/mkdir/remove/rename/realpath/readlink/chmod/chown/statvfs/inotify 记录 literal mapper 行为、UID/user 隔离和未覆盖 caller 透传。
- **验收标准**：每个已支持 operation 有成功、未命中和失败证据；已知缺口不伪装成 capability。
- **关联需求**：设计 2.1、7.3、11.3；ADR-0013。
- **依赖/时间盒**：V-23；4 小时。

### T-17 [红] 冻结 app-path adapter 的 Decision→syscall 契约

- **任务描述**：为单/双路径 libc API、dirfd 相对路径、variadic open、errno 保留、recursive hook guard、unsupported operation 和 bit 19 admission 编写测试。
- **验收标准**：测试因统一 matcher 未接入 Hook 而失败；每个 API 的 operation mask 与 fallback 已明确。
- **关联需求**：设计 7.3、7.6、9.3；ADR-0013。
- **依赖/时间盒**：V-24、R-11、R-13；4 小时。

### I-17 [绿] 接入 app-path adapter

- **任务描述**：在 Hook 边界构造 OperationContext、持有一个 snapshot guard、执行统一 matcher/evaluator/OperationPlan；Pass 调原函数，Deny/Redirect 精确翻译 errno/路径。
- **验收标准**：T-17 全绿；目标 UID 的 glob deny/redirect 生效，其他 UID 不进入 matcher bucket；缺能力 fail-open。
- **关联需求**：设计 5.2、7.3、9.2～9.3；C5/C6。
- **依赖/时间盒**：T-17；4 小时。

### R-17 [重构] 收敛 Hook wrapper 与路径 operand 构造

- **任务描述**：模板化/抽取单路径、双路径和 dirfd 解析的共享 wrapper，保留 API 特有签名；删除新旧 mapper 双重判断。
- **验收标准**：T-17 全绿；Hook wrapper 不含 glob token 或 action precedence；无递归/errno 回归。
- **关联需求**：设计 5.2、7.3；DRY、接口隔离。
- **依赖/时间盒**：I-17；3 小时。

### V-25 [验证] 对比 app-path 核心行为

- **任务描述**：重放 V-24，并新增 C6 文件名/后缀/目录分量规则；比较每个 operation、UID 隔离、errno 和最终路径。
- **验收标准**：literal 行为 unchanged，C6 达到预期；未命中路径无日志/I/O，unexpected regression 为零。
- **关联需求**：设计 1.3、7.3、11.3；C1/C2/C5/C6。
- **依赖/时间盒**：R-17；4 小时。

### V-26 [验证] 记录 Provider caller 与文件 I/O 基线

- **任务描述**：用 LocalSend 和 probe app 记录 ExternalStorageProvider/MediaProvider 的 Binder UID、identity clear/restore、常驻镜像 Hook、literal rewrite 和同路径其他 UID 透传。
- **验收标准**：C3 before 报告含 source/backing、零字节残留、Provider PID/restart、hook committed/active 和调用方 UID。
- **关联需求**：设计 2.1、7.4、11.3；ADR-0006、0012。
- **依赖/时间盒**：V-17、V-25；4 小时。

### T-18 [红] 冻结 Provider caller UID 与 path-I/O 组合准入

- **任务描述**：测试 raw Binder UID、clear/restore 保存、Provider 自身 UID、shared UID、unknown attribution；覆盖 create/open/stat/access/opendir/mkdir/rename/delete/canonical child 必需 operation mask。
- **验收标准**：测试因 Provider composite adapter 未接入而失败；caller UID 不可信时必须 Pass，不能猜 package。
- **关联需求**：设计 7.4、7.6、9.2；ADR-0006、0012、0013。
- **依赖/时间盒**：V-26；4 小时。

### I-18 [绿] 接入 Provider caller 与 path-I/O adapter

- **任务描述**：复用 MatcherSnapshot/OperationContext，在可信 Binder scope 下执行统一决策；只 Hook 常驻白名单镜像，按 action required operations 发布 active。
- **验收标准**：T-18 全绿；LocalSend glob write 到 target；其他 UID 同路径不改写；unknown caller 完整 fail-open。
- **关联需求**：设计 7.4、7.6；ADR-0006、0012。
- **依赖/时间盒**：T-18；4 小时。

### R-18 [重构] 分离 Provider identity、Hook 和执行适配

- **任务描述**：将 Binder attribution、PLT lifecycle、path operand adapter 分为独立 capability producer；禁止单一 `file_picker`/`provider_compat` bool 决定 active。
- **验收标准**：T-18 全绿；observed bits/missing bits/action mask 独立可查；包归因不可信时按 UID 共享语义。
- **关联需求**：设计 7.4、7.6；ADR-0012。
- **依赖/时间盒**：I-18；3 小时。

### V-27 [验证] 对比 Provider path-I/O 与 LocalSend 接收

- **任务描述**：重放 V-26，用 literal 与 glob 图片规则分别接收文件，验证 create/open/query 前的文件系统阶段、最终位置和未命中 UID。
- **验收标准**：C3 path-I/O 结果 unchanged/按计划扩展，source 无错误零字节残留，Provider 不崩溃循环；无 unexpected regression。
- **关联需求**：设计 1.3、7.4、11.3；C3/C5/C6。
- **依赖/时间盒**：R-18；4 小时。

### V-28 [验证] 记录 Provider query/insert 映射基线

- **任务描述**：分别记录 DocumentsProvider `create/query/open`、MediaStore insert/query/scan 和实际 FD 路径；确认现有 prefix mapper 的可见/真实路径转换范围。
- **验收标准**：每个入口有 caller、URI/document ID、RELATIVE_PATH/DATA、FD target 和数据库结果；未覆盖 ABI 明确记录。
- **关联需求**：设计 7.4；ADR-0006、0012；Android DocumentsProvider/MediaProvider 官方文档。
- **依赖/时间盒**：V-27；4 小时。

### T-19 [红] 冻结 Provider query/insert/FD 一致性

- **任务描述**：测试 create 返回稳定 source document ID、query 显示 visible path、insert 写 target、open 获得同一对象、scanner 可读；缺任一能力时 provider glob redirect 不准入。
- **验收标准**：测试因 query/insert adapter 缺失而失败；expected capability bit 17 与 operation mask 已冻结。
- **关联需求**：设计 7.4、7.6、11.3；ADR-0012。
- **依赖/时间盒**：V-28；4 小时。

### I-19 [绿] 实现 Provider query/insert 映射适配

- **任务描述**：把已决策 route 应用到 Provider query/insert/open 契约，维护 source URI/document ID 与 target FD 的一致视图；不根据 ROM 名称硬编码 active。
- **验收标准**：T-19 全绿；bit 17 只在实际 probe 覆盖所需入口时 observed；未知 OEM ABI fail-open/unsupported。
- **关联需求**：设计 7.4、7.6；ADR-0012。
- **依赖/时间盒**：T-19；4 小时。

### R-19 [重构] 统一 Provider route context

- **任务描述**：用一次 route context 连接 create/query/insert/open/scanner，消除各入口独立重算或字符串替换；接口只依赖 Decision 与 provenance contract。
- **验收标准**：T-19 全绿；同一请求各阶段 generation/rule ID 一致；无 URI grant 扩大。
- **关联需求**：设计 7.4、6.3；DRY、安全边界。
- **依赖/时间盒**：I-19；3 小时。

### V-29 [验证] 对比 Provider 数据库与 FD 视图

- **任务描述**：重放 V-28，覆盖图片/普通文件、rename/delete、媒体扫描和 Provider 重启；比较 source URI、数据库、FD 和物理位置。
- **验收标准**：C3 全链路成功；query/insert/FD 无分裂视图；能力缺失设备明确 unsupported，未误报 active。
- **关联需求**：设计 7.4、11.3；C3/C5。
- **依赖/时间盒**：R-19；4 小时。

### V-30 [验证] 记录 Provider Hook 生命周期基线

- **任务描述**：记录 PLT commit 前后、能力探测失败、临时库 dlclose、Provider restart 和 Zygisk unload 请求的当前行为。
- **验收标准**：有正常、commit 后 inactive、故意加载/卸载临时库三类证据；崩溃循环为明确失败。
- **关联需求**：设计 7.4、9.3；ADR-0006；cleanerhooks/Magisk 参考。
- **依赖/时间盒**：V-29；3 小时。

### T-20 [红] 冻结 Hook commit 后驻留透传

- **任务描述**：测试只对白名单常驻镜像提交 Hook；commit 后即使 admission 失败也禁止卸载、清空规则并透传；commit 前无 Hook 才允许 unload。
- **验收标准**：测试因生命周期状态机未统一而失败；分别断言 `hooks_committed` 与 `virtualization_active`。
- **关联需求**：设计 7.4、9.3；ADR-0006。
- **依赖/时间盒**：V-30；3 小时。

### I-20 [绿] 实现 Provider Hook 生命周期状态机

- **任务描述**：集中管理 target allowlist、commit、active/inactive、unload decision 和 restart cleanup；能力失败走已安装 Hook 的透明原函数路径。
- **验收标准**：T-20 全绿；临时库 dlclose 不触及 Hook 记账；Provider 不发生 SEGV/restart loop。
- **关联需求**：设计 7.4、9.3；ADR-0006。
- **依赖/时间盒**：T-20；4 小时。

### R-20 [重构] 隔离不可逆 Hook 边界

- **任务描述**：将 `CommitHooks()` 作为唯一不可逆方法，之后状态只能 active↔passthrough，不可回到 unloadable；删除散落 unload 判断。
- **验收标准**：T-20 全绿；状态转换表覆盖所有分支且无悬空 GOT 指针路径。
- **关联需求**：设计 7.4；ADR-0006；单一职责。
- **依赖/时间盒**：I-20；2 小时。

### V-31 [验证] 对比 Provider 生命周期稳定性

- **任务描述**：重放 V-30，执行 50 次 Provider/LocalSend 冷启动、能力失败注入和临时库装卸；监测 tombstone、restart、FD/memory。
- **验收标准**：零崩溃循环、零悬空 Hook；commit 后 inactive 明确报告且文件操作透传。
- **关联需求**：设计 11.3～11.4；ADR-0006。
- **依赖/时间盒**：R-20；4 小时。

### V-32 [验证] 记录多源反向映射改造前基线

- **任务描述**：用两个 source 指向同一 target，记录旧 canonical visible source、realpath/query/rename/delete、同名文件和重启后的实际表现；标记旧 fallback 为计划内删除。
- **验收标准**：C4 before 证据明确哪些结果是伪唯一映射；所有文件 identity 与路径已记录。
- **关联需求**：设计 6.3、11.0；route provenance ADR。
- **依赖/时间盒**：V-10、V-29；4 小时。

### T-21 [红] 冻结 route provenance 事务日志

- **任务描述**：测试 prepare/commit/abort、重复提交幂等、崩溃点、fsync/原子替换、identity/generation 校验、损坏/陈旧记录和重启 replay。
- **验收标准**：测试因 journal 未实现而失败；缺失/陈旧/冲突记录必须产生 `AmbiguousReverse`，不得猜 source。
- **关联需求**：设计 6.3、8、9.3、11.2；route provenance ADR。
- **依赖/时间盒**：V-32；4 小时。

### I-21 [绿] 实现 provenance journal 最小事务

- **任务描述**：实现版本化 snapshot/WAL、prepare/materialized/commit/abort、CURRENT epoch 原子切换和启动恢复；owner 使用 ADR 冻结的 file handle 或 stable volume + inode + statx btime、RouteScope/RuleId，而非裸路径、裸 inode 或 ctime。
- **验收标准**：T-21 全绿；每个故障点恢复到 committed 或 absent，不出现半记录。
- **关联需求**：设计 6.3、8；route provenance ADR。
- **依赖/时间盒**：T-21；4 小时。

### R-21 [重构] 分离 provenance store 与文件操作协调器

- **任务描述**：抽象 store、transaction 和 resolver 接口，保持 matcher/evaluator 不依赖持久化实现；统一结构化诊断。
- **验收标准**：T-21 全绿；可用内存 fake 做确定性故障测试；生产 store 只暴露已冻结事务 API。
- **关联需求**：设计 5.2、6.3；SOLID。
- **依赖/时间盒**：I-21；3 小时。

### V-33 [验证] 对比 provenance 持久化安全性

- **任务描述**：重放 journal 故障矩阵和 daemon/Provider 重启，检查 committed route、orphan temp、损坏记录与 fail-open/ambiguous 状态。
- **验收标准**：无错误唯一反向映射、无丢失已提交 identity；旧 canonical fallback 删除为 planned_break。
- **关联需求**：设计 1.3、6.3、11.3；C4/C5。
- **依赖/时间盒**：R-21；4 小时。

### V-34 [验证] 记录 rename/delete/reverse 操作基线

- **任务描述**：针对多源同 target 的 create、overwrite、rename source/target、unlink、directory delete、MediaStore query 和 realpath 记录旧行为。
- **验收标准**：每个 operation 的 source/target identity、errno、数据库与 provenance 预期可对比。
- **关联需求**：设计 6.3、7.3～7.4；C4。
- **依赖/时间盒**：V-32；3 小时。

### T-22 [红] 冻结 provenance 与文件操作原子协调

- **任务描述**：测试 create 前 prepare/成功 commit/失败 abort，rename 搬迁 route，delete 清理 route，同 target 同名 `EEXIST`，反向查询只接受唯一 committed record。
- **验收标准**：测试因 operation coordinator 未接入而失败；每个 syscall 故障点有期望状态。
- **关联需求**：设计 6.3、7.3～7.4、11.2；route provenance ADR。
- **依赖/时间盒**：V-34、R-21；4 小时。

### I-22 [绿] 接入 provenance operation coordinator

- **任务描述**：把 app-path/Provider 的 create/rename/delete/reverse 接到事务协调器；保证 filesystem 与 journal 的规定顺序和补偿语义。
- **验收标准**：T-22 全绿；多源前向允许、同名碰撞 reject、反向只返回实际来源。
- **关联需求**：设计 6.3、7.3～7.4；C4。
- **依赖/时间盒**：T-22；4 小时。

### R-22 [重构] 统一 route identity 与 generation 校验

- **任务描述**：提取所有 adapter 共用的 route identity validator；删除基于 target 字符串选择 canonical source 的代码。
- **验收标准**：T-21/T-22 全绿；代码搜索无 v5 canonical fallback 生产引用。
- **关联需求**：设计 6.3、8；DRY。
- **依赖/时间盒**：I-22；3 小时。

### V-35 [验证] 对比多源完整核心闭环

- **任务描述**：重放 V-34，在 LocalSend/Provider 与 app-path 两条通道创建不同名和同名文件，随后 query/rename/delete/reboot。
- **验收标准**：C4 全部最低成功条件达成；旧伪唯一展示为 planned_break；无文件丢失、串源或错误成功。
- **关联需求**：设计 1.3、6.3、11.3；C4/C5。
- **依赖/时间盒**：R-22；4 小时。

### V-36 [验证] 记录安全路径解析基线

- **任务描述**：记录当前 resolver 对 `.`/`..`、NUL、超长组件、symlink/magic link、跨 mount、dirfd、TOCTOU 和 Linux 4.19/5.6+ 的行为。
- **验收标准**：strict/legacy/openat2 available/unavailable 四类证据齐全或标明缺失设备。
- **关联需求**：设计 4.3、9.1、11.3；ADR-0004、0005；`openat2(2)`。
- **依赖/时间盒**：V-23、V-25；4 小时。

### T-23 [红] 冻结安全 target resolution 与 capability probe

- **任务描述**：测试一次性探测 `openat2`/所需 resolve flags，支持时用 BENEATH/NO_SYMLINKS/NO_MAGICLINKS，不支持时逐组件 `openat(O_NOFOLLOW|O_DIRECTORY)`；覆盖 EAGAIN/EXDEV/ELOOP/ENOSYS。
- **验收标准**：测试因统一 secure resolver 缺失而失败；不得按 kernel version 推断 capability。
- **关联需求**：设计 9.1、11.2～11.3；ADR-0004；Linux `openat2(2)`。
- **依赖/时间盒**：V-36；4 小时。

### I-23 [绿] 实现 probe-cached SecurePathResolver

- **任务描述**：实现按 boot/topology generation 缓存的 syscall/flag probe 和逐组件 fallback；target 以 FD identity 固定，不重新字符串解析。
- **验收标准**：T-23 全绿；symlink escape/TOCTOU 被拒绝；缺 openat2 的设备仍有明确安全级别。
- **关联需求**：设计 9.1、7.6；ADR-0004、0005。
- **依赖/时间盒**：T-23；4 小时。

### R-23 [重构] 统一 mount、app-path 与 provenance 路径安全 API

- **任务描述**：让三个执行域共用 capability-aware resolver 和 pinned identity，删除各自字符串 canonicalization 安全判断。
- **验收标准**：T-23 与现有 resolver/mount 测试全绿；legacy TOCTOU 仍明确标注，未被伪装为 fd_pinned。
- **关联需求**：设计 7.1、9.1；ADR-0005。
- **依赖/时间盒**：I-23；3 小时。

### V-37 [验证] 对比路径安全与兼容性

- **任务描述**：重放 V-36，在 strict/fallback 设备或 syscall shim 上比较合法路径、恶意路径、errno、security status 和性能。
- **验收标准**：合法 C1～C4 unchanged；escape 全部失败；计划外兼容回退和性能异常为零。
- **关联需求**：设计 1.3、9.1、11.3～11.4。
- **依赖/时间盒**：R-23；4 小时。

### V-38 [验证] 记录故障隔离与 reload 基线

- **任务描述**：记录损坏 policy、能力消失、预算超限、Provider crash/restart、daemon restart、topology change、stale generation 和 unknown UID 的当前结果。
- **验收标准**：C5 before 矩阵包含 errno/status/log/文件副作用；active deny/collision 与 unsupported 已分开。
- **关联需求**：设计 9.3、11.0、11.3；ADR-0003、0006、0011～0013。
- **依赖/时间盒**：V-19、V-31、V-37；4 小时。

### T-24 [红] 冻结统一失败语义与限速诊断

- **任务描述**：测试 NoMatch/CapabilityMissing/RuntimeUnavailable/BudgetExceeded/InvalidEncoding/UnsafeTarget fail-open，Denied=`EACCES`、Collision=`EEXIST`、AmbiguousReverse 明确失败；Pass 不记日志，错误诊断限速。
- **验收标准**：测试因统一 failure translator/limiter 缺失而失败；每个 reason 有稳定 status code。
- **关联需求**：设计 5.6、9.3、11.4；C5。
- **依赖/时间盒**：V-38；4 小时。

### I-24 [绿] 实现统一失败翻译与诊断限速

- **任务描述**：实现 Decision→adapter result/errno/status 的唯一映射，按 scope/reason/rule 限速审计；snapshot load 失败保留旧 generation。
- **验收标准**：T-24 全绿；Pass/allow 无 I/O；active safety decision 不被通用 fail-open 捕获。
- **关联需求**：设计 5.6、9.3、11.4。
- **依赖/时间盒**：T-24；4 小时。

### R-24 [重构] 收敛故障注入点与状态生成

- **任务描述**：建立 adapter-independent fault hooks 和统一 RuntimeStatus builder，移除测试专用逻辑对生产决策路径的分叉。
- **验收标准**：T-24 全绿；故障注入关闭时为零行为差异；status 带完整 generation/diagnostic ID。
- **关联需求**：设计 7.6、9.3、11.3；ADR-0003。
- **依赖/时间盒**：I-24；3 小时。

### V-39 [验证] 对比完整故障隔离矩阵

- **任务描述**：重放 V-38，并执行 owner death、rollback failure、slot exhaustion、retire cap、Provider inactive 和 provenance corruption。
- **验收标准**：C5 全部通过；不存在 partial redirect、错误 active、namespace 泄漏或未解释核心异常。
- **关联需求**：设计 1.3、9.3、11.3；C5。
- **依赖/时间盒**：R-24；4 小时。

## 第三部分：辅助与边缘功能开发

本部分不得阻塞已经通过的 C1～C6。能力无法满足时允许明确 `unsupported`，但不得退化核心执行域或改变其 admission。

> 历史执行审计（2026-07-31）：有界 `select.except` 已完成并关闭空数组校验缺口；当时 Host
> Release CTest `82/82`、Android NDK arm64-v8a/armeabi-v7a、Zygisk `APP_STL=none` 和
> Host/Android rules parity 通过。Observe、Export、CLI/status、CompleteVfs 和性能门仅完成下表
> 所列契约范围；后续当前设备闭环状态以本表和第四部分汇总为准，未观察能力不得宣称 active。

| 任务 | 状态 | 已验证范围与剩余边界 |
| --- | --- | --- |
| V-40、T-25～R-25、V-41 | complete | canonical except、空/全排除、预算、复用 matcher、候选调用次数与 CLI explain 均有自动化和基准证据 |
| T-26～R-26 | complete（event runtime unsupported） | 正交 effect dispatch、固定窗口限速、basename 脱敏、并发竞争丢弃、有界队列和 metrics 已满足 adapter 合同；设备 event source 属于 T-27/V-43，当前 bit 8～11 不误报 active |
| V-42 | complete + device unsupported | Xiaomi Android 13 内核实测 `CONFIG_FANOTIFY` 未启用，bit 8～11 必须 unsupported；队列/overflow/跨 FS/restart 因 source 不可构造保持 `not_observed` |
| T-27～R-27 | not_observed（用户授权跳过） | Host worker/store/executor 合同已完成；生产 fanotify adapter 因当前内核 `CONFIG_FANOTIFY` disabled 不准入，按范围豁免跳过 |
| V-43 | not_observed（当前设备 unsupported） | myron 缺少 fanotify source；normal/overflow/crash/跨 FS 不执行，bits 8～11 保持 unsupported |
| V-44 | complete | 已归档 status 文本/JSON、静态 explain JSON、exit code、未知 version 边界和 planned breaks |
| T-28～R-28 | complete | 无 STL `RuntimeStatusRecord` v2、共享 policy→per-action admission builder、截断与 counters 已冻结；Zygisk Provider/app-path 安装后原子写入 per-action/counters，CLI JSON 透传稳定字段；实际文件结果对比留在 V-45 |
| V-45 | complete（available-device scope） | myron active/restart fail-open 与实际文件结果一致；inactive/unsupported/collision/ambiguous/overflow 因当前 production 无构造入口记为 not_observed/unsupported |
| V-46 | complete (`adapter-only`) | 仅保留核心零依赖的 fake adapter 合同；未授权真实 CompleteVfs backend 或 bit 18 active 声明 |
| T-29～R-29 | blocked（V-46 非 go） | V-46 决策为 `adapter-only`，未满足任务明确的 `go` 依赖；只保留最小 OperationPlan/backend/admission fake，不实现或发布真实 CompleteVfs backend |
| V-47 | blocked（V-46 非 go） | 无已批准原型可做 available/unavailable/partial 重放；核心 C1～C6 不依赖 CompleteVfs |
| T-30～R-30 | complete（Host CI） | matcher gate 外新增 Provider route、provenance、snapshot reload/slot exhaustion、RSS相对基线与可配置 soak；均输出 machine/compiler/arch profile、P50/P95/P99/max、allocation/counters 并以同次 reference 形成可解析失败门；无证据要求热路径重构 |
| V-48 | complete（available device scope） | 2 秒 CI soak、30 分钟 Host soak、myron 双 Provider restart/republication 与 RSS 采样已通过；第二设备/ROM 已按用户授权记为 `not_observed`，不扩大兼容性声明 |

详细审计证据：`tests/baseline/pattern-v6/p6-auxiliary-20260731/V-42-event-before.md` 和
`tests/baseline/pattern-v6/p6-auxiliary-20260731/V-42-V-48-auxiliary-audit.md`。

### V-40 [验证] 检查 ADR-0015 实施条件

- **任务描述**：确认 V-08 结论。若非 Accepted，记录跳过理由并结束 V-40～V-42；若 Accepted，记录无 except 的 C1～C6 与 candidate benchmark 基线。
- **验收标准**：不存在“ADR 未接受但 policy 已编码 except”的状态；实施或跳过均有明确证据。
- **关联需求**：设计 4.5、10 P0/P1；ADR-0015。
- **依赖/时间盒**：V-08、V-39；2 小时。

### T-25 [红] 冻结有界 `select.except` 集合差

- **任务描述**：在 ADR Accepted 条件下测试 `Base - Union(except)`、root/type 继承、目录本身/后代、剩余集合路由、互斥分区、空/全排除、预算和 specificity；拒绝裸/尾项 `!`。
- **验收标准**：测试因 except refs/matcher 缺失而失败；正向 base 未命中时不检查 except。
- **关联需求**：设计 4.5、5.1～5.4、6.1～6.2、8；ADR-0015。
- **依赖/时间盒**：V-40 且 ADR-0015 Accepted；4 小时。

### I-25 [绿] 实现 ExceptRefTable 与有界集合差

- **任务描述**：按 ADR/format 6 ADR 编译 canonical except refs，在 base bucket 命中后执行有界排除；将结果保持为 selector match/no-match，不新增 action 内排除语义。
- **验收标准**：T-25 全绿；runtime 无 `!` token；超限编译失败且旧 snapshot 保持 active。
- **关联需求**：设计 4.5、5.3～5.4、8；ADR-0015。
- **依赖/时间盒**：T-25；4 小时。

### R-25 [重构] 统一 base 与 except pattern 执行器

- **任务描述**：复用同一 PatternProgram evaluator 和预算计数，避免复制 glob matcher；保持 except 只在 selector 层存在。
- **验收标准**：T-25 全绿；候选索引未退化为全局负规则扫描；canonical dedupe 稳定。
- **关联需求**：设计 5.3～5.4；ADR-0015；DRY。
- **依赖/时间盒**：I-25；3 小时。

### V-41 [验证] 对比反选与剩余集合路由

- **任务描述**：重放 V-40，验证蛋糕式 A/B 优先、剩余给 C，以及 deny 除允许区外全部拒绝；比较无关路径与基准性能。
- **验收标准**：计划内新增语义达成；C1～C6 无回归；无 base 命中时 matcher 次数保持候选快路径预期。
- **关联需求**：设计 4.5、6.1～6.2、11；ADR-0015。
- **依赖/时间盒**：R-25；4 小时。

### T-26 [红] 冻结 Observe 副作用语义

- **任务描述**：测试 Observe 与 Pass/Deny/Redirect 正交组合、事件字段、限速、generation 和隐私脱敏；测试未命中不产生事件。
- **验收标准**：测试因 Observe sink 缺失而失败；Observe 不改变主处置/errno/target。
- **关联需求**：设计 2.2、5.2、5.6、7.1。
- **依赖/时间盒**：V-39；3 小时。

### I-26 [绿] 实现 Observe effect adapter

- **任务描述**：消费 Decision effect mask，异步/有界写结构化事件；复用 selector/action，不新增 matcher。
- **验收标准**：T-26 全绿；队列满只丢 Observe 并计数，不影响核心 I/O。
- **关联需求**：设计 5.2、5.6、7.1、9.3。
- **依赖/时间盒**：T-26；3 小时。

### R-26 [重构] 统一 effect sink 接口

- **任务描述**：抽取 Observe/未来 Export 共用的非阻塞提交、backpressure 和 metrics 接口，不把文件搬运逻辑放入 matcher。
- **验收标准**：T-26 全绿；核心 adapter 不依赖具体日志后端。
- **关联需求**：设计 5.2、7.1；SOLID。
- **依赖/时间盒**：I-26；2 小时。

### V-42 [验证] 记录 export/event 改造前基线

- **任务描述**：记录现有 EventRule/fanotify capability、事件类型、丢失/overflow、跨文件系统和 daemon restart 行为；证明它当前不是同步 redirect。
- **验收标准**：before 报告含 bit 8～11、kernel 支持和 event queue 实测；与核心 redirect 结果分离。
- **关联需求**：设计 7.7、10 P5、11.0；ADR-0004；`fanotify(7)`。
- **依赖/时间盒**：V-39；3 小时。

### T-27 [红] 冻结 Export 异步队列与幂等语义

- **任务描述**：测试 close-write/rename、FID/DFID_NAME/pidfd、overflow、重复/乱序事件、跨 FS copy+fsync+rename、重启恢复和 `(device,inode,generation)` 幂等 key。
- **验收标准**：测试因 Export worker 缺失而失败；任何 event loss 不得被报告为同步 redirect 成功。
- **关联需求**：设计 7.7、11.2～11.4；ADR-0004；Linux `fanotify(7)`。
- **依赖/时间盒**：V-42、R-26；4 小时。

> Host 执行结果（2026-08-01）：新增独立 `pathguard_export_worker_test`，红测确认缺失 event
> source、FID key、recovery store 和结构化 transfer 合同；绿测覆盖 close-write/rename 乱序、
> 重复/事件窗口、overflow、queued/failed/running/complete 重启恢复、完成项淘汰、失败项有界保留、
> 长期记录总量和 4096 条恢复格式硬上限、损坏/超容量 snapshot，及
> copy/sync/rename 失败阶段不得进入 complete。随后补齐带 CRC/版本/容量限制的 durable file store，
> 通过同目录临时文件、fsync 与原子安装实现同 FS/跨 FS 一致搬运，并只在安装成功后删除 move/trash
> source。生产 fanotify capability/DFID_NAME/pidfd adapter 因当前设备内核不支持而未准入，故
> 该批当时将 T-27～R-27 保持 `in_progress`、V-43 保持 `pending`；当前已按设备能力范围豁免
> 更新为 `not_observed/unsupported`。

### I-27 [绿] 实现最小 Export worker

- **任务描述**：从 event execution domain 消费 admitted selector/action，执行有界队列、幂等搬运和 overflow rescan/diagnostic；不阻塞原 I/O。
- **验收标准**：T-27 全绿；失败/丢失可观测且不修改 Redirect 状态；跨 FS 不留下错误完成标记。
- **关联需求**：设计 7.7、10 P5。
- **依赖/时间盒**：T-27；4 小时。

### R-27 [重构] 分离事件摄取、任务状态机与文件搬运

- **任务描述**：拆出 fanotify adapter、dedupe queue、transfer executor 和 recovery store；共享 selector，不共享同步 OperationPlan。
- **验收标准**：T-27 全绿；可用 fake source 注入 overflow/乱序；组件职责单一。
- **关联需求**：设计 7.1、7.7；SOLID。
- **依赖/时间盒**：I-27；3 小时。

### V-43 [验证] 对比 Export 与核心重定向隔离

- **任务描述**：重放 V-42，执行正常、overflow、daemon crash、跨 FS 和能力缺失；同时重放 C2/C3 redirect。
- **验收标准**：Export 结果符合异步语义；C2/C3 unchanged；缺能力为 unsupported，不隐式改用别的执行域。
- **关联需求**：设计 7.7、11.3；C2/C3/C5。
- **依赖/时间盒**：R-27；4 小时。

### V-44 [验证] 记录 CLI/status/explain 改造前基线

- **任务描述**：归档现有 compile/status/runtime 输出、exit code、未知 version 和 capability 展示；列出 v6 字段变更。
- **验收标准**：机器可读与人类可读输出均有 golden；planned_break 字段已列明。
- **关联需求**：设计 5.6、7.6、8、11.0。
- **依赖/时间盒**：V-15、V-39；2 小时。

### T-28 [红] 冻结 explain/status 诊断契约

- **任务描述**：测试 intent/admission/observed/missing/action mask/domain、rule/selector/conflict ID、all generations、unsupported reason、slot/retire/overflow counters；测试 `explain` 不执行真实 I/O。
- **验收标准**：测试因 v6 status/explain model 不完整而失败；JSON schema 与 exit code 已冻结。
- **关联需求**：设计 5.6、7.6、8、11.4；ADR-0011～0013。
- **依赖/时间盒**：V-44；4 小时。

### I-28 [绿] 实现 v6 CLI/status/explain

- **任务描述**：让 CLI 读取统一 Decision/Admission/metrics，输出稳定 JSON 与精简文本；对错误 version、ambiguous reverse 和 inactive adapter 给出结构化原因。
- **验收标准**：T-28 全绿；CLI 不自行重做 matcher/admission；未知字段处理符合 format 6 ADR。
- **关联需求**：设计 5.6、7.6、8。
- **依赖/时间盒**：T-28；4 小时。

### R-28 [重构] 统一状态 DTO 与序列化

- **任务描述**：让 daemon、Zygisk/Provider 和 CLI 共享 versioned DTO/schema，删除字符串日志反解析作为状态来源。
- **验收标准**：T-28 全绿；每个字段只有一个生产定义和 golden；日志仅作为审计证据。
- **关联需求**：设计 5.6、7.6、8；DRY。
- **依赖/时间盒**：I-28；3 小时。

### V-45 [验证] 对比 CLI/status 可观测性

- **任务描述**：重放 V-44，在 active/inactive/unsupported/collision/ambiguous/overflow 各状态比较输出与实际行为。
- **验收标准**：schema/字段变化均 planned_break；状态与真实文件结果一致，不出现“日志有 hook 即 active”。
- **关联需求**：设计 1.3、5.6、7.6、11.3。
- **依赖/时间盒**：R-28；3 小时。

### V-46 [验证] 执行 complete-VFS 投资门评审

- **任务描述**：依据 ADR-0010 和最新设备证据评估自建 FUSE/内核 adapter；确认 lookup/create/rename/unlink/readdir、caller identity、reverse/provenance 和安全维护门槛。
- **验收标准**：形成 `go prototype`、`defer` 或 `adapter-only` 决策；defer 时 V-47～V-48 不实施且核心功能不受阻。
- **关联需求**：设计 7.5、7.8、10 P4；ADR-0010、0012。
- **依赖/时间盒**：V-35、V-39；4 小时。

### T-29 [红] 冻结 CompleteVfs adapter conformance

- **任务描述**：在 go 条件下用 fake backend 测试完整 operation set、listing 虚拟视图、caller UID、provenance reverse、bit 18 probe 和禁止 fallback；FUSE passthrough 后不假设收到 read/write。
- **验收标准**：测试因 adapter contract 未实现而失败；只覆盖跨 backend 公共契约，不绑定 NoMount/单一内核 ABI。
- **关联需求**：设计 7.5、7.8；ADR-0010、0012；AOSP FUSE passthrough。
- **依赖/时间盒**：V-46 结论为 go；4 小时。

### I-29 [绿] 实现 CompleteVfs 原型 adapter

- **任务描述**：实现最小 capability probe、Decision/OperationPlan 翻译和 conformance fake；真实后端只做到评审批准的原型范围，不发布为默认依赖。
- **验收标准**：T-29 全绿；bit 18 仅在完整操作实测通过时 observed；其他情况明确 unsupported。
- **关联需求**：设计 7.5、7.8、10 P4；ADR-0010。
- **依赖/时间盒**：T-29；4 小时。

### R-29 [重构] 固化可插拔 VFS backend 接口

- **任务描述**：隔离 NoMount/社区内核/未来 FUSE 的 ABI adapter，使核心 IR、matcher、provenance 零依赖具体项目；删除原型中的品牌/版本推断。
- **验收标准**：T-29 全绿；替换 fake backend 不改核心；未通过 conformance 的 backend 无法发布 active。
- **关联需求**：设计 7.8；ADR-0010；依赖倒置。
- **依赖/时间盒**：I-29；3 小时。

### V-47 [验证] 验证 CompleteVfs 原型不影响核心

- **任务描述**：在 backend available/unavailable/partial 三种条件重放 conformance 和 C1～C6，检查 mount/app-path/provider admission。
- **验收标准**：原型仅影响 complete_vfs action；核心零依赖成立；partial backend 不宣称 active。
- **关联需求**：设计 7.1、7.5、7.8、11.3；ADR-0010。
- **依赖/时间盒**：R-29；4 小时。

### T-30 [红] 建立性能与退化 CI 门

- **任务描述**：为无规则、scope miss、0/1/2+ match、1000 pattern 退化桶、reload、Provider、provenance 和 slot exhaustion 定义 P50/P95/P99、allocation、matcher-call、RSS 阈值测试。
- **验收标准**：测试因阈值/指标未接入而失败；阈值基于 V-02/V-18 等基线，不使用任意绝对数字。
- **关联需求**：设计 3.7、5.4～5.6、11.4；ADR-0011、0015。
- **依赖/时间盒**：V-39、R-02；4 小时。

> Host 执行结果（2026-08-01）：benchmark schema v2 增加 no-rules、scope-miss、warmup、
> allocation 和 `reject_1000_patterns`。无规则/scope miss/zero candidate 的 matcher calls 与
> 热循环 allocation 均为 0；1000 个同 bucket pattern 在构造 index 时以
> `candidate/bucket limit` 拒绝，未进入 runtime。性能门以当前 machine profile 的
> `one_candidate.p99` 为 reference 并按工作量/噪声容忍计算相对预算，不再用固定纳秒值判定。
> 独立 runtime benchmark 进一步覆盖真实 policy v6 Provider route、provenance
> prepare/abort/materialize/commit、snapshot reload/retire、256 slot exhaustion 与 RSS 相对基线；
> 所有场景输出 machine/compiler/arch、P50/P95/P99/max、allocation 和 counters，并用同次轻量
> reference 计算门限。2 秒并发 reload/match CI soak 已通过；30 分钟 Host soak 归入 V-48，
> Provider restart/profile 继续按设备验证，因此 T-30～R-30 的 Host CI 工作已完成。

### I-30 [绿] 实现可重复性能 gate

- **任务描述**：接入 Release benchmark、warmup、固定 corpus、噪声容忍和 machine profile；无规则/scope miss 明确断言不运行 token matcher、不分配、不写日志。
- **验收标准**：T-30 全绿；结果可被 CI 解析，超阈值明确失败而非只打印。
- **关联需求**：设计 5.4～5.6、11.4。
- **依赖/时间盒**：T-30；4 小时。

### R-30 [重构] 基于证据优化热路径

- **任务描述**：只优化 T-30 证明的瓶颈；可调整 bucket/layout/cache locality，但不放宽 seq_cst、不增加未验证的复杂算法。
- **验收标准**：所有功能测试全绿；目标指标改善，其他核心指标无回退；优化前后报告已归档。
- **关联需求**：设计 3.7、5.4～5.6、11.4；KISS、YAGNI。
- **依赖/时间盒**：I-30；4 小时。

### V-48 [验证] 对比性能、稳定性和资源上限

- **任务描述**：重放 T-30 全矩阵及 30 分钟 reload/match soak，比较 P50/P95/P99/max、RSS、retire、slot、diagnostic drop 和 Provider restart。
- **验收标准**：所有门通过或有明确阻断项；不存在用 fail-open 次数增加换取表面性能的情况。
- **关联需求**：设计 11.4；ADR-0011。
- **依赖/时间盒**：R-30；4 小时。

## 第四部分：同步与收尾

> 离线执行结果（2026-08-01）：V-49～V-56 的 Host/NDK/ABI 工作已完成。干净 MSVC Release
> CTest `77/77`、Clang UBSan CTest `77/77`、三个 libFuzzer 固定种子运行、NDK r27d 双 ABI
> production build、Zygisk `APP_STL=none`/ELF isolation 和旧接口 guard 全部通过；ASan 因当前
> Windows 环境缺少 `stl_asan.lib`、TSan 因 Clang Windows runtime 不可用，已作为工具链限制归档。
> V-59/V-60 的 Host 故障与 30 分钟 soak 已完成，V-61/V-62 的离线追踪审计已完成。
> 0.1.24 设备补充修复归档后，最新 MSVC Release 与 Clang UBSan/static-runtime CTest 均为
> `78/78`，NDK r27d 双 ABI、Zygisk ELF/integration guard 与 comparison report guard 再次通过。

| 任务 | 当前状态 | 剩余边界 |
| --- | --- | --- |
| V-49～V-54 | complete | 无；旧接口删除为 planned break，Host unexpected regression 为 0 |
| V-55 | complete（available Host toolchain） | ASan/TSan 当前 Windows runtime unavailable，非项目失败 |
| V-56 | complete（offline gate） | 实际 Android operation/parity 归入 V-57/V-58 |
| T-27～R-27 | not_observed（用户授权跳过） | Host worker/store/executor 已完成；生产 fanotify adapter 在当前内核不可准入，保持 unsupported |
| V-43 | not_observed（current device unsupported） | Export normal/overflow/crash/cross-FS 不执行，bits 8～11 不误报 active |
| V-45 | complete（available-device scope） | myron active/restart fail-open 与真实文件结果一致；无生产构造入口的状态明确 not_observed/unsupported |
| V-48 | complete（available device scope） | Host soak、myron Provider restart/republication 与 RSS 已观察；第二设备/ROM 已按用户授权记为 `not_observed` |
| T-29～R-29/V-47 | blocked（V-46 adapter-only） | V-46 非 go，合法阻断真实 CompleteVfs backend 与设备重放 |
| V-57～V-58 | complete（available-device scope） | C1～C6 可执行场景、Provider path-I/O、FUSE open、restart 已归档；bit 17、shared UID、identity clear、query/insert 和未构造状态明确 unsupported/not_observed |
| V-59 | complete（available-device scope） | policy reload/race、topology race、snapshot gate、mount fault 和 production recovery 已归档；fanotify overflow 保持 unsupported |
| V-60 | complete（available-device scope） | 50 cold/50 warm 功能、10 cold 补充、Provider restart、RSS 已归档；warm latency telemetry 与 Export 保持 not_observed |
| V-61～V-62 | complete（available-device scope） | Host 与 myron 证据审计闭环；第二设备及跳过能力保留 not_observed/unsupported 边界 |
| V-63 | complete（available-device scope） | 当前范围 go；V-46 adapter-only、fanotify、bit 17、未构造状态及第二设备均为明确边界，不宣称 active |

详细证据索引：`tests/baseline/pattern-v6/README.md`。最终四个 Host/离线门均有通过
`tests/baseline/validate_comparison_report.cmake` 的 format 1 JSON 报告。
第二设备范围豁免记录于
`tests/baseline/pattern-v6/device-matrix-scope-waiver-20260801.md`；未观察组合不标记为通过。
当前设备闭环报告：
`tests/baseline/pattern-v6/p6-final-device-myron-v024-20260801/V-45-V-63-current-device-closure.json`。

### V-49 [验证] 记录 v5/format 1 删除前最后基线

- **任务描述**：确认 v6 已通过 V-15/V-39，运行全部 host tests 并列出 v5 reader/writer、format 1 parser/fixtures、兼容分支和生产引用。
- **验收标准**：删除清单区分生产死代码、只读历史证据和仍被测试误用的路径；C1～C6 当前均有绿基线。
- **关联需求**：设计 4.1、8、10 P1/P2、12；format 6 ADR。
- **依赖/时间盒**：V-39、V-48；3 小时。

### R-31 [重构] 删除 v5/format 1 生产死代码

- **任务描述**：删除旧 reader/writer、双格式分支、旧默认配置和不再可达的 migration/compat glue；保留带明确 `baseline-only` 标记的历史 golden/报告。
- **验收标准**：生产二进制不含 v5 magic/entry reader；旧输入只经统一 version mismatch 拒绝；相关测试同步更新。
- **关联需求**：设计 4.1、8、10 P1、12；YAGNI。
- **依赖/时间盒**：V-49；4 小时。

### V-50 [验证] 对比 v5 清理后的核心行为

- **任务描述**：重放 V-49 的 v6 tests 与 C1～C6 smoke；额外验证 v5/format 1 拒绝。
- **验收标准**：删除旧接口为 planned_break；v6 核心结果 unchanged；无链接/包体残留引用。
- **关联需求**：设计 1.3、4.1、11.0。
- **依赖/时间盒**：R-31；3 小时。

### V-51 [验证] 记录旧 mapper/compat 字段删除前基线

- **任务描述**：定位 `PathRule` prefix mapper、`file_picker`、`provider_compat`、旧 canonical reverse、动作专用 matcher 和字符串日志状态解析的剩余引用；运行对应测试。
- **验收标准**：每个旧符号映射到新 Selector/Action/intent/capability/provenance/status 替代物；无未替代核心路径。
- **关联需求**：设计 2.1、4.2、5、7.4、8；ADR-0006、0012、0013。
- **依赖/时间盒**：V-50；3 小时。

### R-32 [重构] 删除旧 mapper 与含混 compatibility API

- **任务描述**：删除被统一 IR/adapter 替代的旧类型和分支；配置只保留 `provider={enabled=true}` intent，运行状态只依据独立 capability/admission。
- **验收标准**：代码搜索无生产 `file_picker/provider_compat` 决策、canonical visible source fallback 或独立 glob 实现；全套相关测试绿。
- **关联需求**：设计 4.2、5、6.3、7.4～7.6、12；DRY、YAGNI。
- **依赖/时间盒**：V-51；4 小时。

### V-52 [验证] 对比统一 API 清理结果

- **任务描述**：重放 V-51，重点比较 LocalSend、literal prefix、glob、shared UID 和 status/explain。
- **验收标准**：旧 API 删除为 planned_break；C1～C6 与新 status contract 无回归；不存在隐式 fallback。
- **关联需求**：设计 1.3、7.1、11.0。
- **依赖/时间盒**：R-32；4 小时。

### R-33 [重构] 同步配置、接口、测试和设计文档

- **任务描述**：更新默认规则、README、CLI help、format/golden 说明、设备脚本、C/C++ 头、ADR 状态/交叉引用和 `08` 设计中的实现状态；移除过时示例。
- **验收标准**：每个 public 字段/bit/reason/operation 只有一份权威定义；ADR-0002/0006 等过时部分有 Superseded/implementation note，不与当前设计冲突。
- **关联需求**：设计 1.1、1.3、4、8、10、13；项目同步要求。
- **依赖/时间盒**：V-52；4 小时。

### V-53 [验证] 自动检查文档与配置一致性

- **任务描述**：运行/新增只读 guard，核对示例可编译、capability bits、format version、DecisionReason、operation mask、ADR 状态和文档链接；扫描旧字段。
- **验收标准**：所有示例生成有效 v6；无断链、重复 bit、旧 schema 或 Proposed ADR 被当成 Accepted 实现。
- **关联需求**：设计 1.1、4、8、13；项目同步要求。
- **依赖/时间盒**：R-33；3 小时。

### V-54 [验证] 执行最终 Host Release 回归

- **任务描述**：从干净 build 目录执行 configure/build/CTest，覆盖 unit/integration/golden/baseline/CLI/perf guard；与 V-02 测试 manifest 对比。
- **验收标准**：全部测试通过；原 52 项未无故消失，任何合并/更名有 planned_break 记录；新增测试全部进入 manifest。
- **关联需求**：设计 1.3、11；C1～C6。
- **依赖/时间盒**：V-53；4 小时。

### V-55 [验证] 执行 sanitizer、property 与 fuzz 回归

- **任务描述**：运行 parser/tokenizer/reader/matcher/provenance corpus，执行 ASan/UBSan 和可用的 TSan/concurrency harness；检查 seed minimization 和预算。
- **验收标准**：零 crash、OOB、UAF、race、hang；所有历史 crash seed 保留并通过；运行时长和 commit 已记录。
- **关联需求**：设计 11.1～11.2、11.4；ADR-0011、0014、0015。
- **依赖/时间盒**：V-54；4 小时。

### V-56 [验证] 执行 Android NDK/ABI 与模块打包门

- **任务描述**：构建 arm64-v8a/armeabi-v7a production variants，检查 ELF isolation、导出符号、共享协议布局、模块内容、注入 flag 关闭和 artifact hashes。
- **验收标准**：全部目标构建通过；Host/NDK 的 format/capability/static_assert 一致；包内无测试注入和旧 v5 artifact。
- **关联需求**：设计 8、11.3；ADR-0006、0012、0013。
- **依赖/时间盒**：V-54；4 小时。

### V-57 [验证] 重放最终 C1～C6 真机矩阵

- **任务描述**：在至少两个 ROM 家族、适用的 Magisk/KernelSU、不同 kernel capability tier 上按相同场景重放 literal deny/redirect、LocalSend、multi-source、failure isolation 和 glob。
- **验收标准**：每个观测组合都有合法 after 报告和 hashes；核心最低成功条件全部满足；未支持组合明确 unsupported/not_observed。
- **关联需求**：设计 1.3、11.3；C1～C6；ADR-0003、0005、0006、0010～0013。
- **依赖/时间盒**：V-56；每个设备批次 4 小时，作为独立重复任务执行。

### V-58 [验证] 执行 Provider 专项兼容矩阵

- **任务描述**：覆盖 ExternalStorageProvider/MediaProvider 版本差异、shared UID、identity clear、query/insert/open、媒体扫描、Provider restart 和 FUSE passthrough available/unavailable。
- **验收标准**：不按 OEM/Android 名称推断能力；每个 active 组合均有实际 operation probe；partial 组合不宣称 active。
- **关联需求**：设计 7.4～7.6、11.3；ADR-0006、0012；Android 官方资料。
- **依赖/时间盒**：V-57；每个 ROM 批次 4 小时。

### V-59 [验证] 执行最终故障注入矩阵

- **任务描述**：运行 pre/post mutation delay、owner death、rollback failure、policy corruption/reload、topology change、Provider Hook inactive、provenance crash、fanotify overflow 和 snapshot exhaustion。
- **验收标准**：所有失败符合冻结 reason/errno/status；无 namespace taint 被误报 complete、无 partial route、无 Provider crash loop、无文件静默丢失。
- **关联需求**：设计 9.3、11.3；ADR-0003、0005、0006、0011。
- **依赖/时间盒**：V-57；按故障组拆分，每组 2～4 小时。

### V-60 [验证] 执行最终性能与 soak 回归

- **任务描述**：运行 Release Host benchmark、设备 50 次冷/热操作、30 分钟 match/reload、Provider 接收批次和可选 Export；采集 P50/P95/P99/max、RSS 和计数器。
- **验收标准**：满足 T-30 阈值；无 slot/retire/overflow 隐性增长；性能数据不跨不同 verification mode 混合比较。
- **关联需求**：设计 11.4；ADR-0011；`tests/device/r1-safety-validation.md`。
- **依赖/时间盒**：V-57、V-59；每个 profile 4 小时。

### V-61 [验证] 审计所有计划内破坏与意外差异

- **任务描述**：汇总所有 before/after 报告，逐项审计 schema、binary、CLI、status、旧 reverse、旧 API 删除；任何核心结果差异必须追溯到最新需求或判为 regression。
- **验收标准**：`unexpected_regression=0`；每个 planned_break 都有同步测试/配置/接口/文档证据和 reviewer 结论。
- **关联需求**：设计 1.3、11.0；项目改动原则与验收标准。
- **依赖/时间盒**：V-53～V-60；4 小时。

### V-62 [验证] 完成需求—测试—证据追踪审计

- **任务描述**：更新 V-04 矩阵，将设计章节、ADR、代码模块、自动测试、设备场景、报告和已知限制闭环；检查每个任务验收产物可定位。
- **验收标准**：C1～C6、Glob v1 每个符号、五执行域、所有稳定 capability 位和全部 DecisionReason 均有测试/证据或明确 unsupported。
- **关联需求**：设计全文，重点 7、8、11、12。
- **依赖/时间盒**：V-61；3 小时。

### V-63 [验证] 最终实施完成判定

- **任务描述**：基于 V-54～V-62 做 go/no-go 评审；检查未决 ADR、临时 fallback、测试注入、脏生成物、文档冲突和核心闭环。
- **验收标准**：仅当 ADR 前置决策关闭、Host/ABI/设备/故障/性能门通过、unexpected regression 为零时判定完成；否则列出阻断任务 ID，不以“基本可用”替代验收。
- **关联需求**：设计 1.3、10～14；项目最终验收标准。
- **依赖/时间盒**：V-62；2 小时。

### 2026-08-01 myron 单设备批次

comparison format 1 报告：
`tests/baseline/pattern-v6/p6-final-device-myron-20260801/V-57-V-62-myron-device.json`。

| Task | 本批状态 | 结论边界 |
| --- | --- | --- |
| V-57 | partial observed | C1 deny、C2 mount redirect、C3 Provider、C4 多源不同名、C5 lifecycle 与 C6 Provider `**` 文件匹配已观察；第二设备与完整 operation matrix 未观察 |
| V-58 | partial observed | ExternalStorageProvider/MediaProvider `access/open/stat64` 和 MediaProvider FUSE open 已观察；shared UID、query/insert、Provider restart、FUSE unavailable 未观察 |
| V-59 | partial observed | pre-mutation cancel、verified rollback、owner death、rollback failure 和 production recovery 已完成；myron 为 `CONFIG_FANOTIFY` disabled，Provider restart、policy/topology/snapshot 与 fanotify 故障仍未观察 |
| V-60 | partial observed | 50/50 cold 功能检查通过，48 次 `am COLD`、2 次 `am UNKNOWN`；50/50 warm 操作保持同一 PID/status/mount，但 `am` 全部返回 UNKNOWN/0，故不宣称 warm latency；soak 后 TXT/JPG 接收通过；分阶段 timing/counters、Provider restart profile 和可选 Export 未观察 |
| V-61 | partial audited | 本批计划内 Provider resolver 变化无剩余 unexpected regression；最终结论依赖其余设备报告 |
| V-62 | partial audited | 本批 requirements-to-evidence 路径闭环；完整 ROM/framework/operation 追踪仍未闭环 |

该历史批次当时不改变 V-63 `blocked` 结论。后续补充证据与范围豁免已由
`p6-final-device-myron-v024-20260801/V-45-V-63-current-device-closure.json` supersede；
T-29～R-29/V-47 仍按 V-46 `adapter-only` 保持合法阻断。

## 第六部分：Provider contract adapter（方案 B）

本部分由用户于 2026-08-01 明确启动。它不改变此前 current-device closure，只扩展原设计 P3
中尚未投产的 query/insert/document ID/FD/reverse composite contract。现有 Provider 前向
path-I/O 继续作为稳定基线；任何新增 adapter 检查失败都保持 bit 17 `unsupported`，不得影响
已验证的前向保存。

| Task | 状态 | 当前证据/边界 |
| --- | --- | --- |
| T-34～R-34 | complete | `ProviderContractProbeV1`、Documents/MediaStore pair gate、缺 profile/check/operation/failure 矩阵；Host 红绿证据已归档 |
| V-64 | complete（alioth public-contract scope） | Android 13/alioth 上 12 个必需公开操作全部通过且临时对象残留为 0；Provider APK/APEX 身份和 SHA-256 已归档，myron/Android 16 本轮未观察 |
| T-35～R-35 | complete | 精确 APK SHA-256/build identity deployment pair gate、完整 check/operation mask、virtual source/target/URI/document-ID/strong FD/provenance binding conformance；MSVC Release 80/80 |
| T-36～R-36 | complete（Host/production wiring） | 独立 LSPlant C ABI bridge、最小 Java Hooker、精确 alioth APK SHA-256 gate、Zygisk pre/post lifecycle、双 ABI/ELF/许可证/状态观测；MSVC Release 81/81 |
| T-37～R-37 | complete（Host decision contract） | profile/operation/binding/committed reverse 的纯决策层；pass/rewrite/unsupported/ambiguous/fail-open 负矩阵；MSVC Release 82/82 |
| T-38～R-38 | complete（callback safety boundary） | Java dispatcher 异常/类型不匹配 fail-open 到 backup；Java/guard/双 ABI/CTest 通过；真实 provenance wiring 已由 T-54～R-58 完成 |
| T-39～R-39 | complete（native dispatcher seam） | Java/native `nativeDispatch` 注册入口；当前实现固定 pass-through；Host/双 ABI 与 `20260802-104458` 真机回归通过；bit 17 不变 |
| T-40～R-40 | complete（dispatch spec） | 11 方法的 role/operation mask/dynamic argument/result kind 完整表；native method-ID gate；对象提取已由 T-41～R-58 完成 |
| T-41～R-41 | complete（Host/ABI/device passthrough） | `r/w/wt/wa/rw/rwt` 有界解析；参数数量/类型/JNI 异常 fail-open；`20260802-110216` 真机回归通过，结果适配已由 T-54～R-58 完成 |
| T-42～R-42 | complete（Host/ABI/device passthrough） | 5 个 URI、5 个 document ID 方法显式类型/索引；固定 1024 字节 UTF-8 缓冲与 fail-open；`20260802-113334` 回归通过，持久 identity 已由 T-56 完成 |
| T-43～R-43 | complete（Host/ABI/device passthrough） | `getDocIdForFile(File)` 显式 File 类型与索引；固定 4096 字节绝对路径缓冲；`20260802-114618` 回归通过，reverse mapping 已由 T-58 完成 |
| T-44～R-44 | complete（immutable request Host/ABI） | 按值 dispatch request seam；精确 open operation；update/delete 动态事实由 T-56～R-58 补全；V-68 真机验收 pending |
| T-45～R-45 | complete（Host/ABI/device passthrough） | 非空 metadata 与 `_display_name` rename operation 提取；空/错误/JNI 异常 incomplete；`20260802-120913` 回归通过，delete target unsupported |
| T-46～R-46 | complete（Host/ABI；device passthrough） | dispatch request 映射为既有 `ProviderMappingOperation`，binding/profile/reverse 不匹配均保持 fail-open/pass-through；真实 route resolver wiring 已由 T-54 完成 |
| T-47～R-47 | complete（Host/ABI/device passthrough） | operation-specific binding validation：前向 Provider 操作不要求 committed reverse；reverse lookup 继续要求 strong identity 与 unique committed record；`0.1.36-dev` 回归通过 |
| T-48～R-53 | complete | runtime resolver、Java result transport、after-dispatch、result observation、C ABI facts 和 immutable route registry |
| T-54～R-58 | complete（Host/production implementation） | 真实 snapshot/resolver、bounded live publication、持久 external identity、权威 ContentValues、逐行 Cursor、PFD statx、mutation/reverse 接线；Host/双 ABI/ELF 通过 |
| V-67 | complete（Host contract；device pending） | 只允许 uniquely-bound item URI 进入 delete；collection/selection delete 无 binding 时 fail-open，实际 file/directory 由 path hook 的 unlink/rmdir 事实决定 |
| V-65 | pending（当前设备 partial） | `0.1.36-dev` 两组 passthrough/fault gate 复采通过；真实 virtual query/create/open/rename/delete/reverse、FD identity、Java callback fail-open 矩阵当前设备无法构造，记为 unsupported/not_observed；bit 17 不置位 |
| V-66 | pending（当前设备 partial） | `0.1.36-dev` Provider restart/republication 已通过（PID 轮换、hook 重装、状态重发）；Mainline build identity 变化、provenance recovery、真实 virtual mapping 仍 unsupported/not_observed |
| V-68 | pending（需要当前设备安装候选） | `0.1.41-dev` 需执行真实 insert/query/open/update/delete/File reverse/Provider restart 矩阵；通过前 bit 17 保持清零 |

### T-34 [红] 冻结 Provider contract pair gate

- **任务描述**：定义 versioned Provider probe，使 DocumentsProvider 与 MediaStore 必须分别完成
  ABI profile、caller UID、query、create/insert、stable document ID、FD identity、rename/delete、
  reverse 和 restart 检查；任一单域 partial 不得形成部署级 active。
- **验收标准**：测试先因 `ObserveProviderContractPair` 缺失而编译失败；失败点只能是 pair gate
  尚未实现。
- **关联需求**：设计 7.4、7.6、10 P3、11.3；ADR-0006、0012、0017。

### I-34 [绿] 实现版本化 Provider contract evaluator

- **任务描述**：实现无 Android 私有 ABI 依赖的 `ProviderContractProbeV1` 和 Documents/MediaStore
  pair evaluator；只输出 capability/operation/check 缺失事实，不安装生产 Hook。
- **验收标准**：完整 pair active；缺 adapter profile、失败/缺失 check、缺 operation、类型交换
  均保持 inactive/unsupported，bit 17 不误置。
- **依赖/时间盒**：T-34；2 小时。

### R-34 [重构] 建立独立 Android 公共合同探针

- **任务描述**：复用现有 Gradle wrapper，新增独立 probe APK；MediaStore 自动执行
  insert/query/open/rename/delete，SAF 经用户授权目录执行 create/query/document-ID/open/rename/delete，
  临时对象必须清理，JSONL 证据与 Provider/APEX 环境信息归档。
- **验收标准**：`:providerContract:assembleDebug` 通过；PowerShell runner 语法通过；APK 不申请
  `MANAGE_EXTERNAL_STORAGE`，不能把公开 API 基线通过解释为生产 adapter active。
- **依赖/时间盒**：I-34；3 小时。

### V-64 [验证] 可用设备 Provider 公共操作与模块身份基线

- **任务描述**：在可用 production 配置安装 probe APK，选择可写 SAF 测试目录，
  归档两类 Provider 的公开操作结果、fingerprint、SDK、kernel、Provider package/APEX 信息。
- **验收标准**：12 个必需公开操作全部通过且无临时文件残留；失败项精确记录，不据此直接设置
  adapter profile 或 bit 17。
- **依赖/时间盒**：R-34；设备交互 10 分钟。
- **执行结果**：2026-08-01 在 Redmi alioth、Android 13/API 33、MIUI
  `V14.0.8.0.TKHCNXM` 上完成。MediaStore 与 DocumentsProvider 的 12 个必需操作全部通过，
  `pg-contract-*` 残留为 0；ExternalStorageProvider APK 与 MediaProvider APK/APEX 身份及 SHA-256
  已冻结。该结果只关闭公开合同基线，不代表私有 adapter profile、虚拟映射或 bit 17 active；
  myron/Android 16 本轮未连接，按可用设备原则记为 `not_observed`，不阻塞 T-35 Host 工作。

### T-35 [红] 冻结 version-pinned deployment profile 与 route binding

- **任务描述**：DocumentsProvider 与 MediaStore profile 必须分别精确绑定 kind、SDK、versionCode、
  APEX version（适用时）和完整 APK SHA-256；部署级 profile 只有在两端同时匹配时才成立。冻结
  visible source、backing target、Provider URI/document ID、FD object identity 与 reverse provenance
  的单对象一致性合同。
- **验收标准**：测试先因 `provider_adapter_profile.h` 缺失而编译失败；Android 大版本或单一
  versionCode 相同但 APK hash 不同不得匹配；弱 identity、source/target 同址和 reverse 不一致必须拒绝。
- **依赖/时间盒**：V-64；2 小时。

### I-35 [绿] 实现 Host adapter profile 与 mapping conformance

- **任务描述**：实现 `ProviderBuildIdentityV1`、单 Provider profile、部署 pair gate 和
  `ProviderRouteBindingV1` validator；未知 build、profile 缺 check/operation、任一 Provider 不匹配
  均返回非 matched，不设置运行时 capability。
- **验收标准**：alioth 两个已归档 APK 的完整 SHA-256 profile pair 通过；hash/kind/build/mask
  负矩阵及 route binding 负矩阵全绿。
- **依赖/时间盒**：T-35；3 小时。

### R-35 [重构] 固化 composite profile 的 fail-open 边界

- **任务描述**：单 Provider 匹配与 deployment pair 选择复用同一 evaluator；route binding 复用
  ADR-0017 `ObjectIdentity`/`RouteRecord`，不另建 Provider-only reverse owner 模型。
- **验收标准**：Provider 专项测试和完整 MSVC Release 回归通过；现有生产 wiring 与 bit 17 不变。
- **执行结果**：红测为缺少 `pathguard/provider_adapter_profile.h`；实现后两个 Provider 专项测试
  2/2、MSVC Release 80/80、comparison report validator 与 `git diff --check` 通过。
- **依赖/时间盒**：I-35；2 小时。

### T-36 [红] 冻结 LSPlant Java method bridge 合同

- **任务描述**：冻结 alioth ExternalStorageProvider、MediaProvider 和 MediaDocumentsProvider 的
  11 个 Java 方法描述符；profile、library、LSPlant init、DEX、resolve、hook、backup、自检任一缺失
  都必须整体 inactive。
- **验收标准**：Documents 两方法和 Media 九方法分别成组通过；两端 deployment pair 未同时 active
  时不得设置 bit 17。
- **依赖/时间盒**：R-35；2 小时。

### I-36 [绿] 接入独立 LSPlant bridge 与 Zygisk 生命周期

- **任务描述**：以独立 `libpathguard_lsplant.so` 和最小 Java Hooker DEX 接入 Zygisk；未知 APK
  在 SHA-256 gate 前不得 `dlopen`；匹配 profile 在 pre-specialize 初始化 LSPlant，在 post-specialize
  加载 DEX 并成组安装 passthrough Hook，失败时整体 unhook/fail-open。
- **验收标准**：Zygisk 保持 `APP_STL=none`/ELF isolation；bridge 双 ABI 无 `libc++_shared.so`、
  仅导出版本化 C ABI、16 KiB page compatible；runtime status 输出完整 bridge 观测且 bit 17 清零。
- **依赖/时间盒**：T-36；4 小时。

### R-36 [重构] 固化依赖、打包与真机证据边界

- **任务描述**：固定 LSPlant/DexBuilder/parallel-hashmap/xDL/Dobby 版本与许可证；统一构建、ELF
  检查、ZIP 内容和真机状态采集入口，不把 passthrough 自检写成虚拟映射完成。
- **验收标准**：MSVC Release 81/81、NDK r27d Zygisk 双 ABI、NDK 29 bridge 双 ABI、ELF guards、
  ZIP 许可证/产物检查全部通过；V-65 仍明确需要真机。
- **执行结果**：Host 与双 ABI 门已通过。`0.1.27-dev` 真机证据确认 post-specialize
  仍处于 Zygote 调用栈，Provider APK ClassLoader 尚未创建；已改为 worker 等待
  `ActivityThread.currentApplication().getClassLoader()` 后再安装 Hook，并异步发布状态。
  生成 `0.1.28-dev` 真机候选；`build/device-evidence/provider-lsplant-v1/20260802-085431`
  曾通过旧采集脚本的安装状态断言：ExternalStorageProvider 3/3/3/3、MediaProvider
  2044/2044/2044/2044（resolved/installed/backup/self-tested），两端 errno=0，
  deferred hook active/result 均出现，bit 17 仍清零；但完整 logcat 复核发现
  ExternalStorageProvider `null receiver` fatal 和 MediaProvider query NPE，该结果是假阳性。
  `0.1.29-dev` 已冻结 hook 前目标 Method 属性、修正 LSPlant global ref 生命周期，并增强
  采集脚本 runtime fault gate；该脚本已在仍运行 `0.1.28-dev` 的设备上按预期红测，证据为
  `build/device-evidence/provider-lsplant-v1/20260802-090813`。MSVC Release 81/81、NDK r27d
  Zygisk 双 ABI、NDK 29 bridge 双 ABI/DEX/ELF、production integration guard 和打包均通过；
  `0.1.29-dev` 真机复测证据 `build/device-evidence/provider-lsplant-v1/20260802-092822` 已通过：
  两个 Provider 的方法组分别为 3/3/3/3 与 2044/2044/2044/2044，errno=0，runtime fault gate
  无异常，bit 17 仍清零。真实 URI/document ID/route/FD/reverse 映射及真机失败注入仍未完成，
  V-65 继续保持 pending。
- **依赖/时间盒**：I-36；3 小时。

### T-37 [红] 冻结 Provider mapping 改写决策合同

- **任务描述**：在 Java/native 参数改写前，冻结 profile、operation、route binding 与 committed
  reverse 的联合准入；区分无 route 透传、未知 profile/operation unsupported、ambiguous reverse
  和 runtime/binding fail-open。
- **验收标准**：测试先因 `pathguard/provider_mapping.h` 缺失而编译失败；URI/document ID 缺失、
  source/target 同址、弱 FD identity、reverse mismatch、ambiguous reverse 和 profile mismatch
  均不得返回 rewrite。
- **依赖/时间盒**：R-36；2 小时。

### I-37 [绿] 实现无 JNI/I/O 的 mapping evaluator

- **任务描述**：实现版本化 `ProviderMappingRequestV1`/`ProviderMappingDecisionV1`；只消费既有
  adapter profile、route binding 和 provenance resolve 结果，不直接读取 daemon/store，不修改
  Java 对象，不发布 capability。
- **验收标准**：只有 profile/operation/binding/unique reverse 全部一致时返回 rewrite；无 route
  pass，缺能力 unsupported，歧义 reverse 明确返回 ambiguous，其余错误 fail-open。
- **依赖/时间盒**：T-37；2 小时。

### R-37 [重构] 固化 operation mask 与 provenance 单一来源

- **任务描述**：Provider query/create/open/rename/delete/media scan/reverse 统一映射到既有
  `OperationMask`；reverse 比对复用 `RouteRecord`，不创建 Provider-only identity 模型。
- **验收标准**：专项测试 1/1、MSVC Release 82/82；Java/native callback 和 bit 17 保持不变，
  V-65 不因 Host evaluator 完成而提前关闭。
- **执行结果**：红测唯一失败点为缺少 `provider_mapping.h`；实现及完整 Host 回归通过。证据：
  `tests/baseline/pattern-v6/p6-provider-mapping-20260802/T-37-R-37-provider-mapping-host.md`。
- **依赖/时间盒**：I-37；1 小时。

### V-65 [验证] 当前设备 LSPlant passthrough 与真实映射边界

- **执行结果**：增强 collector 于 `20260802-095957` 复采通过；ExternalStorageProvider
  `3/3/3/3`、MediaProvider `2044/2044/2044/2044`，两端 `errno=0`，runtime status
  `action_total=2` 且两条 admission active，`observed_capabilities=65536`，bit 17 清零。
- **设备边界**：当前公开 Provider probe 无法向生产 Provider callback 注入统一
  `RouteRecord`/FD identity，因此 virtual query/create/document-ID/FD/rename/delete/reverse
  和 Java callback fail-open 注入均为 `unsupported/not_observed`，不计入通过。
- **证据**：
  `tests/baseline/pattern-v6/p6-provider-v65-20260802/V-65-current-device-passthrough-and-mapping-boundary.json`；
  `0.1.36-dev` 最新证据为 `build/device-evidence/provider-lsplant-v1/20260802-124319`。
- **结论**：V-65 总状态保持 `pending`；只有真实 composite mapping 矩阵可构造并全部通过后，
  才允许改变 bit 17。

### V-66 [验证] 当前设备 Provider restart/republication

- **执行结果**：在 `0.1.36-dev` 上 force-stop 两个 Provider 后触发公开 query；
  ExternalStorageProvider PID `7611 -> 11434`，MediaProvider PID `5085 -> 11302`。两端重新
  发布 status，方法组分别为 `3/3/3/3` 与 `2044/2044/2044/2044`，`errno=0`，两条 admission
  active，bit 17 仍清零。
- **增强 fault gate**：最终 collector 证据
  `build/device-evidence/provider-lsplant-v1/20260802-124446` 通过，无 Provider fatal/null
  receiver/JNI ref 告警。MediaProvider 在 volume attach 前触发的 `VolumeNotFoundException`
  来自原始 query/其他 LSPosed hook 透传路径，不是 PathGuard dispatcher failure。
- **设备边界**：当前设备未更换 Mainline Provider artifact，无法构造版本身份变化；
  provenance journal recovery、真实 virtual mapping 和 adapter 冷启动回归仍为
  `unsupported/not_observed`。
- **证据**：
  `tests/baseline/pattern-v6/p6-provider-v66-20260802/V-66-current-device-restart.json`；
  `0.1.36-dev` 最新证据为 `build/device-evidence/provider-lsplant-v1/20260802-124446`，
  PID 轮换为 ExternalStorageProvider `7611 -> 11434`、MediaProvider `5085 -> 11302`。
- **结论**：V-66 总状态保持 `pending`，bit 17 不置位。

### T-38 [红] 冻结 Java callback dispatcher 安全边界

- **任务描述**：ProviderHooker 在调用 LSPlant backup 前增加可关闭 dispatcher；dispatcher 只
  能返回显式 pass/rewrite 结果，异常、缺失、结果类型不匹配均必须回到原始 backup。
- **验收标准**：目标返回类型在 hook 前冻结；reference/primitive 返回值做类型检查；默认无
  dispatcher 时行为与 passthrough 相同；不得在该任务中设置 bit 17。
- **依赖/时间盒**：R-37；2 小时。

### I-38 [绿] 实现 callback dispatcher fail-open seam

- **任务描述**：实现 `Dispatcher`/`DispatchResult` Java seam，保留原 receiver/backup 处理；
  不接入 daemon/store，不在 callback 中执行阻塞 I/O。
- **验收标准**：Java 11 编译、production integration guard、NDK 29 双 ABI 和 MSVC Release
  全部通过；dispatcher 未设置时两组 Provider 继续 passthrough。
- **依赖/时间盒**：T-38；2 小时。

### R-38 [重构] 固化真实映射接线前的类型和失败边界

- **任务描述**：把未来 `ProviderMappingDecisionV1` 的 rewrite 结果限制在 Java 目标返回类型
  可兼容范围内，禁止 malformed result 破坏 Provider 进程。
- **验收标准**：当前实现只提供安全 seam，不宣称 URI/document ID/FD/reverse 完成；V-65 和
  bit 17 保持 pending/unsupported。
- **执行结果**：Java 编译、production integration guard、LSPlant 双 ABI 和 MSVC Release
  `82/82` 通过。证据：
  `tests/baseline/pattern-v6/p6-provider-callback-20260802/T-38-R-38-provider-callback-boundary.md`。
- **当前设备复采**：`build/device-evidence/provider-lsplant-v1/20260802-103041` 通过；
  两端方法组、errno、两条 active admission 与 bit 17 清零保持不变。该轮仍只证明
  passthrough 回归，不证明 dispatcher rewrite。
- **依赖/时间盒**：I-38；1 小时。

### T-39 [红] 冻结 Java/native dispatcher 注册合同

- **任务描述**：为 ProviderHooker 注册 native `nativeDispatch(int,Object[])`，确保 callback
  进入统一 native 决策入口；native 返回空决策时必须保持 Java `DispatchResult.pass()`。
- **验收标准**：JNI 方法签名、注册时机和异常边界固定；未有 route binding 时不得改写任何
  Provider 对象，不得设置 bit 17。
- **依赖/时间盒**：R-38；2 小时。

### I-39 [绿] 接入 native dispatcher pass-through seam

- **任务描述**：在 Hooker DEX 加载后注册 native 方法并启用 dispatcher；native 实现暂时只
  返回 pass-through，不访问 daemon/store、不执行阻塞 I/O。
- **验收标准**：Host dispatcher test、production guard、NDK 29 双 ABI、MSVC Release 全部
  通过；当前设备安装后 fault gate 无回归。
- **依赖/时间盒**：T-39；2 小时。

### R-39 [重构] 保持真实 provenance 接入单一来源

- **任务描述**：未来 native dispatcher 只能消费既有 `ProviderMappingDecisionV1` 和
  `provenance_protocol`，禁止在 Java Hooker 内建立第二套 route/identity 模型。
- **验收标准**：本任务只交付 pass-through 接线；V-65 仍 pending，bit 17 仍 unsupported。
- **执行结果**：Host CTest `82/82`、production integration guard、JDK dispatcher test 与
  LSPlant 双 ABI 构建通过。证据：
  `tests/baseline/pattern-v6/p6-provider-native-dispatch-20260802/T-39-R-39-native-dispatch-seam.md`。
- **当前设备复采**：`build/device-evidence/provider-lsplant-v1/20260802-104458` 已确认
  `Provider native dispatcher installed as pass-through`，两端方法组和 fault gate 通过，
  `observed_capabilities=65536` 且 bit 17 清零。该结果只关闭 native seam 的回归风险，
  不关闭真实 mapping。
- **依赖/时间盒**：I-39；1 小时。

### T-40 [红] 冻结 11 方法 dispatch 语义表

- **任务描述**：为每个 version-pinned Java Hook 冻结 dispatch role、最低 operation mask、
  是否需要 mode/ContentValues 动态判定及 Java 返回对象类型。
- **验收标准**：11 个 method ID 完整且顺序唯一；unknown ID、空参数保持 pass-through；
  open/update/delete 不得用静态 method ID 猜测最终 operation。
- **依赖/时间盒**：R-39；2 小时。

### I-40 [绿] 实现 dispatch spec 与 native method-ID gate

- **任务描述**：实现 constexpr `ProviderJavaDispatchSpecV1` 表和完整性 validator；native
  dispatcher 只接受表中 method ID 与非空参数数组。
- **验收标准**：专项 Host 测试、production integration guard 和 LSPlant 双 ABI 构建通过；
  dispatcher 仍固定 pass-through。
- **依赖/时间盒**：T-40；2 小时。

### R-40 [重构] 分离静态方法角色与动态参数决策

- **任务描述**：静态表只声明最小能力边界；open mode、update ContentValues、delete target
  类型留给后续有界 extractor，不在方法名层猜测。
- **验收标准**：无 JNI 对象改写、无 provenance I/O、bit 17 不变；V-65 继续 pending。
- **执行结果**：专项测试、NDK 29 双 ABI、production integration guard 通过。证据：
  `tests/baseline/pattern-v6/p6-provider-dispatch-spec-20260802/T-40-R-40-dispatch-spec-host.md`。
- **依赖/时间盒**：I-40；1 小时。

### T-41 [红] 冻结 Provider open mode 有界解析合同

- **任务描述**：为 MediaProvider `openFile` 与 MediaDocumentsProvider `openDocument` 冻结
  Android mode 到 operation mask 的映射；未知、空值、过长或错误类型必须 fail-open。
- **验收标准**：`r` 仅映射 open-read；`w/wt/wa` 仅映射 open-write；`rw/rwt` 同时映射
  open-read/open-write；其他输入返回 0，dispatcher 不改写结果。
- **依赖/时间盒**：R-40；1 小时。

### I-41 [绿] 实现无分配 JNI open mode extractor

- **任务描述**：从冻结 callback 参数索引读取 mode；先校验参数数量和 `java.lang.String`
  类型，再用最多 3 个 UTF-16 code unit 的栈缓冲解析 ASCII mode。
- **验收标准**：callback 热路径无阻塞 I/O、无堆分配；JNI 查找、数组读取、类型检查或字符串
  读取异常均清理并 pass-through；专项测试与 NDK 29 双 ABI构建通过。
- **依赖/时间盒**：T-41；2 小时。

### R-41 [重构] 保持 extractor 与 mapping 决策解耦

- **任务描述**：本阶段只提取 operation mask，不访问 daemon/store，不创建 Java 对象，不连接
  provenance，也不改变 backup 调用和返回值。
- **验收标准**：MSVC Release `82/82`、production integration guard、双 ABI 与
  `git diff --check` 通过；bit 17 保持清零。URI/document ID/ContentValues 与真实 route binding
  留给后续任务。
- **执行结果**：Host/ABI 与 `0.1.30-dev` 当前设备 passthrough 回归均通过；设备证据
  `build/device-evidence/provider-lsplant-v1/20260802-110216` 显示两端 `provider_bridge_errno=0`、
  方法集合完整、admission active 且 bit 17 清零。证据：
  `tests/baseline/pattern-v6/p6-provider-open-mode-20260802/T-41-R-41-open-mode-host.md`。
- **依赖/时间盒**：I-41；1 小时。

### T-42 [红] 冻结 Provider URI/document ID 参数合同

- **任务描述**：为 11 个 Hook 显式声明主标识参数类型和索引；5 个 MediaProvider 方法使用
  `android.net.Uri`，ExternalStorageProvider 正向方法和 4 个 MediaDocumentsProvider 方法使用
  document ID；`getDocIdForFile(File)` 本阶段保持无标识 extractor。
- **验收标准**：10 个已支持方法均为 callback 索引 1；File reverse 不得被错误转换为 String；
  null、错误类型与非 `content://` URI 必须 pass-through。
- **依赖/时间盒**：R-41；2 小时。

### I-42 [绿] 实现固定容量 UTF-8 identifier extractor

- **任务描述**：从 Java String 或 Uri `toString()` 读取最多 1023 字节的标准 UTF-8；正确处理
  UTF-16 surrogate pair，并拒绝孤立 surrogate、NUL、控制字符和容量溢出。
- **验收标准**：中文、补充平面字符、非法 surrogate、控制字符、超长输入及 URI scheme 矩阵
  通过；JNI 查找/调用/数组/字符串异常均清理并 pass-through。
- **依赖/时间盒**：T-42；3 小时。

### R-42 [重构] 保持标识提取与 route/provenance 分离

- **任务描述**：只产出 version-pinned、固定容量的输入事实；不构造
  `ProviderRouteBindingV1`，不访问 daemon/store，不创建改写结果。
- **验收标准**：MSVC Release `82/82`、production integration guard、NDK 29 双 ABI 与
  `git diff --check` 通过；bit 17 清零，V-65 继续 pending。
- **执行结果**：Host/ABI 已通过；设备候选包待安装。证据：
  `tests/baseline/pattern-v6/p6-provider-identifier-20260802/T-42-R-42-identifier-host.md`。
- **依赖/时间盒**：I-42；1 小时。

### T-43 [红] 冻结 File reverse 参数合同

- **任务描述**：为 ExternalStorageProvider `getDocIdForFile(File)` 声明 File 主参数和 callback
  索引 1；不把 File 当作 document ID 或 URI 处理。
- **验收标准**：错误类型、null、相对路径、空路径和超长路径均 pass-through；文件路径不进入
  `ProviderRouteBindingV1` 或 provenance。
- **依赖/时间盒**：R-42；1 小时。

### I-43 [绿] 实现固定容量 File path extractor

- **任务描述**：校验 `java.io.File`，调用冻结的 `getPath()`，以最多 4095 字节标准 UTF-8
  写入栈上路径缓冲，并要求绝对路径格式。
- **验收标准**：Unicode/代理项、路径边界、错误 Java 类型和 JNI 异常矩阵通过；无 native 堆
  分配、阻塞 I/O 或 Java 返回值改写。
- **依赖/时间盒**：T-43；2 小时。

### R-43 [重构] 保持 reverse extractor 只产出事实

- **任务描述**：File path 仅作为后续 reverse lookup 的输入事实；本阶段不扫描目录、不生成
  document ID、不调用 daemon/store。
- **验收标准**：MSVC Release `82/82`、production integration guard、NDK 29 双 ABI 与
  `git diff --check` 通过；bit 17 清零。真实 reverse mapping 留待后续任务。
- **执行结果**：Host/ABI 与 `0.1.32-dev` 当前设备 passthrough 回归通过；设备证据
  `build/device-evidence/provider-lsplant-v1/20260802-114618` 显示完整方法组、
  `provider_bridge_errno=0`、admission active 且 bit 17 清零。证据：
  `tests/baseline/pattern-v6/p6-provider-file-reverse-20260802/T-43-R-43-file-reverse-host.md`。
- **依赖/时间盒**：I-43；1 小时。

### T-44 [红] 冻结 immutable Provider dispatch request

- **任务描述**：把 method role/result、精确 operation mask、URI/document ID/File path 事实复制为
  versioned 按值请求；请求不得携带 `JNIEnv*`、Java ref 或借用指针。
- **验收标准**：静态 operation 必须精确匹配；open 使用 mode 提取结果；update/delete 在动态
  ContentValues/target 未解析时返回 incomplete，不得用最低 mask 猜测。
- **依赖/时间盒**：R-43；2 小时。

### I-44 [绿] 接入 native request pass-through seam

- **任务描述**：native dispatcher 构建 ready request 后同步交给单一 `DispatchProviderRequest`
  入口；该入口暂时固定返回 pass-through。
- **验收标准**：请求构建器纯 Host 矩阵、production guard、MSVC Release、NDK 29 双 ABI 通过；
  请求不触发 daemon/store、Java 对象创建或结果改写。
- **依赖/时间盒**：T-44；2 小时。

### R-44 [重构] 保持请求生命周期与 mapping 决策解耦

- **任务描述**：请求只表达已验证事实；后续 mapping 层消费 `const` 请求并复用既有
  `ProviderMappingDecisionV1`，禁止在 dispatcher 内复制 route/provenance 模型。
- **验收标准**：MSVC Release `82/82`、production integration guard、双 ABI 与
  `git diff --check` 通过；bit 17 清零，真实 rewrite 仍关闭。
- **执行结果**：Host/ABI 已通过；设备候选包待安装。证据：
  `tests/baseline/pattern-v6/p6-provider-request-20260802/T-44-R-44-request-host.md`。
- **依赖/时间盒**：I-44；1 小时。

### T-45 [红] 冻结 ContentValues 动态 operation 合同

- **任务描述**：为 MediaProvider `update(Uri,ContentValues,Bundle)` 声明 ContentValues 参数索引
  2；非空 values 至少产生 metadata mutation，存在 `_display_name` 时额外产生 rename。
- **验收标准**：null、空集合、错误类型、JNI 异常均 incomplete；不得读取未冻结的任意 key，
  不得从 method ID 静态猜测 rename。
- **依赖/时间盒**：R-44；1 小时。

### I-45 [绿] 实现 ContentValues bounded operation extractor

- **任务描述**：仅调用冻结的 `size()` 与 `containsKey("_display_name")`，把结果转换为既有
  operation mask；临时 Java refs 在 native 边界释放。
- **验收标准**：Host dynamic matrix、production guard、MSVC Release、NDK 29 双 ABI 通过；不
  创建 route binding、不访问 daemon/store、不改变 Java 返回值。
- **依赖/时间盒**：T-45；2 小时。

### R-45 [重构] 保持 ContentValues 与 delete target 分离

- **任务描述**：ContentValues extractor 只负责 update；MediaProvider delete 的 file/directory
  target 类型单独等待可信事实，不能复用 `_display_name` 或 URI 形状。
- **验收标准**：MSVC Release `82/82`、production integration guard、双 ABI 与
  `git diff --check` 通过；delete target 仍 incomplete，bit 17 清零。
- **执行结果**：Host/ABI 与 `0.1.34-dev` 当前设备 passthrough 回归通过；设备证据
  `build/device-evidence/provider-lsplant-v1/20260802-120913` 显示完整方法组、
  `provider_bridge_errno=0`、admission active 且 bit 17 清零。证据：
  `tests/baseline/pattern-v6/p6-provider-content-values-20260802/T-45-R-45-content-values-host.md`。
- **依赖/时间盒**：I-45；1 小时。

### T-46 [红] 冻结 Provider mapping adapter seam

- **任务描述**：将已验证的 Java dispatch request 映射为既有 `ProviderMappingOperation`，并通过
  `BuildProviderMappingRequest` 交给统一 `EvaluateProviderMapping`；不得在 dispatcher 内复制
  route/provenance 模型。
- **验收标准**：query、directory query、create、open read/write、metadata、rename、delete
  file/directory 和 reverse operation 映射矩阵完整；未知版本/operation、identifier 与 binding
  不一致、缺 profile/binding/runtime/reverse 时均保持 pass-through 或明确 fail-open；不创建
  Java 返回对象，不访问 daemon/store，不设置 bit 17。
- **依赖/时间盒**：R-45；2 小时。

### I-46 [绿] 接入 native mapping decision adapter

- **任务描述**：在 `DispatchProviderRequest` 中构造按值 mapping request，复用现有 profile、
  route binding 和 provenance 决策 API；当前无 resolver/binding 注入时固定返回 pass-through。
- **验收标准**：`pathguard_provider_mapping_test`、LSPlant bridge 专项测试、MSVC Release、
  production integration guard、NDK 29 双 ABI 与 `git diff --check` 全部通过。
- **依赖/时间盒**：T-46；2 小时。

### R-46 [重构] 保持 adapter 与真实 resolver wiring 解耦

- **任务描述**：adapter 只负责请求类型转换和既有决策调用；真实 route resolver、provenance
  store、Java 返回对象构造和 bit 17 enable 继续留给后续任务。
- **验收标准**：无 binding/profile/resolver 时不改写 Provider 行为；delete target 未解析时不
  猜测 unlink/rmdir；设备回归只证明 LSPlant passthrough 稳定，不宣称真实 mapping 已启用。
- **执行结果**：Host CTest `82/82`、`pathguard_provider_mapping_test`、LSPlant bridge 专项
  测试、production integration guard、NDK 29 `arm64-v8a`/`armeabi-v7a` 与 `git diff --check`
  均通过。`0.1.35-dev` 真机证据
  `build/device-evidence/provider-lsplant-v1/20260802-122500` 显示 MediaProvider
  `2044/2044/2044/2044`、ExternalStorageProvider `3/3/3/3`，两端
  `provider_bridge_errno=0`、admission active、bit 17 清零；该证据仅覆盖 passthrough
  回归。
- **依赖/时间盒**：I-46；1 小时。

### T-47 [红] 冻结 operation-specific binding validation 合同

- **任务描述**：区分前向 Provider 操作与 reverse lookup 的 binding 要求；query/create/open/
  metadata/rename/delete 只能要求 context、source/target path 和外部标识一致，不得因为尚未产生
  committed reverse record 就 fail-open。
- **验收标准**：前向 operation 在缺 reverse resolve 时仍可进入 rewrite 决策；reverse lookup
  继续要求 strong FD identity、unique committed record 和 generation/rule/scope 一致；未知
  operation、profile mismatch、runtime unavailable、binding path 不合法仍保持原失败分类。
- **依赖/时间盒**：R-46；2 小时。

### I-47 [绿] 实现前向/反向分离的 mapping evaluator

- **任务描述**：在 `EvaluateProviderMapping` 内使用 operation-specific validator；正向矩阵覆盖
  query、directory query、create、open read/write/read-write、metadata、rename、delete file/
  directory，reverse 负矩阵保留 ambiguous/missing/mismatch/error。
- **验收标准**：`pathguard_provider_mapping_test` 覆盖完整矩阵；MSVC Release `82/82`、
  production integration guard、NDK 27d 双 ABI、NDK 29 LSPlant 双 ABI 与 `git diff --check`
  通过。
- **依赖/时间盒**：T-47；2 小时。

### R-47 [重构] 保持真实 rewrite 关闭直到 resolver/provenance 注入完成

- **任务描述**：本阶段只修正 evaluator 合同，不启用 bit 17，不创建 Java 返回对象，不连接真实
  route resolver；设备验证仍只作为 LSPlant passthrough 与重启安全回归。
- **验收标准**：`0.1.36-dev` 安装后两端 Provider hook group 完整、admission active、
  `provider_bridge_errno=0`，bit 17 清零；不可构造的真实 mapping 继续标记
  `unsupported/not_observed`。
- **执行结果**：Host/ABI 全量回归通过；`dist/pathguard-next-v0.1.36-dev-universal.zip`
  SHA-256 为 `0883eee7b3199600e7efd50a5980713d9dcdd117f9c3059c8803cfe3596c0286`。
  当前设备证据 `build/device-evidence/provider-lsplant-v1/20260802-124319` 与重启证据
  `build/device-evidence/provider-lsplant-v1/20260802-124446` 均通过。
- **依赖/时间盒**：I-47；1 小时。

### T-48 [红] 冻结 Provider runtime resolver C ABI

- **任务描述**：定义不携带 JNI ref 或 daemon 句柄的 versioned runtime facts resolver；跨库
  配置头保持 C ABI，facts 只在 LSPlant bridge 内表达 profile、operation、binding、reverse
  resolve 和 runtime availability。
- **验收标准**：C/C++ 均可解析 bridge ABI 头；resolver 缺失/失败保持 pass-through；配置在
  hook 安装前固定，安装中或安装后修改返回 `EBUSY`；不得启用 bit 17。
- **执行结果**：完成。证据：
  `tests/baseline/pattern-v6/p6-provider-runtime-resolver-20260802/T-48-R-48-runtime-resolver-seam-host.md`。
- **依赖/时间盒**：R-47；2 小时。

### I-48 [绿] 接入 native dispatcher runtime resolver seam

- **任务描述**：让 native dispatcher 通过单一 resolver adapter 调用既有
  `EvaluateProviderMapping`，无 resolver 时保留 Java backup/pass-through 路径。
- **验收标准**：Host `82/82`、mapping/bridge/production guards、NDK r27d 双 ABI、NDK 29
  LSPlant 双 ABI 与 ELF export guard 全部通过；不访问 daemon/store、不创建 Java 返回对象。
- **执行结果**：完成（Host/ABI/offline scope）。证据同 T-48。
- **依赖/时间盒**：T-48；2 小时。

### R-48 [重构] 保持真实 Java rewrite 与 capability bit 17 关闭

- **任务描述**：resolver seam 只承载经过验证的 facts；在真实 provenance/binding 和返回
  对象构造完成前，不把任何 decision 转换为 Java 返回值，也不设置 Provider mapping capability。
- **验收标准**：当前设备只验证既有 passthrough/restart；真实 mapping、FD identity、Java
  callback fail-open 和 V-65 完整矩阵继续标记 `unsupported/not_observed`。
- **执行结果**：完成（可用设备/当前架构范围）；设备边界证据同 T-48。
- **依赖/时间盒**：I-48；1 小时。

### T-49 [红] 冻结 native-to-Java DispatchResult transport

- **任务描述**：native dispatcher 的返回值必须通过 Hooker Java 层进入既有
  `DispatchResult`/backup compatibility 流程；null 或错误类型继续 pass-through。
- **验收标准**：native 返回值不被无条件丢弃；Java backup invocation、异常传播和 primitive/
  reference compatibility 规则保持不变；不创建真实 Provider 返回对象。
- **执行结果**：完成。证据：
  `tests/baseline/pattern-v6/p6-provider-java-result-20260802/T-49-R-49-java-result-transport-host.md`。
- **依赖/时间盒**：R-48；1 小时。

### I-49 [绿] 接入安全 DispatchResult 归一化

- **任务描述**：`installNativeDispatcher()` 仅接受 `DispatchResult` 实例，否则显式返回
  `DispatchResult.pass()`。
- **验收标准**：ProviderHooker Host 测试、production/comparison guards、完整 Host CTest
  与双 ABI 构建通过。
- **执行结果**：完成（Host/ABI/offline scope）。证据同 T-49。
- **依赖/时间盒**：T-49；1 小时。

### R-49 [重构] 保持真实 Provider result rewrite 关闭

- **任务描述**：结果传输桥只提供安全承载，不把任意 native 对象视为可改写结果；真实
  resolver/binding/Java object factory 仍需单独完成并通过 V-65 矩阵。
- **验收标准**：当前设备仍只报告 passthrough/restart；bit 17 清零，真实 mapping 保持
  `unsupported/not_observed`。
- **执行结果**：完成（可用设备/当前架构范围）。证据同 T-49。
- **依赖/时间盒**：I-49；1 小时。

### T-50 [红] 冻结 Provider before/after dispatch 生命周期

- **任务描述**：query/insert/document-ID 等依赖原始返回值的映射必须在 backup 正常返回后处理；
  before 继续支持安全短路，原方法异常不得被 after 吞掉或改写。
- **验收标准**：顺序固定为 before→backup→after；before/after 异常和不兼容结果 fail-open；
  原方法异常按原语义传播；默认 after 保持兼容。
- **执行结果**：完成。证据：
  `tests/baseline/pattern-v6/p6-provider-after-dispatch-20260802/T-50-R-50-after-dispatch-host.md`。
- **依赖/时间盒**：R-49；2 小时。

### I-50 [绿] 接入 native after-dispatch JNI seam

- **任务描述**：注册 `nativeAfterDispatch`，复用 frozen request extractor 验证 method/arguments，
  暂不读取或构造 Android 返回对象。
- **验收标准**：Java Host、Host `82/82`、production/comparison guards、NDK r27d/29 双 ABI
  和 ELF guard 全部通过。
- **执行结果**：完成（Host/ABI/offline scope）。证据同 T-50。
- **依赖/时间盒**：T-50；2 小时。

### R-50 [重构] 保持 after-dispatch 结果适配关闭

- **任务描述**：在 result-kind-specific adapter 和真实 binding/provenance 完成前，native after
  固定返回 pass-through，不从 URI 末段、display name、MIME 或返回数量猜测 route。
- **验收标准**：bit 17 清零；当前设备只验证 passthrough，真实 query/insert/reverse 仍为
  `unsupported/not_observed`。
- **执行结果**：完成（Host/ABI/device passthrough scope）；`0.1.38-dev` 证据
  `build/device-evidence/provider-lsplant-v1/20260802-142638` 显示双 Provider hook group 完整、
  `provider_bridge_errno=0`、admission active、bit 17 清零。真实 result rewrite 仍为
  `unsupported/not_observed`。
- **依赖/时间盒**：I-50；1 小时。

### T-51 [红] 冻结 Provider result-kind observation 合同

- **任务描述**：依据已冻结的方法描述符，分别识别 `File`、`String` document ID、`Cursor`、
  `Uri`、`ParcelFileDescriptor`、boxed count 和 `void` 的原始返回形状；不得从对象内容推断
  route、document ID 或 FD identity。
- **验收标准**：引用类型允许 null 并标记为 compatible-null；count 必须为非空 `Integer`；
  `void` 必须为 null；错误类型、未知 kind 和 JNI 异常均 fail-open，不改变原始返回值。
- **执行结果**：完成。证据：
  `tests/baseline/pattern-v6/p6-provider-result-observation-20260802/T-51-R-51-result-observation-host.md`。
- **依赖/时间盒**：R-50；2 小时。

### I-51 [绿] 接入 native after-dispatch 结果观察器

- **任务描述**：`nativeAfterDispatch` 先按 `ProviderJavaDispatchSpecV1.result` 验证原始对象，
  再复用 immutable request extractor；观察器不持有 JNI ref、不读取对象字段、不构造替代对象。
- **验收标准**：结果种类 Host 矩阵、ProviderHooker Java Host、MSVC Release `82/82`、
  production guard、NDK r27d/29 双 ABI和 ELF guard 全部通过。
- **执行结果**：完成（Host/ABI/device passthrough scope）。候选 `0.1.39-dev` 当前设备证据
  `build/device-evidence/provider-lsplant-v1/20260802-145017`：MediaProvider `2044`、
  ExternalStorageProvider `3`，两端 `provider_bridge_errno=0`、hook group 完整、admission
  active，bit 17 清零；采集日志无 Provider/PathGuard/JNI fault。真实 result rewrite 仍为
  `unsupported/not_observed`。
- **依赖/时间盒**：T-51；2 小时。

### R-51 [重构] 保持结果观察与结果适配分离

- **任务描述**：观察器只回答形状是否兼容及是否有值；`Cursor` 行、`Uri`/document ID
  内容、`File` 路径、PFD identity 和 count 语义留给各自 adapter，不因类型兼容启用 rewrite。
- **验收标准**：native after 固定返回 pass-through，bit 17 清零；真实 mapping/provenance 和
  V-65 结果矩阵继续为 `unsupported/not_observed`，候选包需当前设备回归。
- **执行结果**：完成（Host/ABI/device passthrough scope）。证据同 T-51；真实 result rewrite
  仍为 `unsupported/not_observed`。
- **依赖/时间盒**：I-51；1 小时。

### T-52 [红] 冻结 Provider resolver request/facts C ABI

- **任务描述**：把跨库 resolver 的 request 和 facts 显式定义为固定容量 C 结构；禁止把
  `std::string`、`std::vector`、`std::optional`、C++ enum 引用或 `ProviderRouteBindingV1*`
  作为 ABI 传给 Zygisk `APP_STL=none` 侧。
- **验收标准**：C17 和 C++ 均可解析 bridge API；request/facts 尺寸和关键字段 offset 固定；
  identifier/file path 使用 bounded byte arrays；snapshot/binding/reverse 只能以 numeric handle
  表达，未接 registry 时 fail-open/pass-through。
- **执行结果**：完成。证据：
  `tests/baseline/pattern-v6/p6-provider-c-abi-facts-20260802/T-52-R-52-c-abi-facts-host.md`。
- **依赖/时间盒**：R-51；2 小时。

### I-52 [绿] 接入 C ABI facts adapter

- **任务描述**：LSPlant bridge 在内部把 immutable Java request 转成
  `PathGuardLsplantMappingRequestV1`，resolver 返回 `PathGuardLsplantMappingFactsV1` 后再转换为
  既有 evaluator 所需 facts；非零 binding handle 在 registry 未完成前拒绝。
- **验收标准**：C 头解析测试、Provider LSPlant Host 测试、production guard、MSVC Release、
  NDK 29 LSPlant 双 ABI 与 ELF guard 全部通过；仍不启用真实 resolver。
- **执行结果**：完成（Host/ABI/device passthrough scope）。候选 `0.1.40-dev` 当前设备证据
  `build/device-evidence/provider-lsplant-v1/20260802-152004`：MediaProvider `2044`、
  ExternalStorageProvider `3`，两端 `provider_bridge_errno=0`、hook group 完整、admission
  active，bit 17 清零；采集日志无 Provider/PathGuard/JNI fault。真实 resolver 仍为
  `unsupported/not_observed`。证据同 T-52。
- **依赖/时间盒**：T-52；2 小时。

### R-52 [重构] 保持 resolver 数据面与真实 snapshot registry 分离

- **任务描述**：当前阶段只建立稳定 ABI 和 adapter；真实 immutable route snapshot registry、
  provenance reverse handle、trusted caller UID 和 Java object factory 留给后续任务。
- **验收标准**：Zygisk 仍调用 `configure_mapping(nullptr)`；native dispatcher 无 resolver 时 pass-through，
  resolver 返回 snapshot binding 时在 registry 未完成前 fail-open；bit 17 清零。
- **执行结果**：完成（Host/ABI/device passthrough scope）。候选 `0.1.40-dev` 当前设备证据
  `build/device-evidence/provider-lsplant-v1/20260802-152004` 已验证两端 hook group 完整、
  `provider_bridge_errno=0`、admission active 且 bit 17 清零。真实 snapshot binding 仍为
  `unsupported/not_observed`。证据同 T-52。
- **依赖/时间盒**：I-52；1 小时。

### T-53 [红] 冻结 immutable Provider route snapshot registry 合同

- **任务描述**：为 resolver 返回的 numeric generation/binding/reverse handles 建立进程内不可变
  registry；binding ID 与 reverse record ID 使用独立命名空间，禁止 callback 热路径访问
  daemon/store、获取全局锁或复制含 STL 的 route/provenance 对象。
- **验收标准**：snapshot generation、零/重复 ID、stale/unknown binding 和独立 reverse ID 均有
  Host 矩阵；任何不一致 fail-open/pass-through，不创建 Java 替代对象、不启用 bit 17。
- **执行结果**：完成（Host/ABI contract scope）。证据：
  `tests/baseline/pattern-v6/p6-provider-route-snapshot-20260802/T-53-R-53-route-snapshot-host.md`。
- **依赖/时间盒**：R-52；2 小时。

### I-53 [绿] 实现只读 generation/binding/reverse lookup

- **任务描述**：构造阶段复制、排序并验证 snapshot entries；运行期使用二分查找返回稳定 const
  pointer，按 `(snapshot_generation, binding_id, reverse_record_id)` 联合校验。
- **验收标准**：MSVC Release 专项及相邻 Provider tests、NDK 29 LSPlant 双 ABI、ELF guard 和
  production guard 全部通过；LSPlant Android 目标显式链接 registry 实现。
- **执行结果**：完成（Host/ABI contract scope）。证据同 T-53；production resolver 尚未连接。
- **依赖/时间盒**：T-53；2 小时。

### R-53 [重构] 保持 snapshot lookup 与生产发布分离

- **任务描述**：registry 只负责 immutable handle lookup；snapshot 编码/发布、Zygisk resolver、
  trusted caller UID、route/provenance 构建和 Java result factory 留给后续任务。
- **验收标准**：现有 C ABI 不变，`configure_mapping(nullptr)` 仍为生产默认路径，当前设备行为
  不变且 bit 17 清零。
- **执行结果**：完成（Host/ABI contract scope）。真实 snapshot 数据和 rewrite 仍为
  `unsupported/not_observed`。证据同 T-53。
- **依赖/时间盒**：I-53；1 小时。

### T-54～R-54 [生产] 接入真实 route snapshot 与 Java 结果工厂

- **任务描述**：从 companion 枚举已提交 provenance，按当前 policy/scope/rule/plan generation
  过滤并编码 C ABI snapshot；Zygisk resolver 使用 Binder caller UID 返回 numeric handles，bridge
  构造 File、document ID、Uri、Cursor、PFD 和 boxed count 的类型安全结果。
- **验收标准**：任何 generation、scope、identity 或 result type 不一致均 fail-open；callback 不访问
  daemon/store；bit 17 不变。
- **执行结果**：完成（Host/production implementation）。证据：
  `tests/baseline/pattern-v6/p6-provider-production-v1-20260802/T-54-T-58-provider-production-host.md`。

### T-55～R-55 [生产] 有界 live snapshot publication

- **任务描述**：Provider 后台线程按 provenance generation 拉取新快照；Zygisk 与 LSPlant bridge
  分别使用固定 256 reader slots、最多 8 个 retired snapshot 的 hazard publication，热路径无锁、
  无 I/O、无 route/provenance 对象分配；bridge 不引入 `PT_TLS`。
- **验收标准**：进程启动后新 route 不要求 Provider restart；发布失败保持旧 generation 或暂时
  fail-open；retire storage 不无界增长；双 ABI ELF guard 继续通过。
- **执行结果**：完成（Host/production implementation）；真机 generation 推进归 V-68。

### T-56～R-56 [生产] 持久化 Provider URI/document-ID 外部身份

- **任务描述**：provenance protocol v4 和 WAL format 3 增加 external bind 与完整
  FILE_HANDLE type/bytes；保留 WAL format 1/2 恢复兼容。insert 仅从 `_data` 或
  `relative_path + _display_name` 权威字段建立路径；首次
  openDocument/openFile 可由 PFD backing path 与 strong FILE_HANDLE/statx identity 自举
  URI/document ID。
- **验收标准**：禁止 URI 尾段、display name 单字段、MIME 或 count 猜 route；同 scope 外部身份
  冲突被拒绝；daemon 暂时不可用时 bounded queue 保持 fail-open。
- **执行结果**：完成（Host/production implementation）；协议、恢复和冲突测试通过。

### T-57～R-57 [生产] MediaStore Cursor 与 PFD identity

- **任务描述**：Cursor 按真实 `_data`/document-ID 逐行解析 binding，复制列类型并只改写匹配行；
  PFD 按 FILE_HANDLE type/bytes/volume/object type，或 `getFd + statx(AT_EMPTY_PATH)` 的
  volume/inode/btime/object type 比较，全部一致才承认 rewrite。
- **验收标准**：无 `_data`/document fact、projection 缺失、Cursor/JNI 异常、weak/stale FD identity
  均透传；directory query 不从 parent/name 推导 child route。
- **执行结果**：完成（Host/production implementation）；真实 Cursor/PFD 结果归 V-68。

### T-58～R-58 [生产] mutation、reverse 与恢复闭环

- **任务描述**：uniquely-bound item URI 支持 update/delete observation，实际 rename/unlink/rmdir
  继续由统一 path hook 事务和 provenance WAL 更新；ExternalStorageProvider File reverse 和
  MediaDocumentsProvider document ID 均消费同一 committed record；publisher 在 companion 恢复后
  重新置 runtime available。
- **验收标准**：collection/selection delete 无唯一 binding 时 fail-open；rename/delete 不产生第二套
  provenance；Provider restart 后重新加载 committed external identity；bit 17 仍由 V-68 决定。
- **执行结果**：完成（Host/production implementation）；真机 reverse/restart 矩阵归 V-68。

### V-68 [真机] 方案 B production composite matrix

- **任务描述**：安装最新 production 候选后，在 alioth 上执行真实 virtual insert/query/open/update/delete、
  ExternalStorageProvider File reverse、MediaDocuments document ID、PFD strong identity、live publication、
  Provider restart/recovery 与 fault injection。
- **验收标准**：操作结果、Cursor/document ID、实际 FD 和 reverse source 一致；daemon/store 窗口
  fail-open，恢复后无需 Provider restart；无 JNI/LSPlant fault。全部通过后才允许另行修改 bit 17。
- **执行结果**：当前设备支持范围完成；证据：
  `tests/baseline/pattern-v6/p6-provider-v68-20260802/V-68-current-device-production-boundary.json`。
  `0.1.44-dev` 的真实 LocalSend TXT/JPG 仅落 target、源残留为 0；双 Provider hook/admission
  稳定，bootstrap 明确报告 `strong_identity_unavailable` 且不生成 WAL。alioth 的 FUSE 与 backing
  plane 均无 `STATX_BTIME`，`name_to_handle_at` 返回 `ENOSYS`，因此 reverse/PFD/committed live
  publication/mutation-recovery 子矩阵按当前设备不满足记为 `unsupported/not_observed` 并跳过；
  bit 17 继续强制清零。

### T-59～R-59 [生产] Shared Target Namespace Projection

- **任务描述**：多 source 共享声明 target root 时，编译器为每个 canonical projection 生成
  `target/_pg/v1/ns_<SHA256-128-base32>`；Namespace 身份与 Rule 策略解耦，mount/Provider 同投影
  共享 ID。Provider snapshot 增加显式 static/provenance mode，static binding 按请求双向物化，
  不要求 strong identity、reverse record 或 provenance companion。
- **验收标准**：规则重排和 priority 变化不改变 Namespace；不同 source Namespace 不同；同一
  source 的 mount/Provider Namespace 相同；`_pg` 用户规则拒绝；unknown/unsafe path fail-open；
  static reverse 在无 FILE_HANDLE/BTIME 时通过；provenance 原矩阵保持独立。
- **执行结果**：Host/双 ABI 实现完成。MSVC Release CTest `84/84`、Clang UBSan static
  CTest `84/84` 通过，新增
  Namespace SHA-256/Base32、编译稳定性、跨执行域一致、snapshot static decode、无需 strong
  identity 的 reverse 和双向 materialization 测试；NDK r27d Zygisk 双 ABI、零 STL ELF 隔离及
  NDK 29 LSPlant 双 ABI/ELF guard 通过。当前项目未发布，不实现旧布局迁移、adopt/migrate CLI、
  Legacy Namespace、GC 或迁移 UI。

### V-69 [真机] Namespace Projection 当前设备验收

- **任务描述**：第一阶段安装 `0.1.45-dev`，重启后分别从 Download/localsend-source 与 Pictures 创建同名
  TXT/JPG，确认物理文件进入同一 target root 下不同 `ns_*`；验证 LocalSend、MediaStore/
  DocumentsProvider query/open、Provider restart、删除重建及卸载残留。
- **验收标准**：不依赖 FILE_HANDLE/STATX_BTIME；双源同名可共存，逻辑视图各自恢复；同
  Namespace collision 仍 reject；未知 Namespace 不投影；Provider/daemon 重启后静态恢复；无
  JNI/LSPlant fault。当前设备不支持的操作逐项记 `unsupported/not_observed` 后跳过。
- **执行结果**：第一阶段已完成（`0.1.45-dev`，当前设备）。LocalSend 实际接收的
  `test.txt`、`test.jpg`、两个同名 `test1.jpg` 均进入同一声明 target root 下的不同 Namespace：
  `ns_57xfxvj54rskidat5ak4krdeye` 对应 `Download/localsend-source`，
  `ns_vzn4kspwdxed2tgosk2o4z6bpu` 对应 `Pictures`；两个 `test1.jpg` 同时存在且各为
  125478 bytes，target 顶层无扁平残留。LocalSend mount namespace 将前者投影回
  `/storage/emulated/0/Download/localsend-source`，双 Provider 均 `action_total=2`、
  admission `active`、`provider_bridge_errno=0`，无过滤日志中的 FATAL/JNI/LSPlant fault。
  证据：`build/device-evidence/provider-namespace-v1/20260802-235239/`、
  `build/device-evidence/provider-lsplant-v1/20260802-235131/`、
  `tests/baseline/pattern-v6/p6-provider-namespace-v1-20260803/V-69-namespace-projection-device.md`。
  MediaStore/DocumentsProvider 探针已修正并复采，但 shell/root caller 不具备 LocalSend 的
  caller scope，观察到的是物理诊断视图，不能据此宣称应用逻辑 Cursor 已通过；Provider
  `0.1.46-dev` 复测得到相同双 Namespace/同名共存结果；随后 MediaProvider PID
  `4981→29994`、ExternalStorageProvider PID `9169→30195`，两端重新发布 active 状态，四个
  文件路径与 SHA-256 不变，Provider restart 静态恢复通过。精确删除两个 `test1.jpg` 后重新按
  两个逻辑 source 接收，新对象时间戳均为 `2026-08-03 12:03` 且回到原 Namespace，删除重建
  通过。LocalSend UID `10382` 下启动的 shell `content query/read` 在取得 Provider 前因缺少
  `ACCESS_CONTENT_PROVIDERS_EXTERNALLY` 被系统拒绝，不能构造真实 LocalSend ContentResolver
  caller；query/open 按当前设备不满足记为 `unsupported/not_observed` 并跳过。卸载模块并重启后，
  模块目录、daemon PID、全进程相关 mountinfo 和双 Provider PathGuard/LSPlant maps 残留均为 0，
  四个物理测试文件按不删除用户数据的契约保留。LocalSend 对第二个 `collision.txt` 在 Provider
  create 前自动改名为 `collision (1).txt`，exact collision 未构造，记为 `not_observed`；隔离的
  unknown Namespace fixture 在物理路径可见、在刷新后的 LocalSend 逻辑 source 返回 `ENOENT`，
  mount 投影子项通过并已清理。外部 Solid Explorer 删除活动物理 Namespace 后，存量 mount 出现
  `//deleted`，重启 LocalSend 后恢复；当前不宣称热恢复。V-69 当前设备可构造范围完成。当前设备不支持的
  strong identity 场景仍按
  `unsupported/not_observed` 跳过。

## 参考依据

项目内权威来源：

- `docs/08-pattern-redirect-design.md`
- `docs/adr/0001-zygote-inherited-policy-mmap.md` 至 `0015-bounded-selector-negation.md`
- `tests/baseline/host-tests.md`
- `tests/device/r1-safety-validation.md`
- `tests/device/rules/README.md`
- `refer/KernelSU`、`refer/Magisk`、`refer/MaterialCleaner-main`、`refer/zygisk_cleanerhooks_v1.9.2-release`、`refer/NoMount`

官方外部资料（实施时记录实际访问日期）：

- Android Scoped storage：<https://source.android.com/docs/core/storage/scoped>
- Android FUSE passthrough：<https://source.android.com/docs/core/storage/fuse-passthrough>
- Android MediaProvider module：<https://source.android.com/docs/core/media/media-provider>
- Android ContentProvider API：<https://developer.android.com/reference/android/content/ContentProvider>
- Android DocumentsProvider API：<https://developer.android.com/reference/android/provider/DocumentsProvider>
- Android Custom document provider：<https://developer.android.com/guide/topics/providers/create-document-provider>
- AOSP MediaProvider source：<https://android.googlesource.com/platform/packages/providers/MediaProvider/+/master/src/com/android/providers/media/MediaProvider.java>
- LSPlant official README：<https://github.com/LSPosed/LSPlant/blob/master/README.md>
- Linux `openat2(2)`：<https://man7.org/linux/man-pages/man2/openat2.2.html>
- Linux `fanotify(7)`：<https://man7.org/linux/man-pages/man7/fanotify.7.html>
- POSIX `fnmatch()`：<https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/functions/fnmatch.html>
- POSIX `pthread_atfork()`：<https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_atfork.html>
- Linux kernel RCU requirements：<https://docs.kernel.org/RCU/Design/Requirements/Requirements.html>

## 完成定义

本清单完成不等于“代码已写完”。只有同时满足以下条件，Pattern Redirect 才可判定完成：

1. format 2/policy v6、统一 Selector/Action/Decision、五执行域和 capability admission 在代码、测试、配置、状态和文档中一致；
2. C1～C6 在适用设备上端到端通过，能力不足场景准确报告 unsupported/fail-open；
3. 所有破坏性改动都有同场景 before/after 证据，`unexpected_regression=0`；
4. parser/reader/matcher/provenance 通过 unit/property/fuzz/sanitizer，并满足性能与资源预算；
5. ADR-0015、format 6、route provenance 和 CompleteVfs 投资门均有明确结论，不存在用实现抢跑未决架构的情况。
