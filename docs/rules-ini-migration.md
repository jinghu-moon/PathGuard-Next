# rules.ini 到 rules.toml 的一次性迁移

PathGuard 只在运行时读取 `config/rules.toml`。旧 `rules.ini` 不再被 daemon 或 CLI 解析，也不存在按内容猜测格式的兼容分支。

迁移时按应用节逐项改写：

```ini
[com.example.app]
users = 0
processes = *
redirect Download/Source -> PathGuard/Target
```

对应为：

```toml
format = 2

[apps."com.example.app"]
users = [0]
redirect_rules = [
    { select = { root = "Download", glob = "Source", type = "directory" }, to = "PathGuard/Target" },
]
```

旧格式中的 `{user}`、`{package}`、`isolate`、`allow`、`observe` 和 `export` 没有自动迁移。
必须先把占位符具体化，再将 deny/redirect 显式改写为 format 2 Selector/Action。需要 Provider
path-I/O 时使用 `provider = { enabled = true }` 表达 intent；这不代表设备能力已准入。
迁移后运行：

```text
pathguardctl validate config/rules.toml --host
pathguardctl compile config/rules.toml migrated-policy.bin
```

离线输出验证成功后再替换模块目录中的 `config/rules.toml`，由 daemon 负责活动 `policy.bin` 的唯一发布。
