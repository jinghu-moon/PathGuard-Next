# Legacy rules characterization

`manifest.tsv` 记录每个旧格式 fixture 的：

- `preserve`：新格式必须保持该安全/执行语义；
- `replace`：format 1 明确删除或替换的旧语法；
- `compile-gated`：parser 可识别，但当前 executor/admission 仍必须拒绝。

format 1 明确不支持：

- `{user}`、`{package}` 和其他路径占位符；
- 旧 `isolate -> ...` / `allow ...` 写法；
- 旧 `observe` / `export` 写法；
- `failure = closed`；
- 长期 `rules.ini` 运行时兼容分支。

测试会枚举目录，任何未登记的 `.ini` fixture 都会失败。
