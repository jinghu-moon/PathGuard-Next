# ADR-0015：有界 Selector 反选与剩余集合路由

状态：Accepted

日期：2026-07-29

## 背景

[ADR-0014](0014-glob-language-boundary.md) 冻结了 PathGuard Glob v1，并明确拒绝裸
`!pattern`。该结论解决了 matcher 语法、正向索引和声明顺序问题，但没有消除真实的集合差需求：

- deny：拒绝某个目录中的全部内容，但保留少量允许区域；
- redirect：把已分配给若干目标后的严格剩余集合路由到默认目标；
- observe/export：观察或导出一类文件，但忽略缓存、缩略图或临时目录；
- 多个动作共享同一个“选中集合减去例外集合”的选择条件。

反选不是一个单一概念。字符类补集、路径集合补集、动作排除和默认回退的作用域、执行时机及
安全含义不同。如果只给 pattern 加一个含义不完整的 `!`，实现很容易重新退化为全量扫描、
声明顺序覆盖或 deny/allow 混淆。

本 ADR 定义反选的分类，并冻结 PathGuard 首版采用的最小集合代数及其配置、IR、索引、预算、
冲突和诊断语义。它不改变 ADR-0014 的 Glob token 集合。

## 定义

### 作用域全集

反选必须先定义全集。对一个 `PathSelector`，全集不是设备上的所有路径，而是：

```text
U = 当前 IdentityKey
    ∩ 当前可信 attribution bucket（若 adapter 能提供）
    ∩ select.root 下的规范化非空相对路径
    ∩ select.type 指定的对象类型
```

其中最低可信边界是 `IdentityKey = (caller_uid, user_id)`。只有 adapter 能取得并验证 package
attribution 时，才在该 IdentityKey 下进入 package 子桶；shared UID 或 attribution 不可用时，
不得用 policy 包名反推运行时主体。root、identity、可信 attribution 或 object type 不同的规则
不共享全集，禁止跨 app、跨 user 或跨 storage root 计算补集。

### 有界集合差

令 `G(p)` 表示一个 PathGuard Glob v1 pattern 匹配的相对路径集合。带反选的 selector 定义为：

```text
Base      = U ∩ G(select.glob)
Excluded  = G(except[0]) ∪ G(except[1]) ∪ ... ∪ G(except[n-1])
Effective = Base - Excluded
```

等价的单路径判定为：

```text
Match(select, path) =
    InScope(path)
    AND MatchGlob(select.glob, path)
    AND NOT AnyMatch(select.except, path)
```

`except` 只能缩小显式 `Base`，不能脱离 base 构造全局补集。

### 剩余集合

若某个 root 下已经有集合 `A`、`B`，需要把剩余部分交给 `C`，数学语义是：

```text
C = U - (A ∪ B)
```

它可以有两种实现：

1. **默认路由**：低优先级 catch-all `**`，由更具体或更高优先级的 A/B 规则覆盖；
2. **严格分区**：`glob = "**"` 且 `except = [A, B]`，使 C 与 A/B 的有效语言显式不相交。

默认路由适合普通 redirect；严格分区适合 deny、冲突证明、反向映射或必须明确集合边界的规则。

## 反选分类

| 类别 | 形式 | 作用域 | 典型场景 | 决策 |
|---|---|---|---|---|
| 字符类补集 | `[!abc]`、`[^abc]` | 单个路径组件中的一个字符 | 非数字开头、排除少数字符 | 已由 Glob v1 支持 |
| 裸路径补集 | `!pattern` | 若无额外限定就是几乎整个路径空间 | 模仿 gitignore 的整体否定 | 不支持 |
| 有界 selector 差集 | `base - except[]` | 同一 scope/root/type/base | deny 白名单、严格剩余集合、忽略子树 | 建议首版采纳 |
| 动作局部排除 | action 命中后再 veto | 单条 action | 只让某个动作忽略一批路径 | 不单独建模，由 selector 差集覆盖 |
| 默认/回退路由 | 更低优先级 `**` | 同一动作竞争域 | A 给 a、B 给 b、剩余给 c | 使用现有 priority/specificity |
| 顺序反转 | 多个 `!` 按声明顺序开关 | 整个规则列表 | gitignore 式 include/exclude toggle | 不支持 |
| 一般布尔表达式 | `!(A && (B || C))` | 任意嵌套表达式 | 复杂策略语言 | 不支持 |

字符类补集不是 selector 反选：`[!0-9].txt` 只消费一个字符并检查 ASCII bitmap，不会产生
路径全集补集，也不会破坏候选索引。

