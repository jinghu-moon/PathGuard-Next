# PathGuard-Next Hide 1.0-LKM 任务路线

## 0. 当前结论

> 当前 ABI 9/v8 设备闭环已通过：active regression 五场景全部 PASS，当前 boot admission=admitted，清理手动 binding 后 daemon 自动接管两条 Hide 规则。最终 `product_state=supported_scope_myron`，Hide 支持范围锁定为 Redmi K90 Pro Max（myron）。

### 当前最新身份

| 项目              | 值                                                           |
| ----------------- | ------------------------------------------------------------ |
| device            | `myron`                                                      |
| arch              | `aarch64`                                                    |
| boot_id           | `0a6c8490-8453-4bf9-af9f-be21ca47e684`                       |
| kernel            | `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`       |
| module_sha256     | `b95d03516e3c7ec8750e99709222b8b07993126ffef77531651fd1b10278a9d2` |
| pathguardd_sha256 | `36966e821cbf4c67c5aac9a7ac83f56af2b6569359736a182c060dbba247dadf` |

### 当前内核状态

| 字段           | 值    |
| -------------- | ----- |
| abi_version    | `9`   |
| state          | `2`   |
| lifecycle      | `2`   |
| last_error     | `0`   |
| generation     | `1`   |
| operation_mask | `0x0000000000000fff` |
| parent_inode   | `790265` |
| shadow_mode    | `0`   |

当前准确状态是：

> `module_live=true; admission=admitted; daemon-owned state=2/lifecycle=2; product_state=supported_scope_myron`

本轮设备证据来自当前 boot 的实时采样：模块加载成功且哈希匹配；active regression 使用 parent inode `790265`，五个场景均通过；固定设备 profile 自动准入后 daemon 自动重新安装并启用 Hide 规则，Target/Control 快速复验通过。

Host 收口证据：

```text
ctest --test-dir build-release -C Release --output-on-failure  -> 91/91
LKM SHA-256  -> b95d03516e3c7ec8750e99709222b8b07993126ffef77531651fd1b10278a9d2
ZIP SHA-256  -> BB0B52FD9FF50F7EF117E3E233647F58421BB397E827F8263681DCE7C0759101
```

当前 ZIP 为 `dist/pathguard-next-v0.5.0-universal-hide1-myron.zip`，配置已统一使用 `actions`；
最终 admission 证据为 `build/device-evidence/hide1-final-admission/20260926-114554/admission.json`。

### 主模块合并包：安装即用模式

主模块现在可以通过 `scripts/package.ps1 -IncludeHideLkm` 生成固定设备合并包：

```text
dist/pathguard-next-v0.5.0-universal-hide1-myron.zip
```

该包仍使用精确的 `myron / Android 16 / 6.12.23` LKM，但把
`pathguard_hide1.ko`、主模块 `pathguardd`、Zygisk/Provider 和 Hide profile
放在同一个 `pathguard_next` 模块中。包内的 `config/hide1_auto_admit` 只允许
daemon 在 profile、设备、fingerprint、kernel、KMI、LKM 哈希和 live control
device 全部匹配时，自动生成当前 boot 的内存 admission；不再要求用户手动导入
`admission.json`。普通 `pathguard-next-v0.5.0-universal.zip` 不含该标记，仍按
证据 admission 方式保持 fail-closed。

自动准入不会把产品范围扩展为通用支持，`product_state=supported_scope_myron`；
任何设备身份或模块哈希不匹配都会拒绝 Hide，deny/redirect 主模块仍可继续运行。

当前证据：

```text
build/device-evidence/hide1-regression-v7/20260925-165203/full-regression.json
build/device-evidence/hide1-admission-v7/20260925-165956/admission.json
build/device-evidence/hidelab-daemon-v7/20260925-171143/summary.json
```

### 历史验收证据索引（非当前 ABI 9 结论）

下表保留此前设备回归的可追溯路径；其 `PASS` 只表示当时对应产物和 boot，不能作为本轮 v7 包的准入证据。当前包必须重新执行 active regression、allowlist 和 admission 流程。

