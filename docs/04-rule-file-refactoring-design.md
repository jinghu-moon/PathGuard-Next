# PathGuard Next 规则文件重构设计

> 状态：Implemented（RF0～RF9 全部通过）
>
> 文档版本：0.7
>
> 日期：2026-07-27
>
> 适用范围：PathGuard Next 控制面、CLI、daemon 与 Manager

## 1. 文档目的

本文定义 PathGuard Next 用户规则文件的破坏性重构方案。目标是在不牺牲后续模块扩展能力的前提下，使规则具备以下特征：

- 普通用户能够直接阅读并手工修改。
- 常用规则保持“一行一条”，重定向方向一眼可见。
- 路径、字符串、数组、注释和 Unicode 复用成熟 TOML 语义。
- 用户规则与运行时策略分离，应用启动路径不解析文本配置。
- 所有歧义、冲突和不支持能力在编译期拒绝。
- 新能力通过独立命名空间扩展，不把全部规则压进通用 `type` 表。

本文替代现有 `rules.ini` 的表层语法设计，不改变 mount namespace、VFS、Policy IR 和 `policy.bin` 作为运行时快照的总体架构。

## 2. 决策摘要

采用以下方案：

1. 用户编辑的唯一策略源命名为 `rules.toml`。
2. `rules.toml` 定义为 **PathGuard 扩展 TOML**，不是严格 TOML。
3. 除 `redirect` 数组中的 `->` 外，其余输入语法必须遵循 TOML。
4. `"源路径" -> "目标路径"` 是 `{ from = "源路径", to = "目标路径" }` 的受限语法糖。
5. 编译器先执行词法级脱糖，再使用成熟 TOML 解析器构建配置树。
6. `rules.toml` 是唯一事实来源；`policy.bin` 是只读生成物，不允许反向修改源文件。
7. daemon 和 Zygisk 只消费验证完成的 `policy.bin`，不直接解析 `rules.toml`。
8. 当前可执行能力仍由 capability 和 executor 状态决定；语法被识别不代表尚未完成的能力可以生效。
9. 项目尚未正式发布，直接进行破坏性迁移，不长期维护 `rules.ini` 与 `rules.toml` 双解析器。
10. 源文档、展开后策略、Canonical Policy IR 和运行时二进制视图使用不同数据模型，不复用一个万能 `PolicyDocument`。
11. 规则编译只判断设备无关的语法和策略语义；设备 capability/topology 准入在独立阶段完成。
12. daemon 是活动 `policy.bin` 和状态文件的唯一发布者；CLI 和 Manager 不直接覆盖活动快照。
13. 核心编译路径只保留源码缓冲区、行索引和必要 span；无损编辑模型与 `fmt` 按实际需求后置。
14. `->` 已通过 Phase D0 Go/No-Go：保留其方向表达价值，严格 TOML 只作为退出对照，不在 format 1 中形成双语法。
15. Phase D0 已冻结唯一生产语言和 parser：C++20 + toml++ v3.4.0。Rust/`toml_edit` 候选已完成验证但未选择，原型和 C ABI 已删除；不得恢复中间 FFI 或长期双实现。

总体流程：

```text
rules.toml（用户编辑，唯一事实来源）
    |
    v
PathGuard 扩展语法扫描与 -> 脱糖
    |
    v
标准 TOML 文本 / TOML 语法树
    |
    v
字段解析、路径规范化、作用域展开、冲突检测
    |
    v
Canonical Policy IR + Policy Requirements
    |
    v
设备准入（capability / topology snapshot）
    |
    v
policy.bin 编码、校验与原子发布
    |
    v
daemon / Zygisk / companion
```

## 3. 当前格式的问题

当前 `module/config/rules.ini` 示例：

```ini
schema = 2
failure = open
allow_legacy_string_bind = true

[org.localsend.localsend_app]
users = 0
processes = *
provider = virtualize
redirect Download/localsend-source -> Download/localsend-redirect
```

它实际是“INI 键值 + 自定义动作语句”的混合 DSL，存在以下问题：

### 3.1 文件名与真实语法不一致

`redirect A -> B`、`deny A`、`isolate -> B`、`export A -> B @mode=copy` 都不是 INI。标准 INI 工具无法完整解析，项目又必须自行处理路径、注释、转义和错误位置。

### 3.2 用户意图与执行细节混杂

`failure`、`allow_legacy_string_bind` 和 `provider = virtualize` 暴露了底层实现概念。普通用户真正关心的是：

- 哪个应用受规则约束。
- 哪些目录禁止访问。
- 哪个目录被重定向到哪里。
- 是否覆盖 Android 文件选择器访问路径。

### 3.3 自定义解析器维护面持续扩大

当前 `ParseRulesIni()` 同时承担行切分、空白处理、列表解析、动作识别、箭头切分和字段校验。规则能力继续扩大后，解析器会逐步重复 TOML 已解决的字符串、Unicode、注释、类型和源码定位问题。

### 3.4 规则表达不统一

全局字段、应用字段和动作语句使用不同语法。路径没有统一引号约束，包含空格、`#`、`->` 或其他特殊字符时容易产生歧义。

### 3.5 默认值和能力约束不直观

例如 `processes = *` 是常规场景的无意义样板；`users = *` 又与 Provider 虚拟化要求显式用户的约束冲突。格式没有让常用配置自然简洁。

### 3.6 为什么选择 TOML 基础语义

不选择 YAML，是因为安全规则更需要显式类型和稳定解析，而不是 YAML 的隐式标量推断与缩进语义。`users = [0]` 必然是整数数组，不会出现 YAML 1.1 中 `NO`、`on`、`off` 等普通文本被隐式解释为布尔值的同类问题。

不选择 JSON，是因为用户规则需要注释，且 JSON 的括号、逗号和重复对象结构不适合手机端手工维护。不选择 HCL/KDL，是因为它们会引入新的完整语言和相对较弱的 C++/Android 工具生态。

TOML 提供明确类型、注释、字符串、数组和模块化表结构；PathGuard 只为重定向方向增加一个受限 `->` 扩展。

## 4. 设计目标与非目标

### 4.1 设计目标

1. `deny` 使用 TOML 字符串数组，一行一个目录。
2. `redirect` 保留 `->`，一行表达源路径和目标路径。
3. 路径始终作为 TOML 字符串书写。
4. 规则顺序不影响同步策略语义。
5. 应用包名是稳定且唯一的配置键。
6. 每个后续能力拥有自己的 TOML 命名空间和校验器。
7. 编译错误包含文件、行、列、字段路径和相关规则位置。
8. 新配置失败时继续使用上一份有效 `policy.bin`。
9. 等价输入生成等价的 Canonical Policy IR。
10. 同一编译器内核被 daemon、CLI 和 Manager 复用，发布动作仍由 daemon 单点负责。
11. 编译器热路径保持线性或接近线性，不为尚未出现的扩展引入动态插件、通用表达式或全量无损语法树。

### 4.2 非目标

- 不设计通用编程语言、表达式系统或插件脚本。
- 不支持环境变量、命令替换、正则替换或任意函数调用。
- v1 路径不支持 `{user}`、`{package}` 或其他占位符；路径直接表达最终相对路径。
- 不允许配置顺序充当隐式优先级。
- 不允许未知字段被静默忽略。
- 不要求第三方 TOML 工具能够直接解析 `->` 扩展。
- 不在本次重构中实现尚未完成的 deny、isolate、event 或其他执行器。
- 不为尚未出现的需求提前设计通用继承、模板和覆盖系统。

### 4.3 实际编辑环境约束

`rules.toml` 是 Magisk/KernelSU 模块配置，不是以桌面 IDE 为中心的项目文件。典型编辑方式包括：

- MT 管理器、MiXplorer、Root Explorer、FV File Manager、Amaze。
- Android 普通文本编辑器。
- `nano`、`vi`。
- `sed`、shell 脚本和其他行式文本工具。

这些环境通常没有 TOML LSP、Schema 自动补全、Taplo Formatter 或实时语义诊断。因此设计基线必须是：

- 用户只用纯文本编辑器也能理解和修改。
- 常用字段和规则保持短小、稳定、适合逐行搜索和替换。
- 正确性不能依赖编辑器插件或保存前格式化。
- 模块必须在保存后自行编译、验证并保留上一份有效策略。
- 编译失败信息必须能从固定状态文件、CLI 或模块日志中直接找到。
- 不得自动重写用户源文件来“修复”格式。

编辑器插件、严格 TOML 导出和格式化器属于增强工具，不是规则格式可用性的前提。

## 5. 文件角色与单一事实来源

### 5.1 `rules.toml`

`rules.toml` 是用户和 Manager 编辑的唯一策略源：

- 保留注释和适合人工阅读的组织方式。
- 使用 PathGuard 扩展 TOML。
- 可以包含尚需编译验证的用户意图。
- 不直接进入应用启动热路径。

Root 用户仍可用文本编辑器直接修改该文件。Manager 不直接以普通文件写入方式覆盖模块配置，而是携带 `source_digest` 通过 daemon 控制接口提交最小修改；daemon 校验调用方、摘要和文件安全属性后完成原子写入。两种入口最终修改的仍是同一个 `rules.toml`，不会形成第二份配置。

### 5.2 `policy.bin`

`policy.bin` 是规则编译器生成的机器策略快照：

- 只包含规范化且验证完成的策略。
- 使用固定版本、固定宽度表、字符串表和校验和。
- daemon、Zygisk 和 companion 可以直接加载。
- 用户不得手工修改。
- 不保存用户注释和排版。
- 生成失败时不得覆盖上一份有效快照。

### 5.3 禁止双向同步

只允许：

```text
rules.toml -> policy.bin
```

禁止：

```text
rules.toml <-> policy.bin
```

原因是二进制策略已经丢失注释、排版和部分源码意图。任何反向写回都会形成两个事实来源，导致配置漂移和覆盖冲突。

如果需要观察机器策略，应通过只读命令导出：

```text
pathguardctl explain policy.bin <package>
pathguardctl dump policy.bin --format toml
pathguardctl dump policy.bin --format json
```

导出结果只用于诊断，不允许作为新的输入源参与自动合并。

## 6. 文件命名与格式声明

用户文件继续使用 `.toml` 扩展名：

```text
module/config/rules.toml
```

虽然文件包含一个非标准 TOML 运算符，但手机端主要由 Root 文件管理器、普通文本编辑器或 shell 工具处理，通常不存在严格 TOML 实时校验。第三方工具在 `->` 处误报的实际影响有限。保留 `.toml` 可以获得更熟悉的文件类型、可能存在的基础语法高亮和更低的用户认知成本，但设计不得依赖这些能力。

文件头应明确声明方言：

```toml
# PathGuard Rules
# TOML with PathGuard redirect extension: "source" -> "target"
format = 1
```

产品和错误信息必须使用“PathGuard Rules TOML”或“PathGuard 扩展 TOML”，不得宣称该源文件是严格标准 TOML。

## 7. 规则文件格式 v1

