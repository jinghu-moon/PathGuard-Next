# RF0 Host 测试基线

> 记录日期：2026-07-26
>
> 配置：Release
>
> 生成器：Visual Studio 17 2022
>
> 编译器：MSVC 19.44.35209.0（工具集 14.44.35207）
>
> 目的：保护规则文件重构开始前已经存在的 10 个 Host CTest。

## 固定命令

```powershell
cmake -S . -B build -DPATHGUARD_BUILD_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

## 原始测试

1. `pathguard_core_smoke`
2. `pathguard_policy_test`
3. `pathguard_path_test`
4. `pathguard_validation_test`
5. `pathguard_binary_test`
6. `pathguard_mount_transaction_test`
7. `pathguard_mount_backend_test`
8. `pathguard_provider_path_mapper_test`
9. `pathguard_topology_test`
10. `pathguard_runtime_status_test`

2026-07-26 的 RF0 前置执行结果为 10/10 通过。

`pathguard_host_baseline_guard` 会读取 `expected-host-tests.txt`，要求：

- 使用 Release 配置；
- 当前 CTest 列表不少于原始 10 项；
- 上述 10 个名称全部存在；
- 独立执行上述 10 项并要求全部通过；
- 额外注入一个不存在的测试名称时，守卫自身必须失败。

RF0 新增测试是增量测试，不修改这份原始清单。