| 阶段 | 结果 | 证据 |
|---|---|---|
| P0 admission 撤权 | PASS | `build/device-evidence/hide1-latest/negative-admission/20260925-103251/negative-admission.json` |
| P1 Target 生命周期 | PASS | `build/device-evidence/hide1-latest/target-lifecycle/20260925-103620/target-lifecycle.json` |
| P1 规则热更新 | PASS | `build/device-evidence/hide1-latest/rules-hot-reload/20260925-103730/rules-hot-reload.json` |
| P1 deny/redirect/hide 组合 | PASS | `build/device-evidence/hide1-latest/combination/20260925-104127/full-regression.json` |
| P1 重启旧 admission 拒绝 | PASS | `build/device-evidence/hide1-latest/reboot-admission/20260925-105917/admission.json` 导入前状态为全零，boot_id 变更为 `34e0e807...` |
| P1 重启重新准入 | PASS | `build/device-evidence/hide1-latest/reboot-regression/20260925-105836/full-regression.json`、`reboot-admission/20260925-105917/admission.json` |
| P1 Target restart soak（5 轮） | PASS | `build/device-evidence/hide1-latest/reboot-soak/20260925-110202/target-lifecycle.json` |
| P2 身份变化 fail-closed | PASS | `build/device-evidence/hide1-latest/identity-fail-closed/20260925-110228/identity-fail-closed.json` |
| P2 CTest | PASS | `ctest --test-dir build/tests -C Release --output-on-failure`，91/91 |
| P2 NDK | PASS | `scripts/build-native.ps1 -Abi arm64-v8a` |
| P2 WSL LKM | PASS | `scripts/build-hide1-lkm-wsl.ps1 -DeviceSerial f3ba305a` |
| P2 ZIP | PASS | `dist/pathguard-next-v0.5.0-universal-hide1-myron.zip`，SHA-256 `BB0B52FD9FF50F7EF117E3E233647F58421BB397E827F8263681DCE7C0759101` |

本轮已将 teardown namespace 修复后的 LKM 安装到手机并完成上述回归；设备当前运行并准入的 hash 为 `b95d03516e3c7ec8750e99709222b8b07993126ffef77531651fd1b10278a9d2`。文档后部较早的“进行中/待验收”段落是实施过程记录，最终状态以本节索引和最终 admission 证据为准。

Target soak 的实际观测是每轮 PID 变化、旧 binding 不复用；Android 进程的 mount namespace inode 在本设备上保持 `4026535977` 不变。因此本轮完成的是 PID 生命周期撤销与重新绑定验证，不把“namespace inode 必须变化”误记为已发生事实。

更不能标记为：

> 通用 `Hide 1.0 active`

---

## 一、已完成

### 1. HideLab 测试能力

已完成 Target APK 和 Control APK，并建立结构化测试证据采集链路。

覆盖范围包括：

- Java File 和 Java NIO
- libc readdir
- 多缓冲区 `getdents64`
- `stat`、`lstat`、`statx`
- `access`、`faccessat`、`faccessat2`
- `open`、`openat`、`openat2`、`O_PATH`
- 子路径和相对路径
- alias
- cache cold/warm
- 20 线程并发
- 1000 轮可靠性测试
- `create`、`mkdir`、`mknod`、`symlink`、`link`
- `unlink`、`rmdir`、`truncate`
- `rename` 源端和目标端
- 已打开目录 FD
- Root Oracle 文件系统完整性验证
- mountinfo 前后对比

主要实现：

```text
tests/device/hide/run_hidelab_baseline.ps1
tests/device/hide/run_hide1_full_regression.ps1
tests/device/hide/hide_vfs_probe.cpp
tests/device/hide/app-probe
```

完成标准已经达到：

- 自动执行
- 结构化 JSONL
- 场景汇总
- Target/Control 对照
- Root Oracle 验证

### 2. LKM Hide 数据面

已完成以下 operation：

```text
lookup
atomic_open
iterate_shared/readdir
create
mkdir
mknod
symlink
unlink
rmdir
link
rename
d_revalidate
```

已完成的数据面机制包括：

- `i_op`、`f_op`、`d_op` shadow
- per-object metadata
- callback active 计数
- RCU、SRCU、Tasks-RCU 同步
- stale dentry 处理
- positive dentry 失效
- UID、PID、mount namespace 绑定
- generation 约束
- 安装失败回滚
- `STOP_NEW`、`RESTORE`、`DRAIN`、`FREE` 生命周期
- `DISABLE`、`CLEAR`
- mutation 先于真实文件系统修改返回隐藏语义
- rename 源端和目标端双重检查
- link 源端和目标端检查
- 已打开 FD 生命周期保护
- unload 前恢复 operation table

主要实现：

```text
experimental/hide-vfs/pathguard_hide1.c
experimental/hide-vfs/pathguard_hide1_uapi.h
experimental/hide-vfs/hide1_control.c
```