### 7.1 完整示例

```toml
# PathGuard Rules
# TOML with PathGuard redirect extension: "source" -> "target"
format = 1

[compatibility]
allow_legacy_mount = true

[apps."org.localsend.localsend_app"]
enabled = true
users = [0]
file_picker = true

deny = [
    "Pictures/Private",
    "Documents/Secrets",
]

redirect = [
    "Download/localsend-source" -> "Download/localsend-redirect",
    "DCIM/Temp"                 -> "Pictures/Backup",
]
```

用户可直接理解为：

> LocalSend 在 Android 用户 0 下不能访问两个私有目录；访问两个源目录时改为访问对应目标目录；Android 文件选择器访问路径也应用可支持的重定向语义。

#### 7.1.1 随模块分发的默认注释模板

用户通常直接使用 Root 文件管理器或终端编辑配置，因此默认 `rules.toml` 必须自带足够说明，不要求先查外部文档。推荐模板：

```toml
# PathGuard Rules
# PathGuard 扩展 TOML：重定向使用 "应用看到的路径" -> "实际存储路径"
# 所有路径都相对于 /storage/emulated/<Android 用户 ID>/
# 保存后由 pathguardd 自动校验和编译；配置错误时继续使用上一份有效策略。
format = 1

[compatibility]
# 是否允许使用 legacy namespace bind 挂载后端。
# false（推荐）：只允许安全性更强的 strict FD 后端；设备不支持时规则不会降级执行。
# true：strict 后端不可用且 legacy 能力探测通过时，允许整个进程计划使用 legacy 后端。
# legacy 仍是真实 VFS 重定向，但存在字符串路径解析的 TOCTOU 风险。
# 该开关不会让失败中的 strict 事务自动重试 legacy。
allow_legacy_mount = false

# 应用配置。双引号内必须填写 Android 包名。
[apps."org.example.app"]
# 是否启用该应用的全部 PathGuard 规则。
# true：编译并应用本节规则。
# false：保留配置但不写入新 policy；已运行进程可能需要重启后才移除旧挂载视图。
# 默认模板使用 false，填写真实包名并添加规则后再改为 true。
enabled = false

# 适用的 Android 用户 ID。主用户通常为 0，工作资料可能是 10、11 等。
# 未填写时默认 [0]。
users = [0]

# 是否尝试让 Android“文件”应用/SAF 文件选择器访问与直接路径重定向保持一致。
# false（默认）：只保证应用直接文件访问经过 VFS deny/redirect。
# true：要求当前构建和设备支持 Provider 虚拟化，并且至少存在一条 redirect。
# 不代表覆盖 MediaStore、Photo Picker、CloudMediaProvider 或所有系统代开文件路径。
file_picker = false

# 可选：只限制指定进程。省略表示该包名下的全部进程。
# processes = ["org.example.app", "org.example.app:worker"]

# 禁止应用访问以下目录及其全部子目录。
# 当前构建未完成 deny executor 时，启用 deny 会在编译阶段明确失败，不会假装生效。
deny = [
    # "Pictures/Private",
]

# 将左侧可见目录同步重定向到右侧实际目录。
# 推荐保持一行一条，便于 MT 管理器、vi、nano、sed 等工具编辑。
redirect = [
    # "Download/source" -> "Download/target",
]
```

默认模板中的注释是产品接口的一部分。字段语义、默认值或能力边界变化时，必须与解析器、CLI 帮助和正式文档同步更新。

### 7.2 顶层字段

| 字段 | 类型 | v1 规则 |
|---|---|---|
| `format` | 整数 | 必填，只接受 `1` |
| `compatibility` | 表 | 可选，高级兼容设置 |
| `apps` | 表 | 必填，至少包含一个应用 |

未知顶层字段必须报错。

`format` 只表示用户规则源格式，不等同于 `policy.bin format v5`，也不等同于内部 Policy IR 语义版本。

#### 7.2.1 `format` 引导解析

`format` 决定脱糖器应采用哪一版 PathGuard 扩展语法，因此不能等完整 TOML 解析完成后才识别。为避免未来 `format = 2` 出现“必须先脱糖才能知道使用哪种脱糖规则”的循环依赖，规定：

- `format` 必须是文件中第一个非注释、非空白的声明。
- 声明本身必须使用 bare key 和严格 TOML 十进制整数形式，例如 `format = 1`。
- 引导扫描器只识别该声明，不解释其他字段或扩展语法。
- 缺失、首声明类型错误或不支持的版本必须在进入主脱糖器前失败；后续重复 `format` 由标准 TOML parser/decoder 按重复键错误处理。
- 每个规则格式版本拥有独立、冻结的脱糖入口；新版本不得悄悄改变旧版本的 `->` 含义。

该引导扫描是版本选择器，不是第二个配置解析器。

### 7.3 应用表

应用使用包名作为唯一键：

```toml
[apps."com.example.app"]
users = [0]
file_picker = false
```

选择 `[apps."包名"]` 而不是 `[[app]]` 的原因：

- 包名天然是应用策略的唯一标识。
- TOML 解析阶段即可拒绝重复应用表。
- 后续能力可自然放入 `[apps."包名".能力]` 子表。
- 错误字段路径以分段数组或 JSON Pointer 表示，例如 `/apps/com.example.app/redirect`，避免包名中的 `.` 与字段分隔符混淆。

应用字段：

| 字段 | 类型 | 默认值 | 语义 |
|---|---|---|---|
| `enabled` | 布尔值 | `true` | 是否将该应用策略编译进新 `policy.bin` |
| `users` | 整数数组 | `[0]` | 适用的 Android user ID |
| `processes` | 字符串数组 | 省略表示包内全部进程 | 仅在限制特定进程时配置 |
| `file_picker` | 布尔值 | `false` | 请求对受支持的 SAF/文件选择器路径应用兼容逻辑 |
| `deny` | 字符串数组 | `[]` | 禁止访问的可见目录 |
| `redirect` | 重定向数组 | `[]` | 可见目录到实际目录的映射 |

`enabled = false` 时允许保留完整规则或空示例，编译器验证其语法和已有规则的静态语义，但不将该应用写入新 Policy IR。这样默认模板可以保持有效，用户也可以临时停用规则而不必删除配置。设备准入可跳过，因为规则不会发布；Host 编译仍必须发现拼写、类型和路径冲突。

`enabled = true` 时，应用表必须至少包含一条当前或未来能力规则。只有选择器、没有任何能力的空应用策略应当报错。

#### 7.3.1 能力开关、作用域与规则的区别

v1 对用户暴露三个布尔开关：

| 开关 | 默认值 | 含义 |
|---|---|---|
| `compatibility.allow_legacy_mount` | `false` | 是否允许在 strict FD 后端不可用时选择 legacy namespace bind 后端 |
| `apps."包名".enabled` | `true` | 是否把该应用的规则编译进新策略 |
| `apps."包名".file_picker` | `false` | 是否请求对受支持的 SAF/文件选择器访问启用 Provider 重定向兼容 |

以下字段不是能力开关：

- `format`：规则文件格式版本，用户不能用它启停功能。
- `users`、`processes`：决定规则应用到哪些 Android 用户和进程。
- `deny`、`redirect`：实际策略规则；数组为空表示没有该类规则。

不得再增加 `deny_enabled`、`redirect_enabled` 等重复开关。是否存在对应规则已经能够唯一表达用户意图，额外布尔值只会制造“开关与规则互相矛盾”的双重状态。

### 7.4 Deny

`deny` 是字符串数组，每个元素表示一个目录及其全部后代：

```toml
deny = [
    "Pictures/Private",
    "Documents/Secrets",
]
```

不采用以下符号形式：

```text
- Pictures/Private
! Documents/Secrets
```

`-` 容易被理解为列表项，`!` 容易被理解为取反，并且二者都依赖额外上下文。`deny` 本身已经是无需记忆的明确动词。

### 7.5 Redirect

`redirect` 是 PathGuard 扩展数组：

```toml
redirect = [
    "Download/source" -> "Download/target",
]
```

固定方向：

```text
左侧：应用访问的可见路径
右侧：实际存储的 backing 路径
```

`->` 比 `{ from = ..., to = ... }` 更适合普通用户快速阅读，也保留“一行一条规则”的紧凑度。

v1 只接受两个字符串操作数，不接受修饰符：

```text
redirect_rule = toml_string, "->", toml_string;
```

`->` 两侧允许 TOML 空白，包括换行。以下写法合法：

```toml
redirect = [
    "Download/some/very/long/source/path"
        -> "Download/some/very/long/target/path",
]
```

“一行一条”是推荐和格式化器的默认输出，不是解析限制。路径操作数本身只允许单行 TOML 基本字符串或字面量字符串；包含换行、制表符或其他控制字符的解码结果在语义层拒绝。

以下输入必须拒绝：

```toml
redirect = [
    "Source" -> "Target" -> "Other",  # 多个运算符
    "Source" -> 123,                   # 目标不是字符串
    "Source",                          # 缺少目标
]
```

### 7.6 兼容设置

底层兼容开关不得散落在顶层或应用规则中：

```toml
[compatibility]
allow_legacy_mount = true
```

它替代现有 `allow_legacy_string_bind`，使用用户能理解的能力名称，同时保持这是显式、全局且高风险的兼容选择。

默认必须为 `false`。设置为 `true` 只代表允许后端选择器在 capability 探测成功后选择 legacy，不代表强制使用 legacy，也不允许 strict 事务执行失败后在同一次启动中自动切换后端。

现有 `failure = open` 在 v1 中不再作为用户规则字段。当前唯一可执行行为由引擎固定为“新策略编译或加载失败时保留上一份有效策略；单次应用计划失败时按照既有事务和产品状态模型处理”。未来若确实需要用户选择失败策略，应在独立设计中引入，不提前保留无效开关。

### 7.7 注释、字符串与路径中的箭头

注释继续使用 TOML 的 `#`：

```toml
redirect = [
    "Download/source" -> "Download/target", # 下载目录重定向
]
```

箭头只有出现在字符串外部并位于 `redirect` 元素的两个字符串之间时才是运算符：

```toml
redirect = [
    "Download/name -> old" -> "Download/target",
]
```

该规则的源路径是 `Download/name -> old`，不会在字符串内部错误切分。

路径可以包含空格、`#` 和 Unicode，只要它们位于合法 TOML 字符串中：

```toml
deny = [
    "Pictures/家庭照片 # 私有",
]
```

## 8. 扩展 TOML 脱糖设计

本节只保留总体约束；完整词法状态、局部数组上下文、RewriteMap、GeneratedRedirect、错误码、测试和性能预算见 [05-rule-arrow-desugarer-design.md](05-rule-arrow-desugarer-design.md)。

### 8.1 转换结果

用户输入：

```toml
redirect = [
    "Download/source" -> "Download/target",
]
```

转换器生成供 TOML 解析器消费的等价结构：

```toml
redirect = [
    { from = "Download/source", to = "Download/target" },
]
```

