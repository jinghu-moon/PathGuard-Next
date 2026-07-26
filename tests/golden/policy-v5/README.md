# policy format v5 golden

`golden-vector-v5.txt` 是固定语义输入和期望字节的机器可读唯一来源。

- C++ `pathguard_binary_test` 从该文件构建 `PolicyDocument` 并逐字段验证。
- RF1 的 Rust/C++ vertical slice 必须读取同一文件。
- ADR-0002 只描述冻结契约和关键数值，不维护第二份完整十六进制向量。

除非 policy format ADR 明确变更，否则禁止直接覆盖 expected hex。
