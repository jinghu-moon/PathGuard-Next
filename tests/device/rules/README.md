# Rules device tests

本目录记录 Android 编辑器保存方式、C ABI、端到端编译、发布和性能测试。

设备报告必须包含设备、API、ABI、Root、NDK、构建模式和输入摘要。

- `editor_save_modes.ps1`：编辑器保存方式与安全元数据。
- `control_plane_e2e.ps1`：daemon 编译/发布、冻结 binary mmap/index/plan、
  失败候选保留旧 generation。`zygisk_policy_probe.cpp` 只使用 header-only
  `policy_format.h`/`policy_index.h`，不得链接规则编译器。