`from` 和 `to` 是编译器内部规范字段，不要求普通用户手写。

### 8.2 禁止朴素字符串替换

不得实现：

```cpp
ReplaceAll(text, "->", "=");
```

这种实现无法区分字符串、注释和运算符，也无法生成合法的内联表。

### 8.3 流式最小词法扫描器

脱糖器只承担以下职责：

1. 识别 TOML 基本字符串、字面量字符串、多行字符串和转义边界。
2. 识别注释并跳过注释内容。
3. 使用有界容器栈区分 value array、inline table 和 table header 的局部标点上下文。
4. 仅把直接 value-array 元素位置上的两个单行 TOML 字符串和中间 `->` 识别为候选。
5. 拒绝链式箭头、内部注释、缺失操作数、缺失逗号和非数组元素上下文。
6. 将完整候选转换为 `{ from = ..., to = ... }`。
7. 为生成文本维护基于字节区间的 RewriteMap 和 GeneratedRedirect 来源记录。
8. 其他输入原样交给 TOML 解析器。

扫描器不保存完整 TokenStream，只维护词法状态、容器栈、最近显著 token、一个待完成候选和 O(redirect 数量) 的 rewrite。它不是第二个 TOML 解析器，不得自行解释 table path、dotted key、普通数字、布尔值或 Unicode 转义值；这些职责仍由 TOML 库承担。

脱糖器只判断“箭头是否是局部合法的数组元素”，不知道当前字段是否为 `apps.*.redirect`。局部合法但位于 deny、processes、顶层字段或嵌套数组中的箭头先完成脱糖，再由锁定 TOML parser 的 AST scope validator 报告 `PG-RULE-ARROW-SCOPE`。局部箭头错误优先于标准 TOML parse；存在 `PG-ARROW-*` 错误时不生成部分转换文本，也不继续调用标准 TOML parser。

### 8.4 源码位置映射

脱糖会改变行数和列宽，内部位置统一使用原始 UTF-8 字节半开区间：

```text
SourceSpan
  file
  begin_byte
  end_byte
```

原始和生成文本分别维护 LineIndex，输出诊断时再转换为行列。RewriteMap 负责任意 parser generated position 到原始位置的恢复；GeneratedRedirect 负责把生成的 inline table、`from` 和 `to` 绑定到原始规则、源操作数、箭头和目标操作数。Rust 候选优先直接使用 `Range<usize>` byte span，C++ 候选先把 source region 转为 generated byte offset。TOML 解析错误和语义错误最终必须映射回 `rules.toml`，不能向用户显示临时转换文本的位置。

## 9. 路径模型

### 9.1 路径基准

所有用户路径默认相对于目标 Android 用户的共享存储根：

```text
/storage/emulated/<user>
```

规则中不重复书写该绝对前缀：

```toml
"Download/source"
```

### 9.2 规范化

编译阶段必须执行组件级规范化，并拒绝：

- 绝对路径。
- 空路径和空路径组件。
- `.` 与 `..` 组件。
- NUL。
- 越过共享存储根的路径。
- 不能安全映射到 backing root 的路径。
- 非目录级同步规则（在相应能力完成前）。

不得执行 shell 展开、环境变量替换或任意文件系统跟随操作。

### 9.3 占位符

v1 不支持路径占位符。`{user}`、`{package}` 和任何其他 `{...}` 形式都按普通路径字符处理，不执行替换。

原因是用户路径已经相对于目标 Android 用户的共享存储根，普通 deny/redirect 不需要再次写入 user ID；而 `{user}` 会让同一应用的不同用户产生不同规则集合，要求 Canonical Policy IR 和 `policy.bin` 支持逐用户 mount plan。当前没有该必要，不为未来假设增加展开器、冲突倍增和二进制表示。

未来某个独立能力若确实需要变量，必须在该能力自己的版本化字段中定义有限语义，不增加全局模板语言，也不得改变 v1 路径字符串的含义。

### 9.4 编码、大小写与 Unicode

`rules.toml` 必须是合法 UTF-8。路径在 TOML 解码后保持用户输入的 Unicode 标量序列，不进行会改变实际 Linux 文件名的隐式大小写折叠或 NFC/NFD 改写。

原因是 Linux 文件名最终由字节序列标识，而 Android 设备、文件系统和存储后端的大小写及 Unicode 行为可能不同。编译器不能为了“看起来相同”而把两个真实文件名错误合并。

规则如下：

- 确切冲突检测使用规范化路径组件的 UTF-8 字节值。
- Host lint 对仅大小写不同、或 Unicode 规范化后等价的路径给出警告。
- Device admission 结合实际 topology/capability 报告底层存储的比较语义；确认会碰撞时提升为错误。
- Canonical Policy IR 保存实际用于 mount 解析的路径字节，不保存仅用于提示的折叠键。
- 路径解码结果中的换行、制表符和控制字符必须拒绝。

### 9.5 符号链接

文本编译阶段不得跟随符号链接来“修正”规则。可见路径和 backing 路径必须基于固定根目录 FD，在执行阶段使用受约束的组件级解析，禁止通过符号链接逃出共享存储根或 backend root。

Host 编译只能验证静态路径语义；依赖真实设备目录、挂载拓扑和符号链接状态的检查属于 Device admission。

## 10. 语义校验与冲突规则

### 10.1 检测基础与处理等级

所有冲突检测必须在以下阶段之后执行：

```text
TOML 字符串解码
    -> 路径组件规范化
    -> 作用域展开
    -> 冲突检测
```

路径包含关系必须按组件判断，不能使用普通字符串前缀。例如 `Pictures/A` 不是 `Pictures/AB` 的父路径。

规则文件是声明式配置，书写顺序不得决定结果。格式化、排序或移动规则行不能改变策略语义。冲突分为：

- **错误**：存在不同解释、deny 绕过、内容别名重叠、递归挂载或不可执行关系，拒绝发布整份新策略。
- **警告**：规则语义完全相同或被同类父规则完整覆盖，允许发布并在 Canonical Policy IR 中去重。
- **允许**：路径及作用域不冲突，或符合本节明确允许的多对一语义。

冲突不能统一按“是否属于同一应用”判断，而应按能力实际共享的执行域判断：

| 能力 | 冲突执行域 |
|---|---|
| VFS mount | package + Android user + process/namespace plan |
| Provider 虚拟化 | Android user + Provider 实例 + 后端能够可靠区分的请求者身份 |
| 异步事件 | Android user + backing filesystem/event source |
| Isolation | package + Android user + process/namespace plan + isolation anchor |

v1 只需要一个固定的 `ConflictDomain` 枚举和少量路径角色，不引入通用规则引擎或动态插件。各能力校验器输出规范化后的路径声明，例如 deny 可见路径、redirect 源、redirect 目标和 Provider 反向映射；跨模块校验器只在相同执行域内应用固定冲突矩阵。

因此，两个应用的 VFS mount 路径通常互不冲突；但如果 Provider 后端无法可靠区分请求应用，则跨应用的反向映射仍可能冲突。编译器不得用“不同应用一律允许”掩盖共享执行面的歧义。

### 10.2 Deny 自身重复与包含

完全重复的 deny 是冗余：

```toml
deny = [
    "Pictures/Private",
    "Pictures/Private",
]
```

编译器应产生警告、同时指出两处源码位置，并只向 Canonical Policy IR 写入一条规则。

父目录 deny 已经覆盖子目录 deny：

```toml
deny = [
    "Pictures",
    "Pictures/Private",
]
```

`Pictures/Private` 不改变语义，应产生冗余警告并从 Canonical Policy IR 删除。无害冗余不应阻止其他有效修改发布。

大小写或 Unicode 规范化后疑似等价、但 UTF-8 字节不同的路径，按 §9.4 执行 Host 警告和 Device 碰撞确认，不得直接静默合并。

### 10.3 Deny 与 Redirect 冲突

设 deny 路径为 `D`，redirect 源路径为 `S`，目标路径为 `T`。只要 `D` 与 `S` 或 `T` 满足以下任一关系，就必须编译失败：

- 完全相等。
- `D` 是另一方的祖先目录。
- `D` 是另一方的后代目录。

Deny 与 redirect 源冲突示例：

```toml
deny = [
    "Pictures",
]

redirect = [
    "Pictures/Public" -> "Shared/PublicPictures",
]
```

v1 不再使用“更具体的 redirect 覆盖父级 deny”语义。用户看到 `deny = ["Pictures"]` 时，应能确定整个目录树都不会再通过本应用的其他同步规则开放。未来需要“默认隔离、显式放行”时，应使用独立的 `isolation.allow` 能力。

Deny 与 redirect 目标冲突可能形成内容别名绕过：

```toml
deny = [
    "Vault/Secret",
]

redirect = [
    "Shared" -> "Vault",
]
```

应用可能通过 `Shared/Secret` 访问原本被 deny 的内容。即使 backing 路径通过固定根目录 FD 解析、不直接经过可见 deny mount，也必须拒绝这种关系。

不得使用“deny 永远赢”“redirect 永远赢”“更具体规则赢”或“后写覆盖先写”来静默消解跨动作冲突。

### 10.4 单条 Redirect 约束

对每条 `S -> T`，以下情况必须拒绝：

- `S` 与 `T` 规范化后完全相同，形成无意义自映射。
- `T` 位于 `S` 内部，可能形成递归目标。
- `S` 位于 `T` 内部，可能把父级 backing 映射到其自身后代挂载点。
- 任一端不满足固定根目录下的静态安全路径约束；依赖实际设备目录和所选 mount 后端的可解析性由 Device admission/ProcessPlan 校验。
- 任一端与 isolation backing anchor 冲突。

因此，同一条 redirect 的源和目标不得相等，也不得互为祖先或后代。

### 10.5 多条 Redirect 冲突

#### 10.5.1 同源规则

完全重复的规则：

```toml
redirect = [
    "A" -> "B",
    "A" -> "B",
]
```

产生警告并去重。

同一个源映射到不同目标：

```toml
redirect = [
    "A" -> "B",
    "A" -> "C",
]
```

属于同步一对多。一个可见挂载点只能拥有一个 backing，必须编译失败。不得通过挂载顺序选择最后一条规则。

#### 10.5.2 源路径包含

不同 redirect 源不得相等或互为祖先、后代：

```toml
redirect = [
    "A"   -> "Storage/A",
    "A/B" -> "Storage/B",
]
```

这种配置依赖嵌套挂载顺序、目标目录存在状态和隐式最长前缀覆盖，v1 直接拒绝。未来若确有需求，应由显式高级能力承载。

#### 10.5.3 中间规则与链式重定向

任意一条 redirect 的目标不得与另一条 redirect 的源相等或互为祖先、后代：

```toml
redirect = [
    "A" -> "B",
    "B" -> "C",
]
```

目标是固定 backing 路径，不会再次经过可见路径规则。编译器不得自动把该配置压缩成 `A -> C`，而应报告中间规则冲突。

以下隐式链同样拒绝：

```toml
redirect = [
    "A"   -> "B",
    "B/C" -> "D",
]
```

