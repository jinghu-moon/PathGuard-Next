# ADR-0014：冻结 PathGuard Glob v1 与宿主枚举展开边界

状态：Accepted

日期：2026-07-29

## 背景

PathGuard 的 glob 是 deny、redirect、observe、export 共用的持久规则语言。语法一旦编译进
`policy.bin`，后续收紧会造成兼容性和安全语义变化，因此必须在 P0 前冻结核心集合、宿主语法糖
和永久排除项。

项目的实际需求是按文件名、后缀和目录分量匹配，同时保持组件边界、无回溯、有界候选集合和
确定性冲突分析。目标不是兼容某一个 shell，也不是实现正则表达式的另一种写法。

## 决策概览

语法分成两层：

1. **PathGuard Glob v1**：进入 Pattern IR/`policy.bin`，由所有运行时 adapter 统一匹配；
2. **宿主枚举展开**：只在规则编译器/pathguardctl 中处理，展开后生成普通 selector/action，
   运行时不知道该语法存在。

pattern 级排除不属于 glob。默认路由由 action precedence 表达；有界集合差的独立 selector
结构提案见 [ADR-0015](0015-bounded-selector-negation.md)。

## PathGuard Glob v1

### 路径和字符模型

- pattern 相对于 `select.root`，必须是规范化相对路径；拒绝前导/尾随 `/`、空组件、`.`、`..`
  和 NUL；
- `/` 是唯一组件分隔符，必须由 pattern 中的 `/` 显式匹配；
- 规则和动态 glob 输入使用有效 UTF-8；普通字面量按 UTF-8 字节精确比较，`*`、`?` 和 NFA
  状态只在 Unicode scalar value 边界推进，不拆分多字节字符；
- 不做 Unicode normalization、locale collation 或大小写折叠；
- runtime path 不是有效 UTF-8 时 glob 返回 `InvalidPathEncoding`/fail-open，并产生限速诊断，
  不与普通 NoMatch 混淆；字面量 mount 规则不受此限制；
- `.` 是普通字符，不采用 shell 的隐式 dotfile 例外。`*` 可以匹配组件开头的 `.`。

### 核心符号

