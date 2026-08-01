# T-34～R-34 Provider contract Host 合同

## 范围

- Change scope: Provider contract adapter（方案 B）第一阶段
- Device behavior: 未改变
- Production bit 17: 保持 `unsupported`

## T-34 红测

`pathguard_provider_contract_test` 引用尚不存在的
`ObserveProviderContractPair`，MSVC Release 按预期编译失败：

```text
provider_contract_test.cpp: error C3861:
ObserveProviderContractPair: identifier not found
```

失败原因仅为 DocumentsProvider/MediaStore pair gate 尚未实现。

## I-34 绿测

新增 `core/include/pathguard/provider_contract.h`：

- probe format version 1；
- DocumentsProvider/MediaStore 类型；
- provider build ID 与 adapter profile ID；
- caller UID、query、create/insert、stable document ID、FD identity、rename/delete、reverse、
  restart 九项 check；
- observed/missing operation mask；
- 单 Provider 与 pair observation。

完整 pair 只在两个 observation 均 active 时 active。缺 profile、check failure、check missing、
operation missing 或非法 probe 均不设置 `provider_query_insert_mapping`。

验证：

```text
pathguard_provider_contract_test: Passed
```

## R-34 真机探针准备

新增独立 `providerContract` Android APK 模块和 runner：

- MediaStore：insert、open write、publish、query、open read、rename/move、delete；
- DocumentsProvider：用户授权 tree 下 create、query/document ID、open write/read、rename、delete；
- JSONL observations、metadata、status 和 Provider/APEX 环境归档；
- 所有测试对象使用唯一名字并在 finally 路径删除。

Host 构建：

```text
:providerContract:assembleDebug: BUILD SUCCESSFUL
PowerShell parser: pass
```

V-64 已随后在 alioth/Android 13 上完成公开 API 基线。该基线允许进入 T-35 Host profile 与映射
合同工作，但仍不能作为生产 adapter profile 或 bit 17 active 证据；详见
`V-64-provider-public-contract-alioth.json`。