### 3. 固定设备 LKM 构建和加载链路

已完成：

- Android 16 / Linux 6.12 DDK 构建
- Android Clang r536225 构建
- pahole 1.30 接入
- `.BTF` 和 `.BTF.base` 生成
- SukiSU Ultra 加载
- fixed-device profile 校验
- 自动加载时强制 `shadow_mode=0`
- 模块文件哈希校验
- 设备、架构、fingerprint、kernel、KMI 校验

最新模块已在设备上成功加载：

```text
pathguard_hide1 Live
/dev/pathguard_hide1 存在
shadow_mode=0
abi_version=8
```

### 4. daemon Hide backend

已完成：

- 打开 `/dev/pathguard_hide1`
- 校验 ABI
- policy 到 LKM rule 转换
- package 到 target PID 解析
- UID、PID、mount namespace 采集
- `INSTALL`、`ENABLE`
- `DISABLE`、`CLEAR`
- target identity 变化检测
- generation 和 identity 校验
- 安装失败 rollback
- admission 失效时撤权
- idle tick 自动 reconcile

主要实现：

```text
daemon/src/hide1_backend.cpp
daemon/include/pathguard/hide1_backend.h
daemon/src/main.cpp
```

### 5. rules.toml Hide 控制面

已完成链路：

```text
rules.toml
→ schema v2
→ semantic validation
→ canonical hide rule
→ reconciler
→ Hide backend
→ LKM
```

已支持：

- hide rule 解析与验证
- 规则快照
- 新规则失败时恢复旧快照
- 删除 `hide_rules` 时撤销
- 非法规则不覆盖有效配置
- content generation
- 热加载
- deny、redirect、hide 独立后端
- 固定范围内仅允许一条 active hide rule

### 6. admission 生成和消费代码

已完成准入生成器，校验范围包括：

- device
- architecture
- fingerprint
- kernel release
- KMI
- 本地模块哈希
- 设备模块哈希
- undefined symbol 数量
- undefined symbol 集合摘要
- `/proc/kallsyms` 可解析性
- 模块 Live 状态
- state、lifecycle
- generation
- parent inode
- shadow mode
- full regression 身份
- boot ID
- 五个回归场景
- mountinfo unchanged

daemon 已具备以下 fail-closed 校验：

```text
admission == admitted
product_state == supported_scope_myron
boot ID 匹配
fingerprint 匹配
kernel 匹配
KMI 匹配
module SHA-256 匹配
module live
status_shadow_mode == 0
```

缺失、损坏、过期或身份不匹配时，daemon 应拒绝授权并撤销现有 Hide backend。

### 7. admission schema 根因修复

已识别并修复：

- `status_shadow_mode` 原来输出为 JSON 字符串 `"0"`
- daemon 要求 JSON 数字 `0`

修复后应输出：

```json
{
  "status_shadow_mode": 0
}
```

而不是：

```json
{
  "status_shadow_mode": "0"
}
```

涉及文件：

```text
tests/device/hide/admit_hide1.ps1
```

### 8. 旧模块完整回归

旧模块已经在旧 boot 下完成：

```text
baseline=PASS
cache-order=PASS
concurrency=PASS
reliability=PASS
mutation=PASS
mountinfo=unchanged
```

旧证据：

```text
build/device-evidence/hide1-final-active/
  20260924-103128/full-regression.json
```

但该证据只能证明旧模块、旧 boot 的数据面行为，不能用于最新模块的准入。

---

## 二、实施过程记录（已完成）

### 1. 最新 BTF 模块只读门禁

最新模块已经成功加载，但进入 Active 回归前，还需要完成以下只读检查。

#### 1.1 核对 lifecycle 语义

当前状态：

```text
state=0
lifecycle=1
last_error=-95
```

需要从源码确认：

- `lifecycle=1` 是否为正常 loaded/inactive 状态
- `last_error=-95` 是否只是未安装规则时的预期 `EOPNOTSUPP`
- 是否存在未完成 teardown 或 `STOP_NEW` 状态

未经确认前，不应直接执行 `ENABLE`。

#### 1.2 检查 admission 残留

需要确认：

```text
/data/adb/modules/pathguard_hide1_lab/run/admission.json
```

当前究竟是：

- 不存在
- 旧 boot admission
- 旧模块 admission
- 损坏 admission
- 其他残留文件

若存在旧 admission，必须证明 daemon 已按设计拒绝，不能沿用旧授权。

#### 1.3 核对 daemon 启动身份

