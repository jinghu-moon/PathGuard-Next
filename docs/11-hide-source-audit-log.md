# Hide 1.0 源码与 ABI 审计日志

本文件按项目逐轮记录 Hide 1.0 研发所需的源码、构建输入和可验证结论。
每轮只记录已实际读取的本地内容或已核验的公开资料；“参考”不等于“可用于
目标设备构建”。

## 记录规则

- 先记录项目版本、提交和关键文件，再分析用途。
- 明确区分源码、构建输入、设备预编译产物和运行时证据。
- 任何缺少精确 KMI、签名、CFI 或行为回归证据的项目，不能标记为可加载。
- 不改写 vermagic、CRC、签名或 KMI 校验来制造通过结果。

## 轮次 8：Xiaomi `Xiaomi_Kernel_OpenSource`（`bsp-prague-w-oss`）

### 来源与版本

| 项目 | 本地目录 | 分支 / 提交 |
|---|---|---|
| Xiaomi Kernel Open Source | `refer/hide-refer/Xiaomi_Kernel_OpenSource` | `bsp-prague-w-oss` / `bcbffc7544fdaebdde01e3be5607b4ddb0b71043` |
| 提交标题 | `Kernel: Xiaomi kernel changes for Redmi K90 Max Android W` | 2026-06-10，作者/提交者 `xiaoqian5 <xiaoqian5@xiaomi.com>` |

该仓库只有一个 grafted commit（`git rev-list --parents -n 1 HEAD` 没有可用父提交），
因此不能从本地历史还原 Xiaomi 相对于上游 common kernel 的完整差异序列。提交说明声称
面向“Redmi K90 Max Android W”，并注明补丁基于 MediaTek tag
`t-alps-release-b0.mp1.rc-v10.25`，配置名为 `mgk_64_k612_defconfig`。

### 内核版本和设备匹配核验

- 根目录 `Makefile` 明确为 `VERSION = 6`、`PATCHLEVEL = 12`、`SUBLEVEL = 38`，即
  Linux `6.12.38`，不是当前设备已确认的 `6.12.23-android16-5-g16e473de48a3-...`。
- `build.config.constants` 为 `BRANCH=android16-6.12`、`KMI_GENERATION=5`，并固定
  `CLANG_VERSION=r536225`、`RUSTC_VERSION=1.82.0`。这些是该源码快照的 GKI 构建输入，
  不能推导目标 ROM 的厂商 commit、工具链补丁或签名配置。
- `arch/arm64/configs/` 中只有 `gki_defconfig`、`defconfig`、`crashdump_defconfig`
  等公共配置；提交说明所称的 `mgk_64_k612_defconfig` 实际不存在。
- 源码和设备树包含大量 MediaTek 子系统（`drivers/*/mediatek`、
  `arch/arm64/boot/dts/mediatek`），同时保留通用 QCOM 目录；没有 `myron`、
  `SM8850`、`25102RKBEC` 或 `prague` 的专用源码/设备树路径。由此只能判定为“真实的
  Xiaomi Android W kernel source，可能与 Redmi K90 Max 产品线有关”，不能判定为当前
  Redmi K90 Pro Max / `myron` / SM8850 的精确源码。

这解决了此前的设备识别歧义：提交标题中的 **K90 Max** 不能直接等同于用户设备所确认的
**K90 Pro Max（myron、SM8850）**；SoC、产品代号和 release hash 必须逐项匹配。

### 构建与 KMI 输入

- `BUILD.bazel` 采用 Android Kleaf 的 `kernel_build`、`kernel_abi`、
  `kernel_modules_install` 等规则；`aarch64_additional_kmi_symbol_lists` 同时注册
  `mtk`、`qcom`、`xiaomi` 等厂商列表。这说明该树可作为研究 GKI/KMI 构建组织的样本，
  但这些列表是 ABI 允许导出的集合，不是目标设备的 `Module.symvers` CRC 数据。
- `gki/aarch64/symbols/xiaomi` 约 522 行，内容主要是 Xiaomi vendor 模块需要的导出符号
  （tracepoint、内存/存储/网络接口等）；`symbols/mtk`、`symbols/qcom` 也独立存在。
  符号白名单不能证明符号地址、CRC、CFI 类型或厂商实现与目标 boot 完全一致。
- `arch/arm64/configs/gki_defconfig` 开启 `CONFIG_KALLSYMS_ALL`、`CONFIG_KPROBES`、
  `CONFIG_CFI_CLANG`、`CONFIG_MODVERSIONS`、`CONFIG_MODULE_SIG`，并设置
  `CONFIG_LOCALVERSION="-4k"`；它是公共 GKI 配置，不是 Xiaomi 目标构建的最终合并配置。
- `gki/aarch64/abi.stg.allowed_breaks` 是 ABI 冻结后的允许变更清单（包含结构体变更及
  tracepoint 符号移除），可用于理解 Kleaf ABI 检查，但不能作为目标设备 ABI 证明。

### 精确产物缺口

仓库及其 Git 对象中均未提供：

```text
Module.symvers
vmlinux.symvers
vmlinux
System.map
.config
include/generated/utsrelease.h
arch/arm64/configs/mgk_64_k612_defconfig
```

根目录 `.gitignore` 明确排除 `Module.symvers`、`vmlinux.symvers`、`vmlinux`、
`System.map`、`.config` 和 `include/generated/`。因此不能用该仓库直接恢复目标
`g16e473de48a3-abogki462654244` 的 CRC、vermagic、CFI/LTO 或签名证据；即使按
`gki_defconfig` 成功编译出 LKM，也只能证明该公共树的 Kbuild 流程可运行。

### 对 Hide 1.0 的结论

1. **可复用**：Android 16 / KMI generation 5 的 Kleaf/Bazel 组织、KMI symbol list
   语法、ABI `abi.stg` 检查流程，以及 `CONFIG_MODVERSIONS`/CFI/模块签名的配置位置。
2. **不可复用为目标 ABI**：6.12.38 源码不能回溯成设备的 6.12.23 厂商构建；
   `symbols/xiaomi` 不能替代 `Module.symvers`；公共 `gki_defconfig` 不能替代
   OS3.0.23.0.WPMCNXM 的合并配置和 vendor DDK 配置。
3. **不能作为 Hide 1.0 加载依据**：该树没有 SUSFS/PathGuard 的 VFS 修改，也没有
   当前设备的精确输出树。它最多支持离线研究、构建 smoke-test LKM 和验证 ABI 工具链，
   不足以准入 Redmi K90 Pro Max，更不能据此执行设备 `insmod`。
4. **下一步门槛**：若要将该源码用于实际 Hide 后端，必须先确认设备确为该仓库对应的
   MediaTek K90 Max 变体，并取得同一 release 的最终 `.config`、`Module.symvers`、
   `vmlinux`/`System.map`、签名证书和 boot/vendor_dlkm 构建记录；对当前 `myron/SM8850`
   设备，这些条件尚未满足。

### 本轮审计状态

结论等级：**源码真实性高；与当前设备的型号/SoC/版本匹配度未证实；精确 ABI 不可用；
不可直接构建或加载 Hide 1.0 LKM。**

## 轮次 1：Android common `android16-6.12`

### 来源与版本

| 项目 | 本地目录 | 提交 |
|---|---|---|
| Android common kernel | `refer/hide-refer/android16-6.12` | `8eff909dd82da10a599468a514cc1ad07e90c3cc` |
| 分支 | `android16-6.12` | 以本地 Git HEAD 为准 |

### 已确认内容

- `gki/aarch64/abi.stg`：GKI ABI 规范/符号状态输入，约 9.7 MB。
- `gki/aarch64/symbols/`：按厂商划分的符号白名单，包含 `xiaomi`、`qcom` 等文件。
- `abi.bzl`、`BUILD.bazel`、`modules.bzl`：Kleaf/Bazel 的 ABI 与模块构建描述。
- `build.config.gki`、`build.config.gki.aarch64`：GKI 构建配置入口。
- `Makefile`：明确要求 mixed build 使用 `vmlinux.symvers` 和 `System.map`；启用
  `CONFIG_MODVERSIONS` 时会生成/消费 `Module.symvers`；`modules_prepare` 只准备
  外部模块所需的头文件和 Kbuild 输出。
- `include/linux/android_kabi.h`：Android KABI 标注机制。
- `kernel/module/version.c` 及相关 Kbuild 逻辑：模块 CRC、vermagic 和版本检查
  属于构建/加载链的一部分，不能通过文本修改替代。

### 对 Hide 1.0 的事实结论

1. 该树是 Android 16 / Linux 6.12 的公共 GKI 基线，可用于：
   - 研究 VFS、模块版本检查和 KMI 规则；
   - 构建通用 smoke-test LKM；
   - 对照 SUSFS/NoMount 的 6.12 移植位置。
2. 该树不是 Redmi K90 Pro Max 的完整内核树。它没有：
   - Xiaomi `myron` 的精确源码改动；
   - `OS3.0.23.0.WPMCNXM` 对应的 `.config`；
   - 目标 `g16e473de48a3-abogki462654244` 的 `Module.symvers`；
   - 目标厂商的签名证书、CFI/LTO 及完整 prepared output。
3. 因此，即使在该树上成功编译 `pathguard_hide1.ko`，也只能证明工具链和
   Kbuild 流程可用，不能证明能加载到 myron，更不能证明 Hide 1.0 行为通过。

### 与当前设备证据的关系

当前设备运行：

```text
6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

本地公共树 HEAD 并不等于该设备的 release build。必须另外取得同一设备/ROM
的 prepared output、符号 CRC、配置、编译器和签名策略，才能进入设备级准入。

### 不能直接复用的内容

- `gki/aarch64/abi.stg` 或 `symbols/xiaomi` 不能替代设备的 `Module.symvers`。
- `android16-6.12` 当前 HEAD 不能替代目标 `6.12.23` 厂商构建提交。
- `modules_prepare` 产物不能在缺少精确 CRC/KMI 时变成可加载模块。
- 公共 GKI 的 ABI 通过不等于 Xiaomi vendor module ABI 通过。

### 下一轮

继续审计 `kernel-build`，重点记录 Kleaf 的 artifact 获取、mixed build 输入和
`vmlinux.symvers/System.map` 的来源；随后审计 `myron-prebuilt` 的所有 `.ko`。

## 轮次 2：Android `kernel-build` / Kleaf

### 来源与版本

| 项目 | 本地目录 | 提交 |
|---|---|---|
| Android kernel build / Kleaf | `refer/hide-refer/kernel-build` | `ca810dc0113a389d49ab582a1bb3915bcac632d6` |
| 最近提交 | `kleaf: emit empty Kconfig.ext when kconfig_ext is None` | 2026-08-13 |

### 已确认的构建链

1. `kleaf/docs/impl.md` 将“公共 GKI + 设备模块”的路径定义为 mixed build：设备
   `kernel_build` 通过 `base_kernel` 指向公共 GKI，外部模块通过
   `kernel_module`/`ddk_module` 构建，随后由 `kernel_modules_install` 执行
   `modules_install`/`depmod`，最后由 `pkg_install` 组织分发目录。
2. `kleaf/docs/abi_device.md` 明确设备 ABI 监控需要设备自己的
   `kmi_symbol_list`，并要求 `kernel_abi` 同时接收 `kernel_build` 与完整的
   `kernel_modules` 列表。设备 ABI 定义应由 GKI 构建更新，设备侧不能只拿公共
   ABI 文件替代自己的符号列表。
3. `kleaf/impl/kernel_build.bzl` 的严格 KMI 检查要求输出中同时存在 `vmlinux`、
   `Module.symvers`、`raw_kmi_symbol_list` 和所有声明的内核模块，然后使用
   `verify_ksymtab` 检查符号是否落在允许的 KMI 范围内。
4. `kleaf/common_kernels.bzl` 对公共内核默认保留 `.config` 和 `Module.symvers`，
   并将 `scripts/sign-file`、签名密钥/证书列为 GKI 构建期输出的一部分；这说明
   签名、配置和 CRC 是构建产物，而不是模块编译后可随意补齐的元数据。
5. `android16-6.12/Makefile` 与 Kleaf 规则共同表明 mixed build 依赖
   `vmlinux.symvers`、`System.map`、prepared output、匹配 toolchain 和
   `CONFIG_MODVERSIONS` 生成的符号 CRC。

### `download_from_ci` 的实际边界

`gki/download_from_ci` 使用：

```text
https://ci.android.com/builds/submitted/{build_id}/{target}/latest/raw
```

它从 `BUILD_INFO` 读取 artifact 列表，但对常规 arm64 GKI 下载的固定文件是：

```text
Image
System.map
gki-info.txt
```

启用 `fetch_system_dlkm` 时再下载并解包：

```text
system_dlkm_staging_archive.tar.gz
```

Android 15 及以上还会下载 ABI 定义文件。该脚本本身没有把 `vmlinux.symvers` 或
`vmlinux` 加入常规 `download_kernel()` 文件列表；因此必须从发布页面或对应 CI
artifact 单独获取，不能误以为运行脚本后就拥有完整 prepared output。

### 对 Hide 1.0 的事实结论

- Kleaf 可以规范化 PathGuard 的外部模块构建和分发，但前提是拥有与设备完全一致
  的 `base_kernel`/KMI 输入。
- `kernel_module` 能生成 `.ko` 不等于模块可加载；严格 ABI 检查还需要 `vmlinux`、
  `Module.symvers` 和 symbol list，设备运行时还要通过 vermagic、CFI、签名和实际
  `insmod` 验证。
- `modules_prepare` 只解决头文件和 Kbuild 准备，不会凭空恢复厂商的
  `Module.symvers`、签名证书、CFI/LTO 配置或 vendor KMI。
- `vmlinux.symvers`/`System.map` 是 mixed build 输入；它们来自同一 GKI build 才有
  意义，不能用其它设备或其它 release 的文件拼接。

### 对当前 myron 阻塞的影响

该项目没有消除当前阻塞，反而把缺口具体化为：

```text
myron kernel source + device config
same-build vmlinux.symvers + System.map + vmlinux
same-build Module.symvers / vendor module CRC
same compiler, CFI/LTO and module signing policy
```

其中任何一项缺失，都只能做静态或工具链 smoke test，不能进入设备级 Hide 1.0
准入。下一步应审计 `android_kernel_xiaomi_myron-prebuilt`，确认其所有模块的
vermagic、CRC section、依赖关系和文件版本，并检查它是否包含上述缺失输入。

## 轮次 3：`android_kernel_xiaomi_myron-prebuilt`

### 来源与版本

| 项目 | 本地目录 | 提交/来源 |
|---|---|---|
| myron prebuilt kernel | `refer/hide-refer/android_kernel_xiaomi_myron-prebuilt` | `5247ab0731e54c4878e437dbba7f75a9be1db6e6` |
| 提交说明 | `myron-kernel: prebuilt kernel from OS3.0.306.0.WPMCNXM` | 2026-07-07 |

### 仓库内容

顶层包含 `kernel`、`dtb/`、`dtbo.img`、`system_dlkm/`、
`system_dlkm_flatten/`、`vendor_dlkm/` 和 `vendor_ramdisk/`。

模块数量及 release：

| 目录 | `.ko` 数量 | vermagic |
|---|---:|---|
| `system_dlkm` | 103 | `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` |
| `system_dlkm_flatten` | 103 | 同上 |
| `vendor_dlkm` | 375 | `6.12.23-android16-5-gf79b0b15da3a-mi-4k` |
| `vendor_ramdisk` | 419 | 同上 |

`system_dlkm` 与 `system_dlkm_flatten` 的 103 个模块 SHA-256 集合完全相同，
后者是扁平化安装布局，不是另一套 ABI。

### 精确设备内核证据

`kernel` SHA-256 为 `670C8285A079ADE6CFA50E35FAE4F52F020D1A3E1B78395FD2AE1CBBFDE4F6AF`。
从二进制字符串读取到目标 release、`kleaf@build-host`、Android clang
`19.0.1`（toolchain revision `r536225`，llvm commit
`b3a530ec6537146650e42be89f1089e9a3588460`）以及构建时间
`Wed Nov 19 12:38:00 UTC 2025`。构建特征还包含 `+pgo +bolt +lto +mlgo`。

这证明仓库中的 `kernel` 与目标 release 字符串、编译器主版本和构建特征一致，
远强于“同为 6.12.23”的旁证。但仓库提交说明对应 `OS3.0.306.0.WPMCNXM`，
仍应在设备上核对当前 slot/ROM 的 kernel SHA-256 或构建日期，不能只凭仓库
版本推断当前运行镜像完全相同。

### 模块 ABI 证据

抽查 `zram.ko`、`kheaders.ko`、`rust_binder.ko`、`common.ko`、
`machine_dlkm.ko`、`bq27z561.ko` 和 `xiaomi_touch.ko`，结果一致：

- 均为 AArch64 kernel module；
- 均存在非空 `__versions` 和 `__version_ext_crcs`；
- 模块末尾包含 `~Module signature appended~`；
- GKI/system 模块的 `scmversion` 为 `g16e473de48a3`；
- vendor DDK 模块包含 `built_with=DDK`，并使用 `gf79b0b15da3a-mi-4k`。

`modules.dep`、`modules.load`、`modules.alias` 也被保留，足以重建模块依赖和
分区加载顺序，例如 system `zram.ko` 依赖 `zsmalloc.ko`。

### 关键缺失项

Git tree 中没有发现 `Module.symvers`、`vmlinux.symvers`、`vmlinux`、
`System.map`、`.config`、`manifest_*.xml` 或 `gki-info.txt`。

因此这些 `.ko` 可以作为设备已经接受的 CRC/vermagic/签名样本，却不能直接
反向生成原始 `Module.symvers`，也不能单独让 `pathguard_hide1.ko` 通过 Kbuild
的 `modpost`、KMI、CFI、签名和设备加载检查。

### 对 Hide 1.0 的事实结论

1. 当前已经拥有目标设备的真实 `kernel` 和大量同一设备的已签名模块，可以把
   ABI 审计从猜测推进到逐符号 CRC 对照。
2. `system_dlkm` 是精确目标 GKI release 的最佳候选参考；`vendor_dlkm`/
   `vendor_ramdisk` 是 Xiaomi DDK release 的参考，不能把后者 vermagic 直接
   复制到 PathGuard 模块。
3. `system_dlkm` 的 CRC 集合可以用于验证公共 `android16-6.12` 输出是否同源，
   但即使全部重合，也不能证明未导出的、CFI 相关或设备私有符号可用。
4. 仓库仍不足以构建可加载的 Hide 1.0 外部模块。下一步应自动汇总所有模块的
   modinfo，提取 `__versions`/`__version_ext_crcs` 的符号名与 CRC，并与公共
   GKI `Module.symvers`/`vmlinux.symvers` 做逐符号差异。

### 下一轮

继续审计 `android_device_xiaomi_myron`，重点读取 BoardConfig、kernel/vendor
模块路径、分区布局和设备树引用，确认 prebuilt 与设备构建系统之间的关系。

## 轮次 4：`android_device_xiaomi_myron`

### 来源与版本

| 项目 | 本地目录 | 提交 |
|---|---|---|
| LineageOS myron device tree | `refer/hide-refer/android_device_xiaomi_myron` | `3d8ae35bde8727bd2f37120c8090e618e9ab2917` |
| 最近提交 | `myron: Update device manufacturer` | 2026-08-23 |
| 本地分支 | `lineage-24.0` | shallow clone，仅当前提交 |

### 实际内核集成方式

`BoardConfig.mk` 定义：

```make
KERNEL_PATH := kernel/xiaomi/myron-prebuilt
BOARD_PREBUILT_DTBOIMAGE := $(KERNEL_PATH)/dtbo.img
BOARD_PREBUILT_DTBIMAGE_DIR := $(KERNEL_PATH)/dtb
TARGET_NO_KERNEL_OVERRIDE := true
TARGET_KERNEL_SOURCE := $(KERNEL_PATH)/kernel-headers
PRODUCT_COPY_FILES += $(KERNEL_PATH)/kernel:kernel
```

这不是源码构建配置，而是 prebuilt 打包配置：kernel、DTB/DTBO 和模块全部从
`android_kernel_xiaomi_myron-prebuilt` 复制到最终 ROM。仓库中实际不存在
`kernel-headers/`，所以 `TARGET_KERNEL_SOURCE` 当前不是一个可用的 prepared
Kbuild tree，更不能用于构建 PathGuard 外部模块。

模块装配关系为：

- `vendor_ramdisk/modules.load` 决定首阶段 vendor ramdisk 模块；
- `vendor_ramdisk/modules.load.recovery` 决定 recovery 模块；
- `vendor_dlkm/modules.load` 决定 vendor DLKM 加载列表；
- `system_dlkm` 和 `system_dlkm_flatten` 被整体复制进 system DLKM。

### 发现的版本不一致

`BoardConfig.mk` 将 `system_dlkm` 复制到：

```text
/lib/modules/6.12.23-android16-5-g316453da9a9e-abogki441133159-4k
```

但同一 prebuilt 仓库中的 kernel/system 模块实际是：

```text
6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

vendor DDK 模块则是：

```text
6.12.23-android16-5-gf79b0b15da3a-mi-4k
```

这是三个不同 release。硬编码的 `g316453...abogki441133159` 看起来是设备树从
另一批 prebuilt 更新时遗留的路径；除非构建系统另有重写逻辑，否则会造成
system DLKM 安装目录与内核 release 不一致。该问题不应被用于推断 ABI，后续
若使用此设备树构建 ROM，需要先通过真实产物/启动测试修正并验证。

此外，`lineage_myron.mk` 的 build fingerprint 是
`OS3.0.303.0.WPMCNXM`，prebuilt 提交说明是 `OS3.0.306.0.WPMCNXM`，而本项目
最初目标设备记录是 `OS3.0.23.0.WPMCNXM`。三者不能视为同一 OTA/KMI 批次。

### 其它依赖与边界

- 设备树继承 `device/xiaomi/sm8850-common/BoardConfigCommon.mk` 和 `common.mk`；
  当前下载目录不含该公共设备树，因此还无法审计公共分区、boot header、页面
  大小、AVB 和 kernel cmdline 配置。
- 它还依赖 `vendor/xiaomi/myron/BoardConfigVendor.mk` 与 `myron-vendor.mk`；这些
  proprietary vendor 生成物也不在当前目录。
- `board-info.txt` 只允许 `myron|canoe`，可以确认板级别名，不能证明内核 ABI。
- `proprietary-files.txt` 主要描述 Android 用户态 blobs，不包含
  `Module.symvers`、prepared output 或内核构建 manifest。

### 对 Hide 1.0 的事实结论

1. 该设备树证明社区 ROM 目前依赖 prebuilt kernel 路线，而不是能从 myron
   源码重建 kernel/vendor modules。
2. 它无法补齐 PathGuard LKM 所需的源码、`.config`、`Module.symvers`、
   `vmlinux.symvers`、toolchain manifest 或签名密钥。
3. 设备树自身存在明确的 kernel release 目录漂移，不能把其中任何硬编码版本
   当作目标设备 KMI 证据。
4. 对 Hide 1.0 最有价值的仍是上一轮 prebuilt 二进制本身，以及连接设备上的
   当前 slot 证据，而不是此处的 BoardConfig 常量。

### 后续补充材料

为了完整审计社区 ROM 装配，还需要下载它依赖的公共设备树：

```text
device/xiaomi/sm8850-common
```

但这只能补充分区和打包逻辑，预计不能解决精确 ABI。下一轮进入
`susfs4ksu-gki-android16-6.12`，审计 Android 16/6.12 的真实补丁入口与其是否
支持 LKM、逐 UID/逐 namespace scope、mutation 和 dcache 一致性。

## 5. SUSFS `gki-android16-6.12` 补丁级审计

审计对象：

```text
refer/hide-refer/susfs4ksu-gki-android16-6.12
commit: 82e00eadb4c0390a71a6a2db8f7cb0e5074cc539
patch: kernel_patches/50_add_susfs_in_gki-android16-6.12.patch
KSU patch: kernel_patches/KernelSU/10_enable_susfs_for_ksu.patch
SUSFS version: v2.3.0
```

### 5.1 补丁覆盖面

`50_add_susfs...patch` 修改 24 个内核文件，除 `fs/susfs.c` 外还涉及：

- 路径解析：`fs/namei.c`；
- 目录枚举：`fs/readdir.c`；
- 属性和文件系统统计：`fs/stat.c`、`fs/statfs.c`；
- 打开、读写、执行：`fs/open.c`、`fs/read_write.c`、`fs/exec.c`；
- mount namespace 和 `/proc/*mount*`：`fs/namespace.c`、`fs/proc_namespace.c`；
- `/proc/<pid>`、maps、fd、inotify：`fs/proc/base.c`、`fs/proc/fd.c`、
  `fs/proc/task_mmu.c`、`fs/notify/fdinfo.c`；
- SELinux、kallsyms、uname/cmdline 等检测面：`security/selinux/*`、
  `kernel/kallsyms.c`、`kernel/sys.c`。

这说明 SUSFS 是配合 KernelSU 的内核源码补丁，不是可以独立插入任意厂商
内核的通用 VFS LKM。README 要求先选定 KernelSU tag 和对应 GKI 版本，再手工
应用两个 patch、复制 `fs/susfs.c` 与头文件、打开配置并重新构建/刷入内核；
README 还明确警告同一内核版本也可能需要自定义 patch。

### 5.2 `sus_path` 的数据模型和作用域

`fs/susfs.c` 的 `sus_path` 实现如下：

1. `add_sus_path` 通过 `kern_path()` 解析一个**当前已存在**的路径，然后把
   `AS_FLAGS_SUS_PATH` 写入目标 inode 的 `inode->i_mapping->flags`。FUSE 还要
   同时给 FUSE inode 和普通 inode 设置该位。
2. `add_sus_path_loop` 只把字符串放入内核链表；后续由 workqueue 以
   `ksu_cred` 重新解析路径并反复设置 inode 标志，因此适合目标会被重新挂载或
   inode 变化的场景，但它仍然不是持久化规则数据库。
3. `susfs_is_inode_sus_path()` 首先要求
   `TIF_PROC_UMOUNTED` 且 `current_uid().val >= 10000`，随后要求 inode mapping
   上存在标志，并检查当前 UID 与 inode owner UID 不同。实际条件是：

   ```text
   当前线程已被 KernelSU 标记为 umounted
   AND 当前 UID 是 app UID
   AND inode 已被标记
   AND 当前 UID != inode owner UID
   ```

4. UID/线程标志由 `KernelSU/10_enable_susfs_for_ksu.patch` 的
   `setuid_hook.c` 设置，只针对 zygote/zygote_next 派生流程；普通 init 派生进程、
   root/su 进程、未被 KSU allowlist 处理的其它身份不会自动得到同样作用域。

因此这是“按线程标志 + inode owner 例外”的 root hiding 机制，不是
`package identity -> namespace -> rule generation` 的策略模型。共享 UID、
MediaProvider 代查、Provider/isolated process 以及不同 mount namespace 都可能
使调用 UID 或 inode/dev 发生变化。项目 README 也直接记录了 `/sdcard` 路径会因
MediaProvider UID 而仍然可见，以及 zygote_next、magic mount 泄漏等已知问题。

### 5.3 各 VFS 入口的实际行为

**路径解析 / 打开。** `namei.c` 在 dcache 命中、慢路径、最后组件和
`lookup_open()` 等位置检测 `susfs_is_inode_sus_path()`。命中后通常丢弃原 dentry，
改用固定名称 `..5.u.S` 的假 dentry，并在继续解析子路径时返回 `-ENOENT`。这能让
普通路径访问、目录进入和基于路径的 open/stat 看起来像目标不存在；但假名称是
固定字符串，README 要求每次启动删除真实的 `/sdcard/..5.u.S`，说明它存在可被
创建/交叉检查的旁路。

补丁没有新增独立的 `unlink/rename/mkdir/create` 策略层，也没有对已经打开的
file descriptor 做统一隐藏。相关 mutation 是否失败，取决于该 syscall 是否经过
被改写的最后组件解析；通过已有 fd、不同 namespace、文件系统专用 ioctl 或其它
句柄路径不能由这组 hook 自动保证。

**目录枚举。** `fs/readdir.c` 覆盖 32/64 位以及 compat 的
`old_readdir/getdents/getdents64`。每个 dirent 回调使用目录 fd 的 `super_block`
和 mount `idmap` 调用 `ilookup(sb, ino)`；inode 未命中时直接 `orig_flow`，该项
照常返回。因此过滤依赖 dirent inode 号能在同一 superblock 的 inode cache 中命中，
不能作为所有 overlay/FUSE/跨层目录枚举的强一致保证。命中后只是跳过用户态 dirent
写入，不改变底层目录游标。

**属性伪造。** `SUS_KSTAT` 在 `vfs_getattr_nosec()`、`statx`、`/proc/fd`、
inotify、maps 和 `statfs` 等路径按 inode/dev 哈希表替换 ino/dev/nlink/size/时间/块数
和 mount id。它的文档作用域是所有 `(uid % 100000) >= 10000`，比 `sus_path` 更宽，
不是按 package 或 namespace 隔离。代码中 `KSTAT_SPOOF_CTIME_TV_SEC` 写成
`(1 < 8)`（而不是 `1 << 8`），该常量在用户态和内核头文件中都存在，导致 ctime 秒
字段的 spoof flag 与预期不一致，这是已确认的实现缺陷。

**open_redirect。** 该功能在添加时要求 target 和 redirected 路径都已存在，拒绝
FUSE，并以 inode number + `s_dev` 建立双向哈希项；打开路径时按五种固定 UID scheme
（非 app、root 非 su、非 su、umounted app、umounted）重写 filename。它不是“隐藏”，
而是条件重定向；目标发生替换、重新挂载或 dev 改变后需重新登记。README 还要求用户
自行处理 SELinux 权限。

