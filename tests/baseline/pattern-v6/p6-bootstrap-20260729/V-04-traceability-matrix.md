# V-04 C1～C6 需求追踪矩阵

## 记录信息

| 字段 | 值 |
| --- | --- |
| Change ID | `p6-bootstrap-20260729` |
| Task | `V-04` |
| Baseline commit | `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` |
| 设计来源 | `docs/08-pattern-redirect-design.md` 1.3、7、11 |
| 执行清单 | `docs/09-pattern-redirect-tdd-task-list.md` |
| Reviewer conclusion | C1～C6 均有现状、目标、测试和证据入口 |

## 状态定义

- `existing-green`：当前 52 项 Host 测试或本批真机证据已经覆盖；
- `partial`：核心子结果已观测，但设计要求的复合语义尚有缺口；
- `planned-red`：必须由后续 `[红]` 任务先建立失败测试；
- `unsupported/not_observed`：设备能力或场景未完成，不能推断通过。

## 核心场景矩阵

| Matrix ID | 场景 | 输入/作用域 | 关键操作 | 最低预期 | 执行域/能力 | 当前自动测试 | 当前设备证据 | 后续 TDD | 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C1-A | Literal deny | package→UID 10382, user 0, `Pictures/Nagram` | lookup/list/open | 目标主体失败，其他 namespace 不受影响 | mount；bits 0～4；strict operation set | `pathguard_mount_transaction_test`、`pathguard_mount_backend_test`、`pathguard_media_query_filter_test` | V-03：deny anchor mount；受限探针失败；Settings 无业务 mount | T-10、T-16、T-24；V-23/V-39 | existing-green，精确 errno 仍待统一 |
| C1-B | Literal deny | 同上，`DCIM/Screenshots` | lookup/list/open | active deny 不被 fail-open | mount；FD pinned | 同 C1-A + `pathguard_runtime_status_test` | V-03：`Permission denied`、runtime active/complete | T-24；V-39 | existing-green |
| C2-A | Literal redirect | `Pictures`→redirect | create/read/rename/delete | visible path 实际落 backing，tail 一致 | mount；bits 0～4 | `pathguard_provider_path_mapper_test`、mount tests、policy tests | V-03：唯一临时文件写入 backing，source/global absent | T-16/T-17；V-23/V-25 | existing-green（create）；rename/delete 待专项重放 |
| C2-B | Literal redirect | `Download/localsend-source`→redirect | create/read | 与 C2-A 同语义 | mount/app-path | 同 C2-A | V-03：第二 source 写入同 backing | T-16/T-17 | existing-green |
| C3-A | LocalSend SAF 普通文件 | Binder caller UID 10382, user 0 | DocumentsProvider create/open | 接收成功且 FD 写入 backing | provider caller UID + path I/O；目标 bits 16/19 | `pathguard_provider_caller_uid_test`、`pathguard_provider_redirect_lifecycle_test` | V-03：ExternalStorageProvider `caller_uid=10382` rewrite，LocalSend Saved | T-18；V-26/V-27 | existing-green |
| C3-B | LocalSend 图片 | Binder caller UID 10382 | MediaProvider open/FUSE | 图片写入 backing，source absent | provider caller UID + path I/O/FUSE-adapter subset | provider caller/lifecycle、media query filter | V-03：MediaProvider `caller_uid=10382` open rewrite，LocalSend Saved | T-18/T-19；V-27/V-29 | partial |
| C3-C | Provider query/insert/scan | 同 C3 | query/insert/scanner | URI、数据库、FD、实际文件一致 | target bit 17 + required operations | 当前无完整复合测试 | V-03：MediaStore 无记录，scanner 对 source `NoSuchFileException` | T-19→I-19→R-19；V-28/V-29 | planned-red；before 缺口已冻结 |
| C4-A | 多源前向不同名 | Pictures 与 Download source→同 target | create | 两个合法 source 均成功 | mount/app-path/provider；collision reject | provider mapper 与 conflict tests 仅覆盖旧 prefix | V-03：两 source 不同名均落 backing | T-11/T-22；V-35 | existing-green（旧 mount） |
| C4-B | 多源同名碰撞 | 两 source→同 target/name | mkdir/create | 第二次确定性 `EEXIST`/reject | evaluator/collision guard | `pathguard_rules_conflict_test` 不是运行时 target collision | V-03：第一次 rc=0，第二次 rc=1 | T-11→I-11→R-11 | partial；需冻结 errno/diagnostic |
| C4-C | 反向映射 | target identity→visible source | realpath/query/rename/delete/reboot | unique provenance 或 `AmbiguousReverse`，不得猜 | route provenance contract | 当前无 provenance 测试 | V-03 明确未宣称旧 canonical reverse 正确 | V-10、T-21/T-22；V-32～V-35 | planned-red |
| C5-A | 未命中主体透传 | Settings/其他 UID | source lookup | 无 LocalSend mount/rewrite | scope index + fail-open | provider caller UID/path mapper、policy lookup tests | V-03：Settings namespace 无业务 mount，不见虚拟 source 文件 | T-09/T-13/T-24；V-17/V-39 | existing-green |
| C5-B | 能力/运行时缺失 | missing/stale/invalid | load/admission/I/O | 对应 action fail-open，不误报 active | capability snapshot；bits 0～4/8～11/16～19 | runtime status、format cutover、release audit、hot reload tests | V-03 仅有旧 PID preflight rollback；历史 R1 含故障注入 | T-13/T-14/T-24；V-39/V-59 | partial；专项矩阵后续重放 |
| C5-C | active 安全决定 | deny/collision/ambiguous | I/O | 不被通用 fail-open 吞掉 | Decision reason/errno | mount/status tests 局部覆盖 | V-03 deny/collision 返回失败 | T-10/T-11/T-24 | planned-red（统一决策） |
| C6-A | Glob 后缀/文件名 | app/user/root + `*.jpg`/`IMG_*` | compile/match/create | 只匹配目标文件，其他透传 | app_path/provider；bit 19/16/17 + op mask | 当前无 Glob v1 测试 | 当前 format 1/v5 不支持，not observed | T-04～T-10、T-17～T-19；V-25/V-29 | planned-red |
| C6-B | Glob deny | `**/private-*`、字符类 | lookup/open | 命中 deny，作用域外透传 | provider/complete；明确 enforcement | 当前无 Glob deny 测试 | not observed | T-04～T-10/T-24 | planned-red |
| C6-C | Glob 多 operand | rename/link 两路径 | rename/link | 同一 snapshot，禁止半重写 | app_path/provider + operation mask | 当前无统一 matcher/evaluator | not observed | T-11/T-14/T-17/T-18 | planned-red |