## 应用场景

### Redirect：默认分配剩余文件

“蛋糕 A 给 a、B 给 b、剩余给 c”优先使用默认路由：

```toml
redirect_rules = [
    { select = { root = "Pictures", glob = "A/**" }, to = "Download/a", priority = 100 },
    { select = { root = "Pictures", glob = "B/**" }, to = "Download/b", priority = 100 },
    { select = { root = "Pictures", glob = "**" }, to = "Download/c", priority = 0 },
]
```

该方案不需要反选计算：A/B 先取得自己的集合，低优先级 `**` 接收其余路径。它保持配置简单，
但三个 selector 的原始匹配语言仍有重叠，结果依赖已经冻结的 priority/specificity 规则。

### Redirect：建立严格互斥分区

当编译器、反向展示或目标碰撞分析要求集合显式不相交时，使用有界反选：

```toml
redirect_rules = [
    { select = { root = "Pictures", glob = "A/**" }, to = "Download/a" },
    { select = { root = "Pictures", glob = "B/**" }, to = "Download/b" },
    { select = { root = "Pictures", glob = "**", except = ["A/**", "B/**"] }, to = "Download/c" },
]
```

第三条 selector 的有效语言是 `U - (A ∪ B)`，不依赖前两条规则的声明位置，也不引用其 RuleId。

### Deny：除允许区域外全部拒绝

deny precedence 高于 redirect，因此不能用低优先级 catch-all deny 表达白名单。必须从 deny 的
base 中减去允许区域：

```toml
deny_rules = [
    { select = { root = "Pictures", glob = "**", except = ["Allowed-A", "Allowed-A/**", "Allowed-B", "Allowed-B/**"], type = "any" }, enforcement = "provider" },
]
```

命中 `except` 只表示“该 selector 不命中”，不会产生 allow 决策，也不会覆盖其他 deny rule。
如果另一个 active deny selector 命中，同一操作仍然返回 `EACCES`。

### Observe/Export：忽略噪声路径

```toml
observe_rules = [
    { select = { root = "Download", glob = "**", except = [".cache/**", "**/*.tmp", "**/thumbnail-*"] } },
]
```

同一个带 `except` 的 selector 可以由 observe、export、deny 或 redirect 引用；排除语义不在各
action adapter 中重复实现。

### 目录本身与后代

`except` 使用与 base 完全相同的 Glob v1 组件语义。排除一个目录名不会隐式排除后代：

```text
except = ["private"]       # 只匹配 root 直接子项 private
except = ["private/**"]    # 匹配 private 下的非空后代，不匹配 private 自身
except = ["private", "private/**"]  # 同时排除目录实体及其后代
```

首版不增加隐式 subtree 标志，避免同一 pattern 在 include 与 except 中产生不同语义。

## 决策

### 决策门结论

P0/V-08 评审接受本 ADR，并要求 format 6 首版同时编码 except ref。接受理由是：deny 白名单和
严格互斥剩余分区无法由低优先级 catch-all 完整表达，而显式 `Base - Union(except)` 保持
action-neutral、正向索引和有界执行。当前 LocalSend 的普通 Pictures redirect 本身不要求
`except`，简单 redirect 仍优先使用现有 priority/specificity，不强迫所有规则承担反选复杂度。

V-08 Release 结构微基准以 4096 条固定路径、31 次采样验证 base 命中后扫描成本：单 selector
2 个真实排除的 P95 为 9～10 ns/路径，8 个最坏非命中排除为 61 ns/路径，16×8 退化候选为
976 ns/路径，64×8 极端 bucket 为 4010 ns/路径。该基准只证明有界扫描的增长趋势，不代表最终
NFA 或 Android Provider 延迟；T-25/T-30 仍必须用生产 matcher、transition 计数和设备数据建立
正确性与性能发布门。现有 8/256 except refs、64 candidate 和 4096 transition 上限维持不变。

### 1. 配置模型

`select.glob` 继续是必选单字符串；`select.except` 是可选字符串数组：

```toml
select = { root = "Pictures", glob = "**/*.{jpg,jpeg,png}", except = ["**/private/**", "**/thumbnail-*"], type = "file" }
```

约束如下：

- `glob` 必须存在，不能用只有 `except` 的 selector 表达隐式全集；
- `except` 每项都相对于同一个 `select.root`，继承同一 `select.type`、IdentityKey 和可信
  attribution bucket；