**mount/proc/检测面。** `SUS_MOUNT` 为 KSU 创建/克隆 mount 分配伪 mnt id，且可
在 `/proc/self/mounts|mountinfo|mountstats` 对非 su 进程使用自定义 seq 输出；
`SUS_MAP` 只过滤若干 `/proc/<pid>/maps` 相关接口，不支持匿名内存，也不保证规避
高质量注入检测。`uname`、cmdline、kallsyms、SELinux AVC 是检测面伪装，不改变文件
访问语义。

### 5.4 对 Hide 1.0 的复用结论

可复用：

- 在内核路径解析的早期阶段把目标转换为一致的“不可见/不存在”结果；
- 对 `getdents*` 的多 ABI 覆盖和 inode/dev 维度的证据采集思路；
- 用 RCU/哈希表保存运行时规则、用静态 key 控制可选功能；
- 将路径隐藏、mount 隐藏、stat 伪造、maps 过滤拆成独立编译选项。

不可直接复用：

- 依赖 KernelSU zygote `TIF_PROC_UMOUNTED` 的线程标志，无法满足 PathGuard 的
  package/UID/namespace 精确策略；
- 依赖 inode mapping bit，不能表达“路径规则在 rename、rebind、overlay 后仍稳定”
  的对象身份；
- `ilookup()` miss 时回退原流程，无法宣称目录枚举强一致；
- 固定 `..5.u.S` 假名、已知 `/sdcard` MediaProvider 和 zygote_next 旁路，不符合
  Hide 1.0 的“无可观察泄漏”标准；
- 需要修改并重编译目标内核，不能由当前已找到的 Redmi K90 Pro Max prebuilt
  kernel 直接加载为通用 LKM。

本轮结论：SUSFS 是目前最接近真实 VFS 隐藏的公开 Android 参考实现，但它证明的
是“特定 GKI + KernelSU + root hiding 场景可工作”，而不是“存在一个无需厂商内核
源码即可部署的通用 Hide 1.0 后端”。在 Redmi K90 Pro Max 上，除非获得并重建精确
`myron` 内核，否则只能将上述逻辑用于静态设计、补丁差异和 HideLab 对照实验，不能
把 SUSFS 用户态工具或单个 `.ko` 宣称为设备可用隐藏能力。

## 7. `KernelSU-Next` 对照审计

审计对象：

```text
refer/hide-refer/KernelSU-Next
commit: 16ee781 (manager: use in-app language picker #1492)
```

### 7.1 与 upstream 相同的核心边界

该仓库的 `kernel/Kconfig` 和 `kernel/Makefile` 与 `KernelSU-upstream` 保持同一
LKM/KMI 路线：`KSU` 是依赖 `KPROBES`、`EXT4_FS` 的 `tristate`，构建后用
`tools/check_symbol` 对 `.ko` 的未定义符号和 `vmlinux` 符号表做静态检查。检查器
仍不验证 CRC、KMI symbol list、CFI、签名和厂商配置，因此不能解决目标设备缺少
`Module.symvers/vmlinux.symvers/.config` 的问题。

`feature/kernel_umount.c` 也逐项保留 upstream 逻辑：只在 zygote 派生的 app/
webview/isolated UID 切换时，根据 `ksu_uid_should_umount()` 对预登记 mount 路径
执行 `path_umount()`。`policy/allowlist.c` 以 UID/profile（模 100000）决定
`allow_su`、`umount_modules` 和 namespace 模式。

### 7.2 Next 的增量能力

- `infra/file_wrapper.c` 创建受 SELinux 约束的 anon-inode wrapper，把原始 `struct
  file` 路径保存到 `d_fsdata`，转发 read/write/ioctl/mmap/iterate 等 file
  operations，并用自定义 `d_dname` 对外显示原路径。它用于给 KSU 控制的 fd 提供
  安全包装，不是隐藏任意目标文件；wrapper 本身反而需要确保访问语义完整转发。
- `manager/pkg_observer.c` 在 `/data/system` 监听 `packages.list` 的创建/移动，
  触发 throne tracker 更新 manager UID 和 allowlist 清理。这是 manager 身份维护，
  不会为 PathGuard 建立 package-scoped 的文件隐藏策略。
- `extras.c` 和 `feature/selinux_hide.c` 提供 AVC/SELinux 相关检测面处理；它们
  影响 root/SELinux 伪装，不修改 VFS 的路径解析或目录枚举结果。

### 7.3 明确未提供的能力

对仓库 `kernel/` 的完整搜索未发现 `fs/namei.c`、`fs/readdir.c`、`fs/stat.c`、
`vfs_getattr`、`security_path_*` 或 unlink/rename 专用隐藏实现。也就是说：

- 没有 SUSFS 的 inode 标记和假 dentry 逻辑；
- 没有 `getdents*` 过滤；
- 没有 stat/kstat、mountinfo、maps 或 readlink 的目标对象伪造；
- 没有按 package/namespace 对 VFS 访问结果做决策。

因此 `KernelSU-Next + file_wrapper` 不能被描述为 Hide 1.0 后端。它最多可作为
PathGuard 的隔离基础设施候选，负责 root/module mount 的 namespace 生命周期和
受控 fd 通道；真正的“路径在 open/readdir/stat/mutation 各入口一致不可见”仍需
独立内核实现（例如 SUSFS 的源码补丁级设计，但要重新解决其 UID、inode、dcache
和旁路限制）。

### 7.4 对 PathGuard 的结论

可借鉴：KMI-aware Kbuild、runtime symbol resolver、app profile/allowlist、
kernel umount 生命周期、file wrapper 的引用计数和 SELinux 初始化方式。

不可直接复用：任何 VFS hide 语义、package identity 绑定、跨 namespace 的规则
一致性、rename/rebind 后对象追踪，以及当前 Redmi K90 Pro Max stock kernel 上的
直接 LKM 激活。该项目和 upstream 应在调研报告中归为同一“KernelSU 隔离基础设施”
家族，而不是两套独立隐藏方案。

## 6. `KernelSU-upstream` 审计

审计对象：

```text
refer/hide-refer/KernelSU-upstream
commit: 33d0c92 (manager: Block background taps without consuming drag events)
```

### 6.1 构建和 LKM 边界

`kernel/Kconfig` 将 KernelSU 声明为 `tristate`，因此理论上可以生成
`kernelsu.ko`，但它依赖目标内核已有的 `CONFIG_KPROBES` 和 `EXT4_FS`。构建
脚本把 `KernelSU/kernel` 链接到 GKI 的 `drivers/kernelsu`，随后通过目标内核
Kbuild 编译，不能脱离目标 kernel tree 独立编译。

`kernel/Makefile` 按 `KDIR` 目录名识别 KMI（包含 `android16-6.12` 等），并在
构建后运行 `tools/check_symbol <kernelsu.ko> <vmlinux>`。该检查器仅做三件事：

- 读取 `.ko` 的 ELF 未定义符号；
- 确认 `vmlinux` 有同名、非 undefined 的符号；
- 要求 `.ko` 存在且 `__versions` section 大小为 0。

它**不**校验 `Module.symvers` CRC、KMI symbol list、CFI 类型、LTO/编译器版本、
模块签名或厂商 ABI。因此“check_symbol 通过”绝不能当作 Redmi K90 Pro Max
模块可加载证据。

源码中的 `infra/symbol_resolver.c` 还会运行时从 `kallsyms` 查找内核地址，兼容
KCFI/符号变体，并明确忽略已经属于其它模块的符号。这是一种运行时 hook 解析器，
不是 ABI 生成或验证机制；目标内核关闭/裁剪 kallsyms 时会失效。

### 6.2 KernelSU 的实际隔离模型

`kernel/feature/kernel_umount.c` 维护一份由 userspace 添加的 mount 路径列表，
在 zygote 派生 app/isolated/webview 进程切换 UID 时，使用 `ksu_cred` 在子进程
mount namespace 中执行 `path_umount()`。触发条件包括：

- 新 UID 是 app UID、webview zygote 或 isolated UID；
- `ksu_uid_should_umount(new_uid)` 对该 app profile 返回 true；
- 当前进程确实是 zygote child（通过 SELinux domain 判断）。

`policy/allowlist.c` 的 app profile 以 UID（模 `PER_USER_RANGE`）为主，控制
`allow_su`、`umount_modules`、root profile 和 namespace 选项。supercall UAPI
公开了 `KSU_IOCTL_UID_SHOULD_UMOUNT`、`GET/SET_APP_PROFILE`、`ADD_TRY_UMOUNT`
等管理接口，状态由 KernelSU/ksud/Manager 共同维护。

这能很好地支持“不给目标 app 看见 KernelSU 模块 mount”，但它不是 VFS 对象级
隐藏：

- 不改变 inode、dentry、readdir、stat 或 open 语义；
- 规则主体是 UID/profile，而非稳定 package identity（同 UID 多包、共享 UID、
  isolated UID 需要额外处理）；
- 只处理 KSU 记录的 mount 路径，不能覆盖设备原生 mount、overlay/FUSE 视图和
  其它句柄旁路；
- 进程创建和 setresuid 时机决定隔离是否生效，非 zygote 派生或已打开 fd 不会被
  统一回溯处理。

### 6.3 可复用性和限制

可复用：

- KMI 感知的 GKI 集成方式、Kbuild 组织和 runtime symbol resolver 设计；
- 按 app profile 决定 mount namespace 隔离的生命周期；
- supercall/anon-inode FD 作为受控 userspace -> kernel 配置通道；
- 对 KCFI、符号后缀和不同 Android KMI 的兼容处理思路。

不可直接作为 Hide 1.0：

- KernelSU 本身没有路径隐藏或目录枚举过滤实现；真正的 `sus_path` 仍来自
  SUSFS 对 VFS 源码的修改；
- `check_symbol` 不是完整 ABI 检查，不能解决当前 myron 缺失的
  `Module.symvers/vmlinux.symvers/.config`；
- UID/profile mount 隔离不足以满足 PathGuard 的 package、namespace、规则版本
  和 rename/rebind 后对象稳定性要求；
- 依赖修改目标内核或已有 KernelSU 集成，不能在当前 stock Redmi 内核上凭一个
  通用 `.ko` 激活。

本轮结论：KernelSU-upstream 是实现“按 app profile 隔离模块挂载”的基础设施和
LKM/KMI 研究样本，不是 Hide 后端。对 PathGuard 最有价值的设计是 profile 生命周期、
受控 supercall 和 KMI 证据链；隐藏语义仍需独立 VFS/LSM 设计，并且必须在精确
myron 内核构建条件满足后才有设备验证资格。

## 轮次 9：`extract-symvers-ng`

### 来源与版本

| 项目 | 本地目录 | 提交 |
|---|---|---|
| extract-symvers-ng | `refer/hide-refer/extract-symvers-ng` | `b857bc095ea6111813cd335dde8b8d58a788adcc` |
| 上游 | `https://github.com/bol-van/extract-symvers-ng` | 2023-01-22 |

项目只有 `extract-symvers.py` 和 `readme.md`，是一个离线 Python 脚本，不是内核补丁、
LKM 或完整构建系统。

### 算法和输入要求

- `KernelImage.decompress_kernel()` 可识别 gzip、lzma、zstd，以及 U-Boot 头；legacy
  lz4 会明确退出，Android boot 容器、AVB/dm-verity 和厂商自定义封装不在脚本的完整
  解析范围内。
- `scan_symsearch()` 按 Linux 内核版本选择 `struct symsearch` 布局（5.12 前后）和
  `kernel_symbol` 布局（4.19/5.4、`CONFIG_HAVE_ARCH_PREL32_RELOCATIONS`），扫描三类
  导出表 `EXPORT_SYMBOL`、`EXPORT_SYMBOL_GPL`、`EXPORT_SYMBOL_GPL_FUTURE`。
- `-B/--base-address` 是必需的虚拟加载基址（除非只使用 `-d` 解压）；`-k`、`-p`、
  位宽和端序必须与目标内核匹配。脚本通过指针是否落在镜像范围内验证候选表，随后读取
  符号名和 CRC，输出类似 `0x<crc>\t<name>\tvmlinux\t<export-type>` 的文本。
- README 明确指出 arm64 `CONFIG_RELOCATABLE` 常导致镜像中的结构地址为零；需要
  `nokaslr`、真实 `/proc/kallsyms` 基址、未压缩内核或运行内存转储。Android 上获取
  `nokaslr` 往往需要修改/启动 boot 或 recovery，不能把普通用户态读取当作无风险步骤。

### 能解决什么，不能解决什么

该工具可以在**确实拿到与当前 boot 完全一致的未剥离 vmlinux/内存镜像，并且知道正确
基址**时，恢复内核自身导出的符号名和 CRC，作为 `Module.symvers` 的一部分输入。它
不能恢复：

```text
厂商/外部模块的 __versions 和 CRC
vmlinux.symvers、System.map、.config、include/generated/
KMI symbol list、KMI generation、CFI/LTO/toolchain 参数
模块签名证书、AVB/dm-verity 或 vendor_dlkm 装载策略
```

README 的 “Not solved” 章节说明，已加载模块的 `struct module` 形态随版本和配置变化，
没有可靠的静态搜索模式，因此脚本不能提取 vendor DDK 模块 ABI。脚本输出也只描述
`vmlinux`，不会自动合并 `Module.symvers` 中的模块来源、命名空间或导出许可元数据。

### 对当前 Redmi K90 Pro Max 的可行性

当前设备是 arm64、可重定位 Android GKI 6.12.23，已知的是压缩/预编译 boot 中的 kernel
二进制和 vermagic，尚未取得未剥离 `vmlinux`、可靠运行时基址或精确 `CONFIG_HAVE_ARCH_PREL32_RELOCATIONS`
值。因而直接对 boot.img 运行该脚本不能形成可信结果；即使扫描成功，也只能得到
`vmlinux` 导出 CRC，不能补齐 Xiaomi vendor DDK 模块的 CRC、CFI、签名和 KMI 证据。

### 安全与验证边界

- README 建议修改 boot 参数、启动 recovery、QEMU 内存转储等操作；本项目审计不执行
  刷写、修改 boot、`insmod` 或内核注入。
- 可以在离线样例 vmlinux 上做算法回归：核对脚本输出的符号集合与同一构建的官方
  `Module.symvers`；不能把脚本“扫描到表”当作设备可加载证明。
- 对 PathGuard 的正确定位是“证据提取辅助工具”，不是 ABI 修复工具，也不是 Hide
  后端实现。

### 本轮审计状态

结论等级：**算法对公共 vmlinux 有研究价值；对当前 myron 缺失的精确 ABI 只能提供
部分内核符号线索；无法单独使 Hide 1.0 LKM 获得设备加载资格。**

## 轮次 10：`LKM-PathMask-main`（PathMask）

### 来源与版本

| 项目 | 本地目录 | 提交 / 版本 |
|---|---|---|
| PathMask | `refer/hide-refer/LKM-PathMask-main` | `cc69259a07e0d7c9f0ec2abf18decadcfafdddef`（本地分支 `feature/pattern-redirect-v6`） |
| 模块版本 | `ksu-module/module.prop` | `2.5.0`，versionCode `50` |
| KMI 发布 | `update/android16-6.12.json` | 指向 PathMask v2.5.0 的通用 `android16-6.12` 包 |

这是一个 Android arm64/GKI 外部模块和 KernelSU 包，核心代码集中在
`kernel/pathmask.c`（约 1129 行），并配有启动脚本、WebUI、配置迁移、GitHub Actions
DDK 构建和多个 KMI 发布包。仓库工作树另有用户未提交的 PathGuard 文件变更，本轮只读
PathMask 自身内容，未改动或回滚这些变更。

### 隐藏模型

- `add_target_path()` 在 `insmod` 时通过 kprobe 解析的 `kern_path()` 取得目标对象，
  保存 `(superblock s_dev, inode i_ino, 原始文本路径)`；缺失路径会被跳过，全部缺失时
  加载失败，最多 64 个目标。
- `inode_permission` 和 `vfs_getattr` 使用 kretprobe，在返回阶段把命中目标的结果改为
  `-ENOENT`。代码明确说明直接 hook `security_inode_*` 会被 ThinLTO 内联，因此改挂在
  VFS helper 上。
- 为覆盖 ThinLTO 绕过 VFS helper 的路径，默认注册 7 个 arm64 syscall 入口中的 6 个
  （`newfstatat`、`statx`、`faccessat2`、`readlinkat`、`openat`、`openat2`，默认排除
  `faccessat`），按用户态 filename 做绝对路径前缀匹配。`openat/openat2` 在返回阶段
  通过 kprobe 解析的 `close_fd()` 关闭已分配 fd，再改回 `-ENOENT`。
- `__arm64_sys_getdents64` 返回后复制用户目录缓冲区、删除匹配的 `d_ino` 记录并缩短
  返回长度，实现目录列表过滤。scope 支持 `global`、UID deny、UID allow，并额外覆盖
  Android 90000-99999 isolated UID 范围。
- KernelSU `service.sh` 负责包名到 UID 解析、动态 `/dev` glob/父目录展开、等待目标
  出现、失败熔断、热重载和诊断 UI；这些是装载编排，不改变内核隐藏语义。

### 已确认的能力边界

PathMask 对一个**预先存在且可解析的普通文件/目录**，在命中的 UID 和调用路径上可以
实现比纯 mount unmount 更强的效果：inode 权限/属性返回、部分绝对路径 syscall、目录
枚举都可能返回“不存在”。它还正确意识到 ThinLTO 会使“已注册的 kretprobe 实际不触发”，
并通过 syscall 入口兜底；这对研究 GKI hook 覆盖很有价值。

但是这些能力不能等同于完整 VFS 对象隐藏：

1. **探针上下文中存在睡眠/缺页风险（阻断可靠性）**。`getdents_entry()` 在 kretprobe
   回调中执行 `kmalloc(count, GFP_KERNEL)`；`getdents_exit()` 执行
   `copy_from_user()`/`copy_to_user()`；syscall entry 执行 `strncpy_from_user()`。
   kprobe/kretprobe 处理器不能假设可以睡眠或处理用户页缺失，通常应使用原子分配并避免
   可阻塞的用户拷贝。当前实现未证明这些路径在 Android GKI 上满足探针上下文约束，
   可能造成 `sleeping function called from invalid context`、死锁、数据损坏或随机重启。
   仓库没有 KASAN/lockdep/压力测试证据来消除这一风险。
2. **inode 和文本路径是两套不一致的匹配**。inode 路径能覆盖 rename、hard link、
   通过同一对象的部分访问；syscall 兜底只比较用户传入的绝对字符串，不能覆盖
   `dirfd + 相对路径`、已打开目录 fd、`/proc/self/fd`、某些 `open_by_handle_at` 或
   其它不经过这些 syscall 的访问。相反，重绑定后新 inode 可能只被文本前缀遮挡，
   语义取决于调用形式。
3. **目录过滤忽略设备号**。`is_target_ino()` 只比较 `d_ino`，而目标解析和直接访问
   使用 `(s_dev, i_ino)`；不同挂载/文件系统复用 inode 号时，`getdents64` 可能误隐藏
   无关项。过滤失败（用户缓冲区拷贝失败、返回长度超出 64 KiB、探针注册失败）会
   明确回退为可见，因而不是 fail-closed。
4. **目标必须在加载时存在**。动态 glob 由 shell 展开后再加载，不能原子覆盖目标在
   加载后创建、替换或跨 mount 的整个生命周期；等待和后台 watcher 只是编排补偿。
5. **函数指针和 CFI 风险**。`kern_path()`、`path_put()`、`close_fd()` 地址通过
   `register_kprobe()` 动态解析，并用 `__nocfi` 包装间接调用。这样绕过了 CFI 类型检查，
   也没有验证厂商函数签名、KMI CRC、LTO 形态或 RCU 生命周期；符号存在不等于可安全
   以该原型调用。
6. **策略主体仍是 UID，不是稳定 package/namespace 身份**。包名只在 userspace 启动
   脚本中解析成 UID；共享 UID、work profile、多用户、isolated UID 重用和 namespace
   变化都可能使策略与实际调用者不一致。`scope_mode=global` 还会把模块自身的诊断
   读取拦截，脚本只能依赖额外的 `resolved_count` 参数规避自检。
7. **覆盖面和性能存在取舍**。7 个 syscall kretprobe 位于 `stat/access/open` 热路径；
   代码注释和 changelog 承认纳秒到微秒级额外开销，特定检测器可通过时延识别。默认
   排除 `faccessat` 是针对单一测试样本的经验折中，不是行为等价性保证。

### 构建和发布证据

- `kernel/Makefile` 只是 `make -C $(KDIR) M=$(MDIR) modules`；模块必须使用目标 DDK
  的 prepared kernel tree。
- GitHub Actions 使用 `ghcr.io/ylarod/ddk-min:<kmi>-20260313` 容器，按
  `android12-5.10` 到 `android16-6.12` 矩阵编译，然后 `llvm-strip -d` 并上传 `.ko`。
  发布名按 KMI 分类，不绑定 Redmi K90 Pro Max 的精确 `uname -r`、厂商 commit、
  `Module.symvers`、签名证书或 CFI/LTO 参数。