## 稳定 capability 追踪

| Bit | 名称 | 当前代码 | 当前设备/证据 | 目标测试 |
| ---: | --- | --- | --- | --- |
| 0 | `openat2` | 已定义 | 不按 kernel 版本推断；专项 probe 留 V-36/V-37 | T-23 |
| 1 | component FD walk | 已定义 | mount runtime 为 fd_pinned | existing resolver/mount tests + T-23 |
| 2 | proc-fd mount | 已定义 | V-03 backend=2 active | mount backend/transaction + T-16 |
| 3 | open_tree/move_mount | 已定义 | alioth 返回 ENOSYS 后 fallback | mount backend tests |
| 4 | legacy string bind | 已定义 | 本次未选用 | mount backend tests、R1 matrix |
| 8～11 | fanotify family | 已定义 | 本批未探测 | T-27/V-42/V-43 |
| 16 | provider caller UID | ADR 已冻结、代码未共享落地 | V-03 真实日志观测 caller_uid=10382，但不能代替 stable bit | T-13/T-18 |
| 17 | provider query/insert mapping | ADR 已冻结、代码未共享落地 | V-03 MediaStore/scan 缺口，不能 observed | T-13/T-19 |
| 18 | complete FUSE path | ADR 已冻结、代码未共享落地 | 仅有部分 Hook/FUSE open，不满足 complete | T-13/T-29 |
| 19 | app-path adapter | ADR 已冻结、代码未共享落地 | 现有 Hook/mount 行为不能代替 stable bit | T-13/T-17 |