- `except = []` 编译失败，要求删除空字段，避免把未完成配置静默当成无反选；
- 每项使用 ADR-0014 的 Glob v1；宿主 brace 可在每项中展开，但不进入运行时 IR；
- `except` 内的声明顺序不影响语义；编译器按 canonical pattern 排序、去重；
- 不支持 leading `!`、尾项 `!`、多次反转或跨 rule 引用；字面量开头 `!` 继续写成 `\!`；
- `except` 是 selector 字段，不是 action 字段，因此 selector 去重和跨动作复用保持成立。

### 2. 匹配职责

Pattern Engine 负责计算 selector 的有效集合；ActionEvaluator 不重新解释 `except`：

```text
lookup positive candidates by base glob
  -> base does not match: NoMatch
  -> base matches: evaluate attached except patterns
       -> any except matches: selector omitted from MatchSet
       -> no except matches: selector added to MatchSet
  -> ActionEvaluator evaluates actions referencing matched selectors
```

普通运行时 API 把“被 except 排除”表现为该 selector 的 NoMatch，不产生 allow、deny 或 pass-through
动作。`pathguardctl explain` 可以输出 `ExcludedByPattern`、SelectorId 和 ExceptPatternId，但热路径
不为每次排除写审计日志。

### 3. 索引策略

候选索引只由 base `glob` 构建：

```text
IdentityKey -> attribution bucket -> root
            -> base literal/extension bucket -> candidate selectors
```

`except` 永远不注册为顶层候选，因此不会让 `** - A` 变成“每次操作扫描所有反选规则”。只有
base 已命中的 selector 才检查自己的 except range。except pattern 可以在 selector 内保存预计算
literal suffix/prefix 以快速拒绝，但首版不建立第二套全局负向索引。

多个 selector 引用相同 canonical except pattern 时可以共享 PatternTokenTable bytes；授权和作用域
仍来自 PackageTable→ActionTable 引用以及运行时 IdentityKey/可信 attribution，不能从共享 token
推断 package。

### 4. Specificity、优先级与动作语义

- specificity 只由 base `glob`、root 深度和 literal prefix 计算；`except` 不增加 specificity；
- `except` 不能用来把宽泛规则伪装成更具体规则并赢得同优先级竞争；
- priority、deny precedence 和 action admission 在 selector 有效匹配后按既有规则执行；
- 被一个 selector 排除不影响其他 selector，尤其不能形成隐式 allow-over-deny；
- 默认路由仍用低优先级 catch-all，不增加 `default`/`else` action kind；
- rename/link 的两个 operand 在同一个 MatcherSnapshot 下分别计算有效 selector，再由一次
  ActionEvaluator 调用决策。

### 5. 冲突与语言分析

冲突分析使用 selector 的有效语言，而不是只看 base：

```text
S1 = Base1 - Excluded1
S2 = Base2 - Excluded2
```

编译器应使用已有有限自动机/bitmap 能力证明交集为空、包含关系或目标 tail 不相交。首版允许
保守结果：无法证明两个互斥动作不相交时仍拒绝策略，不能因存在 `except` 就假设冲突已消除。

静态规则：

- base 与某个 except canonical 完全相同时，selector 必为空，编译失败；
- 若可证明 except 覆盖整个 base，返回 `EmptySelectorLanguage`；
- 若可证明 except 与 base 不相交，产生 `RedundantExcept` 编译诊断，但不改变 policy 语义；
- 重复 except canonical 去重，不因声明次数改变 RuleId 或匹配结果；
- 反向映射只考虑 effective selector；存在多个有效来源时仍按既有 ambiguity 规则处理。

### 6. IR 与 policy format 6

在 `PathSelector` 中增加连续 except pattern range，不增加 `NEGATE`/`NOT` PatternToken：

```cpp
struct PathSelector {
    PatternKind kind;
    StringId root;
    PatternId base_pattern;
    uint32_t first_except;
    uint16_t except_count;
    uint8_t object_type;
    uint16_t depth;
    uint32_t first_action;
    uint16_t action_count;
};

struct SelectorExceptRef {
    PatternId pattern;
};
```

format 6 的 SelectorTable 增加 `first_except/except_count`，并增加固定宽度
`SelectorExceptRefTable`。每个 ref 只能引用已经验证的 PatternTable row；PatternTable 再拥有连续
PatternTokenTable range。精确 row layout、PatternId 和引用校验由
[ADR-0016](0016-policy-format-v6.md) 唯一定义。

canonical SelectorId 输入包括：

```text
(root, base canonical tokens, sorted unique except canonical tokens, object_type)
```

`policy.bin` 不保存 leading `!`、声明顺序或集合表达式字符串。ADR-0016 已将本字段一次纳入
format 6/schema 3；后续改变反选语义必须升级 format/schema，不能用 reserved bytes 偷渡。