需要确认实际 cmdline 类似：

```text
/data/adb/modules/pathguard_hide1_lab/bin/pathguardd \
  --module-dir /data/adb/modules/pathguard_hide1_lab
```

目的是排除：

- 设备中运行了其他 `pathguardd`
- daemon 使用了错误 `module-dir`
- 旧进程未退出
- 测试连接到了错误 daemon

#### 1.4 计算新模块 symbol 身份

需要从新 `.ko` 重新计算：

- `undefined_symbol_count`
- `undefined_symbols_sha256`

并验证所有 undefined symbols 均能在设备：

```text
/proc/kallsyms
```

中解析。

旧 allowlist 的：

```text
undefined_symbol_count=78
undefined_symbols_sha256=26c161cd...
```

不能直接假设仍适用于新 BTF 模块。

### 2. 新模块 Active 回归准备

需要准备：

- Target APK 正常运行
- Control APK 正常运行
- 获取 target UID
- 获取 target PID
- 获取 mount namespace inode
- 建立新 fixture
- 确认 parent 目录存在
- 记录 mountinfo 基线
- 确认 Root Oracle 初始状态
- 使用新的 evidence 输出目录

建议新证据目录：

```text
build/device-evidence/hide1-btf-final-active/<run_id>/
```

---

## 三、验收拆解记录（已完成）

以下条目保留实施时的验收标准；当前均已由本节顶部证据索引覆盖。

### 已完成 P0：最新模块完整 Active 回归

只读门禁通过后，需要执行手动部署：

```text
INSTALL
→ 核对 binding
→ ENABLE
→ 核对 Active 状态
→ 完整 HideLab 回归
```

目标内核状态：

```text
state=2
lifecycle=2
last_error=0
operation_mask=0xfff
generation=1
shadow_mode=0
parent_inode=<当前 fixture 实际 inode>
```

必须重新通过：

- baseline
- cache-order
- concurrency
- reliability
- mutation
- mountinfo unchanged
- Target 隐藏
- Control 可见
- Root Oracle 不变

输出必须是新的 `full-regression.json`，不能修改旧证据冒充新模块结果。

### 待当前 ABI 9 回归：更新固定设备 allowlist

只有新模块 Active 回归通过后，才更新：

```text
tests/device/hide/hide1_device_kmi_allowlist.json
```

需要更新：

- `module_sha256`
- `undefined_symbol_count`
- `undefined_symbols_sha256`

并继续保持：

```text
device=myron
arch=aarch64
kmi=android16-6.12
product_state=supported_scope_myron
```

### 待当前 ABI 9 回归：生成当前 boot 正式 admission

使用新 full regression 运行：

```text
tests/device/hide/admit_hide1.ps1
```

目标：

```text
admission=admitted
failures=[]
boot_id=0a6c8490-8453-4bf9-af9f-be21ca47e684
module_sha256=b95d03516e3c7ec8750e99709222b8b07993126ffef77531651fd1b10278a9d2
status_generation=1
status_parent_inode=<实际值>
status_shadow_mode=0
product_state=supported_scope_myron
```

还必须验证 JSON 类型：

```json
"status_shadow_mode": 0
```

### 待当前 ABI 9 回归：daemon 自动接管闭环

正式 admission 生成后：

```text
导入 admission
→ DISABLE
→ CLEAR
→ 确认不存在手动 binding
→ daemon 自动 reconcile
→ daemon 自动 INSTALL
→ daemon 自动 ENABLE
```

需要证明：

- Active 状态不是手动残留
- daemon 接受了当前 admission
- daemon 使用 `rules.toml` 部署规则
- daemon 重新解析了 target identity
- 内核状态与规则一致
- daemon 日志能区分“模块已加载”和“设备已准入”

### P0：负向 admission 与自动撤权（历史证据，待当前包复验）

逐项验证：

1. admission 文件删除
2. admission JSON 损坏
3. admission 为 rejected 或 `admitted=false`
4. boot ID 不匹配
5. module SHA-256 不匹配
6. fingerprint 不匹配
7. kernel release 不匹配
8. KMI 不匹配
9. 只有 boot-state，没有 admission
10. admission 运行中被破坏
11. admission 被替换为旧 boot 文件
12. admission 失效后不能沿用内存中的上一次授权

每个负向用例都应达到：

```text
daemon 拒绝 admission
→ DISABLE
→ CLEAR
→ state inactive
→ operation_mask=0
→ parent_inode=0
```

不能只检查日志，必须同时核对内核状态。

