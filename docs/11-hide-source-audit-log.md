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