编译器还必须对规范化后的 redirect 关系执行独立图循环检测，拒绝直接和间接循环：

```text
A -> B -> A
A -> B -> C -> A
```

#### 10.5.4 多对一共享目标

不同且互不包含的源路径允许映射到规范化后完全相同的目标：

```toml
redirect = [
    "Download/AppA" -> "PathGuard/Shared",
    "Pictures/AppA" -> "PathGuard/Shared",
]
```

这是明确支持的多对一别名组：两个可见路径访问同一 backing 内容，不是复制。任一入口的写入和删除会立即反映到另一入口。

编译器应在 `explain` 和 `plan` 中显示共享目标组。用户还必须获知：两个别名属于不同 bind mount，别名之间的 `rename()` 或 hard link 操作可能返回 `EXDEV`；未来文件事件也未必能从 backing 事件唯一还原用户使用了哪个可见别名。

多对一只允许目标完全相同。目标仅存在父子包含关系仍然拒绝：

```toml
redirect = [
    "A" -> "PathGuard/Shared",
    "B" -> "PathGuard/Shared/Subdir",
]
```

该配置会让两个可见入口暴露同一物理目录树的重叠区域，不属于简单共享目标。

#### 10.5.5 多对一与 File Picker

VFS 后端可以将同一 backing bind 到多个可见源，但 Provider 虚拟化可能需要从实际路径反向选择唯一可见路径。

当 `file_picker = true` 且存在多对一组时：

- Host 编译在 `PolicyRequirements` 中标记需要 `provider_many_to_one` capability。
- Device admission 只有在当前 Provider 后端明确声明支持确定性多对一映射时才允许发布。
- 后端不支持时，报告能力错误；不得随机选择别名或静默关闭 File Picker 兼容。

v1 不增加 `canonical_source` 等额外字段。等实际 Provider 需求和行为完成验证后，再决定是否需要用户指定规范入口。

### 10.6 冲突判定矩阵

| 关系 | 处理 |
|---|---|
| deny 完全重复 | 警告并去重 |
| deny 父子包含 | 警告并删除冗余子规则 |
| deny 与 redirect 源相等或互相包含 | 错误 |
| deny 与 redirect 目标相等或互相包含 | 错误 |
| redirect 完全重复 | 警告并去重 |
| 同源、不同目标 | 错误，不支持同步一对多 |
| 单条 redirect 的源目标相等或互相包含 | 错误 |
| 多个 redirect 源互相包含 | 错误 |
| 一条目标与另一条源相等或互相包含 | 错误，中间规则 |
| redirect 构成直接或间接循环 | 错误 |
| 不同源、目标完全相同 | 允许，多对一别名组 |
| 不同目标相等以外的父子包含 | 错误，物理目录树重叠 |
| 大小写/Unicode 疑似碰撞 | Host 警告，Device 确认后错误 |
| 不同应用出现相同 VFS 路径或目标 | 允许，mount 执行域独立 |
| 不同应用出现 Provider 反向映射歧义 | 按 Provider 执行域检查；无法区分请求者时错误 |

### 10.7 语义编译与设备准入

解析成功不等于规则可部署。规则编译和设备准入必须分开：

- **语义编译**检查扩展语法、TOML 类型、字段、路径、规则冲突以及当前构建是否包含对应 executor。成功后生成 `CanonicalPolicy` 和明确的 `PolicyRequirements`。
- **设备准入**使用不可变的 `CapabilitySnapshot` 与 `TopologySnapshot` 判断该策略当前是否可执行，成功后生成引用 Canonical Policy 的轻量 `AdmissionResult`，不复制一份新的策略对象。
- executor 未包含在当前构建中：语义编译失败并报告 `unsupported rule capability`。
- `file_picker = true` 但 Provider 兼容后端在当前设备不可用：设备准入失败，不得伪装成语法错误，也不得假装已覆盖该访问路径。
- `file_picker = true` 但没有 redirect：属于设备无关的无效组合，在语义编译阶段失败。Provider 只能覆盖部分访问路径时，由设备准入返回明确的能力说明。
- legacy mount 未显式允许：设备准入不得自动降级。
- 同一个 ProcessPlan 不得混用 strict 与 legacy 后端。具体后端仍在创建 ProcessPlan 时根据当时的目标 namespace topology 和有效 capability snapshot 选择；规则文件准入不把瞬时后端选择固化进 `policy.bin`。

当前 README 声明 deny、isolate/allow、event 和 MediaStore filtering 仍受 compile gate 约束；规则格式重构不得绕过这些门禁。

校验分为两层：

| 模式 | 检查范围 |
|---|---|
| Host compile/validate | 扩展语法、TOML 类型、字段、路径静态语义、规则冲突、构建期 executor 和 Canonical Policy IR 可构建性 |
| Device admit/validate | Host 全部结果，加 Provider、mount backend、topology、实际文件系统比较语义和设备路径能力 |

Host 编译通过但设备能力不足时，应报告“规则语法和语义有效，但当前设备不可执行”。这种结果是 `environment_unsupported`，不是 `source_invalid`；可以保留 Canonical Policy 作为诊断候选，但不能生成或发布活动策略。

### 10.8 未知内容

以下情况全部作为错误：

- 未知字段。
- 未知应用能力表。
- 字段类型错误。
- 重复应用。
- 重复键。
- 当前 `format` 不支持的字段。
- 当前构建不支持的动作。

禁止为了“向前兼容”静默忽略未知配置，因为这会造成用户以为规则已经生效。

## 11. 后续模块扩展模型

扩展性不通过增加更多单字符运算符实现，而是通过应用下的独立能力命名空间实现。

以下内容是结构示例，不表示相应执行器已经可用。

### 11.1 Isolation

```toml
[apps."com.example.writer".isolation]
storage = "Android/data/com.example.writer/isolated"

allow = [
    "Download/Public",
    "Pictures/Shared",
]
```

`isolation` 决定未命中更具体规则时进入独立存储；`allow` 是隔离模式中的真实目录例外，不单独引入 `mode = allowlist/denylist` 作为第二个行为来源。

### 11.2 Events

```toml
[[apps."com.example.camera".events]]
path = "DCIM/Camera"
when = "write_completed"

[[apps."com.example.camera".events.actions]]
type = "copy"
to = "Pictures/CameraBackup"

[[apps."com.example.camera".events.actions]]
type = "media_scan"
```

事件规则属于异步事件面，不参与同步 deny/redirect 的最长前缀决策。

### 11.3 高级 Redirect

如果未来 redirect 确实需要只读、创建目标或条件属性，应通过新的、版本化的结构表达，例如：

```toml
[[apps."com.example.app".redirect_rules]]
from = "Pictures/Original"
to = "Pictures/Managed"
access = "read_only"
create_target = true
```

在该需求落地前，v1 只实现箭头形式，避免同时维护两套等价写法。未来高级形式必须编译为同一个 RedirectRule IR，不能形成平行执行路径。

## 12. 编译器架构

控制面建议拆分为以下阶段：

```text
ConfigReconciler
    -> RulesSourceLoader
    -> RulesFormatProbe
    -> PathGuardTomlDesugarer
    -> TomlParser
    -> RulesDocumentDecoder
    -> PathNormalizer
    -> ModuleValidators
    -> CrossModuleConflictValidator
    -> CanonicalPolicyBuilder
    -> CanonicalPolicy + PolicyRequirements
    -> PolicyAdmission(CapabilitySnapshot, TopologySnapshot)
    -> PolicyEncoder
    -> PolicyVerifier
    -> PolicyPublisher
```

这些名称表示职责边界，不要求每个阶段都创建一个类。简单、无状态的步骤优先实现为纯函数或小型命名空间；只有持有 I/O 状态、监控状态或发布事务的组件才需要对象生命周期。不得为了图形上的每个方框增加无意义抽象。

### 12.1 组件职责

| 组件 | 单一职责 |
|---|---|
| `ConfigReconciler` | 合并文件事件、串行化候选编译，并保证变化期间最终收敛到最新源文件 |
| `RulesSourceLoader` | 稳定读取源文件并记录文件元数据 |
| `RulesFormatProbe` | 从首个有效声明读取规则格式版本并选择脱糖器 |
| `PathGuardTomlDesugarer` | 只处理 `redirect` 中的 `->` |
| `TomlParser` | 标准 TOML 语法和基础类型解析 |
| `RulesDocumentDecoder` | 将 TOML 节点映射到强类型配置结构 |
| `PathNormalizer` | 只执行设备无关的路径组件规范化 |
| `ModuleValidators` | 各能力内部的类型组合、路径和构建期 executor 校验 |
| `CrossModuleConflictValidator` | 在固定执行域内检查 deny、redirect、Provider、isolation 等路径声明 |
| `CanonicalPolicyBuilder` | 排序、去除表层差异并生成确定性 IR |
| `PolicyAdmission` | 根据不可变 capability/topology 快照判断当前设备是否具备发布资格，不改写 Canonical Policy |
| `PolicyEncoder` | 将 Canonical Policy 确定性序列化为 `policy.bin` 字节，不编码瞬时设备探测结果 |
| `PolicyVerifier` | 对生成字节执行独立的格式、边界、校验和与 content generation 自检 |
| `PolicyPublisher` | 负责权限、持久化、原子替换和崩溃恢复，不解释规则语义 |

不得把所有阶段重新堆入一个新的 `ParseRulesToml()` 巨型函数。

### 12.2 数据模型边界

源配置、展开后语义、规范化策略和运行时快照必须使用不同模型：

```text
RulesDocument
  -> ResolvedPolicy
  -> CanonicalPolicy + PolicyRequirements
  -> PolicyBlob / RuntimePolicyView
```

| 模型 | 包含 | 明确不包含 |
|---|---|---|
| `RulesDocument` | 用户字段、默认值、启停状态、源码 `RuleId` | 设备 capability、二进制偏移 |
| `ResolvedPolicy` | 规范化路径、package/user/process 作用域 | 注释、TOML 节点、运行时字节布局 |
| `CanonicalPolicy` | 确定性排序、去重后的策略语义 | 源码行号、空白、注释、禁用应用、设备瞬时状态 |
| `PolicyRequirements` | 策略要求的 mount action mask、Provider/event capability 位和少量约束 | 某台设备的探测结果、重复的规则树 |
| `PolicyBlob` / `RuntimePolicyView` | 固定二进制格式及只读查询视图 | TOML、诊断和编译器对象 |

源码位置不写入 Canonical Policy。编译器只需使用单次编译内稳定的顺序 `RuleId`，并在独立的 `OriginMap<RuleId, SourceSpan>` 中维护主位置和关联位置，不设计跨编辑持久化规则身份。这样运行时模型不会反向依赖文本格式，CLI/Manager 仍能获得精确诊断。

设备准入返回轻量 `AdmissionResult`，其中只保存 Canonical Policy 的 content generation、准入结论、缺失要求以及 capability/topology generation，不拥有或复制另一份策略树。它是一次环境判定结果，不是第五种 Policy 模型，也不把某次 strict/legacy 后端选择固化进策略快照。

