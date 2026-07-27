# toml-test fixture provenance

- Upstream：`toml-lang/toml-test`
- Local reference：`refer/toml-test-main`
- Version：`v2.2.0`
- License：MIT
- PathGuard TOML version：1.0
- Selection authority：`refer/toml-test-main/tests/files-toml-1.0.0`

RF1 使用 `files-toml-1.0.0` 过滤后导入 137 个原始 TOML fixture，范围为：

- TOML 1.0 的有效/无效字符串边界；
- 无效编码和 UTF-8 安全终止；
- 有效注释边界；
- 与数组 frame、逗号和闭合符有关的代表用例；
- UTF-8 BOM、LF 和 CRLF 代表用例。

`manifest.txt` 固定每个本地 fixture 的上游相对路径、SHA-256 和用途。
`files-toml-1.0.0` 是成员资格的唯一依据；`excluded-toml-1.1.txt`
记录直接扫描整个上游目录时会误纳入的 TOML 1.1-only 代表用例。

不得直接复制整个 upstream `tests/`，不得依赖 v2 默认 TOML 版本。
