# toml-test fixture provenance

- Upstream：`toml-lang/toml-test`
- Local reference：`refer/toml-test-main`
- Version：`v2.2.0`
- License：MIT
- PathGuard TOML version：1.0
- Selection authority：`refer/toml-test-main/tests/files-toml-1.0.0`

RF0 只建立目录、来源和校验契约。RF1 才会使用显式
`toml-test copy -toml=1.0` 或 1.0 文件清单导入选中的原始 TOML fixture。

不得直接复制整个 upstream `tests/`，不得依赖 v2 默认 TOML 版本。