`PolicyRequirements` 优先使用现有 capability bitset、action mask 和固定枚举表达，不建立可递归的需求对象图。只有 bitset 无法表达且已有真实设备差异时，才增加新的小型约束字段。

模型转换应尽量使用只读输入和显式输出，禁止靠同一个 `PolicyDocument` 在多个阶段原地改变含义。Canonical Policy 构建完成后视为不可变，以便安全缓存、并行只读验证和确定性哈希。

### 12.3 模块扩展接口

每个能力模块应拥有：

- 唯一 TOML 字段或子表名。
- 强类型配置结构。
- 独立字段解码器。
- 独立语义校验器。
- Canonical Policy IR 映射。
- 构建期 executor 检查和 `PolicyRequirements` 生成。
- 单元测试和错误码范围。

跨模块冲突只共享最小内部结构：执行域、路径角色、规范化路径和 `RuleId`。不设计通用表达式、规则代数或动态插件协议。当前静态编译的模块表和固定冲突矩阵已经足够，新增能力只有在真实需求出现时才增加对应路径角色。

### 12.4 规则编译语言与 TOML parser 选择

> D0 已于 2026-07-26 完成，正式结论为 C++20 + toml++ v3.4.0。下文候选说明作为决策证据保留；生产实现不得重新同时引入两套 parser。详见 `docs/decisions/rules-compiler-d0.md`。

Phase D0 必须在同一套输入、断言和预算下比较两个完整候选，不允许只比较 parser 的单个 API：

| 候选 | 生产边界 | 优点 | 主要验证点 |
|---|---|---|---|
| Rust + `toml_edit` | 从规则源字节到已验证 `PolicyBlob` 的纯编译链 | byte span API 直接、文本处理内存安全、可复用 Rust TOML 生态 | TOML 1.0 兼容、不可变 Document span、Android 静态库、体积与 C ABI |
| C++ + toml++ v3.4.0 | 同一纯编译链保持 C++ | 不引入第二工具链、复用现有构建 | source region 精度、line/column 到 byte offset、隐藏标记是否成为必需 |

比较的是整条：

```text
RulesFormatProbe
    -> PathGuardTomlDesugarer
    -> TOML parse / generated node binding
    -> RulesDocumentDecoder
    -> normalize / validate / canonicalize
    -> PolicyEncoder / PolicyVerifier
```

不得采用“Rust 只做脱糖器，C++ 再用 toml++ parse”的中间 FFI。该方案既保留两套 parser/toolchain 成本，又迫使 AST、RewriteMap 或 GeneratedRedirect 跨语言传递，收益不足以抵消新增边界。

#### Rust 候选（已验证、未选择）

D0 暂定验证 `toml_edit` 0.23.7 的 parse-only 组合，并锁定 TOML 1.0 parser 依赖。候选清单必须进入 `Cargo.lock`，所有 `cargo build/test/ndk` 命令使用 `--locked`；升级只能通过显式依赖审核和 conformance 测试。

```toml
[dependencies]
toml_edit = { version = "=0.23.7", default-features = false, features = ["parse"] }
toml_parser = "=1.0.4" # 仅作为 TOML 1.0 版本约束，与 toml_edit 共享同一实例
```

`toml_edit` 0.23.7 对 `toml_parser` 的普通 semver 约束可能允许后续 1.x 版本，因此候选显式固定 `toml_parser = 1.0.4` 并提交锁文件。当前 `toml_edit` 0.25.x 标注 `spec-1.1.0`，不能在 `rules.toml format = 1` 中直接升级使用。若未来采用 TOML 1.1，必须通过新的规则格式版本明确引入，不能由依赖更新偷偷扩大语法。

解析必须使用保留原始输入的不可变 `toml_edit::Document<S>`：

```rust
let document = toml_edit::Document::parse(generated_toml)?;
let span = inline_table.span(); // generated byte Range<usize>
```

不得在 generated node 绑定完成前转换为 `DocumentMut`；`into_mut()` 会执行 `despan()` 并清除节点来源区间。`serde_spanned` 可以辅助强类型值解码，但 generated inline table 的来源绑定以 `Item::span()`、`Value::span()` 和 `InlineTable::span()` 为准，不能把 serde span 当作唯一来源证明。

`toml_edit` 的格式保留能力不代表核心编译器获得了编辑职责。Rust 编译器只读不可变 Document 并输出 Canonical Policy；Manager/`fmt` 仍按 §12.5 独立决策。其额外 AST/trivia 内存必须纳入 D0 峰值内存和二进制体积测量，不能因属于冷路径而免测。

#### C++ 候选（已选择）

C++ 候选继续使用已审核的 toml++ v3.4.0：

```text
third_party/tomlplusplus/toml.hpp
size: 489563 bytes
sha256: 2089217190195E12E9A4A454BC94CFB95B58A07FF927F1505D068188C2F864DF
license: MIT
```

```cpp
#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_FORMATTERS 0
#define TOML_ENABLE_UNRELEASED_FEATURES 0
#include "toml.hpp"
```

该候选必须继续验证 `node::source()`、Unicode/CRLF/BOM、无异常 parse_result、Android NDK 体积和编译时间。隐藏标记只能作为 D0 原型；若正常生产路径必须依赖它，应触发 Go/No-Go 复审，不能因为 fallback 可工作就自动选择 C++ 候选。

#### 共同验收与唯一选择

两个候选必须运行同一套 binder-neutral/source-map golden、TOML 1.0 conformance 子集、错误快照、资源极限和当前 207-byte `policy.bin format v5` golden vector。Rust 候选还必须由现有 C++ `DecodePolicy()` 独立读取其输出，证明 producer/consumer 只通过冻结字节契约连接。

D0 最终只允许以下三种结论之一：

1. 选择 Rust 完整规则编译器，删除生产构建中的 toml++、隐藏标记和 C++ 规则编译实现。
2. 选择 C++ 完整规则编译器，不把 Rust 引入规则编译路径。
3. 两者均未满足箭头投入产出门槛，退回严格 TOML。

实际决议采用第 2 项并保留箭头。Rust 原型、Cargo 构建入口和 C ABI 已删除；严格 TOML 不作为 format 1 的并存语法。

不得把双实现长期保留为 fallback，也不得按 Host/Android、daemon/CLI 分别选择不同 parser。依赖版本、许可证、哈希、Rust MSRV、NDK target 和构建命令必须在 D0 结论中一并冻结。

### 12.5 源文档与编辑能力

核心编译阶段对外只维护诊断所需的轻量源码索引：

```text
RulesSourceIndex
  source buffer
  line starts
  extension token spans
  redirect rule spans
  semantic node links
```

如果最终选择 `toml_edit`，其不可变 Document 可以在单次 parse/decode 生命周期内暂时持有 parser 自带的 trivia/span，但不得进入 RulesDocument、Canonical Policy、C ABI 或缓存。PathGuard 不自行建立第二套完整无损 token/trivia 模型；只有 Manager 局部编辑或 `fmt` 确实开始实现时，才增加独立 `rules_editor` 能力。这样既使用成熟 parser 的现成 span，又不把编辑职责扩散到核心编译模型。

Manager 应通过源码 span 做最小局部修改，并在保存时携带读取到的 `source_digest`。daemon 只有在当前源摘要仍匹配时才接受写入；摘要不匹配表示用户或其他工具已修改文件，Manager 必须重新加载，不能覆盖新内容。若无法保证注释不丢失，Manager 和 `fmt` 均不得原地重写整个文件。

### 12.6 构建与依赖边界

控制面依赖应从构建目标上隔离：

```text
pathguard_policy_model
pathguard_policy_binary
pathguard_rules_compiler      # D0 后唯一依赖选定 TOML parser
pathguard_policy_admission
pathguard_policy_store
pathguardd
pathguardctl
```

目标名称可按仓库实际布局合并，但必须保持以下依赖方向：

- `pathguard_rules_compiler` 可以依赖 policy model 和 policy binary，反向依赖禁止；若选择 Rust，该名称先实现为一个 crate 内的模块边界，不为每个流水线阶段建立独立 crate。
- `pathguard_policy_binary` 不依赖 TOML、源码 span 或 Manager 编辑模型。
- `pathguard_policy_admission` 只消费 Canonical Policy、Policy Requirements 和设备快照。
- `pathguard_policy_store` 只处理活动快照与状态发布，不重新执行语义解析。
- Zygisk 只依赖二进制格式定义和只读索引，不链接 toml++、`toml_edit`、Rust runtime、编译器、诊断或发布代码。

如果选择 Rust，首版保持现有 C++ daemon/CLI，只在完整编译请求处建立一个 C ABI：

```text
C++ RulesSourceLoader / ConfigReconciler
    -> pg_rules_compile(source bytes, mode, immutable admission snapshot)
    -> opaque compile result
       - verified policy bytes
       - content generation / requirements / admission result
       - bounded structured diagnostics
    -> existing C++ DecodePolicy contract verification
    -> C++ PolicyPublisher
```

C ABI 约束：

- 输入使用指针加长度和固定宽度整数，不使用 NUL 结尾字符串推断长度。
- 输出使用 opaque handle 和只读 accessor；Rust 分配的内存只能由 `pg_rules_result_free()` 释放。
- 不跨边界传递 Rust `String`/`Vec`、C++ STL、AST、RewriteMap、RulesDocument 或 Canonical Policy 对象。
- ABI 带显式版本；枚举使用固定宽度整数并拒绝未知值。
- 首版手写一个极窄、版本化的 C 头，并用 Rust `#[repr(C)]`、C/C++ `static_assert` 和 `sizeof`/`alignof`/`offsetof` 测试锁定布局；不为单一入口默认增加 `cbindgen`。只有 ABI 实际扩张且手工同步已成为真实维护问题时才引入生成工具。
- 不从 Rust 编译闭包回调 C++，避免重入、异常和生命周期复杂度。
- 每个 `extern "C"` 入口在 Rust 内部使用 `catch_unwind`，把 unwind panic 转为 `PG-COMPILER-INTERNAL`；compiler profile 必须是 `panic = "unwind"`。
- release profile 启用 `overflow-checks = true`，所有用户输入派生的 offset/length 仍使用 `checked_add`、`checked_sub` 和显式上限。
- `catch_unwind` 不能捕获 abort panic、OOM abort、stack overflow、信号崩溃或 native UB；失败模型仍以“不发布候选、保留上一份有效策略”为准。

CLI 未来若迁移 Rust，可直接调用同一 crate 的原生 API；这不是首版规则重构的前置条件。companion 是否迁移 Rust 也不属于本阶段。

Android 构建集成在 Rust 候选通过 D0 后采用最小方式：

