# V-08 ADR-0015 反选决策门

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-08` |
| Before commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| After commit | N/A（架构决策，尚未修改生产代码） |
| Branch | `feature/pattern-redirect-v6` |
| Classification | `planned_break`（format 6/schema 首版新增 `select.except`） |
| Decision | ADR-0015 `Accepted` |
| Reviewer conclusion | 有界 selector 差集进入 format 6 首版；裸/尾项 `!pattern` 继续拒绝 |

## Before 基线与计划内变化

- 当前 format 1/v5 不支持 Glob 或 `select.except`；
- 当前 LocalSend 的 Pictures/Download literal redirect 和两个 deny 路径已由 V-03 建立基线；
- 本任务只改 ADR/设计状态，不修改 rules parser、policy reader、matcher 或设备模块；
- 计划内破坏是 format 2/format 6 首版新增 `select.except` 和 ExceptRefTable；format 1/v5 最终按
  V-09/V-15 计划一次性拒绝，不做兼容迁移猜测；
- 核心 C1～C5 当前实际行为保持 unchanged。

## 真实规则样本

| 场景 | 推荐表达 | 是否必须用 except | 结论 |
| --- | --- | --- | --- |
| LocalSend 将 Pictures 图片重定向到统一目录 | 正向 glob + redirect | 否 | 简单正向规则保持 KISS，不因能力存在强制使用反选 |
| A 给 a、B 给 b、剩余给 c | A/B 高优先级，低优先级 `**` 给 c | 通常否 | 普通 redirect 用既有 precedence 即可 |
| 上述分区必须对 provenance/冲突证明显式互斥 | C 使用 `glob="**"`、`except=["A/**","B/**"]` | 是 | `Base - Union(except)` 提供声明顺序无关的严格分区 |
| Pictures 除 Allowed-A/Allowed-B 外全部 deny | deny base `**` 减去允许区实体与后代 | 是 | deny precedence 使低优先级 catch-all 无法表达白名单 |
| Observe/Export 忽略 cache/tmp | 正向 base 减去 `.cache/**`、`**/*.tmp` | 是 | selector 级复用优于每个 action 自建排除 |

真实需求证明 `except` 不是纯配置糖，但适用面有界；不新增 allow action、一般布尔 AST 或
gitignore 式顺序反转。

## 身份边界修正

ADR 原文的 `ScopeKey=(caller_uid,user_id,subject_package)` 与当前设计冲突，因为 Provider/shared UID
不一定能取得可信 package attribution。已修正为：

```text
IdentityKey = (caller_uid, user_id)
U = IdentityKey ∩ optional trusted attribution bucket ∩ root ∩ object type
```

package 只有在 adapter 验证 attribution 后才参与子桶；禁止从 policy 包名反推运行时主体。

## 候选退化与微基准

### 方法

- Release / MSVC 19.44.35209.0 / x64；
- 固定 4096 条 Pictures 风格路径，31 次计时采样；
- 比较 scope miss、1 个 base、1×2、1×8、16×8 和 64×8；
- `except` 只在 base 命中后扫描，热路径不分配；
- 源码：`build/pattern-v6-v08-except-bench/main.cpp`；
- source SHA-256：`79D35944EAEDB5D2CE71AA4A79AE4DEFAA9F153321079292FEE01B3150C4FDDA`；
- executable SHA-256：`27C340D7CDA20A86A2065987F6D6470139B9CBFFAEA62EAF88ABE0822D15CE5A`。

该 harness 只模拟 base 命中后的有界 predicate 扫描，不实现生产 Glob NFA。因此数值用于观察
候选数×except 数的放大趋势，不能作为最终发布延迟或 T-30 性能阈值。

### 结果

| Case | Candidates | Except/selector | P50 ns/path | P95 ns/path | P99 ns/path |
| --- | ---: | ---: | ---: | ---: | ---: |
| scope miss | 0 | 0 | 0 | 0 | 0 |
| one base, no except | 1 | 0 | 2 | 2 | 2 |
| one base, two non-hit except | 1 | 2 | 10 | 10 | 10 |
| one base, eight non-hit except | 1 | 8 | 61 | 61 | 64 |
| LocalSend Pictures/Nagram+Screenshots 样本 | 1 | 2 | 10 | 10 | 10 |
| deny except A/B 样本 | 1 | 2 | 9 | 9 | 9 |
| general bucket upper bound sample | 16 | 8 | 967 | 976 | 979 |
| full candidate bucket extreme | 64 | 8 | 3886 | 4010 | 4051 |

结果支持“只在 base 命中后、最多 8 个 except”的结构，但 64×8 已显示明显线性放大。因此接受
不等于放宽预算：退化 pattern 16/root、32/app，candidate 64、except 8/selector、256/app 和
transition 4096 必须同时生效；1000 条无索引 pattern 必须编译期拒绝，不能进入运行时扫描。

## 冻结结果

| 项目 | 决策 |
| --- | --- |
| 配置 | `select.glob` 保持必选 string；`select.except` 为可选 string array |
| 语义 | `Effective = Base - Union(Except)`；except 命中只使当前 selector NoMatch |
| Pattern IR | 不增加 NOT/NEGATE token；except 复用普通 PatternProgram |
| Canonical form | `(root, base tokens, sorted unique except tokens, object_type)` |
| Format 6 | SelectorTable 冻结 `first_except/except_count`，新增固定宽度 ExceptRefTable |
| 预算 | 8/selector、256 refs/app；token/class/transition 计入共享预算 |
| 索引 | 只用正向 base 建候选；except 不注册全局负向 bucket |
| 冲突 | 使用 effective language；无法证明不相交时保守拒绝 |
| 符号 | `[!abc]` 是字符类补集；裸/尾项 `!pattern`、顺序反转继续拒绝 |

## After 对比

- 计划内变化：ADR-0015 从 Proposed 变为 Accepted，主设计删除条件式/占位表述；
- 计划内变化：format 6 的 V-09 决策必须包含 ExceptRefTable，不允许以后用 reserved bytes 偷渡；
- 非预期副作用：无。未修改生产代码、当前规则、policy、模块或设备状态；
- C1～C5：unchanged，沿用 V-03/V-04 基线；
- Host 回归：复用 V-02 Release 构建执行 CTest，`52/52` 通过，0 failed，总耗时 10.54 秒；
- 性能风险：已识别并转交 T-25/T-30 的生产 matcher 和 CI gate，不把结构 benchmark 当作通过。

## 验收结论

- ADR-0015 已明确 Accepted；
- 已用真实规则说明必要场景与不必要场景；
- 已修正旧 ScopeKey，使其服从 UID/user 最低可信边界和可信 package attribution；
- 已冻结 except 预算、canonical form、冲突语义和 format 6 table；
- 已覆盖 1/16/64 candidate 与 2/8 except 的结构微基准；
- 已完成前后对比，核心行为无非预期变化；
- V-08 判定 `complete`。
