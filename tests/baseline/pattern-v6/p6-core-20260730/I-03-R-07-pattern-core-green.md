# I-03～R-07 Pattern core P0 绿测与重构

- Change ID: `p6-core-20260730`
- Tasks: `I-03/R-03`, `I-04/R-04`, `I-05/R-05`, `I-06/R-06`, `I-07/R-07`
- Before commit: `0d03f63d198ac212f1b72642ce2208b6aaa44a71`

## 实现结果

- `schema_v2` 分离 TOML adapter、字段校验和 Canonical IR 构建；format 1 由 v2 parser 以
  `PG-FORMAT-UNSUPPORTED` 拒绝。
- deny/redirect 共用 `SelectorInputV2`；Provider intent、priority、preserve、collision 和
  enforcement 均有严格枚举校验。
- Glob v1 支持 literal、`*`、`?`、完整组件 `**`、escape 和 128-bit ASCII class；
  `[^...]` canonical 为 `[!...]` 等价 bitmap。
- matcher 使用调用方 `PatternMatchScratch`，无 regex、递归或热路径 heap allocation；非法
  runtime UTF-8 与 transition budget 分别返回结构化结果。
- brace 只在宿主编译期展开，多组按笛卡尔积生成；嵌套、range、空项、slash、metachar 和
  32/64 KiB 超限均原子失败，不产生 BRACE runtime token。
- 第一部分 tokenizer/matcher fuzz target 已改为调用生产 Pattern API，不再是摘要占位实现。

## 验证

Release rules/Pattern 专项 `41/41` 通过，其中 P0 新增契约 `4/4` 通过。旧 parser、semantic、
conflict、v5 pipeline、CLI integration 与 hot reload 均保持通过。

## 设计原则

- KISS：Glob 只实现 ADR-0014 冻结语法，不引入 regex/extglob/casefold。
- DRY：UTF-8、token、class、brace 与 limits 只有一套生产实现。
- SOLID：规则 schema 不依赖 Android/Zygisk；matcher 不解释 action。