- `README` 虽建议“选择匹配 KMI 的包”，但 `android16-6.12` 只是 KMI 大类；对于当前
  `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，仓库没有设备级加载报告、
  `dmesg`、CRC 对照或 HideLab 全量回归证据。

### 对 PathGuard Hide 1.0 的结论

- **可借鉴**：以 `(dev, inode)` 保存对象身份；对 ThinLTO 采用多个入口覆盖的思路；
  把 syscall 兜底作为可配置风险项；加载前解析目标并记录 resolved_count；按 KMI
  组织 DDK 构建与发布；失败熔断和诊断报告。
- **不能直接复用**：当前 kretprobe 回调中的阻塞/用户拷贝操作；仅按 inode 的
  `getdents64` 过滤；以 UID/文本路径代替 package、mount namespace 和对象生命周期；
  `__nocfi` 动态函数指针调用；通用 KMI 包作为设备 ABI 证据。
- **验收定位**：PathMask 可以作为 HideLab 的“矛”或 VFS 覆盖实验样本，但在修复上下文
  安全、补齐相对路径/dirfd/多挂载/rename/rebind/FD 旁路并完成压力与崩溃回归前，不能
  作为 Hide 1.0 后端，更不能在当前 myron stock 内核上直接加载其发布 `.ko`。

### 本轮审计状态

结论等级：**VFS 隐藏研究价值高；实现存在未证明的探针上下文安全风险和多条可复现
语义旁路；通用 `android16-6.12` 发布包没有 Redmi K90 Pro Max 精确 ABI 证据；不准入
Hide 1.0，也不授权设备 `insmod`。**

## 轮次 11：`nomount-master`（NoMount）

### 来源与版本

| 项目 | 本地目录 | 提交 / 分支 |
|---|---|---|
| NoMount | `refer/hide-refer/nomount-master` | `cc69259a07e0d7c9f0ec2abf18decadcfafdddef` |
| 本地分支 | `feature/pattern-redirect-v6` | 提交时间 2026-09-01 |
| 内核子系统 | `kernel/src/nomount.c`、`nomount.h` | `NOMOUNT_VERSION "20"` |
| 模块元数据 | `module/module.prop` | v2.0.0，KernelSU/APatch metamodule |

本轮只读审计 NoMount 源码、构建脚本、CLI 和启动脚本；没有执行其 `setup.sh`，没有修改
任何外部内核树，也没有执行 `insmod`、`ksud insmod` 或设备刷写。

### 设计模型：在 VFS 对象上做内存重定向

NoMount 不是 OverlayFS，也不是 syscall/kprobe 过滤器。它保存虚拟路径到真实路径的规则，
然后对目标路径的父目录和所在 superblock 的操作表做运行时替换：

1. `nomount_generate_virtual_topology()`（约 1029-1113 行）逐级解析虚拟路径。已经存在的
   父目录会被附加 `nm_iop`/`nm_fop`，不存在的父级则创建 `NM_FLAG_VIRTUAL_DIR` 的虚拟
   目录节点。
2. `nomount_hijacked_lookup()` 在父目录 lookup 中匹配规则，使用 `new_inode()`、
   `d_splice_alias()` 创建虚拟 inode/dentry；真实文件的 `struct path` 被保存并增加引用。
3. `nm_open()` 对虚拟 inode 调用 `dentry_open()` 打开真实路径，把真实 `struct file` 放入
   虚拟文件的 `private_data`。`read_iter`、`write_iter`、`llseek`、`ioctl`、兼容 ioctl、
   splice、fsync 等操作再转发给真实文件。
4. `nomount_hijacked_iterate_dir()` 使用代理 `dir_context` 过滤原目录项，再由
   `nomount_emit_virtual_children()` 注入虚拟项。`NM_FLAG_WHITEOUT` 项不被 emit，等价于
   对该 UID 返回“目录中不存在”。
5. 虚拟 inode 的 `getattr`、`setattr`、xattr、符号链接 `get_link` 尽量转发真实 inode，
   同时把虚拟 inode 的 inode 号和设备号写入 stat 结果。

因此，它的目标是让调用者看到一套经过 VFS lookup、readdir 和 file operation 的统一视图，
而不是只拦截几个用户态系统调用。这是目前参考项目中最接近“对象级路径隐藏/重定向”的方案。

### Whiteout、虚拟项和 UID 语义

- `nm rule add --whiteout <virtual>` 创建没有真实路径的规则。lookup 命中后通过负 dentry
  和 `d_revalidate` 表现为不存在；readdir 代理跳过该项。该行为只作用于规则命中的目录、
  当前 UID 和被覆盖的 VFS 路径，不会抹除底层 inode 或阻止已经持有的 fd。
- 注入规则可把一个真实文件或目录映射到虚拟路径。不存在的中间目录在内存中形成虚拟拓扑，
  可用于向只读 `erofs`/`ext4` 目录注入文件。
- 规则中的 `target_uid == 0` 表示全局规则；非零值只对 `current_uid().val` 相等的调用者
  生效。另有独立 UID IDR：被 `nm uid add <uid>` 标记的 UID 会跳过 NoMount 规则，看到
  原始文件系统。
- 该 UID 例外是“绕过所有重定向”的 deny/exclusion 语义，不是 package、安装实例、进程组、
  mount namespace 或 SELinux domain 身份。共享 UID、多用户、work profile、isolated UID
  重用以及 namespace 变化均可能导致策略主体与实际调用者不一致。

### 操作表替换和生命周期审计

`nomount_hijack_dir_ops()` 复制原始 `inode_operations`/`file_operations`，仅替换 lookup、
iterate_shared（旧内核还替换 iterate），再用 release-store 写回 `inode->i_op/i_fop`。
`nomount_hijack_superblock()` 复制 `super_operations`，替换 destroy/drop/evict inode，并
为整个 superblock 复制 xattr handler 数组。代码使用 `seqcount`、RCU 和 SRCU 保护规则数组，
删除规则时等待 RCU/SRCU 后释放规则和路径引用；这些机制是正确性设计的核心。

但这仍不是可以直接授予 Hide 1.0 的生命周期证明：

1. `inode->i_op`、`inode->i_fop`、`sb->s_op` 和 `sb->s_xattr` 属于共享 VFS 对象。代码没有
   与其它内核组件协商“谁拥有当前操作表”的协议；若 OEM 文件系统、另一个 LKM 或调试工具
   在 NoMount 之后再次替换操作表，NoMount 的 `__get_nm()` 识别和恢复条件可能失效。
2. `nomount_restore_superblocks()` 会 shrink dcache、遍历 `s_inodes` 并恢复已识别的 inode
   操作表，但 `nomount_hijack_dentry_ops()` 没有保存每个 dentry 原始 `d_op`。卸载依赖
   shrink dcache 丢弃相关 dentry，外部仍持有引用的 dentry 是否安全恢复没有独立证明。
3. `nm_destroy_hijacked_inode()` 通过操作函数指针反推私有包装对象，并在恢复时写回原指针。
   这要求包装对象在整个 inode 生命周期中保持可识别；操作表被第三方改写或 inode 在并发
   回收时出现非预期组合，都没有 KASAN/lockdep/压力测试证据覆盖。
4. 虚拟 inode 复用真实 inode 的 `i_mapping`，文件操作只转发一组常见回调；poll、fasync、
   lock、某些异步/uring 扩展以及文件系统特有 ioctl 没有通用代理。对“读取配置文件”的
   样本可能足够，对任意 Android 文件对象则不能宣称等价。
5. `nm_setattr()`、xattr、`get_link()` 直接作用于真实路径；这对重定向很方便，但意味着
   虚拟视图上的写属性、xattr 和符号链接解析可能改变真实文件，必须纳入策略权限模型。

### Keyring 控制通道

userspace `nm` 不创建 `/dev` 节点。它把 4096 字节对齐的 `nm_payload` 固定在一页中，调用
`add_key("nomount", "trigger", &ptr, sizeof(ptr), -1)`；内核注册同名 key type，在
`nm_key_preparse()` 中检查 `CAP_SYS_ADMIN`，读取用户指针并执行规则/UID命令，最后返回
`-ECANCELED`，使 key 不真正落盘。

该方式的优点是避开设备节点权限和 ioctl 编号兼容问题，且协议实现简单。风险和限制是：

- key type 名称是全局的，重复加载或其它模块占用同名类型会导致初始化失败；
- 内核通过 `get_user_pages_fast(FOLL_WRITE)` 固定用户页后直接在映射页上读写 payload，
  协议依赖页边界、结构大小和架构缓存一致性，错误地址或并发复用由调用方自行承担；
- 仅有 `CAP_SYS_ADMIN` 粗粒度门槛，没有 package、UID 范围、SELinux domain 或调用者命名空间
  的细粒度授权；拥有该能力的进程可以清空或重写全部规则；
- `NM_CMD_GET_LIST` 在读锁内把规则拷贝到用户页，规则修改和用户消费之间没有事务版本号，
  CLI 只能获得某一时刻的分页快照。

### 构建和“通用 GKI”声明的实际含义

`kernel/README.md` 同时支持：

- 通过 `setup.sh` 把 `fs/nomount` 链接到源码并以 `CONFIG_NOMOUNT=y` 内建；
- 在 prepared kernel output tree 上以 `CONFIG_NOMOUNT=m make -C <out> M=fs/nomount modules`
  编译 LKM。

GitHub Actions 的 `build-lkm.yml` 只按 `android12-5.10` 到 `android16-6.12` 的 KMI 大类
选择 `ghcr.io/ylarod/ddk:<kmi>` 容器，并把生成的 `.ko` 上传为通用 artifact。它没有绑定
设备 codename、厂商 commit、精确 `uname -r`、`Module.symvers`、签名证书、CFI/LTO 配置或
vendor_dlkm 装载策略。README 中“Standard GKI 不需修改内核”实际只表示“可以尝试加载预编译
LKM”，不等于任意 GKI 设备 ABI 兼容。

对本项目目标设备 `myron`（`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`），
NoMount 的 `android16-6.12` artifact 仍缺少精确 CRC、vermagic、签名和运行时加载证据；在
拿到匹配 prepared tree 前不能准入，也没有执行设备加载实验。

### 与 Hide 1.0 的关系

**可以借鉴的部分：**

- 在 lookup/readdir/file operation 层统一处理虚拟对象，而不是把语义拆成多个 syscall 旁路；
- 用 `(superblock, inode/path)` 引用和虚拟 inode 保存对象生命周期；
- 对规则树使用 hash、排序数组、Bloom mask、seqcount、RCU/SRCU，降低热路径锁竞争；
- 通过白名单/排除 UID 做对照实验；
- 用 boot semaphore、失败后自动 disable 和日志降低启动失败扩散范围；
- 使用 Android GKI VFS namespace 导入声明，明确 LKM 依赖的内核导出边界。

**不能直接复用的部分：**

- 直接改写共享 VFS 操作表，当前没有证明与 OEM/其它 LKM 并发修改的互操作性；
- 仅按 UID 的排除模型不能满足 PathGuard 的 package、namespace、profile 和多用户策略；
- dentry 原始 `d_op` 未保存、卸载恢复依赖 shrink dcache，不能作为“可热卸载”安全证明；
- 虚拟文件不是所有 file operation 的完整透明代理，不能把配置文件样本外推到任意文件类型；
- keyring 只提供控制通道，不提供策略隔离或 ABI 兼容性；
- 通用 `android16-6.12` 构建矩阵不能替代 Redmi K90 Pro Max 精确 ABI 证据。

### 本轮审计状态

结论等级：**NoMount 是当前研究集里最接近 VFS 级隐藏/重定向的架构样本，适合作为 HideLab
的“矛”或 VFS prototype 参考；其 whiteout 能力不等于对已有 fd、所有 file operation、
namespace 和底层 inode 的完全隐藏。由于共享操作表 hijack、dentry 恢复、UID 身份模型和
通用 GKI artifact 均缺少设备级证明，NoMount 不准入当前 Hide 1.0，也不授权在 myron 上加载。**

## 轮次 12：`susfs4ksu-master`（历史 SUSFS 1.3.8）

### 来源与版本

| 项目 | 本地目录 | 版本/证据 |
|---|---|---|
| SUSFS 历史主仓 | `refer/hide-refer/susfs4ksu-master` | README 明确声明 master 永停 1.3.8 |
| 内核补丁 | `kernel_patches/50_add_susfs_in_kernel-{4.9,4.14,4.19,5.4}.patch` | 仅 4 个旧内核版本 |
| KernelSU 补丁 | `kernel_patches/KernelSU/10_enable_susfs_for_ksu.patch` | 增加 KSU_SUSFS 配置和 prctl 命令 |
| 工作树状态 | 本地目录无独立 `.git` 元数据 | 不能给出可信 commit；按下载目录审计 |

该目录不是当前 `gki-android16-6.12` 分支（后者已单独审计），而是历史版本的源码、补丁、
userspace 工具和演示模块。README 自身警告：不同内核版本甚至同版本的补丁可能不同，
使用者需要为目标内核重新制作补丁。

### `sus_path` 的真实覆盖方式

该版本不是独立 LKM。补丁把 `fs/susfs.c` 和 `include/linux/susfs.h` 编入目标内核，并在
`fs/namei.c`、`fs/open.c`、`fs/readdir.c`、`fs/stat.c`、`kernel/sys.c`、`mm/memfd.c`、
`fs/proc/*` 等内部入口加入条件调用：

- `do_filp_open`、truncate、faccess、mknod、mkdir、rmdir、unlink、symlink、link、rename
  等路径通过 `susfs_sus_path_by_filename()` 检查；命中后按 syscall family 返回 `-ENOENT`、
  `-EROFS`、`-EACCES` 或 `-EXDEV`，模拟“该对象不存在/不可修改”。
- `vfs_getattr` 在路径级返回错误；`cp_new_stat` 通过 `susfs_sus_kstat()` 改写 inode、dev、
  nlink 和时间；`filldir64` 通过 `susfs_sus_ino_for_filldir64()` 按 inode 号跳过目录项。
- `/proc/self/mounts`、`mountinfo`、`mountstat` 可过滤用户定义挂载路径；可选实验功能重排
  mount id/parent id。`/proc/self/maps`、`smaps`、`map_files` 可改写 pathname、ino、dev、
  pgoff 和权限；`/proc/self/fd` 链接可改名；`uname` 可伪造；指定 memfd 名称可被拒绝。
- `try_umount` 不是隐藏本身，而是按 UID 在非 init mount namespace 中卸载 overlay，且只认
  overlay 文件系统；它是 KernelSU mount 清理的前置动作。

这些入口覆盖了 PathGuard HideLab 中最常见的 `open/stat/access/readdir/readlink/rename` 和
检测痕迹，但仍是“入口集合 + 字符串规则”，不是统一的 VFS 对象策略。

### 数据模型、作用域和明显边界

- `st_susfs_sus_path` 只有 256 字节路径和可选 `target_ino`，全局链表最多没有统一事务版本；
  userspace 通过 `prctl(KERNEL_SU_OPTION, CMD_SUSFS_*, ...)` 把结构交给 KernelSU，再由
  `core_hook.c` 分发到 SUSFS 函数。
- `uid_matches_suspicious_path()` 和 kstat 直接采用 `current_uid().val >= 2000`，mount id
  重排采用 `>= 10000`。因此 root/system UID 默认不隐藏，规则主体不是 package、user/profile、
  SELinux domain 或 mount namespace；同一 UID 的多个进程不能细分策略。
- `susfs_sus_path_by_filename()` 先对 `name->name` 做 `kern_path()`，再用 `d_path()` 与
  256 字节绝对字符串精确比较。相对路径、`dirfd` 语义由内核解析后才可能命中；不存在的
  目标、路径别名、bind mount/rename 后对象和跨 namespace 生命周期需要 userspace 重新添加
  或更新，规则不会按 inode 自动跟踪。
- `filldir64` 只传 inode 号给 `susfs_sus_ino_for_filldir64()`，没有设备号或 superblock
  比较；不同文件系统复用 inode 号时可能误隐藏无关目录项。kstat 也按 inode 号查找，存在
  同样的跨设备碰撞。
- `susfs_sus_path_by_path()` 每次分配 `PAGE_SIZE` 缓冲并调用 `d_path()`，随后线性遍历链表；
  规则越多，所有相关系统调用的路径开销越高，没有 hash/Bloom/RCU 读路径优化。

### 并发、内存和错误语义审计

`susfs.c` 的写入函数使用 `copy_from_user()`、`kmalloc(GFP_KERNEL)`，然后在自旋锁内把节点
加入链表；但大量读路径使用 `list_for_each_entry_safe()`，没有获取同一个 `susfs_spin_lock`
或使用 RCU。历史实现基本没有删除普通规则节点，不能因此把“无删除”当作完整并发证明：
并发插入期间读者可能观察到未完成链接或字段写入，内核模块/补丁也没有 lockdep/KASAN/压力
测试证据。

用户结构中的字符串来自固定长度数组。代码依赖 `strcmp`/`strncpy` 的 NUL 终止，但 add
路径没有对超长、未终止字符串统一拒绝；恶意或损坏的 root userspace 输入可能导致越界读或
非预期匹配。返回值大量使用 `1` 表示失败而非标准负 errno，KernelSU 命令层需额外转换，
不利于 PathGuard 统一错误分类。

### 与 Hide 1.0 的关系

**可借鉴：**

- 在内核内部入口统一返回不同 syscall family 的错误码，使“隐藏/禁止修改/跨设备拒绝”在
  行为上接近真实文件系统错误；
- 同时覆盖 `vfs_getattr`、`filldir64`、`/proc` 输出、maps、fd-link 和 uname，提醒 HideLab
  不能只测 `open()`；
- 将 kstat、maps 和 mountinfo 作为独立检测面，并提供静态/动态更新命令；
- 用 Kconfig 功能开关拆分风险特性，明确 `mnt_id_reorder`、proc fd、memfd 等实验项。

**不能直接复用：**

- 本版本没有 6.12 适配，不能用于当前 Redmi K90 Pro Max；必须改写到目标内核内部 API，
  不是通用 `.ko` 的替代品；
- UID 数值阈值和绝对路径字符串不能表达 PathGuard 的 package/profile/namespace 策略；
- inode-only 的目录/kstat 匹配会产生跨文件系统碰撞，且对 rename/rebind/已打开 fd 没有对象
  生命周期保证；
- 链表读路径缺少明确 RCU/锁协议，`copy_from_user`、`kern_path` 和每次 `PAGE_SIZE` 分配
  使高频路径的稳定性与时延没有证明；
- `sus_path` 只改变若干入口的错误码，不会抹除底层 inode、已有 fd、page cache 或其它未打补丁
  的内核接口；它仍可能被 proc、ioctl、uring、文件系统专用入口或内核内部调用绕过。

### 本轮审计状态

结论等级：**历史 SUSFS 1.3.8 是“需要内核源码改造的多入口隐藏/反检测补丁”，对研究
HideLab 检测面很有价值，但不是当前 6.12 设备可用实现，也不提供通用 LKM ABI。其路径隐藏
覆盖面明显大于单一 syscall hook，却仍受 UID/字符串/inode 语义、并发同步和未覆盖入口限制；
不准入当前 Hide 1.0，不授权在 myron 上直接套用。**

## 轮次 13：`susfs4ksu-module-1.5.2`

### 来源与版本

| 项目 | 本地目录 | 版本/证据 |
|---|---|---|
| KernelSU 模块封装 | `refer/hide-refer/susfs4ksu-module-1.5.2` | `module.prop`: `v1.5.2-R28`，versionCode `105002028` |
| 上游模块 | `sidex15/ksu_module_susfs` | `v1.5.2+` release/CI 链接 |
| 内核代码 | 本目录不存在 `fs/susfs.c`、Kconfig 或内核 patch | 能力依赖外部 SUSFS patched kernel |
| 预编译工具 | `tools/ksu_susfs_arm64`、`tools/sus_su_arm64` | SHA-256 分别为 `8B9FA0174AB9BC95910258F935628DFB9B551AE9B5074BA0EB1FA85E06590641`、`267BAE56A96B358D21E6886C0A793C827F188F3CFB3420901C9B0C622FECD7CC` |

本轮审计的是完整模块目录、启动脚本、配置样例、WebUI、CI 文件和预编译 ELF；没有在
设备上安装模块、执行 `resetprop`/`mount`/`ksud`，也没有从网络更新二进制。目录没有独立
`.git` 元数据，版本以模块文件和 CHANGELOG 为准。

### 它真正提供的内容

该项目本身不实现 VFS 隐藏。它安装两个 userspace 工具到 `/data/adb/ksu/bin/`：

- `ksu_susfs`：通过 KernelSU 的 `prctl`/supercall 协议调用内核中已经编译的 SUSFS 命令，
  包括 `add_sus_path`、`add_sus_path_loop`、`add_sus_mount`、`add_open_redirect`、
  `add_sus_kstat(_statically)`、`add_sus_map`、`add_try_umount`、`set_uname`、
  `hide_sus_mnts_for_all_procs` 等；
- `sus_su`：旧版 SUSFS 的 root shell/UID 隔离辅助工具。模块在检测到 SUSFS v2.0.0+ 时
  明确跳过安装，因为该接口已弃用。

内核侧的路径隐藏、目录过滤、kstat/maps/fd-link/uname 欺骗和 open redirect 均不在此目录；
它们来自外部 SUSFS 内核补丁（历史 1.3.8 或当前 2.x 分支）。因此必须严格区分：

```text
susfs4ksu-module-1.5.2 = 配置/编排/展示/工具分发
SUSFS patched kernel  = 实际隐藏和欺骗语义
```

### 启动编排和配置模型

- `customize.sh` 只接受 KernelSU 环境，检查 `/data/adb/ksu/bin`，从 zip 解出 arm64 工具，
  读取内核返回的 SUSFS 版本；若网络可用，会从 `sidex15/susfs4ksu-binaries` 下载最新
  `ksu_susfs_arm64`，仅以“下载后能执行”和远端实时 SHA-256 比对作为判断，不使用签名或
  固定版本锁定，然后删除模块内 `tools/` 和安装脚本。
- `post-fs-data.sh` 设置/检测 SUSFS 功能，按配置启用“隐藏所有进程/非 su 进程的 SUSFS
  mount”、`sus_su` 和日志；`post-mount.sh` 把 `sus_mount.txt`、`try_umount.txt` 的路径
  交给内核/KSUD；`boot-completed.sh` 再处理 `sus_path.txt`、`sus_path_loop.txt`、
  `sus_open_redirect.txt`、maps/kstat JSON、zygote mount 清理和统计日志。
- 默认配置包含 `hide_sus_mnts_for_all_or_non_su_procs=1`、`hide_loops=0`、
  `auto_try_umount=0`、`spoof_uname=0` 等；路径文件采用逐行文本，open redirect 使用
  `original_path redirected_path execute_on [uid_scheme]`，kstat 使用 WebUI 生成的 JSON。
- 通过 `susfs show version`/`show enabled_features` 判断 v1.5.x、v2.0.x、v2.1.x 的功能
  差异。脚本为旧版/新版 sus_path layout、`CONFIG_KSU_SUSFS_*` 名称和 `try_umount` 行为
  写了许多版本分支，说明协议并非稳定通用 ABI。

### 真实能力与限制

- 当设备内核已经启用 SUSFS 时，该模块可以批量注册路径隐藏和 loop 规则，隐藏/改写 mount
  表，重定向打开结果并配套伪造 kstat；这能覆盖 HideLab 的路径、目录、属性、`/proc` 和
  root 检测样本。
- `add_sus_path` 的最终作用域、UID 阈值、`filldir64` inode 匹配、rename/rebind 和已有 fd
  行为由内核版本决定，模块脚本自身无法扩大覆盖面，也无法把路径规则升级为稳定对象身份。
- `try_umount` 会实际卸载 overlay/bind mount，属于挂载编排而不是“无挂载隐藏”；脚本根据
  `/proc/1/mountinfo` 的启发式 ID 范围（旧版 100k/300k、新版 500k/2b）收集目标，误判会
  造成模块失效或影响正常挂载。
- `service.sh`/`boot-completed.sh` 会使用 `resetprop` 修改 verified boot、debuggable、
  encryption、recovery、qemu 等系统属性；这些属于反检测副作用，不应与文件隐藏结果混为一谈，
  也不应在 HideLab 的“文件语义正确性”通过条件中自动计入。
- 启动阶段大量 `grep`/`awk`/`stat`/`find` 和等待循环，依赖模块安装顺序、zygote 时序、
  `/mnt` 可写性以及其它模块的 mount 结果；配置的成功只说明命令被发出，不能证明目标 UID
  看到的 VFS 视图一致。

### 供应链、兼容性和故障边界

1. 项目 README 明确要求“自定义内核已打 SUSFS patch”，并建议 SUSFS 1.5.2+；没有任何
   针对 Redmi K90 Pro Max `myron`、`6.12.23...g16e473de48a3...` 的内核加载或 HideLab
   回归证据。
2. `ksu_susfs_arm64` 是预编译、压缩/剥离的黑盒工具；本地只记录 SHA-256，无法从中恢复
   内核 `Module.symvers`、SUSFS 结构布局、KMI CRC、CFI/LTO 或签名信息。安装时的“实时下载
   + SHA 比对”存在远端内容变化和 TOCTOU 风险，且没有密码学签名/版本 pinning。
3. 模块只面向 KernelSU，`customize.sh` 会拒绝非 KernelSU 环境；它不提供 APatch、Magisk
   或普通 Android 服务接口。`sepolicy.rule` 还会增加 zygote/netd 的搜索和 unmount 权限，
   是系统安全策略变更，必须单独审计。
4. 配置持久化会把默认文件复制到 `/data/adb/susfs4ksu`，升级时按键名追加；旧键、格式错误
   或手工编辑内容可能跨版本残留。脚本对 JSON 采用受限 awk 解析，不是通用 JSON parser。
5. 检测不到 SUSFS 时，`boot-completed.sh` 会修改模块描述并创建 `disable`；这是一种用户态
   失败指示，不是内核能力探针，更不是设备准入证据。

### 与 Hide 1.0 的关系

**可借鉴：**

- 将内核功能探测（版本、enabled features、variant）和启动阶段动作分离；
- 通过独立路径文件、open redirect 表和 kstat JSON 管理规则，便于 HideLab 生成和复现测试；
- 对不同 SUSFS 版本/KernelSU supercall 代际进行能力协商，并在缺失功能时禁用相应特性；
- 在 boot-completed 记录规则数量、mount 数量和错误日志，作为自动化采集的输入。

**不能直接复用：**

- 它没有内核实现，不能绕过当前 myron 缺失的 SUSFS/精确 ABI，也不能作为 Hide 1.0 后端；
- shell 文本配置、启发式 mountinfo 解析和 UID/版本分支不能替代原子策略、package/namespace
  身份和对象生命周期；
- 远端预编译二进制的供应链与 ABI 证据不足，不能把“命令执行成功”当作隐藏通过；
- `resetprop`、mount/umount、sus_su 和反检测属性修改必须与文件隐藏测试隔离，否则会污染
  HideLab 结果并掩盖根因。

### 本轮审计状态

结论等级：**`susfs4ksu-module-1.5.2` 是成熟的 KernelSU userspace 编排和配置界面，能把
SUSFS 内核功能应用到大量反检测场景，但本身不包含任何内核隐藏代码。它可作为 HideLab 的
规则生成器、版本/能力探针和“矛”的自动化驱动，却不能提供通用性、ABI 兼容性或 Hide 1.0
后端；当前 myron 必须先证明精确 SUSFS 内核集成和设备级回归，且应把属性伪造、挂载卸载和
文件语义分别验收。**

## 轮次 14：`BRENE-main`

### 来源与版本

| 项目 | 本地目录 | 版本/证据 |
|---|---|---|
| KernelSU/SUSFS userspace 模块 | `refer/hide-refer/BRENE-main` | `module.prop`: `v0.0.55`，versionCode `55` |
| README 声明的依赖 | 同上 | 仅支持 `susfs4ksu v2.2.0+`；要求 SUSFS 已打入内核 |
| 内核源码/补丁/Kconfig | 同上 | 未发现 `fs/susfs.c`、`*.patch`、Kconfig、`Module.symvers` 或构建脚本 |
| 预编译控制工具 | `tools/susfs` | ELF64 AArch64 PIE，SHA-256 `DB802BFBB8286D1176AE3C551EC6950EEB1046F12FC6C01182160FA35C9E491E` |

本轮只做离线源码、脚本和 ELF 静态审计；没有安装模块、执行 `ksud`/`resetprop`/`mount`，也没有
加载任何内核模块。`tools/susfs` 的动态依赖是 Android `libc.so`、`libm.so`、`libdl.so`，
导入包含 `syscall`、`getuid`、`realpath`、`stat` 等，字符串中含有 `add_sus_path`、
`add_sus_map`、`set_uname`、`hide_sus_mnts_for_non_su_procs` 等 CLI 帮助文本；本地没有可复现
构建所需的源码、编译参数或签名证明。

### 项目实际架构

BRENE 是一个 KernelSU 模块，职责是把配置转换成外部 SUSFS 内核命令：

```text
customize.sh
  └─ 安装 tools/susfs -> /data/adb/ksu/bin/susfs
     建立 sus/ksu_susfs 兼容链接、初始化 /data/adb/brene 配置
post-fs-data.sh
  └─ cmdline/bootconfig、avc、SUSFS mount、uname 等全局设置
boot-completed.sh
  └─ 路径/path_loop、maps、mount/umount、PTY、属性伪造和日志
```

内核侧的路径过滤、目录枚举、kstat/maps 处理、open redirect、mount 视图和 uname 修改均来自
外部 SUSFS patched kernel。BRENE 自身不能在普通 GKI 或当前 myron 内核上增加这些能力。

### 安装与启动时序

`customize.sh`（第 11--34 行）只接受 KernelSU/fork、`arm64`，并要求
`KSU_KERNEL_VER_CODE >= 32336`；这只是 KernelSU 版本门槛，不是 KMI、`uname -r`、厂商
commit 或符号 CRC 校验。脚本把一个预编译 `susfs` 复制到 `/data/adb/ksu/bin/`，然后建立
`sus` 和 `ksu_susfs` 符号链接。它会创建 `/data/adb/brene` 下的四个逐行规则文件，并把新配置键
追加到已有 `config.sh`；已有键不会被迁移或类型校验。

安装时还会对 `/data/adb/modules/susfs4ksu` 和 `/data/adb/modules/susfs_manager` 写入
`disable`（第 47--53 行），这是模块间有副作用的冲突处理，不是能力协商。随后根据当前
`uname -r` 和 `ksud boot-info current-kmi` 生成一个伪造 release，并硬编码
`-9-g690101101069`（第 88--94 行）。在目标设备上这会与真实
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` 不一致，不能作为 ABI 修复。

`post-fs-data.sh`（第 87--169 行）按 SUSFS variant 选择 `/proc/bootconfig` 或
`/proc/cmdline` 的副本，替换 warranty/verified-boot 字段；然后启用 AVC 日志伪造、非 su 进程
的 SUSFS mount 隐藏、uname 伪造和 SUSFS 内核日志开关。其余 kstat、open redirect、模块镜像
隐藏示例大多是注释，实际规则主要在后续 boot-completed 阶段下发。

### `boot-completed.sh` 的隐藏动作和真实作用域

- 等待 `/sdcard/Android` 出现后，删除 SUSFS 路径重定向使用的 `..5.u.S` 哨兵，并启动
  `inotifyd` 反复删除四个外部存储位置的同名项（第 85--103 行）。源码明确承认：若应用拥有
  `MANAGE_EXTERNAL_STORAGE` 并能写入目标路径，单纯 `sus_path` 会被重新创建；没有 HMA vold
  app-data 支持时还存在 zwc 旁路。
- 把非标准 `/sdcard`、非标准 `/sdcard/Android`、`/data/local/tmp` 和少数 root/recovery
  应用目录注册为 `add_sus_path_loop`；固定的 recovery 脚本使用 `add_sus_path`（第 110--236
  行）。这是按启动时 glob 快照和规则循环的路径列表，不是按 inode、mount namespace 或
  package identity 建立的稳定对象策略。
- 从 `custom_sus_map.txt`、`custom_sus_path.txt`、`custom_sus_path_loop.txt`、
  `custom_sus_mount.txt` 逐行读取规则（第 238--276 行），分别调用 `add_sus_map`、
  `add_sus_path(_loop)` 和 KernelSU `kernel umount add -f 2`。
- 扫描 `/data/adb/modules` 下的 system 文件和 `.so`，调用 `add_sus_map` 隐藏 maps 痕迹；
  随后按 `/proc/1/mountinfo` 的正则和字段 5 收集挂载点并加入 KernelSU umount 列表（第 278--
  352 行）。源码特别要求目标文件的 device number 与全局 mount namespace 一致，否则
  `sus_map` 对目标进程不可见；这属于脆弱的 namespace/设备号前提。
- 将 `/dev/pts/0..9`、`framework-res.apk`、Lineage 路径加入 path/map 规则（第 354--446
  行），并通过 `resetprop` 大量伪造 `ro.debuggable`、verified boot、加密、ADB、qemu、锁定
  状态、fingerprint 等属性（第 377--437 行）。这些是反检测属性副作用，不等于文件语义隐藏。

### 静态缺陷与旁路

1. **规则枚举不稳健。** `for i in /sdcard/*`、`for x in $(find ...)` 和 `for i in $(find ...)`
   依赖 shell word splitting；包含空格、换行、通配符或非 UTF-8 名称的路径会被拆分或重新展开，
   造成漏隐藏、错隐藏或把错误参数传给 SUSFS。没有 `set -e`、集中错误收集或规则数量/命中率
   校验，日志开启时记录的只是命令调用结果。
2. **“成功”不是语义证明。** `brene_sus_path*`/`brene_sus_map` 只检查 CLI 退出码；脚本不验证
   目标 UID 的 `open/stat/readdir/readlink/proc maps/mountinfo` 是否同时满足一致的结果，也不
   覆盖已有 fd、rename/rebind、io_uring、文件系统专用 ioctl 等入口。
3. **固定伪造值会产生交叉矛盾。** `config_uname_spoofing` 默认开启，却生成固定的
   `g690101101069` release；属性脚本同时写入 Xiaomi、Realme、Pixel 风格键值。检测方可以把
   `uname`、bootconfig、ro 属性、内核模块和实际行为交叉比对，发现伪造而不是被隐藏。
4. **WebUI 是高权限 shell 控制面。** `webroot/script.js` 用模板字符串把自定义 uname 直接拼入
   `susfs set_uname` 命令（第 314--319 行），规则编辑器把用户文本写入 heredoc（第 385--417
   行），没有长度、换行、`UNIQUE_EOF` 或 shell 元字符校验；恶意输入可改变命令边界或配置文件。
   “启用/禁用全部 KSU 模块”按钮还会对每个模块批量删除/创建 `disable` 文件（第 296--307 行）。
5. **生命周期和安全策略影响未隔离。** `inotify.sh`/卸载脚本会删除共享存储哨兵或整个
   `/data/adb/brene`；`sepolicy.rule` 放宽 zygote/netd 对 `adb_data_file`、`apk_data_file` 和
   `shell_data_file` 的搜索权限。它们必须作为独立系统副作用测试，不能混入 HideLab 文件隐藏
   通过条件。

### 与当前设备和 Hide 1.0 的关系

BRENE 没有对 Redmi K90 Pro Max / `myron` / `6.12.23-android16-5-g16e473de48a3-abogki462654244`
的 KMI、`Module.symvers`、CFI/LTO、签名或 HideLab 回归证据。它的 arm64 和 KernelSU 版本检查
无法替代精确 ABI 准入；`tools/susfs` 的 AArch64 动态 ELF 也不能反推出内核结构布局或符号 CRC。

**可借鉴：** 将 kernel feature probe、规则文件、启动阶段编排和日志分离；用
`show version/variant/enabled_features` 作为 HideLab 的环境记录；把 path、path_loop、map、
mount、kstat、open redirect 分成不同测试族。

**不能直接复用：** shell glob/逐行配置、UID/namespace 隐含条件、启发式 mountinfo、属性伪造、
实际 umount、共享存储删除和未签名预编译工具都不能作为 Hide 1.0 的正确性或通用性基础。BRENE
只能作为“矛”的 SUSFS 规则生成器和能力探针，不能作为 Hide 1.0 后端，也不能授权在当前 myron
设备加载。

### 本轮审计状态

结论等级：**`BRENE-main` 是功能覆盖较广的 SUSFS/KernelSU 用户态编排模块，但所有真正的
VFS 隐藏能力都外置于 patched kernel。其脚本明确记录了 MANAGE_EXTERNAL_STORAGE、zwc、
mount namespace/device number 等旁路，且存在 shell 解析、伪造值不一致、WebUI 高权限输入和
退出码假成功问题。它对 HideLab 的价值是提供可复现的规则/反检测测试样本；对当前 myron
没有 ABI、源码或设备回归证据，Hide 1.0 继续保持不准入。**

## 轮次 15：`SukiSU-Ultra-main` 与当前设备 LKM 实证

### 来源与版本

| 项目 | 本地目录/设备证据 | 结论 |
|---|---|---|
| SukiSU Ultra 源码 | `refer/hide-refer/SukiSU-Ultra-main` | 本地下载快照不含独立 `.git` 元数据，无法从目录证明 upstream commit；`cc69259a...` / `feature/pattern-redirect-v6` 属于 PathGuard 父仓库，不能作为 SukiSU 版本证据 |
| KernelSU manager | `manager/` | 本地源码包含 LKM 状态、KPM、SUSFS 管理和 bugreport 采集 |
| KernelSU 内核模块源码 | `kernel/` | C 内核实现，包含 syscall/LSM hook、mount namespace、file wrapper、符号解析和 supercall |
| 目标设备 | `adb` 只读采集 | `myron` / `25102RKBEC` / `Redmi K90 Pro Max`，内核 `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` |
| 设备运行模式 | `/proc/modules` | `kernelsu 200704 1 - Live ... (O)`；确认是可加载内核模块（LKM），不是 built-in |
| 设备 KernelSU | `ksud` | `ksud 4.1.3`，`debug version` 返回内核版本码 `40796`，`boot-info current-kmi` 返回 `android16-6.12` |
| 设备 SUSFS | `ksud susfs` | `status=false`、`version=unsupport`、features 为空；当前运行内核没有 SUSFS 能力 |

本轮仅执行只读 `adb shell` 查询和本地源码审计，没有执行 `insmod`、`ksud insmod`、KPM 加载、
卸载、刷写 boot 或修改设备配置。

### SukiSU 的三条集成路线

`docs/guide/how-to-integrate.md` 明确区分：

1. **GKI KPROBES LKM**：要求 `CONFIG_KPROBES=y`，默认用于 GKI；内核树不必把 KernelSU
   代码编译进 `vmlinux`，由 `kernelsu.ko` 加载。
2. **built-in/manual hook**：把 `kernel/` 接入目标内核源码，通过 `CONFIG_KSU=y` 编入内核；
   non-GKI 也可以走这条路线，但必须有可启动的公开内核源码。
3. **tracepoint hook**：要求 `CONFIG_KSU_TRACEPOINT_HOOK=y`，需要对 `fs/exec.c`、`fs/open.c`
   等入口添加 trace 调用；它是内核源码改动，不是普通 LKM 的自动能力。

仓库的 `kernel/setup.sh` 实际只是在目标树的 `drivers/` 下建立 `kernelsu` symlink，并追加
Makefile/Kconfig 条目；脚本没有为厂商内核生成通用 ABI。README 也明确说无法为 non-GKI 提供
通用 boot image，前提是“开源、可启动的内核”。

### LKM 为什么能在 myron 上工作

本设备的 `/proc/config.gz` 只读结果包括：

```text
CONFIG_KPROBES=y
CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y
CONFIG_MODULES=y
CONFIG_MODVERSIONS=y
CONFIG_MODULE_SIG=y
CONFIG_CFI_CLANG=y
CONFIG_CFI_ICALL_NORMALIZE_INTEGERS=y
CONFIG_LTO_NONE=y
CONFIG_TRIM_UNUSED_KSYMS=y
```

SukiSU 内核模块在 `kernel/Kconfig` 中依赖 `KPROBES` 和 `EXT4_FS`；`kernel/Kbuild` 将
`kernel/` 的各子模块编译成 `kernelsu.ko`。在 `uapi/supercall.h` 中，内核返回的
`KSU_GET_INFO_FLAG_LKM` 明确表示模块模式；`kernel/supercall/dispatch.c` 在编译为 module 时
设置该标志，userspace `ksucalls.rs` 依据该标志区分 `lkm`/`built-in`/`late-load`。

这不是“忽略 ABI”。SukiSU 的兼容层有三层：

- `.github/workflows/ddk-lkm.yml` 按 `android12-5.10` 到 `android17-6.18` 的 **KMI 大类**
  使用对应 DDK 容器构建 LKM；`build-lkm.yml` 明确把 `android16-6.12` 作为独立 variant。
- `kernel/infra/symbol_resolver.c` 使用 `kallsyms_lookup_name`、`kallsyms_on_each_symbol`/
  `kallsyms_on_each_match_symbol` 解析运行时符号，并处理 KCFI/符号变体；`arm64/syscall_hook.c`
  查找 `sys_call_table` 和 `__arm64_sys_ni_syscall`，用一个 dispatcher 槽路由 syscall hook。
- `userspace/ksuinit/src/lib.rs` 在需要用户态装载时读取 `/proc/kallsyms`，为 ELF 未定义符号写入
  运行时地址，再调用 `init_module`；失败日志中如果内核只报 vermagic 不匹配，代码会在内存中的
  ELF buffer 上替换 `vermagic` 后重试，不会持久化改写原始 `.ko`。该加载器还要求
  `__versions` 为空，并检查未定义符号能在运行时内核符号表中找到。这是 SukiSU 的特定加载器
  行为，不能当作 PathGuard 可以修改 CRC、签名或 `__versions` 的许可；Hide 1.0 仍必须以原始
  设备 ABI、CFI、签名策略和行为回归为准。

因此，SukiSU 解决的是“同一 KMI 下，内核导出符号和 LKM 装载流程如何对齐”，不是“任何
6.12.23 模块都兼容”。用户从当前 slot `init_boot_b` 解出的 `kernelsu.ko` 记录为
`vermagic=6.12.76-4k-gae4e2f4f997e-dirty`、无签名、`__versions` size 为 0，并且设备配置中
`CONFIG_MODULE_SIG_FORCE` 未启用；这与通用 `android16-6.12` DDK + 加载期重定位的路线一致。
但是该文件大小（374200 bytes）与 `/proc/modules` 中已加载模块大小（200704）不同，可能是
压缩/未 strip/ramdisk 原文件与内存模块统计口径差异，二者是否为同一构建物仍须用 SHA-256、ELF
section 和加载日志核对，不能直接合并为一条二进制证据。

据此修正判断：**精确 `Module.symvers` 不是该设备采用 SukiSU 加载配方时的绝对装载前提**；
它仍是传统 Kbuild `MODVERSIONS` 校验、结构布局核对、符号 CRC 审计和高可靠 Hide 后端开发的
重要输入。缺少它时只能沿用已在设备上证明的空 `__versions` + kallsyms 运行时重定位路径，
并把函数原型、结构布局、CFI、未导出符号和实际 HideLab 结果列为独立硬门槛。

### SukiSU 内核代码对 Hide 的直接启示

SukiSU 自身不是 SUSFS，但它提供了可复用的内核工程骨架：

- `kernel/hook/arm64/syscall_hook.c` 对 syscall table 做受 mutex 保护的保存、替换和恢复，
  用 dispatcher 表管理多个 hook；卸载前恢复原始指针。
- `kernel/hook/lsm_hook.c` 在 6.12 使用 `static_calls_table`、`lsm_active_cnt` 和静态调用槽
  查找 LSM hook，记录原始函数并在退出时恢复；这比简单覆盖共享 `inode_operations` 更接近可
  审计的所有权模型，但仍依赖未公开/未稳定的厂商符号和结构布局。
- `kernel/feature/kernel_umount.c` 的“隐藏”是按 app UID 派生进程，在 zygote 子进程切换时调用
  `path_umount()` 卸载 KernelSU 模块挂载点。它解决的是 mount namespace 隔离，不是 VFS 路径
  `lookup/readdir/stat/open` 的对象级隐藏。
- `kernel/infra/file_wrapper.c` 为已有 `struct file` 创建 anon inode wrapper，并代理大量
  `read/write/read_iter/write_iter/poll/ioctl/mmap/splice/fsync/lock/fallocate/copy_file_range`
  等操作，同时持有原文件引用。这为 Hide 1.0 处理“已打开 fd 仍可用”提供了工程参考，但
  wrapper 的 SELinux SID、dentry 名称和异步操作仍需单独测试。
- `kernel/feature/uts_spoof.c` 只修改 `init_uts_ns` 的 release/version，是属性欺骗示例，不是
  文件隐藏；设备 `ksud susfs` 的 `unsupport` 也说明当前 SukiSU LKM 没有自动携带 SUSFS。

### 当前设备运行证据与边界

设备日志显示 KernelSU 正在拦截 `execve`、`faccessat`、`newfstatat`、`setresuid`，并在应用
UID 派生时执行 KernelSU mount 处理；这证明 LKM hook 已经工作。但 `ksud susfs status=false`
且没有 `susfs` 模块/功能输出，说明当前环境只能证明：

```text
SukiSU KernelSU LKM root  ✅
SUSFS VFS path hiding    ❌（当前未集成）
PathGuard Hide 1.0       ❌（尚未实现/准入）
```

设备还安装了 PathGuard Next 模块，但其状态为 disabled；本轮没有启用它，也没有改变设备状态。

### 对 PathGuard 的路线修正

“修改内核不可行”已经被当前设备实证否定；准确说法应改为：**修改/加载内核在 myron 上可行，
但要实现真正的 Hide，必须为 `android16-6.12` KMI 和该精确厂商构建建立独立的内核能力层。**

建议路线（按验证依赖排序）：

1. 保留 SukiSU 当前 KernelSU LKM 作为 root/namespace 基础，不把它误当作 Hide 后端。
2. 先复刻 SukiSU 的 `android16-6.12` DDK 构建与离线 ELF 检查，产出最小
   `pathguard_probe.ko`：确认 AArch64、undefined symbols、`.modinfo`、`__versions`、import
   namespace、CFI/LTO 编译选项，并实现与 SukiSU 相同的 kallsyms/vermagic 适配器；此阶段不接触
   设备、不修改模块文件、不宣称可加载。
3. 取得明确授权后，仅在 myron 上加载最小 probe，采集 vermagic、符号解析、CFI、签名、模块
   初始化/卸载和设备稳定性证据；失败必须按装载错误类别归档，不能直接进入 VFS 数据面。
4. probe 通过后，再引入固定设备、固定 parent/basename、固定 target UID/namespace 的最小
   VFS prototype，覆盖 lookup、`atomic_open`、readdir、stat/open 一致性、mutation、已有 fd
   和缓存失效；优先使用可回滚的 hook 所有权模型，不整体移植 Kasumi/NoMount。
5. 使用同一 HideLab 全矩阵回归；最后建立设备/KMI/内核 build fingerprint 白名单和 OTA 重新准入。
   只有所有核心 case、稳定性、签名/CFI/KMI 检查都通过，才允许激活 Hide 1.0。

### 本轮审计状态

结论等级：**`SukiSU-Ultra-main` 和设备实测共同证明，Redmi K90 Pro Max 上的 GKI LKM 内核扩展
是可行的；可行性来自 `android16-6.12` KMI 构建、运行时符号解析、KPROBES/LSM/syscall hook
和严格的设备级装载条件。SukiSU 的 KernelSU LKM 只提供 root、mount 隔离和检测面处理，当前
设备明确没有 SUSFS；它不能直接提供 Hide 1.0，但为 PathGuard 构建同一 KMI 下的独立 Hide LKM
提供了可执行的工程路线。精确 myron ABI、签名、CFI 和 HideLab 回归仍是硬性准入条件。**

## 轮次 16：PathGuard 离线 LKM adapter 实证

PathGuard 已使用 SukiSU 相同的
`ghcr.io/ylarod/ddk-min:android16-6.12-20260828` 构建最小 `pathguard_probe.ko`，并实现纯 C++20
离线 ELF adapter。该 adapter 不包含或调用 `init_module`，只在复制的内存 image 中把具备非零
地址的 `SHN_UNDEF` 改为 `SHN_ABS`，并把 `.modinfo` 重定位到新区域后写入目标 vermagic；原始
模块 bytes 保持不变。

GitHub Actions run `34736258439` 对真实 DDK ELF 验证通过：唯一 undefined symbol
`init_uts_ns` 被改为 `ABS 0xffffffc0826f87b8`，`__versions` size 为 0，目标 vermagic 写入成功，
模块无签名。地址来自 DDK `vmlinux` 测试 fixture，不是设备地址，因此 artifact 被命名为
`offline-adapted-do-not-load.ko`，不能作为设备加载输入。

本轮把“DDK 构建可用”和“loader ELF 变换正确”两项从假设提升为离线实证，但没有执行 ADB、
`init_module`、卸载或设备稳定性测试。下一闸门是 Android 受限 loader shell 与 myron 最小 probe
加载实验；该操作必须单独取得明确授权，且不能直接加载 VFS Hide prototype。

## 轮次 17：PathGuard 受限 Android loader shell

新增的 `pathguard_lkm_loader` 只实现设备侧 prepare 边界：读取模块、只读解析 `/proc/kallsyms`、
复用 `OfflineModuleAdapter` 在内存中适配并输出报告。CLI 不接受 adapted module 输出路径，适配后的
bytes 在返回时销毁。`--load` 是显式保留字，但当前会在任何文件读取前失败；编译产物没有
`init_module`/`finit_module` 引用，也没有 `kptr_restrict` 路径或修改逻辑。

实现同时增加 `KernelLogCursor`：非阻塞打开 `/dev/kmsg` 或 `/kmsg`，在建立游标时消费历史记录，
以后只返回新记录。这样将来只有“本轮 probe 加载失败后产生的日志”能传给
`ExtractRequiredVermagic`，不会从历史日志捡取其它模块的错误。当前 prepare-only 模式没有加载
动作，因此拒绝把历史 kmsg 当作 vermagic 来源，只接受单独记录并显式传入的设备值。

验证包括 Windows/MSVC host 构建、adapter 与 restricted loader 两组单测、测试 ELF 的完整
prepare 流程，以及 Android NDK 29 arm64/API 26 `-Wall -Wextra -Werror` 编译。Android 二进制使用
静态 libc++，仅依赖系统 `libm`、`libdl`、`libc`。本轮没有执行 ADB、加载/卸载 `.ko`、修改
`kptr_restrict` 或设备配置；因此结论仅为“设备侧 prepare 工具可构建且默认无加载能力”，不能提升
Hide 1.0 的准入状态。

## 轮次 18：本地 DDK probe 构建与离线回归

由于 GitHub Actions 的 ubuntu-latest runner 长时间未分配，最新排队运行已由用户取消；该运行没有进入任何 workflow step，不能归因于源码或构建失败。本轮改用本机 WSL2 Ubuntu，直接使用已下载的 android16-6.12 DDK tar、DDK clang r536225 和仓库中的最小 probe 源码完成本地构建。

本地构建从原始 tar 在 Linux 文件系统解包 DDK prepared tree，修正临时 Makefile 中容器内的 /opt/ddk/src/android16-6.12 路径，并以 ARCH=arm64、LLVM=1、LLVM_IAS=1 调用 Kbuild。第一次构建因主机默认选择 x86，第二次因本机没有 pahole 在可选 BTF 模块步骤失败；最终关闭 CONFIG_DEBUG_INFO_BTF_MODULES 的生成步骤后成功链接。这只影响调试 BTF，不改变 probe 的最小代码、目标架构或符号集合。

证据目录：

    build/device-evidence/pathguard-probe-local/20260913/

关键结果：

    ELF: ELF64 / little-endian / REL / AArch64
    undefined symbols: init_uts_ns（唯一）
    vermagic: 6.12.76-4k SMP preempt mod_unload modversions aarch64
    __versions size: 000000
    module signature: absent
    missing symbols against DDK vmlinux: none
    stripped SHA-256: f0fe7b783b1996b1318d904a138b9a024a326f6b55b118818e8be1f35cdbd26a
    unstripped SHA-256: 726c7f83fdcf742a9af5ce0b8ed4d13de71a90157e4c30156fd0702bba6a1c19

同一目录中的 kernel-symbol-fixture.txt 使用 DDK vmlinux 中的 init_uts_ns 地址，离线 adapter 验证结果为：

    vermagic_before=6.12.76-4k SMP preempt mod_unload modversions aarch64
    vermagic_after=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k SMP preempt mod_unload modversions aarch64
    relocated=1
    symbol=init_uts_ns index=52 address=0xffffffc0826f87b8

pathguard_probe.offline-adapted-do-not-load.ko 的地址来自 DDK vmlinux fixture，不是 myron 设备实时 /proc/kallsyms 地址，严禁加载。该结果只证明本地 Kbuild、ELF 检查和内存适配算法可以复现；不证明设备 insmod、初始化、卸载或稳定性。

Windows 宿主侧重新编译并通过：

    hide_loader_test
    restricted_loader_test

本轮仍未执行 ADB、init_module、finit_module、insmod、模块卸载、刷写 boot/init_boot 或修改设备配置。因此 Hide 1.0 继续保持 unsupported；下一步只能是取得单独明确授权后，使用真实设备 kallsyms 对最小 probe 做一次受控加载/卸载实验。

## 轮次 19：myron 最小 probe 受控加载与卸载

在用户确认后，本轮使用本地 DDK 构建的**未适配**最小 `pathguard_probe.ko`，通过设备已有的 SukiSU Ultra `ksud 4.1.3 insmod` 入口执行一次加载。没有使用 DDK fixture 生成的
`pathguard_probe.offline-adapted-do-not-load.ko`，没有加载 `pathguard_hide1.ko` 或任何 VFS prototype，也没有修改
`/proc/sys/kernel/kptr_restrict`、boot/init_boot 或设备配置。

设备和输入：

    product/device: 25102RKBEC / myron
    kernel: 6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
    root: uid=0(root), context=u:r:ksu:s0
    kptr_restrict: 2（保持原值；未改写）
    module: build/device-evidence/pathguard-probe-local/20260913/pathguard_probe.ko
    SHA-256: f0fe7b783b1996b1318d904a138b9a024a326f6b55b118818e8be1f35cdbd26a

实验步骤和结果：

1. 模块推送到 `/data/local/tmp/pathguard_probe.ko`，设备侧 SHA-256 与本地一致。
2. 加载命令 `/data/adb/ksud insmod /data/local/tmp/pathguard_probe.ko` 返回 0，输出
   `Loaded kernel module`；随后 `/proc/modules` 出现 `pathguard_probe 16384 0 ... Live`。
3. 本轮唯一相关内核日志为 `pathguard_probe: no extended symbol version for module_layout`。该提示与本 probe 的空
   `__versions` 设计一致；没有出现 vermagic mismatch、unknown symbol、invalid module、CFI 或签名拒绝。
4. 设备在加载后保持在线，`sys.boot_completed=1`，无重启迹象。
5. `ksud unload pathguard_probe` 被 4.1.3 CLI 正确拒绝（该子命令无模块名参数），未改变设备状态；随后使用
   `/system/bin/rmmod pathguard_probe` 返回 0，模块从 `/proc/modules` 消失。
6. 卸载后设备仍在线，`sys.boot_completed=1`，没有新增 probe 错误日志。

证据目录：

    build/device-evidence/pathguard-probe-local/20260913/device-load/

其中保存了设备型号、`uname`、kptr_restrict、加载/卸载前后模块列表、相关 dmesg、设备侧哈希、uptime、
boot 完成状态和 ADB 状态。

本轮结论严格限定为：**在当前 Redmi K90 Pro Max / myron 运行内核上，使用 SukiSU Ultra 已验证的加载入口，
PathGuard 的最小无 hook LKM probe 可以成功加载、初始化并卸载。** 这证明的是最小 LKM 装载能力，不证明
VFS 隐藏、HideLab 行为矩阵、CFI/KMI 白名单、OTA 重新准入或 Hide 1.0；Hide 1.0 继续保持 `unsupported`。
## 轮次 20：只读 VFS capability probe 的设计与本地构建

在进入真实 VFS 数据面前，新增独立的 pathguard_vfs_cap_probe.ko。它固定到 myron 的
android16-6.12 目标，仅把后续 prototype 所需的导出符号保留为 ELF undefined references，并在模块初始化时
通过只读 misc 设备报告符号能力位图。模块不注册 kprobe、不调用 VFS helper、不改写 inode/file_operations、
不安装策略，也不执行路径隐藏。

本地使用与最小 probe 相同的 DDK source/output 和 Android clang r536225 构建；最终 ELF 证据位于
build/device-evidence/vfs-capability-probe-local/20260913/：

    ELF: AArch64 relocatable module
    undefined: lookup_one_len, vfs_create, vfs_mkdir, vfs_mknod, vfs_symlink,
               vfs_unlink, vfs_rmdir, vfs_link, vfs_rename,
               register_kprobe, unregister_kprobe
    extra DDK dependency: alt_cb_patch_nops
    vermagic: 6.12.76-4k SMP preempt mod_unload modversions aarch64
    __versions / __version_ext_crcs: present but empty (local DDK output)
    module signature: absent
    SHA-256: a5568baebdb371b0f7f82d3380c69bb3578273872ad60006814a7941f1c4d8ef

源码静态检查确认没有 register_kprobe()、VFS helper 调用、inode/file operation 表写入或
iterate_shared/d_revalidate 实现。该模块即使在设备上报告 READY，也只代表符号链接和加载期解析成功，
不能代表任何 Hide 语义或 HideLab 通过。设备加载需要单独授权，且仍限制为一次加载、读取状态、卸载事务。
## 轮次 21：myron 只读 VFS capability probe 设备验证

使用本轮本地构建的 pathguard_vfs_cap_probe.ko 在 myron 上执行一次受控加载。设备侧哈希与本地
一致：a5568baebdb371b0f7f82d3380c69bb3578273872ad60006814a7941f1c4d8ef。SukiSU Ultra
ksud 4.1.3 insmod 返回 0，模块进入 Live，创建 /dev/pathguard_vfs_cap_probe。

通过 Android arm64 状态读取器执行一次 ioctl，返回：

    abi_version=1 size=160 state=1 last_error=0
    available_ops=0x3ff required_ops=0x3ff
    release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k

这证明设备加载期解析了 capability probe 声明的全部十项符号能力。随后 /system/bin/rmmod
pathguard_vfs_cap_probe 返回 0，模块从 /proc/modules 消失，设备节点消失，设备保持在线且
sys.boot_completed=1。原始证据位于 build/device-evidence/vfs-capability-probe-local/20260913/device/。

该结果只推进“符号存在且可由 SukiSU 加载期解析”的设备闸门，不证明 lookup、readdir、atomic_open、
mutation 或 cache 的语义正确性。下一步仍需设计并审计固定设备范围的真实 VFS prototype；Hide 1.0
继续保持 unsupported。

## 轮次 22：只观测 VFS kprobe trace probe 本地构建

为把“导出符号可解析”与“kprobe 入口可观测”分开验证，新增
`experimental/hide-vfs/trace/` 独立探针。它固定注册六个入口：
`lookup_one_len`、`vfs_create`、`vfs_mkdir`、`vfs_unlink`、`vfs_rmdir`、`vfs_rename`。
pre-handler 只执行 `atomic64_inc()` 并返回 0；不访问 `pt_regs`，不调用 printk，不改写
返回值或任何 VFS 对象。注册失败时逆序清理，卸载时同样逆序注销。misc ioctl 只读导出
每个入口的命中数、`nmissed`、注册状态和运行内核 release。

使用与 capability probe 相同的 DDK source/output 和 Android clang r536225，本地构建成功：

    ELF: AArch64 ET_REL
    vermagic: 6.12.76-4k SMP preempt mod_unload modversions aarch64
    __versions / __version_ext_crcs: present but empty
    module signature: absent
    stripped SHA-256: 0fe62c9a92fea52f371ed8678457c5d2815ee4b47e4f9e0864ff6e1854602cce

该产物是本地 DDK 编译证据，不是 myron 加载证据；本地 release 与设备
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` 不同，不能直接加载。配套
Android arm64 `status_reader` 已编译，待单独授权后用于一次加载、计数读取、受控文件操作和
卸载事务。设备实验尚未执行，Hide 1.0 仍为 unsupported。

## 轮次 23：myron kprobe trace probe 设备验证

在明确确认后，对本地构建的 `pathguard_vfs_trace_probe.ko` 执行一次受控加载、观测和卸载。
设备为 `25102RKBEC / myron`，运行内核为
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，使用 SukiSU Ultra
`/data/adb/ksu/bin/ksud insmod`。设备侧哈希与本地 SHA-256
`0fe62c9a92fea52f371ed8678457c5d2815ee4b47e4f9e0864ff6e1854602cce` 一致。

加载成功，`/proc/modules` 显示模块为 `Live`，misc 节点创建成功。初始 ioctl：

    state=1 last_error=0 probes=6 registered=6
    all hits=0 nmissed=0

在专用临时目录执行 mkdir、touch、rename、rmdir、unlink 后，ioctl 计数为：

    lookup_one_len  hits=14 nmissed=0
    vfs_create      hits=0  nmissed=0
    vfs_mkdir       hits=2  nmissed=0
    vfs_unlink      hits=12 nmissed=0
    vfs_rmdir       hits=2  nmissed=0
    vfs_rename      hits=13 nmissed=0

其中 `vfs_create=0` 是重要观测：`touch` 的创建流程没有经过该通用 helper，后续必须把
`atomic_open` 和文件系统特定创建路径纳入 HideLab，而不是把单个 helper 的命中当作语义覆盖。

随后 `/system/bin/rmmod pathguard_vfs_trace_probe` 返回 0；模块从 `/proc/modules` 消失，
`/dev/pathguard_vfs_trace_probe` 消失，`sys.boot_completed=1` 且 ADB 设备保持在线。模块相关
dmesg 仅见注册成功信息；设备普通 dmesg 读取受权限/审计噪声限制，但没有本模块失败日志。
推送到 `/data/local/tmp` 的模块和读取器已清理。证据目录：
`build/device-evidence/vfs-trace-probe-local/20260913/device/`。

结论严格限定为：myron 当前内核支持这六个 kprobe 的注册、命中和注销；`nmissed=0` 说明
本次短事务没有丢失 probe。它不证明任何 VFS 隐藏或修改能力，Hide 1.0 继续保持
`unsupported`。

### 轮次 53：v8 加载与匹配 namespace INSTALL（2026-09-14）

v8 在 myron 上通过 SukiSU loader 加载，初始 `status` 为 `state=0/EOPNOTSUPP`，设备
稳定。启动新的 HideLab target（PID `19278`，UID `10549`）后读取其 mount namespace
`4026535993`，并在该 namespace 内创建测试目录、执行 INSTALL generation `2002`。
返回 `state=1`、`operation_mask=0x0fff`、parent inode `796616`，说明本轮已消除上次
因 target PID 退出导致的 namespace 失配。尚未执行 ENABLE。

### 轮次 51：v7 ENABLE/恢复失败（2026-09-14）

用户确认后执行底层 `hide1_control enable 2001`，返回 `state=2 (ACTIVE)`，设备当时
在线且未立即重启。由于 INSTALL 绑定的 target PID `19251` 已退出，随后启动的 HideLab
进程使用了新的 mount namespace `4026536057`，而 binding 仍为 `4026536046`；
HideLab 因观察者不匹配而正确放行，外部目标路径统计为 `27` 项可见、`36` 项返回错误，
不能判定为隐藏成功。

随后执行 `hide1_control disable`，ADB 立即断开；约一分钟后设备重新上线，uptime
重新计时，`/proc/modules` 中模块未加载。`/sys/fs/pstore/console-ramoops-0` 存在，
但当前权限和日志编码无法提取可读的 panic/call trace。该结果证明 operation-table
恢复/teardown 仍会触发设备重启，Hide 1.0 必须继续保持 `unsupported`，不得再次上机
ENABLE，直到离线修复并取得可观测的恢复证据。

### 轮次 50：v7 INSTALL 设备验证（2026-09-14）

在不执行 ENABLE 的前提下创建一次性测试目录，并以 HideLab target UID `10549`、
测试 PID `19251`、generation `2001` 执行 INSTALL。结果成功：

```text
state=1
target_uid=10549 target_pid=19251
target_mnt_ns=4026536046 generation=2001
operation_mask=0x0000000000000fff parent_inode=799429
release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

测试 Activity 随后退出，模块仍为 Live，设备未重启。该结果只证明 binding 校验、
namespace/UID 绑定和完整 operation mask 检查通过；由于尚未执行 ENABLE，没有产生
任何隐藏行为证据，产品状态仍为 `unsupported`。ENABLE 仍属于高风险步骤，需单独确认。

## 轮次 24：namei/FUSE coverage probe 源码核对与本地构建

上一轮 `touch` 未命中 `vfs_create`。对 android16-6.12 源码继续核对发现：`lookup_open()`
在目录 inode 存在 `.atomic_open` 时进入该 operation；FUSE 的目录 inode operations 明确绑定
`fuse_atomic_open`，目录 file operations 绑定 `fuse_readdir`，缓存重验使用
`fuse_dentry_revalidate`。因此新增独立只读 coverage probe，注册 `path_openat`、
`fuse_atomic_open`、`iterate_dir`、`fuse_readdir`、`fuse_dentry_revalidate`、`do_filp_open`。

模块不读取 `pt_regs`，handler 只执行 `atomic64_inc()`；没有拒绝、返回值修改、VFS helper
调用或 operation table 写入。六个符号名均保留在 ELF 数据中，`register_kprobe` 和
`unregister_kprobe` 为加载期依赖。本地结果：

    ELF: AArch64 ET_REL
    vermagic: 6.12.76-4k SMP preempt mod_unload modversions aarch64
    __versions / __version_ext_crcs: present but empty
    module signature: absent
    SHA-256: 1aaef8da70bb3b24b1916f480ff1e12d2a13846d09dca76e23b1bc45d37d5ac7

配套状态读取器已构建为 Android arm64 PIE，证据位于
`build/device-evidence/vfs-coverage-probe-local/20260913/`。设备是否保留并允许探测这些本地
符号仍未知，尚未执行加载。即使设备验证通过，也只用于确定 Hide prototype 的实际覆盖面，
不改变 Hide 1.0 的 `unsupported` 状态。

## 轮次 25：myron namei/FUSE coverage probe 设备验证

coverage probe 在 myron 上通过 `/data/adb/ksu/bin/ksud insmod` 加载，六项 kprobe 全部注册，
设备 release 与预期完全一致。首次状态读取已经出现大量系统全局命中，说明通用 namei probe
会观察整机活动，必须使用短窗口前后快照而不是绝对计数。

在 `/storage/emulated/0/Pictures/PathGuardCoverage-<run-id>` 内执行受控创建、读取、枚举、重命名
和删除。前后增量为：

    path_openat             +1327
    fuse_atomic_open        +1
    iterate_dir             +11
    fuse_readdir            +2
    fuse_dentry_revalidate  +143
    do_filp_open            +1327
    all nmissed             0

通用入口的增量混有设备后台任务，不能解释成测试自身的调用次数；但 `fuse_atomic_open` 和
`fuse_readdir` 从 0 增长，证明受控共享存储操作确实进入两条 FUSE 专有路径。一次重复 insmod
因模块已经 Live 返回 `EEXIST`，属于实验命令重复，不是注册失败，也没有改变已加载状态。

`rmmod pathguard_vfs_coverage_probe` 返回 0，模块和 misc 节点消失，临时 fixture、模块与读取器
均已删除，ADB 在线且 `sys.boot_completed=1`。相关 dmesg 仅见注册成功信息。原始证据位于
`build/device-evidence/vfs-coverage-probe-local/20260913/device/`。

结论：myron 当前内核同时保留并允许探测 namei/FUSE 内部入口；正式数据面必须覆盖
`.atomic_open`、`.iterate_shared` 和 dentry revalidation，不能把 `vfs_create` 或普通 lookup
当作完整代理。本轮依然没有修改 VFS 行为，Hide 1.0 继续保持 `unsupported`。

## 轮次 26：VFS operation-table preflight 本地构建

为避免直接复制 Kasumi/NoMount 的 operation-table shadow 代码，新增只读
`experimental/hide-vfs/preflight/`。模块使用 `kern_path` 解析一个绝对父目录，报告
文件系统名、父 inode/设备号以及 `lookup`、`.atomic_open`、`.iterate_shared`、所有父目录
mutation 回调和 dentry `d_revalidate` 的存在位图。它不输出地址，不改写 `i_op/i_fop/d_op`，
也不注册 kprobe；`SCAN`、`STATUS`、`CLEAR` 均受 mutex 保护。

本地使用 android16-6.12 DDK + clang r536225 构建成功。期间编译器拒绝旧式
`file_operations.iterate` 字段，确认目标 6.12 只支持 `.iterate_shared`，已移除该旧字段。
产物证据：

    ELF: AArch64 ET_REL
    vermagic: 6.12.76-4k SMP preempt mod_unload modversions aarch64
    module signature: absent
    SHA-256: 4e8c229a4562527327e2dda04c9ea27c67a57745339d67b3b764da4ca9e4c7e3

配套 Android arm64 `status_reader` 已构建。设备扫描尚未执行；即使扫描得到完整位图，也只
能证明目标目录适合进入 prototype 安装前检查，不能证明任何隐藏语义。

## 轮次 27：myron operation-table preflight 设备验证与状态修复

只读 preflight 首次加载前，`ksud` 路径曾短暂返回 inaccessible，确认模块没有进入
`/proc/modules` 后重新定位并加载成功。首次扫描得到 FUSE `0x0fff` 和 f2fs `0x0ffd`，但发现
`SCAN` 中 `memset` 同时清除了初始化时写入的 `kernel_release`。该问题不影响 operation-table
读取，却会产生不完整审计证据，因此没有接受首次结果。

修复方式是在每次成功 scan 重建状态时重新从 `init_utsname()->release` 填入 release。重新构建
模块，最终 SHA-256 为
`4e8c229a4562527327e2dda04c9ea27c67a57745339d67b3b764da4ca9e4c7e3`，卸载旧版本后加载修复版并
重复扫描：

    /storage/emulated/0/Pictures
      fs=fuse dev=0:280 inode=15771 mode=042770 mask=0x0fff
      lookup/atomic_open/iterate_shared/all mutation/d_revalidate present

    /data/local/tmp
      fs=f2fs dev=254:55 inode=600 mode=040771 mask=0x0ffd
      all required entries except dentry d_revalidate present

两项状态均包含精确设备 release。`rmmod pathguard_vfs_preflight` 返回 0；模块、misc 节点和设备
临时输入均已清理，ADB 在线且 `sys.boot_completed=1`。证据位于
`build/device-evidence/vfs-preflight-local/20260913/device/`。

结论：目标共享存储 FUSE 父目录具有 Hide 1.0 定义的全部 12 个可包装 operation，但这只是结构
准入条件，不是行为实现。后续 operation-table shadow 必须原子覆盖并可回滚全部 12 项，且仍需
HideLab 验证 observer、cache、mutation 和并发语义。

## 轮次 28：Kasumi 生命周期复核与 Hide 1.0 决策模型

继续审计 `kasumi_dirhijack.c`、`kasumi_fop_override.c` 和 android16-6.12 的 `fs.h`、
`dcache.h`、`fs/namei.c`、`fs/fuse/dir.c` 后，确认不能把 Kasumi 的 lookup/readdir 代码直接扩写
成 PathGuard 数据面：Kasumi 的 per-dentry shadow、SRCU/Tasks-RCU drain 和 operation table
发布/回滚值得借鉴，但其 hide 路径仍未包装 FUSE `atomic_open` 和全部 mutation。NoMount 覆盖面
更小，同样不能作为 12 项完整实现。

本轮先新增可复用的 `experimental/hide-vfs/model/hide_vfs_model.{h,c}`。该模型不解析路径、不接触
VFS 指针，只接受已解析的 parent identity、basename、observer 和 immutable generation，输出
pass、synthetic negative、omit、reject、keep cache 或 invalidate cache。target mutation 一律
输出 `ENOENT + no original call`；rename 同时检查 source/destination；同 UID 不同 namespace 和
root oracle 均不命中。缓存决策确保 synthetic negative 不能跨 observer 或 generation 复用。

模型实现采用 C11 固定大小结构，没有动态分配和平台 API。宿主测试覆盖 11 类 lookup/readdir/
mutation 决策、rename 双端、target/control/root、同 UID 跨 namespace、parent superblock/inode、
basename、generation，以及 real positive、real negative、synthetic negative cache。验证命令与结果：

```text
cmake --build build --config Debug --target pathguard_hide_vfs_model_test
  PASS

ctest --test-dir build -C Debug --output-on-failure
  -R "pathguard_hide_vfs_model_test|pathguard_(hide_loader|restricted_loader)_test"
  3/3 PASS

android16-6.12 Kbuild + clang r536225: hide_vfs_model.o
  arm64 kernel object compile PASS
```

内核侧 mount namespace 身份不能用 `nsproxy *` 代替，因为进程可在不改变 mount namespace 时得到
新的 nsproxy。实现保存并 pin `struct mnt_namespace *`，同时持有目标 `nsproxy` 引用；回调以
mount namespace 指针比较，审计状态通过 `from_mnt_ns()->inum` 报告。该方案已在 myron 上通过
加载、绑定和清理验证。

本轮没有构建或加载 Hide 数据面模块，没有修改设备。现有 `pathguard_hide1.c` 仍是 fail-closed
shell；Hide 1.0 状态仍为 `unsupported`。

## 轮次 29：Hide 1.0 inactive 绑定 shell 与 myron 验证

`pathguard_hide1.c` 现在只实现数据面之前的资源绑定：`INSTALL` 通过带引用的 PID 查找获取
目标 task，验证 `fsuid`，在 `task_lock` 下引用目标 `nsproxy`，解析并 pin parent path，并要求
完整的 `0x0fff` operation mask。调用者必须已进入目标 mount namespace，否则返回 `EXDEV`。
安装先构建 candidate 后原子替换，失败保留旧 binding；`DISABLE` 保留 inactive binding，
`CLEAR` 才释放引用。没有任何 `i_op/i_fop/d_op` 写入，`ENABLE` 固定返回 `EOPNOTSUPP`。

本地 DDK 构建的 AArch64 模块经 SukiSU loader 在 myron 上加载成功。29 个 undefined symbol
同时存在于 DDK vmlinux 和设备 kallsyms。HideLab target `dev.pathguard.hideprobe.target`
（uid/pid `10549/1226`）在 `/storage/emulated/0/Pictures` 安装成功，状态为：

```text
state=INACTIVE target_mnt_ns=4026536018 generation=11
operation_mask=0x0fff parent_inode=15771
kernel=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

错误 UID 返回 `EPERM`，跨 namespace 返回 `EXDEV`，两次失败均保留旧 binding；`ENABLE 11`
返回 `EOPNOTSUPP`，`DISABLE` 保留 binding，`CLEAR` 回到 `UNSUPPORTED`。最后 `rmmod` 成功，
设备保持在线且 `sys.boot_completed=1`。

证据仅证明 LKM 启动、observer/parent 绑定、失败回滚和引用释放正确；没有任何隐藏效果。
下一步仍是 operation-table shadow、synthetic negative、readdir、atomic_open、mutation、
并发回收和完整 HideLab 回归，Hide 1.0 继续 `unsupported`。

随后以加入显式 `igrab/iput` parent inode 引用的最终二进制重跑闭环，模块 SHA-256 为
`2a5913da867df54ade60aa4eab09547b68e67698ef5718d9433c7ef6e893d2d9`，generation 使用 21。
最终二进制同样在 myron 上加载、进入 `INACTIVE`（mask `0x0fff`）、`ENABLE` 返回
`EOPNOTSUPP`、`CLEAR` 释放引用并成功 `rmmod`。因此本轮结论适用于当前源码，而非仅适用于
早期构建物。

## 轮次 30：VFS shadow 数据面第一版本地构建

本轮在 `experimental/hide-vfs/pathguard_hide1.c` 中实现单 binding 的 operation-table shadow：

- 保存并复制 parent inode 的原始 `inode_operations`、`file_operations`；
- 覆盖 `lookup`、`atomic_open`、`iterate_shared`、`create`、`mkdir`、`mknod`、`symlink`、
  `unlink`、`rmdir`、`link`、`rename` 和 `d_revalidate` 共 12 个入口；
- 使用 `smp_store_release` 发布/恢复 i_op、i_fop，逐 dentry 保存原始 d_op；
- 使用静态 SRCU 域包围所有 shadow callback，恢复指针后先 `synchronize_srcu`，再摘链释放
  dentry metadata；
- 安装前检查原始 operation pointer 未被第三方替换，parent dentry 已缓存 basename 时通过
  `d_lookup` 预装 dentry shadow，安装任一步失败均恢复已发布指针；
- `lookup` 为目标观察者发布 synthetic negative，`iterate_shared` 过滤 basename，所有 mutation
  和 rename source/destination 对目标观察者返回 `ENOENT`，其它 fsuid/namespace 透传原始回调。

本地真实 Kbuild 使用 clang r536225 和 `android16-6.12` prepared DDK 成功，生成 AArch64
`pathguard_hide1.ko`；`__versions` 段大小为 0，符合 SukiSU loader 的运行时符号解析路线。
宿主 `pathguard_hide_loader_test`、`pathguard_hide_vfs_model_test`、
`pathguard_restricted_loader_test` 均通过。

尚未在设备启用该 shadow。仍需设备加载/卸载实验、warm positive dentry、并发/生命周期和
HideLab 全矩阵回归；在这些证据完成前状态保持 `unsupported`，不能宣称 Hide 1.0 active。

## 轮次 31：shadow 模块设备 inactive 装载验证

使用本轮 clang r536225 产物通过 SukiSU `/data/adb/ksud insmod` 加载到 myron。由于普通
`su -c` 的 SELinux domain 无法访问 misc device，控制程序在目标进程 mount namespace 内通过
`su -mm -c 'nsenter -t 1226 -m ...'` 执行。`INSTALL 10549 1226 1002 /storage/emulated/0/Pictures
pathguard_hide1_probe` 成功，状态为 `INACTIVE`、mount namespace `4026536018`、operation mask
`0x0fff`。随后执行 `CLEAR` 并卸载模块；设备短暂 USB 重连后恢复，`sys.boot_completed=1`，
`/dev/pathguard_hide1` 和 `/proc/modules` 均确认已清理。

本轮没有执行 `ENABLE`，因此没有发生 VFS operation-table 替换，也没有 HideLab 隐藏效果证据。
`ENABLE` 仍是单独的高风险设备闸门，必须在明确确认后执行；产品状态继续为 `unsupported`。

## 轮次 32：首次 ENABLE 失败与 dentry 发布修复

在用户明确确认后，使用 target PID `24278`、UID `10549`、generation `1003` 执行了首次
`ENABLE` 实验。`INSTALL` 返回成功（namespace `4026536020`、mask `0x0fff`），但 ENABLE
命令期间 ADB 立即断开，设备随后自动重启；重连后 `sys.boot_completed=1`、
`ro.boot.bootreason=reboot`，模块未持久化。没有 pstore 或上一轮 kmsg 可供进一步定位，
因此该实验结论标记为 `CRASH/设备重启`，绝不能视为通过。

源码复核发现 dentry shadow 发布路径没有按 Kasumi 的做法在 `d_lock` 下同时更新 `d_op` 和
operation flags，也没有保存原始 flags 与初始化发布屏障。该并发缺陷已修复：安装和恢复均在
`d_lock` 下完成，保存/恢复完整 `d_flags`，并在 metadata 入链后使用 `smp_wmb()` 再发布指针；
释放仍在 SRCU 排空并摘链后执行。修复后的模块已重新通过 android16-6.12 clang/Kbuild，
但尚未再次执行 ENABLE。

当前状态：首次 ENABLE 实验失败，HideLab active 回归未开始，产品状态保持 `unsupported`。
下一次设备实验必须先有可观测的 kmsg/pstore 或最小化 shadow 发布范围，并继续保留自动重启后的
恢复检查。

## 轮次 33：修复版 ENABLE 第二次设备失败

针对轮次 32 的 dentry 发布并发修复重新构建模块，并在新启动的 target PID `19811`、namespace
`4026536036`、generation `1004` 上执行。`INSTALL` 成功，状态为 `INACTIVE`、mask `0x0fff`；
`ENABLE` 没有返回状态，ADB 随即断开，设备再次自动重启。重连后 `sys.boot_completed=1`，
模块未加载，说明修复未消除启用阶段故障。

本轮停止继续设备重试。当前证据只能证明 inactive binding 和 LKM 装载安全，不能证明任何
operation-table shadow 能在 myron 上稳定发布；HideLab active 回归和设备准入均被阻断。后续
必须先通过更小范围的隔离实验或获得可留存的崩溃日志，不能继续把完整 shadow 当作可用后端。

后续代码审查又发现 `d_revalidate` 原先在进入 SRCU 前从 `d_op` 反推 metadata，卸载线程可能
在该窗口内完成恢复并释放对象，形成 UAF。现已调整为先进入 `hide1_srcu` 再解析 metadata，
并重新通过 clang/Kbuild；新模块 SHA-256 为
`61aa84be1d5879440b1e987a2cbf4e1b3c650d5ecb7e2da9079208e2d4b69030`。该修复尚未上机验证，
之前两次 ENABLE 失败仍然有效。

## 轮次 34：Hide 1.0 实验刷入包（未上机）

为支持后续受控设备实验，新增独立目录 `experimental/hide-vfs/package/` 和
`scripts/package-hide1-lab.ps1`。它与正式 `module/` 完全分离，包含：

- `pathguard_hide1.ko`（本轮 SHA-256：
  `61aa84be1d5879440b1e987a2cbf4e1b3c650d5ecb7e2da9079208e2d4b69030`）；
- `hide1_control` 和 `hide1ctl` 手动控制入口；
- 安装时的 arm64 与精确 `uname -r` 检查；
- 不执行自动 `insmod`、`INSTALL` 或 `ENABLE` 的 `post-fs-data.sh`/`service.sh`；
- 卸载时仅尝试 `DISABLE`、`CLEAR` 和 `rmmod`，不持久化启用状态。

本地生成的包为 `dist/pathguard-hide1-lab-myron-v2.zip`，包含 12 个条目，
`build-info.txt` 记录设备、内核 release、模块和控制程序哈希。所有 shell
脚本通过 `sh -n`，ZIP 条目可正常读取。`dist/` 被 `.gitignore` 排除，包尚未
刷入设备；因此本轮没有新增加载、绑定或 Hide 行为证据，产品状态仍为
`unsupported`。

该 v2 包已传送到已连接的 `myron` 设备临时路径
`/data/local/tmp/pathguard-hide1-lab-myron-v2.zip`；设备端 SHA-256 为
`bc6c43a92c51f77e51b3150bea5ed06cdaae717dd865919c532d42536b60a995`，与本地
文件一致。传送后未执行模块安装、加载、绑定或 `ENABLE`。

包内 `enable` 明确标记为高风险且不会自动重试。由于轮次 32、33 的完整
shadow `ENABLE` 均导致设备重启，下一步应先使用更小范围的 shadow 隔离实验，
并保留 pstore/kmsg 采集，再考虑执行完整 `ENABLE`。

## 轮次 35：myron 刷入包安装与仅加载验证

用户已在设备上安装 `pathguard-hide1-lab-myron-v2.zip` 并重启。设备信息仍为：

```text
product/model: myron / 25102RKBEC
kernel: 6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
sys.boot_completed: 1
ro.boot.bootreason: reboot,0,32628
```

通过设备内置 KernelSU CLI 执行：

```text
/data/adb/ksud insmod /data/adb/modules/pathguard_hide1_lab/bin/pathguard_hide1.ko
```

返回 `Loaded kernel module`，`/proc/modules` 显示：

```text
pathguard_hide1 36864 0 - Live 0x0000000000000000 (O)
```

`/dev/pathguard_hide1` 存在（权限 `0600`，root:root）。使用同一构建的
`hide1_control` 临时复制到 `/data/local/tmp` 后读取状态，结果为：

```text
abi_version=1 size=184 state=0 last_error=-95 target_uid=0 target_pid=0
target_mnt_ns=0 generation=0 operation_mask=0x0000000000000000 parent_inode=0
release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

这证明包内 LKM 可经 SukiSU loader 加载，且加载后仍保持 `UNSUPPORTED`，没有
`INSTALL`、`ENABLE` 或 VFS shadow 发布；设备在观察窗口内保持在线并完成启动。

同时观察到两类非通过信息：

1. 安装时 `init` 对 `/data/adb/modules_update/pathguard_hide1_lab` 及包内文件有
   SELinux `relabelfrom` 拒绝；KernelSU 仍将模块列为已启用，需后续确认 action/
   service 脚本是否能在该设备上执行，不能假定脚本可用。
2. 内核日志出现 `pathguard_hide1: no extended symbol version for module_layout`。
   这是空 `__versions` + SukiSU 加载期解析路线的告警；本轮加载成功，但必须在
   后续稳定性审计中保留该告警，不能将其记录为零告警通过。

本轮结论：**LKM 加载和 inactive 状态验证通过；Hide 行为、shadow 稳定性和
HideLab active 仍未验证，产品状态继续为 `unsupported`。**

## 轮次 36：i_op-only 隔离实验包（待设备验证）

为定位轮次 32、33 的完整 shadow 重启原因，`pathguard_hide1.c` 增加加载期
`shadow_mode` 参数：`1=i_op-only`、`2=f_op-only`、`3=dentry-d_op-only`、
`0=all`。隔离模式只发布对应 operation table，不安装其它类型的 shadow；
`lookup` 中的 dentry shadow 也仅在 d_op 模式启用。

本地 android16-6.12 DDK/Kbuild 构建成功，宿主 loader、VFS model 和 restricted
loader 三项测试全部通过。实验包默认使用 `shadow_mode=1`，版本为
`0.1.1-experimental-iop`，打包脚本默认输出到 `download/`。该包尚未传送或加载到
设备；设备实验顺序固定为加载、`INSTALL`、单次 `ENABLE`、观察、`DISABLE`、
`CLEAR`、卸载，任何断连/重启立即停止后续模式。

本次构建指纹：模块 `pathguard_hide1.ko` SHA-256 为
`b5989a64b43e01e5fe0611da8e4602fc0d19ebf61cf234d3f9d74b6ae816b781`；ZIP
`download/pathguard-hide1-lab-myron-iop-v1.zip` SHA-256 为
`2b7afeafd791ee57b7f74702db7b8e8f19b69214c038c6e8f355e207cfd6b020`。

## 轮次 37：i_op-only 包设备加载验证

用户安装 `download/pathguard-hide1-lab-myron-iop-v1.zip` 后重启。设备启动完成，
`/proc/modules` 无旧模块，内核 release 仍为目标 `myron` 字符串。通过：

```text
/data/adb/ksud insmod /data/adb/modules/pathguard_hide1_lab/bin/pathguard_hide1.ko shadow_mode=1
```

返回 `Loaded kernel module`；`/sys/module/pathguard_hide1/parameters/shadow_mode`
返回 `1`，`/proc/modules` 显示模块为 `Live`。使用同一控制程序读取状态：

```text
state=0 last_error=-95 target_uid=0 target_pid=0 target_mnt_ns=0
generation=0 operation_mask=0x0000000000000000 parent_inode=0
release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
```

观察窗口内 ADB 在线、`sys.boot_completed=1`，没有执行 `INSTALL` 或 `ENABLE`，
因此没有产生 VFS 指针发布或隐藏效果证据。安装日志仍包含 SELinux relabel 拒绝，
内核仍提示 `pathguard_hide1: no extended symbol version for module_layout`；两项
告警均保留为后续稳定性审计风险。

本轮结论：**i_op-only LKM 加载和参数生效通过；i_op shadow 的设备 ENABLE 稳定性
尚未验证，Hide 1.0 继续为 `unsupported`。**

## 轮次 38：i_op-only ENABLE/恢复隔离实验结果

在轮次 37 的已加载模块上启动 HideLab target，得到 UID `10549`、PID `19252`、
mount namespace `4026535993`。在目标 namespace 中执行 `INSTALL`，generation
`2001`，返回 `INACTIVE`、operation mask `0x0fff`、parent inode `15771`。

单次执行 `ENABLE 2001` 返回成功并报告：

```text
state=2 target_uid=10549 target_pid=19252
target_mnt_ns=4026535993 generation=2001 operation_mask=0x0fff
```

连续 5 次、间隔约 2 秒读取状态均保持 `ACTIVE`，ADB 在线。随后 target 进程被
重新启动为 PID `22492`、namespace `4026536030`；旧 binding 正确不匹配新
namespace，说明 namespace 隔离条件生效。为回收旧 binding 执行 `DISABLE`，命令
期间 ADB 断开，设备随后重启。重连后：

```text
ro.boot.bootreason=reboot
sys.boot_completed=1
/proc/modules 无 pathguard_hide1
/dev/pathguard_hide1 不存在
/sys/fs/pstore 为空
```

本轮只证明 `i_op-only` 指针发布可短时进入 `ACTIVE`；**恢复/卸载路径仍会导致
设备重启**。由于 target namespace 已在 ENABLE 后变化，本轮没有把 target APK
的观察结果用于判定隐藏效果；完整 HideLab 仍未运行。实验状态为 `CRASH/设备
重启`，产品状态继续为 `unsupported`，后续不得再执行同一恢复路径重试。

## 轮次 39：DISABLE/恢复路径离线审查与修复

依据 `refer/Kasumi-main` 的 iop/fop/dentry teardown 实现和
`refer/hide-refer/nomount-master` 的 operation-table 恢复代码，对
`experimental/hide-vfs/pathguard_hide1.c` 进行离线审查。确认原实现只调用一次
`synchronize_srcu()`，没有覆盖“已加载 operation-table 指针但尚未进入 callback”的
Tasks-RCU 窗口；dentry shadow 也在恢复后立即 `dput/kfree`，存在旧 callback 入口
访问已释放 metadata 的风险。

本轮修复：

1. `DISABLE`/rollback/卸载改为 `retiring -> 恢复指针 -> Tasks-RCU -> SRCU ->
   Tasks-RCU -> RCU -> 释放 metadata`，并使用 `__nocfi` 包装调用
   `synchronize_rcu_tasks()`。
2. dentry 恢复持有 `d_lock`，先恢复原始 flags/d_op，再执行 `d_drop()`；shadow
   metadata 保留到所有 grace period 完成后才释放。
3. 删除无效的 `dentry_shadows.next` 空链表判断；恢复过程中发现 operation pointer
   已被其它 owner 改写时返回 `-EAGAIN`，不再伪报成功。
4. f_op shadow 设置 `owner=THIS_MODULE`，并通过 open/release 计数阻止存在旧目录
   fd 时执行 `CLEAR`，避免旧 fd 与新 binding 混用。

离线验证：

```text
android16-6.12 DDK/Kbuild: CC -> MODPOST -> LD 全部通过
host pathguard_hide_vfs_model_test: Passed
host pathguard_hide_vfs_teardown_contract_test: Passed
```

本地 Kbuild 使用已准备的 `ddk-kdir-local-linux/android16-6.12` 输出；因环境未安装
`pahole`，关闭模块 BTF 生成后完成链接。包含 f_op 旧 fd 门禁的最终模块 SHA-256
为 `7DDA5964D6C4E79A0C62349F1D027CB955E48BB0D49096A73F85D18E078E86A4`。
**尚未进行设备侧 ENABLE/DISABLE 回归，不能据此宣称恢复路径或 Hide 1.0 通过。**

## 轮次 40：修复后模块加载与安全清理

使用包含 Tasks-RCU/SRCU/RCU teardown 修复及 f_op 旧 fd 门禁的
`pathguard-hide1-lab-myron-iop-v3.zip`。用户安装模块并重启后，设备侧确认：

```text
kernel=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
fingerprint=Redmi/myron/myron:16/BP2A.250605.031.A3/OS3.0.23.0.WPMCNXM:user/release-keys
module_sha256=7dda5964d6c4e79a0c62349f1d027cb955e48bb0d49096a73f85d18e078e86a4
```

通过 `/data/adb/ksu/bin/ksud insmod` 加载成功，`shadow_mode=1`，
`/proc/modules` 显示 `pathguard_hide1 ... Live`，`/dev/pathguard_hide1` 已创建设备节点。
加载后未执行 `INSTALL` 或 `ENABLE`。加载前使用包内 wrapper 查询状态时，由于模块尚未
加载、`/dev/pathguard_hide1` 尚不存在，得到 `open: No such file`；wrapper 的全局日志
重定向还掩盖了该错误。加载完成后直接调用包内 `hide1_control status` 确认：

```text
state=0 last_error=-95 generation=0 operation_mask=0x0000000000000000
```

随后执行低风险 `DISABLE`（无 active shadow）和 `CLEAR`，两者均返回 0，状态仍为
`UNSUPPORTED`。直接 `rmmod` 在 SELinux enforcing 环境被内核拒绝为 `EPERM`，未继续
强行操作，改由用户从 KernelSU 卸载模块并重启。

## 轮次 41：用户卸载后的设备健康检查

用户通过 KernelSU 卸载 `PathGuard Hide 1.0 Lab` 并重启。重连后证据：

```text
ro.boot.bootreason=reboot
sys.boot_completed=1
kernel=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
fingerprint=Redmi/myron/myron:16/BP2A.250605.031.A3/OS3.0.23.0.WPMCNXM:user/release-keys
```

root 检查确认 `/proc/modules` 不再包含 `pathguard_hide1`，`/dev/pathguard_hide1`
不存在，模块目录已清除；`/sys/fs/pstore` 未发现 panic、oops 或 watchdog 记录。
本轮证明修复后模块在未启用 shadow 的情况下可安全加载、清理并由 KernelSU 卸载，
**不构成 `INSTALL/ENABLE/DISABLE` active 回归，也不改变 Hide 1.0 的
`unsupported` 状态。**

## 轮次 42：v4 active DISABLE 设备回归失败与修复

用户安装 `pathguard-hide1-lab-myron-iop-v4.zip` 并重启后，设备侧确认目标 release、
模块加载、`INSTALL` 和单次 `ENABLE` 均成功。随后 target 被重新启动为不同 PID 和
mount namespace，原 binding 与 probe 不再属于同一观察者，之前的文件可见性结果作废，
不能归类为 LEAK。

对旧 binding 执行 `DISABLE` 时 ADB 立即断开；设备重新上线后：

```text
ro.boot.bootreason=kernel_panic,null
sys.boot_completed=1
/proc/modules 无 pathguard_hide1
```

这证明 v4 的 active 恢复路径会触发设备级内核异常，HideLab active 回归立即停止，
产品状态保持 `unsupported`。

离线审查发现 v4 在没有活动回调计数的前提下，`DISABLE`/rollback/卸载无条件调用两次
`synchronize_rcu_tasks()`，而现有 wrapper 已统一进入 `hide1_srcu`。修复版删除无条件
Tasks-RCU 调用，采用“先恢复 operation pointers，再 `synchronize_srcu()`，最后
`synchronize_rcu()`，然后释放 dentry metadata”的顺序，并同步更新 teardown 契约测试。
该修复尚未重新打包或在设备上验证；不得把离线测试通过解释为设备恢复已修复。

下一步是重新构建并审查新包，先做 `i_op-only` 的短时 ENABLE/观察/DISABLE 隔离实验；
在确认设备不再 kernel panic 前，不允许进入完整 d_op/f_op active 矩阵。

## 轮次 43：v5 i_op-only 有效序列与恢复重启

用户安装 `pathguard-hide1-lab-myron-iop-v5.zip` 并重启。设备 release 和 fingerprint
仍命中 myron allowlist，模块通过 SukiSU 加载，`shadow_mode=1`。目标进程固定为
PID `20048`、mount namespace `4026535993`；在该 namespace 内完成：

```text
INSTALL uid=10549 pid=20048 generation=5001
state=INACTIVE operation_mask=0x0fff parent_inode=788054
ENABLE 20048 5001
state=ACTIVE target_mnt_ns=4026535993
```

未 force-stop target，使用 `FLAG_ACTIVITY_CLEAR_TASK|NEW_TASK` 重新触发同一进程的
probe，metadata 仍报告 namespace `4026535993`，因此观察序列有效。结果显示：

```text
Java exists/isDirectory/list = visible
stat/lstat/access/open/opendir = success
readdir/getdents64 (4K/32K/64K/128K) = visible
```

该结果归类为有效 `LEAK`。根因是 `shadow_mode=1` 不安装 dentry shadow；fixture
中的目标目录在 `ENABLE` 前已经是 positive dentry，后续路径解析命中缓存，不会再次
调用 parent `i_op.lookup`，所以仅替换 `i_op` 无法撤销正缓存可见性。i_op-only 不能
进入 Hide 1.0 active 候选。

随后对同一 active binding 执行 `DISABLE`，ADB 立即断开；设备重新上线后：

```text
sys.boot_completed=1
ro.boot.bootreason=reboot
/proc/modules 无 pathguard_hide1
/sys/fs/pstore 为空
```

未获得 kernel panic/oops 的持久日志，但设备级重启已足以判定 v5 active 恢复路径
不稳定。本轮停止后续 `ENABLE`，不得把 `reboot` 当作恢复成功。需要先在离线代码中
重新设计 active callback/teardown 计数和 dentry 生命周期，再构建新包；在此之前产品
状态仍为 `Hide 1.0 = unsupported`。
## 轮次 44：按 shadow 类型条件化 teardown

针对轮次 43 的恢复重启，继续收缩 teardown 的同步范围。`i_op`/`f_op` shadow
本身嵌入 binding，在 `DISABLE` 后仍由 inactive binding 持有，不需要为动态 dentry
metadata 执行全局 SRCU/RCU 排空；只有 `retired` dentry shadow 非空时才调用
`synchronize_srcu()` 和 `synchronize_rcu()`，随后释放 dentry metadata。这样避免了
`i_op-only` 隔离实验中无动态对象却进入全局同步路径，同时保留 dentry shadow 的回收
顺序约束。

离线验证通过：

```text
pathguard_hide_vfs_model_test: Passed
pathguard_hide_vfs_teardown_contract_test: Passed
android16-6.12 Kbuild: CC -> MODPOST -> LD -> BTF: Passed (PAHOLE=/bin/true)
```

新实验包已生成，尚未安装或在设备上验证：

```text
download/pathguard-hide1-lab-myron-iop-v6.zip
module SHA-256: 85392b51d4985fb80f46f0d7dd5efffaee4a6903176c3dbbe9400a5cc2ce9924
ZIP SHA-256:    592faac289adf649108d78b011913c769555ad7ee0bda31e2f95383ab8f7b69f
```

设备实验仍需从加载、稳定 target、`INSTALL`、`ENABLE`、probe、`DISABLE` 开始；在
恢复稳定性确认前不得进入 d_op/f_op 全量矩阵，也不得改变产品 `unsupported` 状态。
## 轮次 45：v6 i_op-only teardown 仍触发设备重启

用户安装 `pathguard-hide1-lab-myron-iop-v6.zip` 并重启。设备 release/fingerprint
命中 allowlist，模块以 `shadow_mode=1` 加载。target 固定为 PID `19583`、mount
namespace `4026536007`；`INSTALL generation=6001` 与 `ENABLE` 均成功，probe 在
同一 target 进程内完成。

本轮重点验证条件化 teardown。对 active binding 执行 `DISABLE` 时命令返回码为 `255`
且 ADB 断开；设备重连后连续观察得到：

```text
sys.boot_completed=1
ro.boot.bootreason=reboot
```

模块未重新加载。由于应用私有目录在重启后不可访问，无法取得 probe 文件的持久副本；
但设备级重启本身已足以判定 `DISABLE` 恢复路径仍不稳定。v6 不能作为 Hide 1.0 候选，
不得继续 d_op/f_op 或完整 active 矩阵。

当前根因尚未闭合：即使 i_op-only 没有动态 dentry shadow，恢复 operation pointer 后
仍可能存在已加载旧回调、文件系统并发路径或 KernelSU 模块生命周期交互。下一步必须
在离线代码中增加 i_op/f_op/dentry 各自的 active callback 计数与显式 quiesce 状态机，
并建立可重复的 teardown 单元/并发测试；在获得新的离线证据前，不再向设备执行
`ENABLE`/`DISABLE`。产品状态保持 `Hide 1.0 = unsupported`。
## 轮次 46：参考项目 teardown 实现对照

### Kasumi：完整的对象生命周期和 quiesce 状态机

Kasumi 的 `kasumi_sop_shadow.c`、`kasumi_iop_override.c`、
`kasumi_fop_override.c` 和 `kasumi_dirhijack.c` 采用同一类安全模型：

1. 每个 inode/dentry/superblock 都有独立 metadata，metadata 通过 RCU hash/list
   发布；不使用单个全局 binding 推断对象归属。
2. 安装时保存原始 operation table，并持有 inode、dentry、superblock 和必要的
   `THIS_MODULE` 引用。superblock shadow 额外持有 `s_active`，避免 shutdown 在
   Kasumi 仍需要 reclaim 回调时开始。
3. wrapper 进入时先增加活动计数，再在 RCU/SRCU 保护下查找 metadata；退出时减少
   计数并唤醒等待者。`iop` 有全局 active callback 计数，`sop` 还有每个 superblock
   的 `callback_active`，`fop` 另设 iterate client 的 SRCU 域。
4. 清理不是一个 ioctl 内的直接 free，而是显式状态机：停止新请求和策略路由，恢复
   原始指针，关闭新的 client/vnode 获取，执行第一轮 Tasks-RCU 以覆盖“已读到旧
   pointer 但尚未进入 wrapper”的窗口，等待 active callback 归零，再执行第二轮
   Tasks-RCU 覆盖 wrapper epilogue，最后 `synchronize_rcu()` 后才从 hash 移除并释放
   metadata。
5. dentry shadow 由 `dget()` 持有生命周期；恢复时在 `d_lock` 下先恢复完整 flags，
   再恢复 `d_op`，随后 `d_drop()`。已失效 dentry 的回收转移到 workqueue，避免在
   `d_revalidate()` 的 RCU-walk 上下文中执行 `dput/path_put/kfree`。
6. f_op 还有旧 KMI bridge：ingress table 保留原 owner，文件真正打开时转移到 module-
   owned live table，避免 VFS 多次 `fops_get()` 在替换窗口中拿到不一致 owner。
7. 模块退出前要求 quiesce 状态达到 READY，并检查 control fd、proxy、active callback、
   vnode、module refcount 等计数；任何计数不一致都拒绝卸载，而不是继续释放。

关键源码位置：

```text
refer/Kasumi-main/src/core/kasumi_sop_shadow.c:38,95-110,365-390
refer/Kasumi-main/src/features/kasumi_iop_override.c:38,330-404
refer/Kasumi-main/src/features/kasumi_fop_override.c:100-190,328-390
refer/Kasumi-main/src/core/kasumi_dirhijack.c:717-811,1586-1620
refer/Kasumi-main/src/control/kasumi_ioctl.c:1590-1710
```

### NoMount：全局 hijack，恢复简单但不具备当前要求的完整安全证明

NoMount 在 superblock/inode 层批量替换 `i_op`、`i_fop`、`s_op` 和 dentry d_op，
通过 `kfree_rcu()`、`synchronize_rcu()`/`synchronize_srcu()` 回收规则数组和
metadata；退出时 `shrink_dcache_sb()`、遍历 `s_inodes` 并恢复 operation table。
实现没有 Kasumi 那样的 per-object callback_active、module pin、vnode/s_active
生命周期门禁，且 `nomount_restore_superblocks()` 在遍历 superblock inode 列表时
直接执行批量恢复。因此它可作为“恢复顺序和 dcache 处理”的参考，但不能直接移植
到 PathGuard 的固定 target/namespace Hide 语义，也不能作为设备级稳定性依据。

### PathMask：改 hook syscall/VFS 入口，不替换 operation table

LKM-PathMask 通过 kretprobe 拦截 `inode_permission`、`vfs_getattr`、
`__arm64_sys_getdents64` 及 `newfstatat/statx/faccessat/openat/openat2` 等入口，
以 `(dev, inode)` 识别目标，返回 `-ENOENT` 或过滤 dirent。openat 命中后必须先调用
解析得到的 `close_fd()`，再改写返回值，否则会产生 fd 泄漏；解析不到 `close_fd()`
时自动放弃该 hook。它绕开了 positive dentry 与 operation-table 生命周期问题，
但只能覆盖已实现的 syscall 表面，不能满足本项目要求的 VFS lookup/atomic_open/
mutation/d_revalidate 全语义。

### SUSFS：补丁内核能力，不是通用 LKM teardown 模板

SUSFS 通过对 GKI/KernelSU 内核打补丁实现 `sus_path`、mount 隐藏、kstat spoof 和
try-umount；用户态工具只提交规则。它要求与 kernel branch/defconfig 对齐，兼容性
由重新构建内核保证，而不是依靠 LKM 运行时替换 operation table。因此 SUSFS 能说明
“修改内核可获得更早、更稳定的拦截点”，但不能直接解决当前 LKM 的卸载恢复问题。

### 对 PathGuard 当前原型的直接结论

当前 `pathguard_hide1.c` 与 Kasumi 的差距不是同步函数数量，而是缺少四个结构性
组件：

1. i_op/f_op/d_op 各自的 active callback 计数和 wait queue；
2. metadata 的 per-object RCU 索引，以及模块/inode/dentry 引用的独立生命周期；
3. `STOP_NEW -> RESTORE -> first Tasks-RCU -> active=0 -> second Tasks-RCU ->
   synchronize_rcu -> FREE` 的可观测 quiesce 状态机；
4. f_op ingress/owner bridge，以及 dentry stale 回收 workqueue。

在补齐这些组件并通过离线并发/卸载测试前，不能继续设备 active 实验，也不能把
任何 `DISABLE` 后的 `reboot` 解释为恢复成功。产品状态保持 `Hide 1.0 = unsupported`。

## 轮次 47：参考项目实现方式与 PathGuard 差距复核

本轮重新逐文件核对 `refer/Kasumi-main`、`refer/hide-refer/nomount-master`、
`refer/hide-refer/LKM-PathMask-main`、`refer/hide-refer/susfs4ksu-*` 和
`refer/hide-refer/SukiSU-Ultra-main`。结论如下。

### 1. Kasumi：对象级 shadow + 可证明的撤销顺序

Kasumi 不是把一个静态操作表写入 inode 后直接恢复，而是为每个对象保存原始
指针和独立 metadata：

- `kasumi_iop_override.c` 以 inode 为键建立 RCU hash，shadow 只覆盖 `getattr`；
  安装前 `ihold()`，恢复指针后 `hash_del_rcu()`，再经 `synchronize_rcu()` 和
  `iput()`/RCU 回收。`stop_new` 先关闭安装门，再恢复所有仍由 Kasumi 持有的
  `i_op`，随后通过 Tasks-RCU 关闭“读到旧指针但尚未进入 wrapper”的窗口。
- `kasumi_fop_override.c` 按原始 `file_operations` 指针共享 template，并额外
  保存 ingress/live 两套表。ingress 保留原 owner，live 表归模块所有；iterate
  client 通过独立 SRCU 域发布和撤销，避免 `fops_get()` 在替换窗口拿到失效 owner。
- `kasumi_dirhijack.c` 为每个 dentry 克隆完整 `dentry_operations`，只改写
  `d_revalidate`，在 `d_lock` 下恢复原 flags 和 `d_op` 后 `d_drop()`；dentry
  持有 `dget()`，失效回收放到 workqueue，绝不在 LOOKUP_RCU 回调里 `dput/kfree`。
- `kasumi_sop_shadow.c` 还要持有 `s_active` 和模块引用，按 superblock 记录
  `vnode_live`、`callback_active`、client 数，等待回调归零后才恢复 `s_op`、
  删除 hash 并释放引用。

Kasumi 的 lookup/readdir hide 语义仍只覆盖其 dirhijack 规则；虚拟目录的
`create/mkdir/mknod/symlink/unlink/rmdir/link/rename` 是对“虚拟节点写穿”的
实现（`kasumi_vnode.c`），不是对任意真实目录进行全局隐藏。因此不能把它误解为
已经证明了 PathGuard 的全部 12 项隐藏语义。

### 2. NoMount：覆盖面大，但生命周期证明不足

NoMount 在 `nomount_hijack_dir_ops()` 中直接复制并替换 inode 的 `i_op/f_op`，
在 superblock 层替换 `s_op`，dentry 则使用一个静态 `dentry_operations` 并清理
原有 operation flags。规则数组用 seqcount + SRCU，退出时 `shrink_dcache_sb()`、
遍历 `s_inodes` 恢复指针并 `kfree_rcu()`。

这条路线适合参考“规则数组快照、虚拟 inode、dcache 收缩”的数据面，但它缺少
Kasumi 级别的每对象 `callback_active`、模块 pin、superblock `s_active` 和
明确的 STOP/RESTORE/DRAIN 状态机；静态 d_op 还可能覆盖文件系统原有回调。因此
不能直接作为 PathGuard 的卸载模板。

### 3. PathMask：syscall/kretprobe 遮罩的边界

PathMask 不修改 operation table，而是以 `(dev, inode)` 识别目标，在
`inode_permission`、`vfs_getattr`、`__arm64_sys_getdents64` 及若干 `__arm64_sys_*`
入口返回 `-ENOENT` 或压缩 dirent。对 `openat/openat2`，它先调用解析到的
`close_fd()` 再改写返回值，避免“调用者得到 ENOENT 但内核泄漏 fd”。无法解析
`close_fd()` 时主动放弃该 hook。

它绕开了 positive dentry 和 operation-table 回收难题，但覆盖的是 syscall 表面，
不能保证 `lookup/atomic_open/d_revalidate`、相对路径、别名路径和全部 mutation
语义；同时 kretprobe 位于热点路径，会引入可测量开销和可被探测的时序特征。

### 4. SUSFS 与 SukiSU：稳定性来自内核集成/加载器，而非运行时替换

SUSFS 的真正隐藏逻辑位于打补丁的内核 `fs/susfs.c` 和各处调用点，用户态
`ksu_susfs` 只提交路径、mount、kstat 等规则。其 README 明确要求按 kernel branch、
defconfig 和补丁版本重新构建；这提供了稳定拦截点，却没有可复用的 LKM teardown
协议。

SukiSU 的 `ksuinit` 则证明了另一层问题：它解析 `/proc/kallsyms`，将 LKM 未定义
符号重定位为绝对地址，必要时从 kmsg 读取内核要求的 vermagic 后重试
`init_module`；`check_symbol` 要求 `__versions` 为空。该机制只解决“模块如何进入
当前内核”，不提供 VFS 隐藏数据面，也不能替代回调生命周期安全。

### 对当前 `pathguard_hide1.c` 的具体判断

当前原型已具备固定 parent/namespace、12 个回调入口、正负 dentry 的基本
`d_revalidate` 逻辑和事务式安装回滚，但仍有以下结构性差距：

1. 单个全局 `hide1_binding` 不能表示多个 inode/dentry 对象，metadata 也没有
   RCU hash 索引；`hide1_dentry_shadows` 仅由自有 spinlock 保护，无法像 Kasumi
   那样让回调无锁查找并安全跨代缓存。
2. i_op/f_op/d_op 没有各自的 active callback 计数；当前 `SRCU` 只保护函数体，
   不能证明“已读到 shadow 指针但尚未进入回调”的窗口已经关闭。
3. f_op 只有一个静态 shadow 表和 `fop_open_count`，没有 ingress/live owner
   bridge，不能覆盖 VFS 对 `f_op` 的多次读取及原 owner 生命周期。
4. dentry 失效没有 workqueue 退休路径；`d_revalidate` 不能在 RCU-walk 中完成
   `dput/kfree`，而 DISABLE/CLEAR 必须等待 stale callback 和 worker 全部结束。
5. mutation wrapper 当前只是“命中目标名返回 `-ENOENT`，否则调用保存的原回调”，
   尚未证明 create/unlink/rename 与 positive dentry、双端 rename、目标进程退出、
   namespace 销毁之间的一致性。

因此下一步不是继续打包或刷写设备，而是先按 Kasumi 的对象生命周期模型重构
`pathguard_hide1.c`：分别建立 iop/fop/dop metadata 索引、活动计数和 wait queue，
实现 `STOP_NEW -> RESTORE -> Tasks-RCU -> active=0 -> Tasks-RCU -> RCU -> FREE`，
再补 fop ingress/owner bridge 与 dentry stale workqueue。离线并发/卸载契约测试
通过后，才允许进行一次受控设备回归；Hide 1.0 仍保持 `unsupported`。

### 轮次 48：VFS shadow 生命周期修复（2026-09-14）

本轮将上述生命周期要求落实到 `experimental/hide-vfs/pathguard_hide1.c`：

- `i_op`、`f_op`、`d_op` 分别使用按 inode/dentry 键控的 RCU hash；回调入口在
  RCU 读侧内完成查找并立即增加全局及对象 active 计数，退出路径统一递减并唤醒
  wait queue，避免“查找到 metadata 后尚未计数”窗口。
- f_op 使用 ingress/live 两张表。ingress 接管 open/release/iterate，成功 open
  后切换 live；两张表的 owner 都固定为本模块，并由 module pin 保证卸载期间代码
  常驻。
- 卸载前先检查所有 ingress 指针仍指向本模块；任一指针被外部替换即返回
  `-EAGAIN`，不释放 metadata。正常路径依次执行 `STOP_NEW -> RESTORE -> DRAIN`，
  摘除 RCU 索引后执行两次 `synchronize_rcu_tasks()`、active 归零等待和
  `synchronize_rcu()`，再释放对象。
- dentry shadow 在 `d_lock` 下恢复 flags/`d_op` 并 `d_drop()`；stale 对象由
  workqueue 退休，DISABLE/CLEAR/模块退出先 `cancel_work_sync()`，避免 worker 与
  teardown 并发回收。
- dentry shadow 的重复安装在持锁窗口内变为幂等成功；CLEAR 在卸载失败时保留
  binding，防止悬空 operation pointer。

新增/更新离线测试：

```text
pathguard_hide_vfs_model_test              Passed
pathguard_hide_vfs_teardown_contract_test  Passed
pathguard_hide_vfs_concurrency_test       Passed
```

真实 Android DDK 编译仍被阻塞：下载的 `android16-6.12` 源码没有
`include/generated/autoconf.h`、`include/generated/rustc_cfg` 和
`include/config/auto.conf`，直接执行 `make -C experimental/hide-vfs KDIR=...`
会在内核 `prepare` 阶段失败。因此本轮没有生成可刷写新模块，也没有进行设备验证；
现有设备准入状态仍为 `Hide 1.0 = unsupported`。

随后复用工作区已有 prepared tree `build/ddk-kdir-local-linux/android16-6.12`，并显式使用
`build/ddk-clang-r536225/.../clang-r536225/bin` 工具链重新执行 Kbuild。源码编译、modpost
和链接均通过；由于环境没有 `pahole`，BTF 步骤以 `PAHOLE=/bin/true` 仅完成实验模块生成。
最终 `pathguard_hide1.ko` 大小为 351192 字节，`__versions` 段大小为 0，vermagic 为
`6.12.76-4k SMP preempt mod_unload modversions aarch64`。该 vermagic 与 myron 目标
release 不一致，且构建树是通用 prepared tree，因此产物只能用于离线加载器/符号审查，
不能直接刷写设备或宣称 ABI 匹配。

基于该产物生成了 `download/pathguard-hide1-lab-myron-iop-v7.zip`（177248 字节）。包内
`automatic_load=no`、`automatic_enable=no`，默认 `shadow_mode=1`，仅用于人工控制的
受控实验；模块 SHA-256 为
`9e1a537de286e3500d54df69d88ebd564d2e6cf70b8353a08323cd355938191e`。在用户安装并重启
前，不执行任何设备侧 ENABLE。

### 轮次 49：v7 设备加载验证（2026-09-14）

用户安装 `pathguard-hide1-lab-myron-iop-v7.zip` 并重启后，设备
`25102RKBEC/myron` 在线。通过模块内控制器执行 `load 1` 成功，随后观察到：

```text
pathguard_hide1 ... Live ... (O)
abi_version=1 size=184 state=0 last_error=-95
release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
shadow_mode=1
```

这证明 SukiSU loader 能在目标设备上加载本轮 `.ko`，且模块初始化未导致重启；
同时 `state=0`（UNSUPPORTED/INACTIVE）与 `last_error=-95`（`EOPNOTSUPP`）符合
默认门禁。当前未执行 INSTALL/ENABLE，未产生 HideLab active 证据，产品状态继续为
`unsupported`。

### 轮次 52：DISABLE 死锁路径修复与 v8 构建（2026-09-14）

对照 Kasumi 的 teardown 顺序，修复了两个结构性问题：`synchronize_rcu_tasks()`、
active 等待和 `synchronize_rcu()` 不再持有 `hide1_lock`；`DISABLE/CLEAR` 在调用
`cancel_work_sync()` 前先释放该锁，避免 stale worker 与控制线程互等。修复后使用
prepared android16-6.12 tree 和 r536225 clang 完成真实 Kbuild、modpost、链接；三项
VFS 离线测试继续全部通过。

生成 `download/pathguard-hide1-lab-myron-iop-v8.zip`（177380 字节），模块 SHA-256：
`d9fd2b53795d26bb379f4010679b8337b58087adcf863c84f8eb12a186e68406`。该包尚未上机；
由于 vermagic 仍为通用 prepared tree 的 `6.12.76-4k`，必须先完成离线 ABI 审查后才能
考虑再次设备实验。Hide 1.0 状态保持 `unsupported`。

### 轮次 54：v8 ENABLE 进入 ACTIVE（观测参数无效）

在 target PID `19278`、namespace `4026535993` 仍存活时执行 `hide1_control enable 2002`，
返回 `state=2 (ACTIVE)`，设备未重启。随后启动探测 Activity 时使用了 `--es`，而 APK
要求 `--esa observe_paths`；Activity 因已是 top-most 实例未重新执行，metadata 仍显示
默认路径 `Pictures/Nagram` 和 `DCIM/Screenshots`，没有采集到本次 binding 的
`v8-hidden/hidden` 路径。因此本轮没有有效 HideLab 结果，也未执行 DISABLE；产品状态
继续为 `unsupported`，不得将 ACTIVE 状态当作 hide 通过。

### 轮次 55：v8 ACTIVE 状态复核（2026-09-14）

`hide1_control status` 持续返回 `state=2`，target PID `19278` 仍存活，设备 uptime
稳定，模块在 `/proc/modules` 中为 Live。当前 Activity 为 top-most，探测 APK 没有重新
读取新的 `--esa observe_paths` 参数，故未产生匹配本次 binding 的有效观测。没有执行
DISABLE（此前该操作已证实会触发重启）；设备暂保持 ACTIVE 作为故障复现状态，Hide 1.0
仍为 `unsupported`。

### 轮次 56：HideLab Intent 刷新修复与 APK 构建（2026-09-14）

修复 `ProbeActivity` 在 Activity 已经是 top-most 时不重新读取探测参数的问题：新增
`onNewIntent()`，并将探测启动集中到 `startProbeFromIntent()`；该方法会取消旧的 probe
task，再使用最新 `observe_paths`、`scenario` 和 `run_id` 启动采集。后续使用
`am start --esa observe_paths ...` 时，路径参数不会静默退回默认值。

本地验证命令：

```text
./gradlew.bat :app:testTargetDebugUnitTest :app:testControlDebugUnitTest \
  :app:assembleTargetDebug :app:assembleControlDebug --stacktrace
```

结果：`BUILD SUCCESSFUL`；target/control 两组 Kotlin 单元测试均通过，两个 debug APK
均构建成功。产物及 SHA-256：

```text
download/hidelab-app-target-intent-refresh-v1.apk
B22F69E0E4C889F9D2310FC409FDCA17E3FFB3F9B5F6B4F7B4A87682BA1242B2

download/hidelab-app-control-intent-refresh-v1.apk
CDE24B5F8E238570EEE7EB8CF3B0CF06DD511CC15452E99F55508F677374345F
```

设备仍保持 v8 `ACTIVE`（PID `19278`、mount namespace `4026535993`、generation `2002`）。
由于安装新版 target APK 通常会终止该进程并使现有 binding 失效，本轮仅完成构建和静态
证据归档，未执行 `adb install`、`DISABLE` 或重启。已有 `hide1-enable-2002` 观测仍使用
错误/default 路径，不能作为 HideLab active 证据；产品状态继续为 `Hide 1.0 = unsupported`。

随后又将 `scenario`、`attack_mutations` 和 `run_id` 在提交后台 probe 前复制为不可变快照，
避免 `onNewIntent()` 与旧任务并发时读取可变 Activity `intent`。重新构建后两 flavor 单元
测试及 APK 构建再次 `BUILD SUCCESSFUL`。最新产物：

```text
download/hidelab-app-target-intent-refresh-v2.apk
B1E6F369936FB9A35D04DC612664F7007D441A35EFD3739998A8C48CE76DA39F

download/hidelab-app-control-intent-refresh-v2.apk
CBD02480A4240F3B1C573CAA64C3CA8FA61E71546E4B645D582CBA7DCE7DDC71
```

### 轮次 57：安装新版 target 并验证参数快照（2026-09-14）

按用户明确指示执行：

```text
adb install -r download/hidelab-app-target-intent-refresh-v2.apk
```

安装返回 `Success`。旧 target PID `19278` 已退出，模块控制器仍显示旧 binding：
`state=2`、`target_pid=19278`、`target_mnt_ns=4026535993`、`generation=2002`；该 binding
不再对应现存进程，不能作为新进程的保护证据。

随后仅启动新版 target 做无写入参数验证，使用路径
`/storage/emulated/0/Pictures/PathGuardHideLab/manual-v2-hidden`。探测完成并归档：

```text
build/device-evidence/hidelab-manual-v2-install/
```

metadata 正确记录 `observe_paths` 为 `manual-v2-hidden`，新进程 PID 为 `12005`、mount
namespace 为 `4026536086`。这证明 APK 的 Intent 刷新和参数快照修复在设备上生效；由于
新 namespace 与旧 binding 不同，本轮未执行 `INSTALL/ENABLE`，也未宣称任何隐藏能力。
Hide 1.0 继续为 `unsupported`。

### 轮次 63：i_op-only safe-v2 真机 ENABLE 稳定性复验（2026-09-15）

安装并重启 `pathguard-hide1-lab-myron-fuse-iop-safe-v2.zip`。KernelSU 安装器确认
目标 release 匹配，模块未自动加载。手动执行 `load 1` 成功，模块 SHA-256 为
`ef600ee1a83cd2303c2e66b09a9378986d8652e48f63c80c87ba7aef8bfc0cf6`；状态为
`UNSUPPORTED/INACTIVE`。

新启动的 HideLab target 为 UID `10551`、PID `19500`、mount namespace `4026535993`。
执行 `INSTALL generation=7001` 成功，`operation_mask=0x0fff`；随后执行
`ENABLE 19500 7001` 返回 0，状态进入 `ACTIVE`，设备持续在线，`ro.boot.bootreason`
为普通 `reboot,shell`，无 pstore 崩溃记录。该结果证明恢复默认 `shadow_mode=1` 后，
此前 fuse-ro-v1 的 ENABLE 重启回归不再出现。

本轮没有形成有效 HideLab 隐藏判定：为刷新探测权限而 force-stop target 后，新进程
namespace 变为 `4026536013`，但 binding 仍固定在旧 namespace `4026535993`；探测还受
MediaProvider UID/存储权限影响，返回 `EACCES` 而非 Hide 语义要求的 `ENOENT`。因此不能
记为 PASS、LEAK 或 Hide 1.0 active 证据。当前 ACTIVE binding 保留在设备上，未执行
高风险 DISABLE；后续必须先安排一次受控恢复，再在同一进程/namespace 内完成采集。

产品状态仍为 `Hide 1.0 = unsupported`。

### 轮次 64：target 生命周期身份与 HideLab namespace 闸门（2026-09-15）

继续审查 safe-v2 后发现，binding 原先只校验 UID、mount namespace 和 generation；target
进程退出后，旧 binding 仍可能对同 UID/同 namespace 的新进程生效。内核 binding 现额外
持有 `target_task` 引用，并要求观察线程与原 target 属于同一 thread group 且 target 未处于
`PF_EXITING`；释放 binding 时在所有 shadow 回调排空后 `put_task_struct()`。这使进程退出和
PID 复用默认为 fail-closed，不会继承旧 hide 规则。

HideLab runner 同步修复：新增 `-KeepTargetProcess`（跳过 target force-stop，并校验采集前后
mount namespace 不变）、`-GrantReadMediaImages`（与 all-files AppOp 分开授予媒体权限）和
只读 `-ExistingHiddenPath`（不重置/删除已有媒体目录，自动禁止 mutation）。对设备基线中
可访问的 `Pictures/Nagram` 做无后端验证时，target/control 均能正常 `stat/open/readdir`，
结果为 `BASELINE_VISIBLE_NOT_HIDE_PASS`；这确认权限闸门有效，但不构成 hide 通过。

内核模块重新完成 Android 16/6.12 Kbuild、modpost 和链接，离线 model/teardown/concurrency
及 PowerShell 语法检查全部通过。新实验包已生成并传送设备 Download：

```text
pathguard-hide1-lab-myron-fuse-iop-safe-v4.zip
ZIP SHA-256 e2e8b7e07971d0d097fb564ed06c1c15bc8f795d20eba7424d84dc46e3747d74
KO  SHA-256 934945c3b8e651902ccc2ea8657e2e962977844396e97ae5e0aa4251fa40ddab
```

当前设备仍保留上一轮 ACTIVE binding；未安装 safe-v4，也未执行 DISABLE/ENABLE。下一次
实验需先重启或经明确批准完成恢复，再在同一 target PID/namespace 内执行只读采集。
Hide 1.0 继续为 `unsupported`。

safe-v4 仅重新打包了同一已通过 Kbuild 的 KO，并包含 target-task 生命周期修复和
HideLab runner 闸门；未安装到当前设备，也未执行任何新的 ENABLE。

### 轮次 58：新版 target 重新 INSTALL 被旧 ACTIVE binding 拒绝（2026-09-14）

按用户明确指示启动新版 target 后，进程为 `PID 12005`、UID `10549`、mount namespace
`4026536086`。创建一次性 fixture：

```text
/storage/emulated/0/Pictures/PathGuardHideLab/20260914-233102/hidden
```

执行：

```text
hide1ctl install 10549 12005 3001 \
  /storage/emulated/0/Pictures/PathGuardHideLab/20260914-233102 hidden
```

内核返回 `Device or resource busy`（`-EBUSY`）。这是预期的状态保护：旧
`ACTIVE` binding（`PID 19278`、namespace `4026535993`、generation `2002`）仍存在，
`INSTALL` 不覆盖活动 shadow，也未发生部分提交。模块状态为 `state=2`、`last_error=-16`，
fixture canary 哈希保持不变。完整证据归档于：

```text
build/device-evidence/hidelab-reinstall-20260914-233102/
```

本轮未执行 `ENABLE`、`DISABLE`、`CLEAR` 或重启。要继续重新绑定，必须先安排一次明确
批准的恢复流程以清除旧 ACTIVE binding；在此之前不能把新 target 进程纳入 HideLab active
验收，Hide 1.0 继续为 `unsupported`。

### 轮次 59：恢复后重新 INSTALL/ENABLE 成功，但 HideLab 采集仍无效（2026-09-15）

用户批准恢复流程后执行 `DISABLE`，设备两次均因该路径自动重启；第二次重启完成后，
模块未自动加载，手动 `load 1` 成功。冷启动新版 target 后得到 `PID 21860`、UID `10549`、
mount namespace `4026535990`，执行 `INSTALL generation=3002` 成功，随后
`ENABLE 21860 3002` 成功，设备未再次断连。

但在 ACTIVE 状态下通过 `onNewIntent()` 触发 HideLab 时，metadata 仍反复写入旧
`run_id=reinstall-active-3002`，未写入请求的 `reinstall-active-3002-probe` 或
`probe2`。原因是旧 native probe 已进入不可中断调用，取消 Future 不能中断底层任务；
单线程 executor 等待旧任务完成后又覆盖输出。该结果证明参数修复尚未形成可靠的重复采集
协议，本轮观测不能用于 HideLab 验收。

最终状态：`state=2`、`target_pid=21860`、`target_mnt_ns=4026535990`、
`generation=3002`、`operation_mask=0x0fff`。证据已追加到
`build/device-evidence/hidelab-reinstall-20260914-233102/`。未执行新的 `DISABLE`，
未清理 fixture；Hide 1.0 仍为 `unsupported`。

### 轮次 60：修复 HideLab 任务生命周期（2026-09-15）

根因确认：`Future.cancel(true)` 无法中断 JNI/native 探测；旧任务会在新 Intent 到达后
继续运行，并覆盖共享的 `status`、`metadata.json` 和 `observations.jsonl`。

新增 `ProbeRunGate`：每次 Intent 产生单调递增 run token，只有当前 token 可以发布
canonical 输出；旧任务即使完成也只能丢弃结果。`onDestroy()` 会使当前 token 失效，
避免 Activity 销毁后后台任务发布状态。新增单元测试覆盖新 Intent 淘汰旧任务和销毁失效。

验证：`testTargetDebugUnitTest`、`testControlDebugUnitTest`、`assembleTargetDebug` 和
`assembleControlDebug` 均 `BUILD SUCCESSFUL`。

产物：

```text
download/hidelab-app-target-run-gate-v1.apk
SHA-256 0226FF67C925438C529E74F0B310BE1B0011613B8AFB73C1F67FE3AF808FB6CF
download/hidelab-app-control-run-gate-v1.apk
SHA-256 2C9687506FD2EE2FEF6D8DCEF2DCF3E3904274C2416CC867F2007C9E24710B9F
```

设备未安装新 APK，当前仍为 `ACTIVE`（PID `21860`、namespace `4026535990`、generation
`3002`）。修复尚未完成真实设备重复 Intent 回归；下一次必须冷启动新 target 或等待旧
native 任务结束，再检查新 run id、输出原子性和 HideLab 全矩阵。产品状态继续为
`unsupported`。
### 轮次 61：run-gate APK 真机回归（2026-09-15）

按批准安装 `download/hidelab-app-target-run-gate-v1.apk`，恢复旧 ACTIVE binding 时
`DISABLE` 触发一次重启。重启后手动加载 LKM、冷启动 target，并完成 `INSTALL 10551
19958 5001` 与 `ENABLE 19958 5001`，模块进入 `ACTIVE`。

连续发送 `run-gate-v2-second-a` 和 `run-gate-v2-second-b` 两个 Intent。最终 metadata
的 `run_id` 为 `run-gate-v2-second-b`，status 为 `complete`，证明旧 native 任务不能
覆盖新任务结果；`singleTop` 路由、run token 门控和 status 代次标记在真机生效。

证据目录：`build/device-evidence/hidelab-run-gate-v2/`。observations 中
`external.0.stat/lstat/open/readdir/getdents64_*` 仍可见，属于 `LEAK`；这证明任务
生命周期修复通过，但不是隐藏能力通过。最终模块状态为 `ACTIVE`、generation `5001`，
产品状态仍为 `Hide 1.0 = unsupported`。

### 轮次 62：fuse-ro-v1 ENABLE 回归二分与安全默认恢复（2026-09-15）

`pathguard-hide1-lab-myron-fuse-ro-v1.zip` 在 `INSTALL generation=6001` 成功后执行
`ENABLE`，ADB 立即断开且设备自动重启；重启后模块未加载，pstore 为空。对比稳定提交
`5590eab` 和此前真机可进入 ACTIVE 的 v8，决定性配置差异是：稳定包默认
`shadow_mode=1`（仅 i_op），fuse-ro-v1 默认 `shadow_mode=0`（i_op/f_op/d_op 同时发布）。
同时，fuse-ro-v1 新增了 atomic_open 回调内安装 dentry shadow 和对正 dentry 执行
`d_drop()`。没有 panic trace，故不能在 f_op、d_op、atomic_open 三者之间进一步断言唯一
崩溃点；可确认的是完整 shadow 组合越过了已验证的 i_op-only 安全边界。

复核 Kasumi 后确认：其 dentry shadow 只在 lookup 结果路径安装；atomic_open 不修改 d_op
或 dentry hash 状态；6.12 的 f_op shadow 使用单独的模板/owner 生命周期，旧 KMI 才需要
额外 ingress/live bridge。PathGuard 已删除 atomic_open 中的 dentry 改写，并把模块变量、
控制脚本和打包元数据的默认值统一恢复为 `shadow_mode=1`。f_op/d_op/完整模式仍保留为
显式隔离实验，尚未准入。

新增源码契约测试，锁定默认模式和 atomic_open 无 dentry 改写。验证结果：

```text
pathguard_hide_vfs_model_test              Passed
pathguard_hide_vfs_teardown_contract_test  Passed
pathguard_hide_vfs_concurrency_test        Passed
pathguard_hide_loader_test                 Passed
pathguard_restricted_loader_test           Passed
android16-6.12 Kbuild                      CC/MODPOST/LD Passed
```

新模块为 AArch64 ELF64 REL，`__versions` 大小为 0，vermagic 仍是通用 prepared tree 的
`6.12.76-4k SMP preempt mod_unload modversions aarch64`。生成并传送到设备 Download：

```text
download/pathguard-hide1-lab-myron-fuse-iop-safe-v2.zip
ZIP SHA-256 cd43911a0eed749d5fc18088de1d3191ff4fec790ad6e80a27ff313fe7e606ce
KO  SHA-256 ef600ee1a83cd2303c2e66b09a9378986d8652e48f63c80c87ba7aef8bfc0cf6
default_shadow_mode=1
automatic_load=no
automatic_enable=no
```

本轮没有安装或 ENABLE 新包。下一次设备实验只能先复验 i_op-only 的
`load -> INSTALL -> ENABLE -> status` 稳定性；该基线通过后，f_op 与 d_op 必须分别隔离，
不能直接重试 mode 0。Hide 1.0 继续为 `unsupported`。

### 轮次 63：离线生命周期审查与 HideLab 身份闸门加固（2026-09-15）

审查发现三个独立的生命周期缺陷，均在设备侧复验前修复：

1. `hide1_commit_binding()` 原先整结构复制临时 binding，会复制 `list_head` 自引用和
   `spinlock_t`，随后清零临时对象可能破坏全局 binding。现在改为显式转移引用与快照，
   重新初始化全局链表/锁，并清空临时对象；新增契约测试锁定禁止整结构赋值。
2. 安装失败回滚原先只有 `synchronize_rcu()`，无法覆盖已退出 RCU 读侧但仍持有 active
   计数的回调。回滚释放 metadata 前现在执行 `hide1_drain_callbacks()`。
3. dentry stale worker 同样补充 dentry callback active drain，避免并发 `d_revalidate`
   仍在访问 metadata 时释放 shadow。

HideLab runner 的 `-KeepTargetProcess` 现在在采集前锁定 PID 与 mount namespace，采集前后
均要求一致；并且只有当前 run id/scenario 的 metadata 配合 `status=complete` 才算完成，
避免 target 重启或陈旧输出伪造有效证据。PowerShell 解析、宿主六项 hide/loader 测试及
Android 16/6.12 Kbuild（CC/MODPOST/LD）全部通过。未安装新包、未执行设备 ENABLE/DISABLE；
Hide 1.0 继续为 `unsupported`。

基于修复后的 KO 生成未安装实验包：

```text
download/pathguard-hide1-lab-myron-fuse-iop-safe-v5.zip
ZIP SHA-256  e47acb79067e9229eee250e08f635d62d48e621e8fd96b97f40f69b57d7bc4a9
KO SHA-256  0f03015e74966542c9089b76e92779971270b3230080ee85b69365772f370759

### 轮次 64：safe-v5 真机 ENABLE 与 active baseline（2026-09-15）

按批准安装并重启 `pathguard-hide1-lab-myron-fuse-iop-safe-v5.zip`。设备恢复在线，内核
release 精确匹配；通过 SukiSU `ksud insmod` 加载成功，`pathguard_hide1` 为 Live。使用
target PID `20371`、UID `10551`、mount namespace `4026536101`、generation `8001` 完成
INSTALL 和 i_op-only ENABLE，设备未重启，状态为 `ACTIVE`、`operation_mask=0x0fff`。

随后使用固定 PID/namespace、媒体权限和 all-files AppOp 执行只读 active baseline。runner
修复了普通 shell 无法读取 `/proc/<pid>/ns/mnt` 的权限问题后，采集结果可信，但 target
对已有 `/storage/emulated/0/Pictures/Nagram` 仍然可见：Java `exists/list`、direct-VFS
`stat/lstat/open/readdir/getdents64` 均返回可见结果，结论为 `LEAK`。control 对照正常，
fixture 与 root Oracle 均未变化。证据目录：
`build/device-evidence/hidelab-v5-active-baseline/20260915-231900/`。

该结果定位出 i_op-only 原型的现存正 dentry/cache 缺口；不能继续 full regression、不能
执行 mutation，也不能宣称 Hide 1.0 通过。设备仍为 ACTIVE，仅为实验状态；产品状态保持
`Hide 1.0 = unsupported`。下一步应离线实现并验证 positive dentry 失效/别名处理，再安排
恢复流程，避免在当前 ACTIVE binding 上重复 ENABLE。

### 轮次 65：Virtual View 分层方案和 FUSE 边界审计（2026-09-15）

本轮完成对本地参考源码及公开资料的交叉审计。

#### 关键证据

1. `refer/Kasumi-main/src/core/kasumi_dirhijack.c` 将 lookup、iterate、per-dentry
   `d_revalidate`、synthetic vnode 和 superblock 回收放在同一 view transport 中，并用
   SRCU/Tasks-RCU 处理可睡眠 callback 和 teardown；`kasumi_fop_bridge.c` 另行维护
   ingress/live file-operation bridge。
2. `refer/hide-refer/nomount-master/kernel/src/nomount.c` 使用完整路径规则树、目录
   节点、RCU children snapshot、virtual topology、lookup/readdir 和 dentry revalidate，
   说明“规则树 + view”比单 basename binding 更适合路径隐藏/重定向。
3. `refer/hide-refer/susfs4ksu-master` 及公开 SUSFS 提交显示，FUSE 的
   `getdents/readdir` 存在 fake-qstr、inode 查询和路径泄漏问题；公开修复还明确限制
   fuse/tmpfs 作为 `SUS_PATH` 目标。该证据与当前 `/storage/emulated/0` 的 LEAK 一致。
4. `refer/hide-refer/LKM-PathMask-main/kernel/pathmask.c` 的 syscall kretprobe 可补
   stat/open/access/getdents，但源码自己承认 ThinLTO inline、热点时序、open 后 fd
   清理以及只覆盖用户 syscall 的边界。

#### 网页调研来源

- Kasumi：<https://github.com/Anatdx/Kasumi>
- NoMount：<https://github.com/maxsteeel/nomount>
- PathMask：<https://github.com/Andrea-lyz/LKM-PathMask>
- HymoFS：<https://github.com/superturtlee/HymoFS>
- ZeroMount：<https://github.com/enginex0/zeromount>
- Linux VFS：<https://docs.kernel.org/filesystems/vfs.html>
- SUSFS FUSE 泄漏修复示例：<https://github.com/sidex15/android_kernel_lge_sm8150/commit/c07594f674038b1135f62c84c0a7ff7cf7c9efff>

#### 审计结论

- 当前 `i_op-only` 的失败不是单个回调漏写，而是 dcache/alias/路径上下文层级错误；
  ENABLE 后已有 positive dentry 可直接命中，绕过父目录 lookup。
- iop/fop/dop/atomic_open 一次性叠加又越过了当前 myron 上已验证的稳定边界，不能作为
  修复手段。
- 纯隐藏与重定向/注入必须分离：隐藏优先 synthetic negative 和 namei 早期拒绝；
  synthetic vnode 只用于可见的 virtual view。
- FUSE 路径必须有独立的 namei/FUSE coverage 后端；若只能使用普通 LKM operation
  shadow，则该路径保持 `unsupported`，不能以 syscall mask 或 mount 视图冒充 Hide 1.0。

#### 后续实现门槛

1. 先完成离线规则树、per-object metadata、RCU snapshot、generation/namespace/UID
   判定及 synthetic inode 生命周期模型。
2. 再实现非 FUSE 的 VFS shadow，并分别隔离验证 iop、fop、dop，不在设备上直接启用
   完整组合。
3. 对 FUSE 只先做 read-only namei coverage 探针；覆盖 lookup_fast/slow、open/namei、
   getattr/statx、readdir/filldir、relative/alias/cold-cache 后才进入实验实现。
4. 每个后端独立运行 HideLab 全量回归；任一 `LEAK`、`OVERBLOCK`、`CRASH`、`HANG`、
   `STATE_LIE` 或 `DESTRUCTIVE_FAIL` 都维持 `unsupported`。

### 轮次 66：社区定制 GKI 的真实兼容性边界（2026-09-15）

#### 网页调研对象

- <https://github.com/cofor1ae-byte/xiaomi-kernel-for_sm8850>
- <https://github.com/lsfqxd2006/RainKissM_Mi17pm_GkiKernel>
- <https://github.com/LokumKernel-SM8850/lokum-release>
- <https://github.com/haohao3001/android_device_xiaomi_myron>
- <https://source.android.com/docs/core/architecture/kernel/generic-kernel-image>
- <https://source.android.com/docs/core/architecture/kernel/modules>
- <https://source.android.com/docs/core/architecture/kernel/loadable-kernel-modules>
- <https://source.android.com/docs/core/architecture/kernel/gki-android16-6_12-release-builds>

#### 核验结果

1. 社区“定制 GKI”主要有 LKM、通用 GKI `Image` 重编译、stock kernel + 外部 LKM、
   真正设备源码重建四类。只有最后一类能提供完整 myron 内核修改的 ABI 依据。
2. SM8850 builder 的 README 明确以 OnePlus 15、OnePlus Ace 6T 或通用 AOSP 树为基础，
   并写明其它同版本 SM8850 机型“部分可兼容”；不能将其输出视为
   `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k` 的精确构建。
3. Android GKI 的可启动性来自 kernel `Image`、vendor ramdisk、DT/DTBO、
   `system_dlkm/vendor_dlkm` 分离以及稳定 KMI。替换 `boot.img` 的 Image 后继续复用
   原厂 vendor 镜像，可能启动，但启动成功不等于模块 CRC、CFI、FUSE 行为或 OTA 兼容。
4. 本地 `android_kernel_xiaomi_myron-prebuilt` 的 kernel/system_dlkm 是目标 release 的
   二进制证据，但没有源码、`.config`、`Module.symvers`、`vmlinux.symvers`、签名和
   Kleaf manifest；因此只能作为 stock ABI 样本，不能重建 Hide 2.0 内核。
5. 修改 localversion/vermagic、复用其它机型 `Module.symvers` 或关闭版本校验不能
   制造精确 ABI，反而会掩盖结构体、CFI、KMI 和 vendor module 依赖差异。

#### 对项目路线的影响

- 保留当前 stock kernel + SukiSU LKM，继续完成规则树、非 FUSE View backend 和
  coverage/preflight；该路线风险最小且可回滚。
- 并行建立独立的 SM8850 GKI candidate：只替换 boot kernel Image，把 SUSFS/namei
  改动编入内核，保留原厂 vendor_boot/vendor_dlkm/system_dlkm，并先用
  `fastboot boot`/备用 slot 验证。
- candidate 必须记录 Image、boot/vendor 镜像、KMI、DT/DTBO、模块依赖、pstore、
  HideLab 证据和 OTA 回滚结果；所有条件满足前，产品状态保持 `unsupported`。

### 轮次 67：LunarKernel 6.12 AnyKernel3 审计（2026-09-16）

审计对象：
`refer/hide-refer/LunarKernel6.12-V1.7-fix1-20260915-223331-os4-612-g1faff188861d-AnyKernel3`。

#### 证据

- 包是 AnyKernel3 预编译刷机包，包含 `Image`（42,240,512 bytes）、`Resuki.lkpatch`、
  `anykernel.sh`、`tools/` 和 `lunarkernelmodule/`；没有源码、`.config`、
  `Module.symvers`、`vmlinux`、`System.map`、Kleaf manifest、DTB/DTBO 或 DLKM 镜像。
- `strings Image` 得到：
  `6.12.69-android16-6-g1faff188861d-LunarKernel-V1.7-fix1`，Image SHA-256 为
  `9E3E6F60237104D95CA1E826D006BD41AF284DCFEE7187DFD81846ED1CC24BB1`。
- `version` 声明基础 `CONFIG_KSU=n`，可选 ReSukiSU + SUSFS；`Resuki.lkpatch` 以
  `LKPATCH1` 开头，并携带 BSDIFF 数据。脚本使用 `lkpatch apply` 后再校验目标哈希，
  说明这是绑定特定 Image 的二进制 patch。
- `anykernel.sh` 为 `do.devicecheck=0`、`block=boot`、`is_slot_device=auto`。
  `write_boot()` 的通用框架会枚举 vendor/system DLKM 和 dtbo，但包内没有相应镜像，
  因而本包的有效刷写目标是当前 slot 的 boot。
- `lunarkernelmodule` 的脚本和 `functions.sh` 只引用调频、调度、渲染、daemon、HyperOS
  属性及 `InfoManager` uname 参数；未发现 VFS、FUSE、namei、SUS_PATH 或路径隐藏代码。
- `capabilities.prop` 的 `uname_release=6.12.69-android16-6-gb1493ec68d4a-abogki514973465-4k`
  与 Image release 不同，不能将其当作 ABI 证明。

#### 与 myron 的比对

设备当前为：
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`。
LunarKernel 为 `6.12.69-android16-6-g1faff188861d`。两者不属于同一 GKI generation 或
同一 KMI build；包没有 myron 标识，也没有精确符号/配置输入。因此不能直接刷入 Redmi K90
Pro Max，也不能用于构建 PathGuard Hide 2.0。

#### 路线结论

该项目证明社区内核发布通常采用“预编译 Image + 同 Image 二进制 patch + AnyKernel3
boot 刷写”的工程化交付方式，而不是在设备上重建内核。PathGuard 可借鉴其哈希绑定、slot
感知、失败中止和附加模块事务安装；Hide 2.0 仍需独立的、设备匹配的 GKI candidate，或
在 stock myron kernel 上继续 LKM/探针路线。LunarKernel 本身没有可复用的 VFS/FUSE 隐藏
数据面，不能改变当前 `Hide 1.0/2.0 = unsupported` 的准入结论。

#### 预编译 Image 来源追踪

包本身没有源码 URL、manifest、CI 构建号或 `Module.symvers`。Image 的
`6.12.69-android16-6` 和 Clang/LLD 19.0.1 与 AOSP `android16-6.12` 的 6.12.69 GKI 发布线
相符；AOSP 文档也确认该发布线可提供 Image、pinned manifest、vmlinux 和符号文件，但这些
配套文件未随包发布。

Image 中的 `g1faff188861d` 在公开 Android Common Kernel 和 GitHub commit 搜索中没有命中。
同时二进制包含 `LunarKernel-DEV: Tsukiko Hakura&arclightx`、`mi_sched_ext_ops`、
`Xiaomi dispatch zones` 和大量 `sched_ext` 字符串，说明它不是未经修改的官方 GKI，至少
叠加了未公开的 Xiaomi/sched_ext/LunarKernel 构建改动。公开可找到的
`LunarKernel-Dev/lunarkernel_sched_extention` 只能证明其调度扩展思路和模块源码存在，不能
证明该仓库就是本 Image 的完整构建源。

当前可审计的来源链为：

```text
AOSP android16-6.12 / 6.12.69 基线
  -> 未公开的 LunarKernel/Xiaomi sched_ext 补丁或构建树
  -> 6.12.69 LunarKernel Image
  -> 针对 Image 哈希绑定的 Resuki.lkpatch
  -> AnyKernel3 发布包
```

该链是证据支持的最合理解释，不是已确认的唯一来源。复现构建必须向发布者索取精确
manifest、源码 commit、defconfig、构建日志、符号文件和 CI artifact；在此之前不能将该
Image 当作可复现、可移植或适用于 myron 的内核输入。

### 轮次 68：FUSE/namei coverage matrix 与 Android Kbuild 离线验证（2026-09-16）

#### 本轮修改

- coverage probe ABI 从 v1 升级为 v2，独立观测 namei、stat 和 FUSE 入口。
- 将 `path_openat`、`iterate_dir`、`do_filp_open` 标记为 required，其余入口为 optional。
  optional kprobe 注册失败只记录 `register_error`，不会让只读探针整体加载失败。
- `required_count` 改为从入口表自动统计，避免入口表变化造成错误状态。
- 状态读取器输出 ABI、状态、注册计数、命中数、`nmissed`、注册错误和 kernel release；
  探针仍不改变任何 VFS 行为。

#### 离线验证

宿主四项契约回归结果为 `100% tests passed, 0 tests failed out of 4`：

```text
pathguard_hide_probe_contract_test
pathguard_hide_vfs_model_test
pathguard_hide_vfs_teardown_contract_test
pathguard_hide_vfs_concurrency_test
```

Android 16/6.12 Kbuild 使用项目内 `ddk-clang-r536225`（Android clang 19.0.1）和
`build/ddk-kdir-local-linux/android16-6.12` prepared tree 成功完成 C 编译、modpost、
链接和模块元数据生成，产物为 AArch64 ELF relocatable：

```text
experimental/hide-vfs/coverage/pathguard_vfs_coverage_probe.ko
vermagic=6.12.76-4k SMP preempt mod_unload modversions aarch64
```

产物未包含 BTF：宿主没有 `pahole`，本轮通过显式 `PAHOLE=/bin/true` 跳过 BTF 后处理。
这不影响源码/Kbuild/ELF 结构检查，但该 `.ko` 不能据此获得设备准入资格。

#### 构建环境边界

`build/ddk-kdir-android16-6.12/extracted/android16-6.12` 缺少
`include/generated/autoconf.h` 和 `include/generated/rustc_cfg`；重新 `prepare` 需要
`flex`，而 WSL 未安装。项目内另一份 `ddk-kdir-local-linux/android16-6.12` 已包含完整
prepared headers、`vmlinux` 和 `Module.symvers`，且 release 为 `6.12.76-4k`，故用于本轮
离线编译。该 release 仍不是目标设备的
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，不能作为设备 ABI 匹配证明。

#### 路线门禁

本轮只完成 coverage 探针的离线构建和宿主契约回归，没有执行设备加载、FUSE 命中采集、
ENABLE、刷包或卸载。当前结论仍为：coverage matrix 可继续用于设备观测；Hide 1.0/2.0
数据面和产品状态保持 `unsupported`，下一步必须在设备上验证 required 入口注册及
FUSE/namei 实际命中后，才能决定继续 LKM backend 还是转向设备匹配的 GKI candidate。
```

### 轮次 69：myron coverage probe 受控设备回归（2026-09-16）

#### 前置状态与修复

- 设备为 `myron/25102RKBEC`，release 为
  `6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，root 由 SukiSU 提供。
- 重启后发现历史 `pathguard_hide1` 为 `state=ACTIVE`，但绑定 PID `20371` 已不存在。
  经明确确认执行 `hide1ctl disable`，返回 0，状态恢复为 `INACTIVE`，引用计数为 0；
  boot ID 未变化，设备未重启。
- 修复 `collect_vfs_topology.ps1` 的 Android `su -c` 引号传递，使 `stat` 能可靠采集；
  Android coverage v2 reader 增加 `uint64_t` 显式转换后，以 NDK clang 编译通过。

#### 模块加载与状态

使用本轮产物 `experimental/hide-vfs/coverage/pathguard_vfs_coverage_probe.ko`，通过
设备已验证的 `/data/adb/ksu/bin/ksud insmod` 加载。设备侧结果：

```text
load_exit=0
state=READY
registered=13/15
required=3/3
release=6.12.23-android16-5-g16e473de48a3-abogki462654244-4k
nmissed=0 for every registered probe
```

不可注册的 optional 符号为 `open_last_lookups` 与 `fuse_filldir`，错误码均为 `-ENOENT`；
这不影响 required 门禁。boot ID 在加载前后保持不变。

#### 只读命中矩阵

对 `/storage/emulated/0`、`/sdcard`、`/storage/self/primary` 以及
`/storage/emulated/0/Pictures` 执行 `stat`、目录枚举和 alias 访问，未创建、删除或修改
任何文件。前后计数增量包含：

```text
path_openat             > 0
do_filp_open            > 0
iterate_dir             +45
fuse_readdir            +25
fuse_dentry_revalidate  +738
```

对三个唯一不存在的 basename 执行只读 `stat` 后，额外观察到：

```text
fuse_lookup             +3
fuse_dentry_revalidate  +6
lookup_fast/lookup_slow/path_openat/do_filp_open > 0
```

所有已注册 probe 的 `nmissed` 保持 0。`fuse_atomic_open` 在没有 `O_CREAT` 或 mutation 的
只读场景中未命中，不能据此判定其不可用；它需要后续受控 mutation 场景验证。

#### 卸载与结论

`rmmod pathguard_vfs_coverage_probe` 返回 0；misc 节点和 `/proc/modules` 条目消失，
boot ID 未变化，`pathguard_hide1` 仍为 `INACTIVE`，dmesg 未出现 Oops、BUG、panic 或
Call trace。证据目录为 `build/device-evidence/vfs-coverage-probe-local/20260916/`。

本轮证明：myron 允许通过 SukiSU LKM loader 注册 required namei/FUSE kprobe，且共享存储
实际命中 readdir、lookup、dentry revalidate 路径。它仍然**不是 Hide 1.0 通过**：coverage
probe 只读观测、不改变 VFS 语义；`fuse_atomic_open` 及全部 mutation、并发、DISABLE/CLEAR
和正缓存失效仍未验证，产品状态保持 `unsupported`。

### 轮次 70：只读 FUSE-aware backend 离线实现（2026-09-16）

#### 实现范围

- 新增 `shadow_mode=4`，定义为只读 FUSE-aware 阶段。
- 该模式仅发布 `lookup`、`atomic_open`、`iterate_shared` 和 dentry
  `d_revalidate` wrapper，不替换 create、mkdir、mknod、symlink、unlink、rmdir、link、
  rename 等 mutation callback。
- INSTALL 在 mode 4 下只要求这四类 operation capability；完整模式仍要求 12 项。
- `iterate_shared` 的目录过滤增加 pinned parent inode 检查，避免同 basename 在其它目录
  被误过滤。
- 保留现有 target UID、mount namespace、thread group、generation 和 superblock/inode
  绑定；`LOOKUP_RCU` 下的隐藏 dentry 返回 `-ECHILD`，交给 namei 重试非 RCU 路径。
- 更新实验包控制脚本和文档，mode 4 只能显式传入，默认仍为安全的 mode 1。

#### 验证

- `pathguard_hide_vfs_teardown_contract_test` 通过，新增静态契约覆盖 mode 4、parent
  scope 和 mutation wrapper 不安装。
- 完整四项宿主回归通过：`100% tests passed, 0 tests failed out of 4`。
- 使用 Android clang 19.0.1、android16-6.12 prepared tree 完成模块 Kbuild，产物：

```text
experimental/hide-vfs/pathguard_hide1.ko
SHA-256=0303E3022C5F7643FD4A0108EDDA1C8A977464C4C25FA9E81172F967A1EBCCD9
vermagic=6.12.76-4k SMP preempt mod_unload modversions aarch64
__versions size=0
```

本轮仍跳过 BTF 后处理（宿主无 `pahole`）。该 release 不是设备精确 release，因此模块
尚未安装或 ENABLE。

#### 门禁结论

只读 backend 已完成离线实现，但尚无设备行为证据。下一步只能在卸载旧 Hide 模块、确认
设备健康后，以 mode 4 做一次受控 INSTALL/ENABLE；测试范围限于 lookup/open/readdir/
dentry revalidate、positive/negative cache 和 target/control namespace 对照，mutation
必须保持未替换并单独验证未被破坏。任何 crash、LEAK、OVERBLOCK、SEMANTIC_DRIFT 或
DESTRUCTIVE_FAIL 都阻止进入下一阶段。

### 轮次 71：mode 4 并发与 readdir 游标修复（2026-09-16）

离线审查发现 dentry shadow 安装路径曾通过 RCU 哈希表取得元数据指针，退出 RCU 保护后
仅用于判断“是否已安装”。stale worker 可以在该窗口完成 `hash_del_rcu`、宽限期和释放，
形成真实的元数据 UAF 风险。修复为在 `dentry->d_lock` 下只读取当前 `d_op->d_revalidate`，
不再把 RCU 元数据指针带出保护区；安装临界区保留二次指针校验。

同时修复 `iterate_shared` actor 的游标协议：过滤项显式推进 proxy `ctx->pos`，转发项在
调用原 actor 前同步原始 `pos`，返回后再同步回 proxy。这样 FUSE/小 buffer 的 getdents
不会因代理层丢失 native cookie 而重复或跳过记录。

宿主 `pathguard_hide_vfs_model_test`、`pathguard_hide_vfs_teardown_contract_test` 和
`pathguard_hide_vfs_concurrency_test` 均通过；Android clang 19.0.1 + `android16-6.12`
prepared tree Kbuild 通过（宿主无 `pahole`，BTF 后处理显式跳过）。未执行设备加载、
INSTALL 或 ENABLE，产品状态继续为 `unsupported`。

本轮模块 SHA-256 为
`facf085ed62fd4222f67103be4ff04c7a936d2258c0fc9cc628540efd548ca50`，vermagic 仍为
`6.12.76-4k SMP preempt mod_unload modversions aarch64`，`__versions` size 为 0。

### 轮次 72：myron mode 4 受控设备回归（2026-09-16）

经明确授权安装 `pathguard-hide1-lab-myron-fuse-ro-v2.zip` 后，使用 SukiSU loader 显式
加载 `shadow_mode=4`。模块 SHA-256 为
`facf085ed62fd4222f67103be4ff04c7a936d2258c0fc9cc628540efd548ca50`。Target 固定为
UID `10551`、PID `20535`、mount namespace `4026535993`，规则绑定
`/storage/emulated/0/Pictures/Nagram`，generation 为 `7101`。INSTALL 返回 0，能力掩码
为 `0x0fff`；ENABLE 返回 0，状态进入 ACTIVE，boot ID 未变化。

只读 baseline 证据目录：

```text
build/device-evidence/hide1-fuse-ro-v2/20260916-205034/
```

结果为明确 `LEAK`，但数据面分界清晰：

- Target 的 Java `list`/NIO directory stream、libc `readdir` 和四种 buffer 的
  `getdents64` 均未出现 `Nagram`，证明新打开目录文件上的 `iterate_shared` shadow 生效；
- Target 的 Java `exists`/NIO exists、`stat`、`lstat`、`access`、`open`、`opendir`、
  `fstatat`、`statx`、`faccessat` 和 `openat` 仍成功，说明 positive dentry/lookup 视图未生效；
- `/sdcard` 与 `/storage/self/primary` alias 同样表现为“枚举隐藏、直接解析泄漏”；
- Control UID `10550`、namespace `4026536033` 保持完全可见，没有 OVERBLOCK；
- Root Oracle 前后完全一致，没有 mutation 或 DESTRUCTIVE_FAIL。

首次 `LEAK` 后按门禁停止，没有继续 cache-order、concurrency 或 reliability。DISABLE、CLEAR
均返回 0，状态回到未绑定；同一 APK 的恢复基线位于
`build/device-evidence/hide1-fuse-ro-v2-restore/20260916-205148/`，Target 和 Control 均恢复
完整可见，Oracle 不变。随后 rmmod 成功，设备节点消失，boot ID 始终为
`ec2bae63-1dfa-466b-a680-2f0965af234c`，未发现 Oops、BUG、panic、Call trace 或 pstore 记录。

本轮证明 f_op 发布、过滤和恢复路径已跨过最小设备门禁，但 lookup/dentry cache 仍未通过。
下一步不应扩大 mutation，而应增加只读 callback 命中计数或 trace，区分：目标 dentry shadow
未安装、`d_revalidate` 未进入、observer/parent scope 判定未命中，或 positive fast path 根本
未执行目标 dentry operation。产品状态保持 `unsupported`。

本轮原始 runner 把 `backend` 硬编码为 `none`。原始证据不修改，实际 ACTIVE 状态由控制面
输出和 generation 证明；后续 runner 已改为显式 `-Backend` 参数，避免再次产生该歧义。

### 轮次 73：mode 4 正 dentry 修复与诊断 ABI（2026-09-16）

针对轮次 72 的“枚举隐藏、直接解析泄漏”，离线修复两处根因：

1. ENABLE 时若 `d_lookup(parent, basename)` 没有命中，不再放弃；模块通过绑定 parent
   路径解析一次完整的 `parent/basename`，对真实正 dentry 安装 observer-aware `d_revalidate`
   shadow。不存在的未来对象仍允许安装继续，避免把只读 prototype 误变成创建后端。
2. 目标观察者命中 `d_revalidate` 后不再设置 stale 并异步恢复 `d_op`。shadow 必须持续到
   DISABLE/CLEAR；否则第一次重验证后正 dentry 会重新落回原始 FUSE operation。

新增 status 诊断计数：lookup、atomic_open、readdir、d_revalidate 的总/隐藏计数，以及
dentry shadow 安装总/成功/失败计数。计数只读，不改变策略，用于下一轮设备实验区分 lookup
未进入、observer scope 未命中和正 dentry 未安装。

离线验证：teardown contract 通过；Android clang 19.0.1 + android16-6.12 Kbuild 通过；
新模块 SHA-256 为 `cfd6cf7ba08238cf12042809d4fce779b6859d62a99df3cff92d6b6a971b1036`，
`__versions` size 仍为 0。新控制程序 SHA-256 为
`ef37a0bccf339237c8875899c22eafd504f5cd132f31a9246442304608a2c5d9`。尚未在设备加载。

### 轮次 74：mode 4 v3 诊断包真机只读回归（2026-09-16）

经明确授权在 Redmi K90 Pro Max / `myron` 上加载
`pathguard-hide1-lab-myron-fuse-ro-v3-diagnostics.zip`。设备 release 为
`6.12.23-android16-5-g16e473de48a3-abogki462654244-4k`，Target UID `10551`、PID
`20230`、mount namespace `4026536018`，规则绑定
`/storage/emulated/0/Pictures/Nagram`，generation `7102`。模块和控制程序哈希分别为
`cfd6cf7ba08238cf12042809d4fce779b6859d62a99df3cff92d6b6a971b1036` 与
`ef37a0bccf339237c8875899c22eafd504f5cd132f31a9246442304608a2c5d9`。

LOAD、INSTALL、ENABLE 均成功，boot ID
`e11d7fc0-4d48-40d3-a809-449a5cea370f` 未变化。状态诊断计数为：

```text
lookup=32/30
atomic_open=9/9
readdir=66/21
revalidate=820/72
dentry_install=32/32/0
```

只读 HideLab baseline 证据目录为：

```text
build/device-evidence/hide1-fuse-ro-v3/20260916-233538/
```

结果为 `PASS`：Target 的 Java File/NIO、libc readdir、四种 getdents64、stat/lstat、
access、open/openat、alias 路径和 descendant 访问均按隐藏预期返回；Target 与 Control
Oracle 均未变化，Control 没有 OVERBLOCK。该轮未执行 mutation、cache-order、并发或
reliability 扩展，因此只证明 mode 4 只读 backend 在单设备/单 UID/单 namespace/单 basename
实验范围内通过，不构成 Hide 1.0 全量通过。

按恢复协议执行 DISABLE、CLEAR、rmmod；模块已从 `/proc/modules` 消失，`/dev/pathguard_hide1`
不存在，boot ID 保持不变，设备在线。产品状态仍为 `Hide 1.0 = unsupported`。下一步只能在
审查诊断计数和恢复证据后，单独设计 cache-order、并发及生命周期回归；mutation 封闭、设备
准入和正式集成继续冻结。

### 轮次 75：只读 cache-order、并发与生命周期回归（2026-09-17）

在同一 myron 设备上固定 Target PID `8062`、UID `10551`、mount namespace
`4026536035`，使用 generation `7103` 完成 v3 只读后端复验。cache-order 证据为
`build/device-evidence/hide1-fuse-ro-v3-cache-order/20260917-002731/`，五种顺序
（cold open、cold opendir、stat-then-open、readdir-then-open、positive-warm-then-open）
在主路径 `/storage/emulated/0/Pictures/Nagram` 均返回 `-ENOENT`，summary 为 `PASS`。

并发证据为 `build/device-evidence/hide1-fuse-ro-v3-concurrency/20260917-003012/`。20
线程的 stat/open/readdir 计数均为 0 次泄漏，Target/Control Oracle 与 fixture 未变化，
summary 为 `PASS`。高并发后 status 仍正常，未观察到安装失败、崩溃或卡死。

reliability 证据为 `build/device-evidence/hide1-fuse-ro-v3-reliability/20260917-003119/`。
主路径 1000 次 stat/open/readdir 均无泄漏；generation、capacity、namespace、unload
控制项按当前 ABI 返回 `unsupported`，未将未实现能力伪装为通过，summary 为 `PASS`。

随后执行 DISABLE、CLEAR，恢复 baseline 证实目标重新可见（证据目录
`build/device-evidence/hide1-fuse-ro-v3-restore/20260917-003328/`），再执行 rmmod。
boot ID `e11d7fc0-4d48-40d3-a809-449a5cea370f` 保持不变，模块和设备节点均消失，设备
在线。部分 alias 路径因 Android 权限返回 `EACCES/setup_error`，不纳入主路径隐藏通过依据。

本轮只证明只读 cache-order、并发访问和 DISABLE/CLEAR/卸载恢复在当前单设备实验范围内
通过；未实现 mutation 封闭、真正的 namespace 销毁/OTA 重新准入或完整生命周期控制 ABI。
Hide 1.0 继续保持 `unsupported`。

### 轮次 76：shadow lifecycle 离线实现与 Kbuild 验收（2026-09-17）

本轮先修复生命周期根因，再进入 mutation：

- UAPI ABI 升为 `2`，status 增加 `lifecycle`、i_op/f_op/d_op active 计数和 f_op
  `open_count`，使 STOP_NEW、RESTORE、DRAIN、FREE 的运行时状态可观察；
- i_op/f_op/d_op metadata 各自增加 wait queue，callback exit 同时唤醒全局和对象级 drain；
- dentry shadow 的 RCU hash 发布纳入 `hide1_meta_lock`，卸载前先预检所有 dentry 的当前
  `d_op` 所有权，发现外部替换则保持原安装状态并返回 `-EAGAIN`；
- generation 变化或 dentry unhash 时标记 stale 并调度 workqueue，worker 在 SRCU、RCU、
  active 计数完成后才释放 dentry 引用和 metadata；
- 安装事务失败继续按逆序恢复 i_fop/i_op/d_op，等待 callback drain 后才释放对象和 module pin。

离线验证命令：

```text
cmake --build build --target pathguard_hide_vfs_teardown_contract_test pathguard_hide_vfs_model_test pathguard_hide_vfs_concurrency_test --config Debug -j 4
ctest --test-dir build -C Debug -R pathguard_hide_vfs_(model|concurrency|teardown_contract) --output-on-failure
wsl make -C experimental/hide-vfs KDIR=.../build/ddk-kdir-local-linux/android16-6.12 LLVM=1 LLVM_IAS=1 PAHOLE=/bin/true clean all
```

三项宿主测试全部通过；真实 Kbuild 完成 `CC -> MODPOST -> LD -> BTF`，产出新的
`experimental/hide-vfs/pathguard_hide1.ko`。本轮没有安装、加载或执行设备 mutation；阶段 1
只证明离线生命周期和构建契约，产品状态仍为 `Hide 1.0 = unsupported`。下一步是基于该
生命周期基础实现并审查 mutation 前置封闭，仍须先完成离线矩阵。

### 轮次 77：mutation 前置封闭离线实现（2026-09-17）

在阶段 1 生命周期基础上完成第一轮 mutation 数据面：`create/mkdir/mknod/symlink/unlink/rmdir`
在原始 filesystem callback 前统一执行 target、parent、basename 判定；`link` 同时检查
source dentry、source parent/superblock、目标 dentry 和固定隐藏 inode；`rename` 同时检查
source/destination，两端 superblock 不一致返回 `-EXDEV`，所有非零 flags 在未有完整语义
前返回 `-EOPNOTSUPP`；`atomic_open` 对普通 open、`O_CREAT`、`O_EXCL`、`O_TRUNC` 分支统一
在真实 callback 之前拒绝隐藏 basename。status 增加 mutation 总调用、前置拒绝、原始回调和
unsupported 计数，便于设备回归确认拒绝发生在数据修改之前。

模型新增 link source/destination 与 atomic-open flag 矩阵，源码契约测试新增 operation
顺序和 fail-closed flags 断言。宿主 `pathguard_hide_vfs_model_test`、
`pathguard_hide_vfs_concurrency_test`、`pathguard_hide_vfs_teardown_contract_test` 全部通过；
Android clang r536225 + prepared `android16-6.12` tree 完成 `CC -> MODPOST -> LD -> BTF`。
本轮未安装、加载或执行设备 mutation，尚无 disposable fixture Root Oracle 证据，产品状态仍
为 `Hide 1.0 = unsupported`。

### 轮次 78：HideLab SukiSU root wrapper 修复与设备解锁前置（2026-09-17）

离线检查发现 HideLab runner 的 Root Oracle 和 mount namespace 读取仍使用普通
`su -c`。在当前 SukiSU 设备上该路径进入 `ksu` SELinux 域，无法创建
`/storage/emulated/0/Pictures/PathGuardHideLab/<run-id>` disposable fixture，导致
测试在 fixture 初始化阶段失败。runner 已统一改为 `su -W -c`，包括 fixture 创建、清理、
target mount namespace 读取和结束时复核；该修复已通过现有三项宿主 VFS contract 测试。

真机重跑暂未开始：设备当前 `sys.boot_completed=1` 但用户尚未解锁，
`locksettings get-state` 返回存在锁屏凭据且未提供旧凭据，`ce_available` 为空，
因此测试 APK 的 CE 数据目录不可用，Activity 无法解析。模块状态保持
`state=0/lifecycle=1`，无活动 callback、fd 或 mutation 计数；未执行 ENABLE、INSTALL 或
任何 mutation。设备解锁后才能继续 disposable fixture、INSTALL/ENABLE 及真机 mutation
回归，产品状态仍为 `Hide 1.0 = unsupported`。

### 轮次 79：mutation-v3 正确 parent 绑定与 cache-order 通过（2026-09-17）

设备解锁后从 `/data/local/tmp/pathguard-hide1-v3.ko` 加载同一 DDK 产物。首次实验误将
`parent` 绑定为 fixture 的 `hidden` 目录并再次使用 basename `hidden`，目标实际变成
`hidden/hidden`，HideLab 报 `LEAK`。该结果判定为实验编排错误而非后端结论，立即执行
`DISABLE -> CLEAR -> rmmod`，模块消失且设备未重启。

随后按正确拓扑重新绑定：

```text
parent=/storage/emulated/0/Pictures/PathGuardHideLab/20260917-181500
basename=hidden
uid=10552 pid=22948 mnt_ns=4026536088 generation=8102
```

`ENABLE` 成功，boot ID 未变化。HideLab cache-order 证据目录为
`build/device-evidence/hide1-mutation-v3-cache-order-correct/20260917-205450/`，cold
open、cold opendir、stat-then-open、readdir-then-open、positive-warm-then-open 五种
顺序全部通过；Target 返回 `ENOENT`，Control 保持可见，两个 Root Oracle 与 fixture 均未
变化，summary 为 `PASS`。status 计数为 `lookup=34/33`、`atomic_open=27/27`、
`readdir=75/24`、`revalidate=379/84`、`dentry_install=35/35/0`，active/open/mutation
计数均为 0。

本轮只证明正确 parent 绑定下的只读 cache-order；并发、mutation、生命周期和设备准入
仍未完成，产品状态继续为 `Hide 1.0 = unsupported`。

### 轮次 80：并发泄漏根因与 synthetic-negative cache identity 修复（2026-09-18）

在 generation `8102` 的正确 parent 绑定后执行 20 线程并发回归，证据目录为
`build/device-evidence/hide1-mutation-v3-concurrency-correct/20260917-210315/`。Target
主路径的结果为 `stat=1336/2000`、`open=1336/2000`、`readdir=0/2000`，summary 明确为
`LEAK`；Control 与 Root Oracle 正常，设备无 crash/hang。按 fail-closed 门禁停止 reliability
和 mutation，并执行 `DISABLE -> CLEAR -> rmmod`。因此轮次 75 的旧 v3 并发通过不能替代
本次 disposable fixture 和正确绑定下的失败事实。

对照 Android 16/6.12 `fs/namei.c`、`fs/dcache.c`、FUSE `fs/fuse/dir.c` 与 NoMount 后确认
根因不是简单的 lookup 漏装，而是 kernel backend 违反了自身 cache model：

- `hide1_lookup()` 创建的 synthetic negative 与 ENABLE 时捕获的真实 positive dentry 没有
  metadata 身份区分；
- `hide1_d_revalidate()` 对两者都返回 `0`，导致 synthetic negative 每次访问都被
  `d_invalidate()`，并行路径反复重建 dentry；
- 原 stale worker 又把 `d_unhashed()` 当成立即回收条件，在其他 path walk 仍持有引用时恢复
  原始 FUSE `d_op`，使旧 positive dentry 可间歇通过，形成 stat/open 泄漏；
- 恢复时写回完整历史 `d_flags` 还可能覆盖 positive/negative 类型位，属于独立的缓存一致性
  风险。

修复后的 metadata 显式保存 `synthetic_negative` 与 `cache_generation`。同一 generation 的
Target 对 synthetic negative 返回有效缓存（`1`）；真实 positive 对 Target 才返回失效，
非目标观察者或不同 generation 必须使 synthetic negative 失效。stale work 改为 delayed
work，只有 dentry 已 unhashed 且引用计数仅剩模块自身 pin 时才恢复并释放 metadata；恢复只
修改 `DCACHE_OP_REVALIDATE` 位，不覆盖其余动态 `d_flags`。这与
`pg_hide1_evaluate_cache()` 的既有契约一致，也保留 Control 不被 synthetic negative
OVERBLOCK 的语义。Control 触发真实 FUSE lookup 后，wrapper 会在 `d_lookup_done()` 唤醒并行
waiter 前对实际返回 dentry 重新安装 shadow；安装失败则丢弃该缓存并返回错误，防止 Target
随后从共享 dcache fast path 读取未治理的 positive dentry。

离线验证结果：

```text
pathguard_hide_vfs_model_test              PASS
pathguard_hide_vfs_concurrency_test        PASS
pathguard_hide_vfs_teardown_contract_test PASS
Android 16/6.12 Kbuild                     CC -> MODPOST -> LD
undefined symbols                          68/68 resolved in vmlinux
__versions                                 size=0 (restricted-loader contract)
```

并发模型测试额外执行 20 线程 Target/Control 交替状态转换，覆盖 synthetic negative、real
positive 的共享缓存替换；Target 遇到 real positive 必须失效，Control 遇到 synthetic negative
也必须失效，任何一侧都不得错误保留对方的 observer-specific 缓存。

新实验包为 `download/pathguard-hide1-lab-myron-fuse-ro-v6-shared-cache.zip`，SHA-256
`d611375fef2781af51a3254c69eec32c93e77fd80d7f181bf5864bd121b57b8c`；模块 SHA-256
`d74a11a561681180218ff220f1648181879a59cace7ce625f14a1be8764590db`。构建时关闭了
仅用于模块调试类型信息的 BTF 生成，因为本机没有 `pahole`；这不改变模块代码、重定位、空
`__versions` 或 SukiSU restricted-loader 路径。记录时设备未连接 ADB，因此 v6 尚未上传、
安装或真机验证；阶段仍为失败后待复验，产品状态继续保持 `Hide 1.0 = unsupported`。
## 轮次 81：v6 shared-cache 真机 cache-order 复验与泄漏定位（2026-09-18）

v6 包已在 Redmi K90 Pro Max / `myron` 上以 `shadow_mode=4` 加载。绑定参数为
UID `10552`、PID `22585`、mount namespace `4026536038`、generation `9001`，父目录为
`/storage/emulated/0/Pictures/PathGuardHideLab/20260918-120000`，basename 为 `hidden`。
`ENABLE` 成功且 boot ID 未变化。

首次 cache-order 结果为 `LEAK`：cold open、cold opendir、stat-then-open 返回
`ENOENT`，但 `readdir-then-open` 和 `positive-warm-then-open` 返回成功。根因是
`iterate_shared` 过滤 FUSE 目录记录后仍保留同一父目录下的正 dentry，后续 open 复用
该缓存而绕过隐藏查找。

本轮同时执行了 `DISABLE -> CLEAR -> rmmod`，设备在线、模块节点消失，未发生重启。

## 轮次 82：过滤目录项同步 dentry drop（v7）与 cache-order 通过（2026-09-18）

离线修复在 `hide1_dir_actor()` 过滤目标 basename 时，按父 dentry+basename 查找并
`d_drop` 已缓存子 dentry，同时将本模块 shadow 标记为 stale。宿主
`pathguard_hide_vfs_model_test`、`pathguard_hide_vfs_concurrency_test`、
`pathguard_hide_vfs_teardown_contract_test` 全部通过；Android 16/6.12 Kbuild 完成
`CC -> MODPOST -> LD -> BTF`。

实验包：`download/pathguard-hide1-lab-myron-fuse-ro-v7-cache-drop.zip`，设备端与本地
SHA-256 为
`31ae0b0e39f1e028299a3afa50365d897c68ea10767355f1f56a68fc21bcfa2`。
v7 在新 fixture、generation `9002`、PID `19487`、namespace `4026536023` 上
`ENABLE` 成功。HideLab cache-order 证据：

```text
build/device-evidence/hide1-v7-cache-order/20260918-201258/
```

Target 五种顺序均为 `-1/ENOENT`，Control 保持可见，Root Oracle 未变化，结论为
`PASS`。

## 轮次 83：ACTIVE stale dentry 生命周期竞态修复（v8）与并发通过（2026-09-18）

v7 cache-order 通过后，20 线程并发仍出现 external.0 `stat/open=1692/2000` 的泄漏。
根因是 stale workqueue 在 `ACTIVE` 期间恢复原始 `d_op` 并释放 metadata，和并发路径
重新使用正 dentry 形成窗口。修复后，ACTIVE 期间 stale 只清除 stale 标记并保留 shadow；
仅由 `DISABLE/RESTORE` 统一恢复和回收。

实验包：`download/pathguard-hide1-lab-myron-fuse-ro-v8-concurrency-cache.zip`，模块
SHA-256 为 `8a574155d75b8102c2924775f209ec75a6c1e54149b16dee326d96076c9acdfb`，ZIP
设备端 SHA-256 为 `99ba0ae0e39f1e028299a3afa50365d897c68ea10767355f1f56a68fc21bcfa2`。
设备重启后以 generation `9003`、PID `19620`、namespace `4026536097` 完成加载、
绑定和 `ENABLE`，boot ID 保持不变。

并发证据：

```text
build/device-evidence/hide1-v8-concurrency/20260918-202124/
```

Target external.0 的 `concurrency.stat/open/readdir` 均为 `0`，Control 通过，Root
Oracle 未变化，结论为 `PASS`。随后 reliability 1000 轮顺序回归通过：

```text
build/device-evidence/hide1-v8-reliability-rerun/20260918-202742/
```

执行 `DISABLE` 后 baseline 恢复为 `BASELINE_VISIBLE_NOT_HIDE_PASS`；再执行
`CLEAR -> rmmod`，状态回到 `FREE`、active/open_count 为 0、`/dev/pathguard_hide1`
消失，设备保持在线。

本轮结论：只读 FUSE-aware 单设备/单 UID/单 namespace/单 parent/basename 的
cache-order、20 线程并发、1000 轮 reliability 和 DISABLE/CLEAR/rmmod 恢复均有真机
证据；mutation、跨 alias 完整一致性、namespace 销毁、OTA 重新准入和 daemon 集成仍未
完成，产品状态继续为 `Hide 1.0 = unsupported`。

## 轮次 84：mode 0 mutation parent-level 闸门与 Control 对照（2026-09-18）

为避免旧探针把“目标不可见”误报为副作用，`tests/device/hide/hide_vfs_probe.cpp`
将 external mutation 的 `side_effect` 定义改为“系统调用实际返回成功”；真实文件状态
仍由 root oracle 判定。同时补充了直接针对 governed parent/basename 的
`openat(O_CREAT|O_EXCL)`、`mkdirat`、`rmdir`、`renameat`、`linkat` 和 `symlinkat` 用例。

使用 v8 模块 `shadow_mode=0`、generation `9005`、UID `10552`、PID `26948`、namespace
`4026536047` 完成一次 disposable fixture 攻击。Target 结果如下：

- hidden 目录内 create/truncate/mkdir/unlink/rename/link/symlink 全部 `-1/ENOENT`，
  `side_effect=false`；
- parent/basename 的 create/mkdir/rmdir/rename/link 全部 `-1/ENOENT`；
- parent/basename `symlinkat` 返回 `-1/EACCES`，未产生副作用。这是 Android shared-storage
  FUSE 的平台拒绝，不是 PathGuard 统一返回的 `ENOENT`，因此按当前严格 Hide 语义记为
  **未通过项**，不能宣称 mutation 全矩阵通过。

Target 观察期间 Root Oracle 保持不变。随后以 Control UID 执行同一攻击：隐藏目录内的
create/truncate/mkdir/unlink/rename 成功并产生预期修改，证明目标绑定没有把 Control
错误 overblock；Control 的 link/symlink 仍受设备 FUSE 的 `EACCES` 平台限制。状态计数
显示 `mutation=5/3/2/0`（Control 原始 callback 与 Target 前置阻断均可观察）。测试结束
执行 `DISABLE -> CLEAR -> rmmod` 并删除 disposable fixture，设备在线、无重启。

证据文件：

```text
build/device-evidence/hide1-v8-mutation-v3-target.jsonl
build/device-evidence/hide1-v8-mutation-v3-control.jsonl
build/device-evidence/hide1-v8-mutation-v3-before.txt
build/device-evidence/hide1-v8-mutation-v3-after.txt
```

结论：mutation 前置封闭已在 mode 0 的单设备 fixture 上覆盖并证明多数操作无副作用，
但 symlink 的严格 `ENOENT` 语义仍受 FUSE 平台 `EACCES` 阻塞；namespace 销毁、OTA
准入和正式 daemon 集成尚未开始，产品状态继续为 `Hide 1.0 = unsupported`。

## 2026-09-18：Target 退出生命周期实验与修复

v8 真机实验使用 generation `9101` 绑定旧 Target PID `528`、mount namespace
`4026536020`。Target 被 force-stop 后设备 boot ID 未变化，旧 Target 不再提供
隐藏视图；同 UID 新进程结果为 `BASELINE_VISIBLE_NOT_HIDE_PASS`，说明不会错误
继承旧 binding。但模块状态仍显示 `ACTIVE`，暴露出控制面状态陈旧问题。

本阶段完成离线修复：

- 在 `STATUS`/`DISABLE` 的全局锁路径检测 pinned task 的 `PF_EXITING`；
- 发布 `retiring=true`、`STOP_NEW`、`INACTIVE` 和 `last_error=-ESRCH`；
- `DISABLE` 即使状态已发布为 `INACTIVE`，只要仍有 ingress/dentry shadow，
  仍执行事务性 `RESTORE -> DRAIN -> FREE`；
- 未增加 `sched_process_exit` hook，避免在任意退出回调中扩大 LKM 锁/卸载风险。

离线验证：

```text
ctest --test-dir build -C Release -R pathguard_hide_vfs_(teardown_contract|model|concurrency)_test
100% tests passed (3/3)
```

设备清理已完成：`/proc/modules` 无 `pathguard_hide1`、`/dev/pathguard_hide1`
消失、disposable fixture 删除、boot ID 未变化。修复后的 LKM 尚未完成新一轮
真机回归，因此生命周期和产品准入仍保持 `Hide 1.0 = unsupported`。

## 2026-09-18：v9 Target 退出生命周期真机回归

用户安装 `pathguard-hide1-lab-myron-iop-v9-exit-revoke.zip` 后，按既有
`su -mm -c nsenter` 路径加载模块并完成 INSTALL：

```text
target_pid=19599
target_uid=10552
target_mnt_ns=4026535992
generation=9201
operation_mask=0x0fff
```

ENABLE 返回 0，状态为 `ACTIVE/RUNNING`，boot ID 保持
`7d3d7201-c6ad-4ff1-bb98-0691622d4d82`，设备未重启。

### Target 退出结果

对旧 Target 执行 `am force-stop dev.pathguard.hideprobe.target` 后，立即查询
状态得到：

```text
state=INACTIVE
lifecycle=STOP_NEW
last_error=-3 (ESRCH)
target_pid=19599
target_mnt_ns=4026535992
```

随后以同 UID 启动新 Target（新 PID `22970`、新 namespace `4026536040`），
对原 fixture 执行 HideLab baseline，结果为：

```text
BASELINE_VISIBLE_NOT_HIDE_PASS
fixture_unchanged=true
target_oracle_changed=false
control_oracle_changed=false
```

这证明旧 binding 不会错误授予新 task 隐藏权限，并且退出检测会把控制面从
`ACTIVE` 收缩为 fail-closed 的 `INACTIVE/STOP_NEW`。

### 数据面边界

退出前对 v9 `shadow_mode=1` 执行的 baseline 观测为 `LEAK`：

```text
java.external.0.exists: visible
external.0.lstat: visible
external.0.open: visible
```

因此本轮只证明 Target 退出生命周期契约，不证明 Hide 1.0 隐藏能力；i_op-only
模式仍不能作为只读 FUSE 后端。证据目录：

```text
build/device-evidence/hide1-v9-exit-baseline/20260918-214302/
build/device-evidence/hide1-v9-exit-new-target/20260918-214803/
```

最后执行 `DISABLE -> CLEAR -> rmmod`，删除 fixture；模块、设备节点均消失，
boot ID 未变化。产品状态继续为 `Hide 1.0 = unsupported`。