### 7. 编译顺序

宿主编译器按以下顺序处理：

```text
decode format 2
  -> validate root/type/base/except shape
  -> brace-expand base and each except source pattern
  -> compile all expanded patterns as Glob v1
  -> canonical sort/dedup except patterns
  -> validate budgets and empty/redundant relations
  -> build base candidate index metadata
  -> build selector/action IR
  -> run effective-language conflict analysis
  -> encode policy.bin
```

base brace expansion 会产生多个 selector/action，与 ADR-0014 一致；每个展开后的 selector 共享
同一组 canonical except patterns。except brace expansion 只扩展差集的 union 成员，不产生
selector 笛卡尔积。任一步失败都拒绝整份新策略并继续使用上一份有效 snapshot。

### 8. 预算

在 ADR-0014 和主设计现有预算上增加：

| 限制 | 首版值 |
|---|---:|
| 单 selector effective except pattern | 8 |
| 每 app SelectorExceptRef 总数 | 256 |

except pattern token 和 character class entries 计入已有的每 app 4096 token 总预算，不建立独立
可膨胀配额。base 与 except 的全部状态转移共同计入单次 matcher transition budget 4096；预算
耗尽返回 `BudgetExceeded` 并按既有 action failure mode fail-open。

ADR-0014 的单 source pattern brace 32 个结果/64 KiB 累计字节上限同样先应用于每个 except
source；展开和 canonical 去重后，还必须满足本 ADR 更严格的单 selector 8 个 effective except
上限。任何一层超限都拒绝整条 source rule，不截断。

这些是 reader ceiling，不由 policy header 请求扩容。P0 benchmark 可以下调；扩大任一上限必须
修改 limits profile、golden、reader 和本 ADR，不能只修改 UI。

### 9. 诊断与审计

编译期至少提供稳定诊断：

- `MissingBasePattern`；
- `EmptyExceptList`；
- `TooManyExceptPatterns`；
- `EmptySelectorLanguage`；
- `RedundantExcept`；
- `ExceptBudgetExceeded`；
- `LeadingPatternNegationUnsupported`。

运行时普通 exclude 命中不写审计事件，因为它是 NoMatch 的原因之一。以下入口可显示排除原因：

- `pathguardctl explain <path>`；
- debug/status 的限量采样；
- 编译期冲突诊断的反例路径。

解释输出必须区分 `NoBaseMatch` 与 `ExcludedByPattern`，但两者对 ActionEvaluator 都表示该 selector
未命中。不得把 exclude 命中记录为 deny、allow 或 capability failure。

## 与各动作的精确关系

| 动作 | base 命中且 except 未命中 | except 命中 | 其他规则 |
|---|---|---|---|
| deny | 当前规则产生 deny candidate | 当前规则不适用 | 其他 deny 仍可拒绝 |
| redirect | 当前规则产生 redirect candidate | 当前规则不适用 | 可由其他 redirect 接管或透传 |
| observe | 当前规则产生 observe event candidate | 当前规则不记录 | 其他 observe 独立判断 |
| export | 当前规则产生 export candidate | 当前规则不排队 | 其他 export 独立判断 |

`except` 永远不产生一个正向 allow action。若未来需要“某个 allow 显式覆盖某个 deny”，必须另立
ADR 定义授权模型和 precedence，不能借 selector 反选隐式实现。

## 符号 `!` 与配置可读性

反选能力和 `!` 字符不是同一个决策。本 ADR 首版只冻结显式 `select.except`：

- `[!abc]`/`[^abc]` 继续是 Glob v1 字符类补集；
- pattern 开头 `!pattern` 继续由 Glob parser 拒绝；
- 不把 `select.glob` 从 string 改成带位置语义的数组；
- 不支持“最后一个 `!pattern` 排除前面全部 pattern”的隐式引用；
- literal `!` 继续使用 `\!`。

若未来确有配置简写需求，可以另行评审 host-only sugar，并在编译期转换为相同的
`base_pattern + except refs`；运行时 IR 永远不增加 NOT token。没有真实使用数据前不为符号便利
扩大 source schema。

## 否决方案

### 裸 `!pattern`

否决。它没有显式 base，全集边界容易被误解，并且无法仅靠正向 literal/extension bucket 找到
候选。`!A` 也不能表达 `U - (A ∪ B)`，除非再引入 OR 或对前序规则的隐式引用。

### 只允许尾项 `!`