### P1：target 生命周期（历史证据，待当前包复验）

需要验证：

```text
target 启动
→ daemon 解析新 identity
→ INSTALL
→ ENABLE

target 退出
→ daemon 检测 identity 失效
→ DISABLE
→ CLEAR

target 再次启动
→ 使用新 PID/namespace
→ 重新 INSTALL
→ ENABLE
```

重点检查：

- 旧 PID 不复用
- 旧 namespace 不被误用
- 旧 generation 不被误认为当前部署
- target 不存在时保持 inactive
- target 出现后自动部署

### P1：Hide 规则热更新（Host 已通过，设备待当前包复验）

需要完成：

- hidden 切换为 hidden2
- 删除 `hide_rules`
- 恢复 `hide_rules`
- 非法 basename
- parent 不存在
- 多条 hide rule
- package 不存在
- package 后续出现
- 保存相同内容时不重复部署
- 保存新内容时 generation 正确变化
- 无需重启手机或模块

验收重点：

- 新规则成功才替换旧快照
- 非法规则不破坏旧有效部署
- 删除规则后自动恢复可见
- parent 不存在时 fail-closed
- 多条规则被明确拒绝

### P1：deny、redirect、hide 组合回归（Host 已通过，设备待当前包复验）

必须验证：

- deny 单独热更新
- redirect 单独热更新
- hide 单独热更新
- 三者同时存在
- hide 失败不回滚成功的 deny/redirect
- deny/redirect 失败时 hide 恢复正确快照
- Manager 保存与直接修改 `rules.toml` 行为一致
- content generation 和 deployment epoch 一致

这部分是 rules reconciler 的回归门禁，不属于 LKM 数据面测试。

### P1：重启与重新准入（历史证据，待当前包复验）

需要验证：

```text
同一 boot 内 admission 有效
→ 完整重启
→ boot ID 变化
→ 旧 admission 拒绝
→ Hide 保持 inactive
```

新 boot 必须重新执行：

```text
HideLab Active 回归
→ full-regression.json
→ admission
→ daemon 自动接管
```

### P2：OTA 与系统身份变化

需要覆盖：

- kernel release 改变
- fingerprint 改变
- KMI 改变
- module hash 改变
- slot 切换
- OTA 后旧 admission 残留
- OTA 后模块加载成功但未准入

预期均为 fail-closed。

### P2：离线工程回归（当前已完成）

最新代码最终必须重新执行：

- 91/91 CTest
- NDK arm64 构建
- daemon 构建
- hide1ctl 构建
- HideLab APK 构建
- LKM 构建
- `.BTF`、`.BTF.base` 检查
- module hash 检查
- daemon hash 检查
- 打包内容检查
- 脚本静态检查

历史上的 91/91 不能替代最新代码的最终执行结果。

### P2：文档和证据收口（当前已完成；设备证据待补）

需要更新：

```text
docs/11-hide-source-audit-log.md
docs/12-hide-vfs-execution-roadmap.md
```

并新增：

```text
docs/13-hide1-admission-and-revocation-trust-chain.md
```

文档 13 建议包含：

- 信任边界
- boot-state 与 admission 的区别
- full regression 如何绑定 boot
- module hash 和 symbol digest 的作用
- admission 生成流程
- daemon 消费流程
- 自动激活流程
- 自动撤权流程
- target identity 变化流程
- reboot/OTA 失效模型
- `product_state=supported_scope_myron` 的产品边界

证据目录需要排除：

- 旧 admission
- 临时设备日志
- 无法追溯来源的 JSON
- 手工修改后的证据
- 中间构建物
- 与当前 boot 不匹配的 regression

---

## 四、推荐执行顺序

### 阶段 A：只读门禁

- lifecycle 语义
- admission 残留
- daemon cmdline
- undefined symbols
- kallsyms 解析

### 阶段 B：新模块数据面证明

- 手动 `INSTALL`
- 手动 `ENABLE`
- 完整 Active 回归
- 生成新 `full-regression.json`

### 阶段 C：正式准入

- 更新 allowlist
- 生成 `admission.json`
- 验证 admission 内容和 JSON 类型

### 阶段 D：daemon 自动接管

- 导入 admission
- 清除手动 binding
- daemon 自动 `INSTALL/ENABLE`
- Target/Control/Root Oracle 快速复验

### 阶段 E：撤权与生命周期

- admission 负向测试
- target 退出/重启
- identity 重绑

### 阶段 F：规则控制面