| 语法 | 语义 |
|---|---|
| 普通字符 | 精确匹配自身，允许非 ASCII UTF-8 字面量 |
| `*` | 匹配当前组件内零个或多个 Unicode scalar value，不跨 `/` |
| `?` | 匹配当前组件内恰好一个 Unicode scalar value，不匹配 `/` |
| `**` | 只允许作为完整路径组件，匹配零个或多个完整目录组件 |
| `\x` | 转义后一个字符；末尾孤立 `\` 编译失败 |
| `[abc]` | 匹配一个属于 ASCII 集合的字符 |
| `[a-z]` | 匹配一个位于闭区间内的 ASCII 字符 |
| `[!abc]` | 匹配一个不属于集合的字符，且永不匹配 `/` |
| `[^abc]` | `[!abc]` 的 PathGuard 兼容别名；canonical IR 统一编码为 negated class |

`**` 必须占满组件：`**/foo`、`a/**/b`、`a/**` 和 bare `**` 合法；`ab**`、`***`、
`a/**b/c` 编译失败。`a/**` 匹配 `a` 下至少一个子项，不匹配目录 `a` 自身；bare `**`
匹配 root 下任意非空相对后代，同样不匹配 root 自身。

### 字符类约束

字符类编译为固定 128-bit ASCII bitmap 和一个 negated flag，匹配无回溯、无 locale 依赖：

- class 成员和 range endpoint 只能是 ASCII；非 ASCII class 编译失败；
- range 必须升序，例如 `[z-a]` 编译失败；
- `/` 不能出现在 class 中，negated class 也不能匹配 `/`；
- `]`、`-` 和 `\` 作为 class 字面量时必须转义；
- `!`/`^` 仅在 `[` 后第一个位置表示否定，在其他位置是普通 class 成员；
- 空 class、未闭合 class 和末尾转义编译失败；
- 不支持 POSIX named class、collating symbol 或 equivalence class，例如 `[[:alpha:]]`、
  `[[.ch.]]`、`[[=a=]]` 均编译失败。

ASCII-only 是安全边界，不表示非 ASCII 文件名无法匹配：它们仍可由字面量、`*` 和 `?` 匹配。

### 转义和保留语法

glob 层的 `\` 转义任意下一个字符；在 TOML basic string 中还要经过 TOML 转义，例如 glob
`\*` 的配置文本写作 `"\\*"`。TOML literal string 可以减少双重转义，但仍受 glob 解析器
规则约束。

pattern 开头未转义的 `!` 编译失败并提示使用 deny/action precedence；要匹配字面量 `!`，写
`\!`。识别到 `+(...)`、`@(...)`、`?(...)`、`*(...)`、`!(...)` 等 extglob 形态时编译失败，
需要匹配这些字面量时必须转义引导符号。

## 宿主枚举展开

format 2 的宿主规则编译器支持受限 `{a,b,c}` 作为配置便利语法，例如：

```toml
select = { root = "Pictures", glob = "**/*.{jpg,jpeg,png}" }
```

编译器在 glob parser 之前将其展开为三个普通 selector/action。Pattern IR、PatternTokenTable 和
运行时 matcher 不增加 BRACE token。

首版约束：

- alternative 必须是非空字面量片段，不允许 `/`、glob metacharacter 或另一个 brace；
- 允许多个非嵌套 brace group，按笛卡尔积展开；不允许嵌套；
- 不支持 `{1..10}`、`{a..z}`、步长或空 alternative；
- 单条 source rule 最多产生 32 个 expanded patterns；
- 单条 source rule 的全部 expanded UTF-8 bytes 合计最多 64 KiB；
- 展开后再执行路径长度、selector/action、token、退化 bucket 和冲突预算；
- 任一上限超出时拒绝整条规则，绝不截断或部分发布；
- expanded selector 按 canonical 内容去重；RuleId 由 parent RuleId 和 canonical expanded pattern
  派生，不依赖 alternative 声明顺序。

brace 是 source-language sugar，不是 Glob v1。未来运行时永远不能根据 brace 原文做匹配。

## 明确不支持

### Pattern 级否定

不支持 `!pattern`。没有显式 base 的否定 selector 无法使用正向 literal/extension bucket，容易
迫使每次操作扫描全部反选规则；同时它会把集合组合或 action precedence 塞回 pattern 字符串。
deny 是拒绝动作，不等于排除。有界集合差的显式 base + `select.except` 设计及其 Pattern Engine
职责已由 [ADR-0015](0015-bounded-selector-negation.md) 接受。

`[!abc]` 是单字符集合补集，不是 pattern 级否定，仍可编译为确定性 token。

### Extglob、正则和动态展开

永久排除 `+(...)`、`@(...)`、`?(...)`、`*(...)`、`!(...)`、正则、环境变量、命令替换和
tilde expansion。它们会扩大解析面、引入子模式组合或运行环境依赖，不符合确定性策略语言。

### `case_insensitive`

Glob v1 不增加 `case_insensitive`。大小写折叠涉及 ASCII/Unicode 范围、normalization、目标碰撞
和反向映射，不是一个无成本 bool。ASCII 大小写变体可显式写成 `[Ii][Mm][Gg]_*`；若真实规则
证明需要通用 casefold，必须以独立 selector flag、明确 Unicode 版本和新 ADR 引入。

## Pattern IR 与索引

- `CHAR_CLASS` token 引用 canonical CharacterClassTable 条目；条目包含 128-bit bitmap、negated
  flag 和必须为零的 reserved bytes；
- class token 匹配一个 scalar，不能跨组件；negated class 对非 ASCII scalar 为 true，但仍不
  匹配 `/`；
- specificity 顺序为 literal 高于 char class，高于 `?`，高于 `*`，高于 `**`；具体分值在 P0
  golden 中冻结；
- brace expansion 在 IR 之前完成，不产生 token；
- 含 class 的 pattern 只有在无需枚举 class 才能证明固定 literal anchor 时才进入对应 bucket；
  首版不为了 extension bucket 展开 class，不能制造另一条隐式组合爆炸路径；
- 交集/冲突分析以 bitmap 交集处理正向或 negated class，不回退正则引擎。

## 预算

除设计文档的 selector/token/candidate/transition 预算外，新增：

| 限制 | 首版值 |
|---|---:|
| CharacterClassTable bitmap | 固定 128 bit/entry |
| 单条 source rule brace 展开数 | 32 |
| 单条 source rule expanded bytes 总数 | 64 KiB |

CharacterClassTable 条目数不得大于 PatternTokenTable token 总数，并参与文件总大小校验。宿主
编译器验证 source rule 的展开预算和展开后的全局预算；reader 只验证展开后形成的
selector/action/token/class tables、引用关系和 reader 自身预算。policy 中不存在 source brace
原文或 expansion metadata。

## 否决方案

### 首版仍拒绝字符类

否决。Git/POSIX pathname patterns 都包含 bracket expression；ASCII bitmap 实现有固定空间和
确定性复杂度，不破坏 token/NFA 架构，推迟只会造成一次不必要的 format/token 扩展。

### 把 brace 做成运行时 token

否决。brace 是枚举预处理，不提供新的匹配能力；放进 runtime 会扩大 IR 和状态空间。CVE-
2026-14257 也说明结果数量之外还必须限制累计展开字节。

### 遇到 brace 上限时截断

否决。截断会静默丢失规则覆盖，对 deny 尤其危险。PathGuard 必须原子拒绝整条 source rule。

### 用 `case_insensitive` 替代字符类

否决。两者语义不同；字符类表达任意有限 ASCII 集合，而 casefold 是涉及 Unicode 和碰撞的
全局比较策略。

## 兼容和版本

- PathGuard Glob v1 属于 rules format 2 的首版语法，运行时编码由 policy format 6/schema 3
  [ADR-0016](0016-policy-format-v6.md) 冻结；
- format 1 迁移不猜测 glob，不受影响；
- 未知 token/class flag、非法 class、越界引用或超出 reader 预算必须拒绝新 policy；
- 后续增加新的 runtime token、casefold 或改变现有符号语义，必须新增 ADR 并升级 schema/format；
- 后续增加只会完整展开为现有 Glob v1 的宿主语法糖，也必须有明确预算和 golden，但不一定增加
  runtime format。

## 验证门槛

- `*`、`?`、四种位置的 `**` 和转义的组件边界 golden；
- UTF-8 scalar、无 normalization、dotfile 和 invalid UTF-8 路径行为；
- 正向/否定 class、`^` alias、ASCII range、转义、非 ASCII/非法 range/未闭合 class；
- class bitmap 的 canonical dedup、交集、specificity 和 reader 越界校验；
- brace 单组/多组展开、dedup、稳定 RuleId、32/64 KiB 边界、嵌套/空项/序列拒绝；
- brace 超限绝不产生部分 policy；
- leading `!`、extglob、正则、环境变量和命令替换拒绝；
- fuzz/property test 不产生回溯爆炸、无界展开或 matcher/compiler 语义差异。

## 依据

- [Git gitignore pattern format](https://git-scm.com/docs/gitignore#_pattern_format)
- [POSIX fnmatch](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/functions/fnmatch.html)
- [Bash brace expansion](https://www.gnu.org/software/bash/manual/html_node/Brace-Expansion.html)
- [rsync manpage](https://download.samba.org/pub/rsync/rsync.1)
- [node-glob comments and negation](https://github.com/isaacs/node-glob#comments-and-negation)
- [CVE-2026-14257](https://www.cve.org/CVERecord?id=CVE-2026-14257)
- [Pattern redirect design](../08-pattern-redirect-design.md)