首版否决。它要求把 `select.glob` 从 string 改为 array，并让同一个数组同时表达 OR 和差集；
“尾项排除谁”还会引入位置约束。显式 `except` 具有相同执行效率，schema 和诊断更清楚。

### ActionRule 自有 exclude 列表

否决。排除改变的是选中集合而不是执行动作。放在 ActionRule 会让 deny、redirect、observe、
export 重复匹配和冲突逻辑，也会破坏同一 selector 跨动作复用。

### 新增 Allow action 覆盖 Deny

否决。它会改变 deny precedence 和安全模型，远超集合差需求。`except` 只使当前 selector 不命中，
不会授权访问。

### 只使用低优先级 catch-all

不足以覆盖全部需求。它适合 redirect 默认路由，但 catch-all deny 仍会因 deny precedence 阻断
白名单，且原始语言重叠可能妨碍严格反向映射和冲突证明。

### 一般布尔 selector AST

否决。AND/OR/NOT 任意嵌套会扩大 policy format、canonicalization、冲突证明和 fuzz 面。首版只
需要一个正向 base 减去有界 except union。

## 兼容与迁移

- 不含 `except` 的 format 2 selector 与 ADR-0014 完全等价；
- format 1 不自动生成 except，也不猜测 deny/redirect 的白名单意图；
- leading `!pattern` 继续给出稳定错误，并建议改写为显式 base + except；
- `select.glob` 保持 string，不引入 string/array 双类型；
- reader 遇到越界 except range、未知 ref flag、无 base、未排序/重复 canonical ref 或预算超限时
  拒绝新 policy，并继续使用上一份有效 snapshot；
- ADR-0014 仍拥有 Glob token 语法，本 ADR 只拥有 selector 集合差语义。

## 验证门槛

### 编译器

- 无 except 的 selector 与旧 golden 完全一致；
- 单个/多个 except、canonical sort/dedup、brace expansion 和稳定 SelectorId；
- 空 except、只有 except、同 base/except、超出 8/256 上限或 ADR-0014 的 brace 预算；
- 目录实体与目录后代必须按显式 pattern 分别匹配；
- leading/tail `!` 和 `glob = [...]` 必须给出稳定拒绝诊断；
- effective-language 交集、空集、冗余和目标 tail 冲突分析。

### Matcher 与 ActionEvaluator

- base miss 不检查 except；base hit 后按 canonical range 短路检查；
- except 命中时 selector 不进入 MatchSet，其他 selector 不受影响；
- deny except 不形成 allow，其他 deny 仍优先；
- redirect 默认 catch-all 与严格 except 分区得到预期且确定的目标；
- 0/1/multi selector 快路径与参考 evaluator 结果等价；
- except transition budget 与普通 token 使用同一个硬上限；
- rename/link 两个 operand 在同一 snapshot/generation 下计算。

### Property/Fuzz

- 对随机有限路径域验证 `Effective = Base - Union(Except)`；
- except 顺序置换和重复不改变 SelectorId/Decision；
- 增加 except 只能缩小有效集合，不能扩大集合；
- 删除 except 只能扩大或保持有效集合；
- 不产生回溯、无界扫描、越界 ref 或 compiler/runtime 语义差异。

### 真机

- redirect：A/B 分流、剩余到 C；
- deny：除两个允许目录外拒绝 Pictures；
- observe/export：排除 cache/tmp 后事件数量和目标正确；
- LocalSend/MediaProvider、app-path 和未来 FUSE adapter 对同一 selector 得到一致结果；
- capability 缺失、policy reload 和 Provider 重启不改变 except 的集合语义。

## 后果

- PathGuard 获得 action-neutral、可索引、可预算的反选能力；
- Glob v1 保持简单，不增加 NOT token、顺序覆盖或通用布尔表达式；
- deny 白名单和严格剩余分区可表达，但不会引入 allow-over-deny；
- selector/policy format 增加 except ref range，编译器和冲突分析需要理解有效语言；
- 热路径只对已命中的 base selector 检查至多 8 个 except，NoMatch 仍不产生逐操作日志；
- 默认 redirect 仍可用低优先级 `**` 表达，避免对简单场景强制使用反选。

## 依据

- [ADR-0011：Pattern 快照发布](0011-pattern-snapshot-publication.md)
- [ADR-0014：Glob v1 与宿主枚举展开](0014-glob-language-boundary.md)
- [Pattern redirect design](../08-pattern-redirect-design.md)
- [Git gitignore pattern format](https://git-scm.com/docs/gitignore#_pattern_format)
- [node-glob comments and negation](https://github.com/isaacs/node-glob#comments-and-negation)