1. `scripts/build-native.ps1` 先用与 C++ 相同的 NDK revision、API level 和 ABI 调用 cargo-ndk，生成 `libpathguard_rules_compiler.a`。
2. `native/Android.mk` 以 `PREBUILT_STATIC_LIBRARY` 引入该产物，只链接 `pathguardd` 和需要离线 compile/validate 的 `pathguardctl`。
3. `pathguard_zygisk` 的 module 列表、link map 和 ELF dynamic symbols 必须证明没有链接 Rust staticlib、`toml_edit`、toml++ 或 compiler C ABI。
4. Host 侧直接运行 crate 单元测试、golden 和 fuzz；Android 侧只复测 ABI、端到端编译、体积与性能，不复制另一套测试实现。
5. 首版只有一个 `pathguard_rules_compiler` crate；不为了可能迁移 daemon/CLI 提前创建多 crate workspace。真实出现第二个 Rust 产物后再决定是否提升为 workspace。

Rust 自带的 PolicyVerifier 负责捕获 encoder 内部错误；C++ daemon 在发布前再用现有 `DecodePolicy()` 按运行时字节契约独立读取一次。该检查不重新解释 TOML 或语义，只验证 Zygisk reader 将要消费的格式、checksum、generation、排序和边界，因此不是第二套规则编译器。

不要求一次性拆出所有静态库；实施时可以先按命名空间和文件边界分离，再在依赖或构建成本需要时拆 target。架构约束是依赖方向，不是库数量。

### 12.7 性能原则

规则编译不在应用启动热路径，但保存后的反馈仍应快速且可预测。v1 使用简单的全量确定性编译，不实现增量 AST、跨版本缓存或并行模块调度；在实际性能数据证明必要前，不为小型配置增加缓存失效协议。

实现应满足：

- 每个路径只执行一次 TOML 解码和组件规范化，后续阶段复用 `NormalizedPath`。
- 路径声明先按 `ConflictDomain` 分组，再按组件序排序；重复和祖先/后代关系使用相邻扫描或小型栈处理，避免对全部规则执行无界 O(n²) 两两比较。
- redirect 循环检测使用邻接表/哈希索引和线性图遍历，不重复解析路径字符串。
- Canonical Policy 排序、去重和字符串表构建一次完成；编码器不再执行第二套语义规范化。
- content generation 未变化时跳过二进制重写和运行时 reload。
- 诊断只在发现问题时构建详细上下文，并受 `RulesLimits` 上限约束。

不提前为编译期路径关系或运行时匹配引入 trie。排序扫描无法满足已冻结的性能预算时，再基于基准数据替换内部索引；该优化不得改变配置语义或 Canonical Policy 排序结果。

## 13. 版本模型

必须区分三个版本：

| 版本 | 示例 | 作用 |
|---|---|---|
| 用户规则格式 | `format = 1` | 决定 `rules.toml` 可用字段和语法 |
| Policy IR 语义版本 | 内部定义 | 决定规范化后的策略语义 |
| 二进制格式 | `policy.bin format v5` | 决定运行时字节布局 |

规则文本语法变化不一定要求升级二进制格式；二进制字段布局不变时，`policy.bin format v5` 可以继续使用。

同理，单纯把 `redirect A -> B` 改为扩展 TOML 箭头数组，不应因为表层变化而修改 mount executor 或 Zygisk 读取逻辑。

Canonical Policy IR 和 content generation 必须排除时间戳、注释、空白、键书写顺序等表层信息。只有实际策略语义变化时才生成新的内容 generation，避免无意义重载。

还必须区分以下运行状态标识：

| 标识 | 含义 |
|---|---|
| `source_digest` | 原始 `rules.toml` 字节的 SHA-256，用于变化检测、Manager 乐观并发和失败候选关联 |
| `candidate_sequence` | daemon 当前生命周期内观察到的候选序号，只用于日志排序，不跨重启承诺单调 |
| `content_generation` | Canonical Policy 内容标识；是内容哈希，不是递增版本号 |
| `deployment_epoch` | 成功发布活动策略的部署序号或等价持久化标识 |
| `capability_generation` | capability 探测快照标识 |
| `topology_generation` | 存储和 namespace topology 快照标识 |

日志、状态文件和协议不得把这些概念统一简写为含义不明的 `generation`。运行时为兼容现有 `policy.bin` 可以继续使用 64 位 `content_generation`/`plan_generation`，但控制面必须同时保留完整 `source_digest`，不能使用内容 generation 判断用户源文件是否被并发修改。

## 14. 配置发布与失败处理

### 14.1 编译事务

```text
检测 rules.toml 变化
    -> 稳定读取完整文件
    -> 脱糖和 TOML 解析
    -> 语义编译
    -> 生成 Canonical Policy + Policy Requirements
    -> 使用 capability/topology snapshot 执行设备准入
    -> 编码并校验候选 PolicyBlob
    -> 持久化并原子替换正式 policy.bin
    -> 发布状态并通知运行组件重载 content_generation
```

规则编译、设备准入、二进制编码均应是无外部副作用的确定性步骤。只有 `PolicyPublisher` 可以修改活动快照和状态文件，而且活动路径只允许 daemon 写入。`pathguardctl compile <rules.toml> <output>` 只生成显式离线输出；若目标是模块活动路径，必须改为通过控制 UDS 请求 daemon 重新加载，避免 CLI、Manager 和文件监控线程竞争发布。

发布事务至少满足：

1. 在目标目录 FD 下创建不可预测名称的临时文件；支持时可使用 `O_TMPFILE`，不得复用固定的 `policy.bin.tmp`。
2. 写入完整字节，设置预期 owner、mode 和安全上下文，并对同一份候选字节执行 `PolicyVerifier`。
3. `fsync` 临时文件后通过受约束的 `renameat`/等价原子替换发布。
4. `fsync` 目标目录，确保断电恢复后目录项可见性与预期一致。
5. 策略文件发布成功后再原子更新状态文件；若中途崩溃，daemon 重启时以实际可验证的 `policy.bin` 为准重建状态，不把状态文件当作第二事实来源。

content generation 未变化时不重复写入 `policy.bin`，但仍应更新本次候选的 `source_digest` 和验证状态，使纯注释修改能够显示为“已验证、策略语义未变化”。

### 14.2 编译失败

失败必须区分：

| 状态 | 含义 |
|---|---|
| `source_invalid` | 扩展语法、TOML、字段、路径、冲突或构建期 executor 校验失败 |
| `environment_unsupported` | 源规则和 Canonical Policy 有效，但当前 capability/topology 不满足 Policy Requirements |
| `publish_failed` | 候选可部署，但编码、自检、权限、持久化或原子替换失败 |

任一失败状态下：

- 不覆盖当前 `policy.bin`。
- 不发布部分应用或部分规则。
- daemon 继续使用上一份有效策略。
- 状态模型记录 `source_digest`、`candidate_sequence`、失败阶段和诊断。
- Manager 明确显示“新配置未生效，仍在使用上一版本”。

### 14.3 文件监控

daemon 应监听 `rules.toml` 所在目录，而不是只依赖原 inode，以兼容编辑器通过临时文件加 rename 保存的方式。相关事件至少包括 close-write、create、move 和 watch overflow 后的全量重读。

文件事件只设置“源已变脏”并唤醒单个 reconcile worker。任一时刻最多执行一次候选编译；编译期间再次发生变化时，当前结果处理完成后立即重读最新文件。这样既合并重复通知，又不会因固定 sleep 阻塞事件循环或丢失最后一次保存。

防抖只用于合并短时间内的重复通知，不能代替完整读取、`source_digest` 比较和原子发布。inotify queue overflow 必须设置全量重读标志，不为每个历史事件补偿重放。

不同 Android 编辑器可能采用原地截断写入、临时文件替换、rename、改变换行符或补充 UTF-8 BOM。源文件加载器必须：

- 接受 LF 和 CRLF。
- 允许且只允许文件开头的 UTF-8 BOM。
- 对 close-write/rename 保存直接从新 FD 读取完整快照；轮询或无法判断关闭边界时，比较两次文件元数据和内容摘要后再编译，避免读取写入中间态。
- 使用目录 FD 和 `O_NOFOLLOW` 一类约束打开固定文件名，拒绝符号链接替换。
- 通过 `fstat` 确认是普通文件并执行大小、所有者和权限检查。
- 拒绝可被非 Root 用户写入的规则文件，避免普通应用篡改保护策略。

如果编辑器保存后改变了所有者、权限或安全上下文，daemon 应报告具体诊断，不得静默使用来源不可信的配置。

## 15. 错误与诊断设计

错误示例：

```text
rules.toml:18:5: error[PG-RULE-REDIRECT-CONFLICT]
应用：com.example.app
字段：/apps/com.example.app/redirect
原因：该可见路径同时声明了 deny 和 redirect

18 |     "Pictures/Private" -> "Pictures/Public",
         ^^^^^^^^^^^^^^^^^^

相关规则：rules.toml:12:5
12 |     "Pictures/Private",
         ^^^^^^^^^^^^^^^^^^

新配置未发布，当前仍使用 content generation 42。
```

诊断至少包含：

- 稳定错误码。
- 严重级别和失败阶段。
- 文件名、行和列。
- 应用包名。
- 由分段数组或 JSON Pointer 表示的完整字段路径。
- 用户可理解的原因。
- 所有关联冲突位置。
- 新配置是否已经发布。
- 当前仍在使用的 content generation 和 deployment epoch。

编译器内部只维护一份结构化诊断模型，文本、JSON、状态文件和 Manager UI 都由该模型渲染，不允许每个入口复制一套错误判断。最小字段为：

```text
Diagnostic
  code
  severity
  phase
  primary SourceSpan
  related SourceSpan[]
  field_path segments[]
  message_key + arguments
```

`message_key + arguments` 用于稳定机器接口和后续本地化；核心校验器不拼接多套面向不同输出端的长字符串。为防止恶意或严重损坏的输入放大内存和日志，单次编译还必须限制诊断总数，达到上限后追加一条“其余诊断已省略”。

由于用户通常不会运行桌面 IDE，daemon 还应原子更新固定的人类可读状态文件，例如：

```text
module/run/rules-status.txt
```

内容至少包含：

```text
source: rules.toml
source_digest: sha256:...
candidate_sequence: 43
active_content_generation: 42
deployment_epoch: 17
capability_generation: 8
topology_generation: 5
status: source_invalid
error: rules.toml:18:5 PG-RULE-REDIRECT-CONFLICT
message: 该路径同时声明了 deny 和 redirect
```

该文件是只读诊断输出，不是第三份配置，也不能被反向编译。模块日志和 `pathguardctl status` 应提供同一份状态信息。

CLI 建议提供：

```text
pathguardctl validate <rules.toml> --host
pathguardctl validate <rules.toml> --device
pathguardctl lint <rules.toml>
pathguardctl compile <rules.toml> <output-policy.bin>
pathguardctl reload <module-dir>
pathguardctl plan <rules.toml> --against <policy.bin>
pathguardctl fmt <rules.toml>
pathguardctl explain <policy.bin> <package>
pathguardctl explain <policy.bin> <package> --path <path>
pathguardctl dump <policy.bin> --format pathguard-toml|strict-toml|json
```