## 操作覆盖追踪

| Operation | Literal baseline | Glob target | Multi-operand/provenance |
| --- | --- | --- | --- |
| open/create | Host + V-03 | T-04～T-10、T-17～T-19 | T-21/T-22 |
| stat/access/realpath | Provider Host tests + V-03 log | T-17/T-18 | T-22 |
| opendir/mkdir | mount/Provider tests + V-03 collision | T-17/T-18 | T-11/T-22 |
| rename/link | mount transaction/Provider lifecycle 局部 | T-11/T-17/T-18 | T-22 |
| unlink/rmdir | Provider lifecycle 局部 | T-17/T-18 | T-22 |
| query/insert/scan | V-03 明确缺口 | T-19 | T-22 |
| reload/fork | hot reload existing | T-14/T-15 | provenance recovery T-21 |

## 证据入口

- Host 原始结果：`build/pattern-v6-v02-release/v02-ctest.xml`；
- Host 受控摘要：`V-02-host-baseline.md`；
- 真机基线与文件 hash：`V-03-device-baseline.md`；
- 历史故障证据：`tests/device/r1-safety-validation.md`；
- 新功能的 before/after 任务：`docs/09-pattern-redirect-tdd-task-list.md`。

## 验收结论

- C1～C6 每个核心结果均已映射输入、主体、操作、预期、能力、自动测试和设备步骤；
- 已有、部分、planned-red 和 not-observed 状态明确分开；
- bit 16～19 没有因现有 Hook 日志而被提前宣称落地；
- C3 MediaStore 与 C4 reverse 两个关键缺口已绑定对应 TDD 工作包；
- V-04 判定 `complete`。

## 2026-08-01 当前设备闭环更新

本节 supersede 上述 bootstrap 时点的 `planned-red/partial` 实施状态，不改写历史 before 事实。
机器入口为
`tests/baseline/pattern-v6/p6-final-device-myron-v024-20260801/V-45-V-63-current-device-closure.json`。

| 范围 | 当前结论 | 设备边界 |
| --- | --- | --- |
| C1 | Host 与 myron deny 均 observed | Nagram、Screenshots 返回 Permission denied |
| C2 | Host 与 myron mount redirect observed | create 落入 backing；生产 smoke 三挂载 1/1/1 |
| C3 | Provider path-I/O、TXT/JPG、FUSE open observed | bit 17 query/insert/reverse composite ABI unsupported |
| C4 | 多源不同名前向 observed；collision/provenance Host contract complete | collision/ambiguous 生产注入入口 not_observed |
| C5 | policy reload/race、topology race、mount cancel/rollback/owner death、snapshot gate observed | fanotify overflow unsupported |
| C6 | Glob v1 Host 全覆盖，Provider `**` 文件路由设备 observed | 多 operand 生产设备注入 not_observed |

| Capability | 当前结论 |
| --- | --- |
| bits 0～4 | mount backend/secure resolver Host 覆盖；myron `fd_pinned/backend=1` observed |
| bits 8～11 | `CONFIG_FANOTIFY` disabled，明确 unsupported |
| bit 16 | 两个 Provider 各 2/2 active per-action admission，与文件结果一致 |
| bit 17 | 无 production query/insert/reverse composite adapter，保持 unsupported |
| bit 18 | V-46 `adapter-only`，保持 unsupported |
| bit 19 | app-path/Provider path-I/O Host contract complete；当前 policy 无独立 app-path action |

第二设备、其他 ROM/root/kernel、arm32、当前设备无生产构造入口的 operation/state，以及无有效
warm latency telemetry 均按用户授权保持 `not_observed`。这些边界不阻断当前范围 V-61～V-63，
也不构成兼容性或 capability active 声明。
