# Rules golden contract

每个规则编译 golden 必须记录：

- 输入文件和 SHA-256；
- `format` / TOML 版本；
- compiler backend 和 parser 版本；
- 原始 byte begin/end；
- 原始 begin/end 行列；
- 稳定错误码、message key 和 related spans；
- 对成功样例记录 Canonical Policy 摘要与 PolicyBlob SHA-256。

默认用户诊断不得包含内部脱糖文本 `{ from = ..., to = ... }`。
只有显式 debug 产物可以放入单独目录，且不得被普通 golden renderer 使用。