其中：

- `lint` 报告冗余规则、父子遮蔽、大小写/Unicode 近似碰撞和 legacy 风险，不改变策略。
- `validate --host` 运行设备无关编译；`validate --device` 在其结果上执行当前设备准入。
- `compile` 只写显式离线输出，不直接替换模块活动策略；`reload` 通过控制 UDS 请求 daemon 重新读取并发布。
- `plan` 对比新规则与当前策略，列出应用、deny、redirect 和兼容设置的新增、删除与修改。
- `explain --path` 展示最长前缀命中规则及被遮蔽的父规则。
- `strict-toml` 和 `json` 只用于工具消费与诊断，不成为第二输入源。
- 诊断命令应支持稳定的 JSON 消息格式，供 Manager 和 CI 复用。

`fmt` 是显式调用的可选增强，daemon 保存或热加载时绝不能自动格式化、排序或改写 `rules.toml`。如果实现，不能简单包装 Taplo 等严格 TOML 格式化器，因为源文件中的 `->` 不是标准 TOML。它必须理解 PathGuard 扩展并满足：

- 保留 `->` 用户语法，不输出内部 `{ from, to }`。
- 保留注释及其关联位置。
- 尽量保留用户的空行和能力分组。
- 统一缩进、逗号、引号和箭头周围空白。
- 默认将短 redirect 规范为一行；长规则可以按固定宽度换行。
- 无法保证无损时拒绝原地覆盖，可只输出到 stdout 或新文件。

因此 `fmt` 是语义感知组件，但在手机端文本编辑场景下优先级低于解析、诊断、原子发布和状态文件。它可以在核心格式切换完成后独立交付。

## 16. 迁移方案

项目尚未正式发布，采用一次性破坏性迁移。

### 16.1 字段映射

| 当前 `rules.ini` | 新 `rules.toml` | 处理 |
|---|---|---|
| `schema = 2` | `format = 1` | 解耦源格式与二进制 schema |
| `failure = open` | 删除 | 使用固定事务与上一有效策略语义 |
| `allow_legacy_string_bind` | `compatibility.allow_legacy_mount` | 改为用户可理解名称 |
| `[package]` | `[apps."package"]` | 明确应用命名空间 |
| `users = 0, 10` | `users = [0, 10]` | 使用强类型整数数组 |
| `processes = *` | 省略 | 默认全部包内进程 |
| `provider = virtualize` | `file_picker = true` | 表达用户意图而非后端名称 |
| 无 | `enabled = true/false` | 允许保留配置并临时停用单个应用策略 |
| `deny Path` | `deny = ["Path"]` | 标准字符串数组 |
| `redirect A -> B` | `redirect = ["A" -> "B"]` | 保留箭头并统一引号 |

### 16.2 代码改动范围

实现阶段至少涉及：

- 将 `module/config/rules.ini` 替换为 `module/config/rules.toml`。
- 用新编译管线替换 `ParseRulesIni()`。
- 将现有混合用途的 `PolicyDocument` 拆为源文档、Resolved Policy、Canonical Policy 和运行时二进制模型；源码位置改由 `OriginMap` 保存。
- CLI 参数、帮助信息和错误文本改用 `rules.toml`。
- daemon 监控路径从 `rules.ini` 切换到 `rules.toml`，并成为活动策略唯一发布者。
- 更新架构文档、重定向子系统文档和 README 示例。
- 引入并审计 TOML 解析依赖。
- 增加扩展语法脱糖器和源码位置映射。
- 拆分 Host 编译、Device admission、PolicyEncoder、PolicyVerifier 和 PolicyPublisher。
- 将结构化诊断、状态标识和 `RulesLimits` 作为 daemon、CLI 与 Manager 的共享契约。

### 16.3 不维护长期双格式

不在 daemon 中长期保留：

```text
if rules.toml exists -> parse new
else if rules.ini exists -> parse old
```

这种分支会扩大测试矩阵并阻碍删除旧解析器。若确实需要保留开发机已有样例，可提供一次性离线迁移命令；迁移完成后运行时只识别 `rules.toml`。

### 16.4 二进制兼容

如果等价新规则生成的 Canonical Policy 与当前结构一致，`policy.bin format v5` 无需升级。迁移测试应验证旧样例和新样例生成等价的应用、用户、进程、mount、event 和兼容标志。

旧开发规则若使用 `{user}`、`{package}` 等占位符，不进行静默迁移或字符串替换；迁移工具必须要求用户改成具体相对路径。对不含占位符的具体路径，本次源格式变化不要求升级二进制格式。

## 17. 测试策略

### 17.1 脱糖器

至少覆盖：

- 单条和多条 redirect。
- 运算符两侧不同空白。
- 运算符前后跨物理行。
- 路径字符串内部包含 `->`。
- 注释包含 `->`。
- 路径包含空格、`#`、Unicode 和转义引号。
- 缺少源、目标或运算符。
- 多个外部运算符。
- 普通键值、inline table 字段和 table header 中出现外部 `->` 时报告局部上下文错误。
- 非 redirect 数组中出现局部合法 `->` 时由 AST scope validator 报错。
- 链式箭头、表达式内部注释和两条规则间缺少逗号。
- 数组尾逗号。
- CRLF 与 LF。
- 源码位置映射。
- toml++ `node::source()` 与 `toml_edit::Document` 的 inline table/value byte span 分别和 GeneratedRedirect 一一绑定。
- 使用同一套 binder-neutral 语料验证两个候选 binder；source region 不足时只允许为 C++ 候选验证内部隐藏标记 fallback 原型，并触发 D0 Go/No-Go，不能默认固化为生产实现。
- `toml_edit::Document` 保留 span，而 `DocumentMut`/`into_mut()` 清除 span 的回归测试。
- TOML 1.0 接受/拒绝 conformance 子集，防止 Cargo 依赖更新无意启用 TOML 1.1 语法。
- Rust compiler 输出与现有 207-byte `policy.bin` golden 完全一致，并由 C++ `DecodePolicy()` 独立读取。
- C ABI 对非法 UTF-8、空输入、超限输入、未知 ABI/枚举、null handle 释放、唯一所有权、C/Rust layout 和 panic 转换的测试；不为调用方 double-free 错误引入全局 handle registry。
- `format` 位于首个有效声明、缺失、重复和未知版本。
- 箭头只允许位于 `apps.*.redirect`。

### 17.2 TOML 解码

- 缺少或不支持的 `format`。
- 重复应用表。
- 非法包名。
- `users` 类型错误、重复或非法 user ID。
- 未知字段和未知模块。
- `file_picker` 类型错误。
- `enabled` 类型错误；禁用应用仍执行 Host 静态编译校验但不进入 Policy IR。
- 空应用策略。

### 17.3 语义校验

- 同路径 deny/redirect 冲突。
- 父子路径最长前缀行为。
- 冗余 deny 诊断。
- redirect 自映射、目标位于源内部和循环。
- `{user}`、`{package}` 和未知 `{...}` 不执行占位符展开。
- 相同 VFS 路径在独立 mount 执行域中允许，在同一执行域中按冲突矩阵处理。
- Provider 无法区分请求者时能够发现跨应用反向映射歧义。
- Provider 与显式用户/capability 约束。
- Host 编译稳定生成 `PolicyRequirements`，不读取设备瞬时 capability/topology。
- Device admission 在固定快照上完成 strict/legacy 计划级后端选择。
- 未完成 executor 的构建期 compile gate。
- `enabled = false` 不生成应用策略，重新启用后恢复相同规则语义。
- `RulesDocument`、`ResolvedPolicy`、`CanonicalPolicy` 和 `PolicyBlob` 之间不存在源码位置或设备状态的反向泄漏。

### 17.4 编译与发布

- 等价规则生成确定性 Canonical Policy IR。
- 等价格式变化不改变 content generation。
- 相同 Canonical Policy 始终生成相同 PolicyBlob，不受 capability/topology 快照变化影响。
- capability/topology 变化只触发重新准入，不改变 Host 编译结果。
- 编译失败保留上一份 `policy.bin`。
- 临时文件校验失败不发布。
- 使用唯一临时文件，不复用固定 `.tmp`；文件和目录持久化失败均不会误报发布成功。
- daemon 是活动快照唯一写入者，CLI 离线编译不能覆盖活动路径。
- rename 保存、连续写入和 inotify overflow 后能够重新收敛。
- 编译期间再次保存时，reconcile worker 最终处理最新 `source_digest`。
- daemon 重启后恢复上一份有效策略和最近诊断。
- 状态文件缺失、过期或与 `policy.bin` 不一致时，daemon 以验证后的活动快照重建状态。
- 注释或空白变化不改变 content generation。
- `plan` 能稳定报告语义新增、删除和修改。
- MT 管理器等编辑器采用临时文件 rename 保存后能够触发重新编译。
- 原地截断写入期间不会发布中间态。
- UTF-8 BOM、LF 和 CRLF 按规范处理。
- 非普通文件、符号链接和不安全写权限被拒绝并写入状态文件。

### 17.5 工具与编辑

- `fmt` 保留头部、行尾和规则间注释。
- `fmt` 保留箭头语法并稳定处理跨行规则。
- Manager 局部编辑不删除无关应用和用户注释。
- Manager 携带过期 `source_digest` 保存时被拒绝，不覆盖外部编辑结果。
- JSON 诊断包含与文本诊断一致的错误码和 source span。
- 文本、JSON、状态文件和 Manager 使用同一结构化诊断，不复制错误判断。
- `explain --path` 正确展示最长前缀命中与父规则遮蔽。

### 17.6 健壮性

扩展语法扫描器和规则解码器应加入 fuzz 测试，重点验证：

- 任意输入不会越界、死循环或异常分配。
- 极长字符串、深层 TOML 表和大数组受到明确上限约束。
- 作用域展开、冲突声明和诊断数量受到统一 `RulesLimits` 约束。
- 错误输入不会生成部分有效策略。
- 诊断位置始终落在原始源文件范围内。

## 18. 安全与资源限制

编译器使用一份只读 `RulesLimits` 定义所有阶段共同遵守的资源预算，避免扫描器、TOML 解码、作用域展开和二进制编码各自维护不一致的魔法数字。至少包括：

- 源文件最大字节数。
- TOML/扩展 token 和语义节点数量上限。
- 应用数量上限。
- 每应用同步规则数量上限。
- 每应用事件规则数量上限。
- package/user/process 作用域展开后的计划和规则总数上限。
- 单路径最大 UTF-8 字节数和组件数。
- TOML 最大嵌套深度。
- 单次编译诊断数量和关联位置数量上限。

限制应在最早可判断的阶段执行，并在作用域展开和编码前再次验证，防止小型输入产生超大 IR。具体数值应依据现有 `policy.bin` 字段宽度、性能测试和 Android 设备内存预算确定；F0 必须冻结默认值和错误码，不在解析器中使用无界递归或无界增长。

任何用户字符串都不得拼接为 shell 命令。路径只进入规范化器、Policy IR 和固定参数系统调用链路。