- hide 热更新
- deny/redirect/hide 组合回归

### 阶段 G：系统生命周期

- reboot
- slot/OTA 身份变化
- 重新准入

### 阶段 H：工程收口

- 91/91 CTest
- NDK 和 LKM 构建
- 哈希检查
- 文档 11、12、13
- evidence 整理

---

## 五、里程碑定义

### M1：最新模块数据面通过

条件：

- 新模块完整 Active 回归通过
- 五个场景 PASS
- mountinfo unchanged
- 新 `full-regression.json` 有效

结论只能写：

> 最新 BTF LKM 在当前 myron boot 下通过数据面候选回归。

### M2：myron 正式准入闭环通过

条件：

- 新 `admission=admitted`
- daemon 自动 `INSTALL/ENABLE`
- 手动 binding 已清除
- 负向 admission 自动撤权通过
- target 生命周期通过

结论可以写：

> `Hide 1.0-LKM/myron admitted`，可以在该固定设备实验配置下启用。

### M3：固定设备工程验收完成

条件：

- hide 热更新通过
- deny/redirect 组合回归通过
- reboot 重新准入通过
- 最新 91/91 CTest 通过
- 文档和 evidence 完成

结论仍然必须保留：

```text
product_state=supported_scope_myron
```

### M4：通用 Hide 1.0

当前尚未进入该阶段。至少还需要解决：

- 多设备
- 多 KMI
- 多 UID
- 多 mount namespace
- 多父目录
- 多 basename
- 非固定 FUSE 拓扑
- OTA 兼容矩阵
- 更完整的长期稳定性和卸载安全性证明

---

## 六、路线概括

已完成：

- HideLab
- LKM 数据面
- daemon backend
- rules schema
- 固定设备加载门禁
- admission 代码
- 最新 BTF 模块 Active 回归
- 当前 boot admission
- daemon 自动接管

已完成：

- 负向 admission 自动撤权
- Target 生命周期与 5 轮 restart soak
- Hide 规则热更新
- deny/redirect/hide 组合回归
- 重启重新准入
- 当前身份字段失配 fail-closed
- 最新代码工程回归
- 文档和 evidence 收口

待扩展：

- 真实 OTA/slot 切换矩阵
- 多设备、多 KMI、多 namespace 通用化

尚未实现：

- 通用产品级 Hide 1.0
# 多规则重构进度（2026-09-25）

## 当前收口语义

- `deny`：Provider 层拒绝；不等同于完整文件系统保护。
- `redirect`：Provider 层重定向；不等同于完整文件系统保护。
- `hide`：固定设备上的 direct-VFS LKM 隐藏，当前能力矩阵只允许 myron/android16-6.12。
- 同一应用、同一路径的固定优先级为 `deny > hide > redirect > passthrough`。同 priority、同 selector、不同 redirect target 在编译期报 `PG-RULE-CONFLICT`；无法确定的冲突必须 fail-closed。

Hide 测试除多规则目录 fixture 外，还支持独立文件 fixture：Target 的 stat/open/readdir、Control 可见、Root Oracle 不变，并覆盖 rename/unlink/link mutation。产物 ZIP 必须携带 `build-manifest.json`，记录设备、fingerprint、kernel release、KMI、toolchain、模块哈希、daemon 哈希和 backend 能力。

当前 Hide 规则已从单条 binding 重构为统一 `actions` 中的规则集合：

```toml
actions = [
  { kind = "hide", parent = "/storage/emulated/0/Pictures/PathGuardHideLab/20260925-000308", basename = "hidden" },
  { kind = "hide", parent = "/storage/emulated/0/Pictures/PathGuardHideLab/20260925-000308", basename = "hidden-2" },
]
```

- schema/compiler 已支持多个 `hide` action，并保留 deny/redirect 的统一 action 解析。
- daemon 已按同一 Target identity 聚合完整 Hide RuleSet。
- LKM UAPI 已新增 `INSTALL_SET`，规则集合按一次事务安装，状态包含 `rule_count`。
- 多条规则均失败时，安装必须整体回滚；Target、admission、规则更新仍按整组 revoke/rebind。
- 2026-09-25 本地验证：C++ 构建通过；Hide backend、schema、control-plane 测试通过；WSL LKM 构建通过。
- 设备已安装 ABI 9 验证包并确认模块 Live、`INSTALL_SET` 返回 `rule_count=2`；`ENABLE` 在同一 parent 的重复 shadow 路径上发现并修复，修复版尚未重新安装设备。