## 19. 采用与拒绝清单

### 19.1 采用

- `rules.toml` 作为用户规则源名称。
- PathGuard 扩展 TOML。
- `deny` 字符串数组。
- `redirect = ["source" -> "target"]`。
- 词法脱糖后交给成熟 TOML 解析器。
- `[apps."package"]` 应用命名空间。
- 能力独立子表。
- 单向编译到 `policy.bin`。
- 源文档、Resolved Policy、Canonical Policy 与 PolicyBlob 分层；设备准入只产生轻量 AdmissionResult。
- 编译期严格拒绝歧义和构建期不支持能力，设备能力不足由独立 admission 报告。
- 按 mount、Provider、event 等真实执行域检查冲突。
- 上一份有效策略与原子发布。
- daemon 作为活动策略和状态的唯一发布者。
- `format` 的引导解析和版本化脱糖器。
- Host compile 与 Device admission 两阶段校验。
- 核心编译器只保留诊断所需源码索引；无损编辑和语义感知格式化按需求独立实现。
- 统一结构化诊断、状态标识和 `RulesLimits`。
- `plan`、`lint`、`explain --path` 与机器可读诊断。

### 19.2 拒绝

- 继续扩展现有 INI/DSL 混合解析器。
- 用 `+`、`-`、`!` 表示核心动作。
- 用规则书写顺序处理冲突。
- 用 `>` 代替 `->`；单独的 `>` 方向表达较弱且更像比较或 shell 重定向。
- 使用 `{ from = ..., to = ... }` 作为普通用户的唯一写法。
- 把整个 `"source -> target"` 放入一个字符串再二次解析。
- 对所有文本执行朴素 `->` 替换。
- 在 v1 路径中增加全局占位符、模板或表达式系统。
- 让 daemon 同时维护两份可编辑配置。
- 让 CLI、Manager 和 daemon 同时直接写活动 `policy.bin`。
- 用同一个可变 `PolicyDocument` 贯穿源解析、语义编译和运行时二进制。
- 为尚不存在的第三方模块设计动态插件或通用规则代数。
- 接受未知字段但不执行。
- 在执行器完成前让相应规则通过编译。
- 使用通用 TOML 格式化器直接改写含 `->` 的源文件。
- 为运行时尚无性能证据的路径匹配提前修改 `policy.bin` 为 trie。
- 在开发期为未发布的旧格式保留长期兼容分支。

## 20. 实施阶段

### Phase F0：格式冻结

- 冻结 `rules.toml format = 1` 字段、默认值和错误模型。
- 冻结 `format` 首声明规则和 redirect 跨行语义。
- 冻结局部 value-array 元素上下文、箭头错误优先级和手写 `{ from, to }` 拒绝规则。
- 冻结 v1 不支持路径占位符。
- 冻结 RulesDocument、Resolved Policy、Canonical Policy、Policy Requirements、AdmissionResult 和 PolicyBlob 的边界与不变量。
- 冻结 mount/Provider/event/isolation 的执行域和最小路径角色。
- 冻结 `source_digest`、`content_generation`、`deployment_epoch`、capability/topology generation 的含义。
- 冻结 `RulesLimits` 默认值和资源超限错误码。
- 固定 D0 两个候选：C++/toml++ v3.4.0 与 Rust/`toml_edit` 0.23.7 parse-only；记录 toml++ SHA-256、Cargo.lock、TOML 1.0 parser 实际版本、许可证和 Rust MSRV。
- 完成 Host 与 Android arm64 构建 spike，记录无异常/无 RTTI C++ 模式、Rust `panic = "unwind"` C ABI、stripped 体积、峰值内存、编译时间和运行时间。
- 使用同一套 binder-neutral golden 语料验证两个 generated node binder；C++ fallback 原型也必须复用同一断言。
- 用当前 207-byte `policy.bin format v5` golden 和 C++ reader 验证 Rust encoder 的字节级兼容性。
- 完成箭头和语言/parser 联合 Go/No-Go：source span 不可靠、TOML 1.0 语义无法锁定、精确映射需要自建完整 CST、正常路径必须全面依赖隐藏标记、C ABI 明显扩大复杂度或资源预算失败时，不自动进入 F1，而是选择另一候选或重新比较严格 TOML。
- 形成唯一生产实现决议；未选中的 parser、编译器原型和 fallback 不进入后续生产代码。
- 建立旧样例到新样例的 golden 映射。

### Phase F1：解析基础设施

- 实现不保存完整 TokenStream 的流式 PathGuard TOML 词法脱糖器。
- 实现 value array、inline table 和 table header 的浅层 frame 栈及局部数组元素校验。
- 实现 `RulesFormatProbe` 和版本化脱糖入口。
- 实现全有或全无 rewrite、字节区间 RewriteMap 和 GeneratedRedirect 来源记录。
- 接入 D0 选定的唯一 TOML parser；若选择 Rust，generated node 绑定全过程使用不可变 `toml_edit::Document`。
- 建立 `SourceBuffer + LineIndex + SourceSpan + OriginMap`，不在核心编译器实现完整无损 CST。
- 实现强类型 RulesDocument 解码和未知字段拒绝。
- 若选择 Rust，先在单个 `pathguard_rules_compiler` crate 内按模块实现，不为每个流水线阶段拆 crate；从构建和最终 ELF 依赖上证明 Rust/parser 只进入 daemon/CLI 控制面。
- 若选择 C++，从构建依赖上确保 toml++ 只进入控制面规则编译模块。

### Phase F2：语义编译

- 将 deny、redirect、应用选择器和 compatibility 映射为 Resolved Policy 和不可变 Canonical Policy。
- 复用并补强路径规范化，通过固定执行域和冲突矩阵完成模块内及跨模块校验。
- 生成设备无关的 Policy Requirements，并实现基于不可变 capability/topology snapshot 的 PolicyAdmission。
- 拆分并实现无副作用的 PolicyEncoder 与 PolicyVerifier。
- 验证等价规则继续生成兼容的 `policy.bin format v5`。
- 若选择 Rust，验证 encoder 对固定输入生成当前 207-byte golden，随后由现有 C++ reader 完整校验 checksum、content generation、排序和字段边界。

### Phase F3：控制面切换

- 切换 CLI、daemon、模块模板和 inotify 监控路径。
- 实现 PolicyPublisher、单 worker 的 ConfigReconciler、daemon 单写者发布事务和崩溃恢复。
- 实现 Host compile、Device admission、离线 `compile`、daemon `reload`、固定状态文件和统一 JSON 诊断。
- 验证 Root 文件管理器、Android 文本编辑器、`nano`、`vi`、`sed` 的典型保存方式。
- 删除 `ParseRulesIni()` 和旧 `rules.ini` 运行时分支。

### Phase F3.1：辅助工具

- 实现 `lint`、`plan` 和 `explain --path`。
- 按实际需求实现保留注释与箭头语法的 PathGuard 专用 `fmt`。
- 编辑器语法支持和 strict-TOML 导出均作为可选增强，不阻塞核心规则格式交付。

### Phase F4：文档与 Manager

- 更新架构文档、子系统文档、README 和内置示例。
- Manager 通过 daemon 使用同一编译器和 admission 结果，不复制另一套规则逻辑。
- 只有实际需要局部编辑或格式化时才实现独立 `rules_editor` 无损模型。
- Manager 保存携带 `source_digest`，使用乐观并发避免覆盖外部编辑。
- UI 分别展示 source digest、候选状态、失败阶段、当前 content generation 和 deployment epoch。

## 21. 完成定义

满足以下条件后，规则文件重构才算完成：

1. 仓库运行时不再读取 `rules.ini`。
2. `rules.toml` 的普通 deny/redirect 示例无需查阅文档即可理解。
3. `->` 在字符串、注释和 Unicode 路径中均无误判。
4. 所有未知字段和构建期不支持能力均在编译期失败；设备能力不足由 admission 明确拒绝。
5. 错误能够准确定位原始 `rules.toml` 行列。
6. 等价配置生成确定性的 Canonical Policy IR。
7. 新配置失败不会破坏上一份有效 `policy.bin`。
8. daemon 和应用启动路径不解析文本规则。
9. 旧解析器和长期双格式兼容分支已经删除。
10. Host 单元测试、模糊测试和配置发布集成测试全部通过。
11. `format` 在主脱糖前完成版本选择，未来格式版本不存在引导循环依赖。
12. daemon 永不自动重写 `rules.toml`；任何实现的 `fmt` 或 Manager 编辑都不会静默删除用户注释。
13. Host compile/validate 与 Device admission/validate 能够区分规则错误与设备能力不足。
14. 主流 Android Root 文件管理器和 shell 编辑方式保存后能够可靠触发编译。
15. 用户无需 LSP 或 Manager，也能从固定状态文件定位编译结果和错误。
16. 源码位置、注释和 TOML 节点不会进入 Canonical Policy 或运行时二进制模型。
17. Host 编译不读取设备瞬时 capability/topology，Device admission 不重新解释用户语法。
18. VFS 与 Provider 冲突按各自执行域判断，不再假设不同应用必然隔离。
19. daemon 是活动策略唯一写入者，CLI/Manager 不能绕过发布事务覆盖 `policy.bin`。
20. `source_digest`、content generation、deployment epoch 和 capability/topology generation 在状态与协议中含义明确。
21. 核心编译器不自行维护或持久化完整无损 CST；若选 `toml_edit`，其不可变 Document 只存在于单次 parse/decode 生命周期。Manager 冲突保存能够通过 source digest 被拒绝。
22. 所有解析、展开、诊断和编码阶段遵守统一 `RulesLimits`。
23. 选定的 TOML parser、Rust runtime（如采用）和规则编译器不会链接进入 Zygisk 应用启动数据面。
24. 箭头脱糖器 D0 已通过明确 Go/No-Go，且没有因为 fallback 可实现而跳过与严格 TOML 的投入产出比较。
25. D0 已冻结唯一生产语言/parser；不存在 Rust 脱糖器接 C++ AST、daemon/CLI 使用不同 parser 或双编译器长期 fallback。
26. 若选择 Rust，所有 C ABI panic、内存所有权、ABI 版本、超限输入和 207-byte binary golden 测试通过，编译失败继续保留上一份有效策略。
27. 若选择 Rust，每次候选发布前都由现有 C++ `DecodePolicy()` 独立验证其字节契约；验证失败不进入 PolicyPublisher。

## 22. 最终推荐格式

首版用户模板保持简洁：

```toml
# PathGuard Rules
# TOML with PathGuard redirect extension: "source" -> "target"
format = 1

[apps."org.localsend.localsend_app"]
enabled = true
users = [0]
file_picker = true

deny = [
    "Pictures/Private",
]

redirect = [
    "Download/localsend-source" -> "Download/localsend-redirect",
]
```

该格式把用户侧表达能力和机器侧执行效率分开：用户得到直观、紧凑、可扩展的规则文件；运行时继续得到已规范化、已验证、可快速加载的二进制策略。
