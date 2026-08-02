# Pattern Redirect v6 执行证据

本目录保存 `docs/09-pattern-redirect-tdd-task-list.md` 的受版本控制证据摘要。
原始构建、CTest XML、ADB 日志和设备快照位于报告指向的 `build/` 子目录，生成物不提交。

## 当前批次

| Change ID | Task | Status | Evidence |
| --- | --- | --- | --- |
| `p6-bootstrap-20260729` | V-01 | complete | `p6-bootstrap-20260729/V-01-environment.md` |
| `p6-bootstrap-20260729` | V-02 | complete | `p6-bootstrap-20260729/V-02-host-baseline.md` |
| `p6-bootstrap-20260729` | V-03 | complete | `p6-bootstrap-20260729/V-03-device-baseline.md` |
| `p6-bootstrap-20260729` | V-04 | complete | `p6-bootstrap-20260729/V-04-traceability-matrix.md` |
| `p6-bootstrap-20260729` | V-05 | complete | `p6-bootstrap-20260729/V-05-current-design-gap.md` |
| `p6-bootstrap-20260729` | V-06 | complete | `p6-bootstrap-20260729/V-06-reference-project-evidence.md` |
| `p6-bootstrap-20260729` | V-07 | complete | `p6-bootstrap-20260729/V-07-official-constraints.md` |
| `p6-bootstrap-20260729` | V-08 | complete | `p6-bootstrap-20260729/V-08-selector-negation-decision.md` |
| `p6-bootstrap-20260729` | V-09 | complete | `p6-bootstrap-20260729/V-09-policy-format-v6-decision.md` |
| `p6-bootstrap-20260729` | V-10 | complete | `p6-bootstrap-20260729/V-10-route-provenance-decision.md` |
| `p6-comparison-report-20260729` | T-01 | complete | `p6-bootstrap-20260729/T-01-comparison-report-red.md` |
| `p6-comparison-report-20260729` | I-01 | complete | `p6-bootstrap-20260729/I-01-comparison-report-validator-green.md` |
| `p6-comparison-report-20260729` | R-01 | complete | `p6-bootstrap-20260729/R-01-comparison-report-schema-refactor.md` |
| `p6-comparison-report-20260729` | V-11 | complete | `p6-bootstrap-20260729/V-11-comparison-report-replay.md` |
| `p6-pattern-harness-20260730` | T-02 | complete | `p6-bootstrap-20260729/T-02-pattern-harness-red.md` |
| `p6-pattern-harness-20260730` | I-02 | complete | `p6-bootstrap-20260729/I-02-pattern-harness-green.md` |
| `p6-pattern-harness-20260730` | R-02 | complete | `p6-bootstrap-20260729/R-02-pattern-harness-refactor.md` |
| `p6-pattern-harness-20260730` | 第一部分最终验证 | complete | `p6-bootstrap-20260729/first-part-final-verification.md` |
| `p6-core-20260730` | V-12 | complete | `p6-core-20260730/V-12-rules-schema-before.md` |
| `p6-core-20260730` | T-03～T-07 | complete | `p6-core-20260730/T-03-T-07-pattern-core-red.md` |
| `p6-core-20260730` | I-03～R-07 | complete | `p6-core-20260730/I-03-R-07-pattern-core-green.md` |
| `p6-core-20260730` | V-13 | complete | `p6-core-20260730/V-13-rules-schema-after.md` |
| `p6-core-20260730` | T-08～T-11 | complete | `p6-core-20260730/T-08-T-11-runtime-red.md` |
| `p6-core-20260730` | I-08～R-11 | complete | `p6-core-20260730/I-08-R-11-runtime-green.md` |
| `p6-core-20260730` | V-14 | complete | `p6-core-20260730/V-14-policy-v5-before.md` |
| `p6-core-20260730` | T-12～R-16 / V-15～V-23 | complete | `p6-core-20260730/V-15-V-23-policy-runtime-after.md` |
| `p6-core-20260730` | T-17～R-20 / V-24～V-31 | complete + device not_observed | `p6-core-20260730/V-24-V-31-path-provider-after.md` |
| `p6-core-20260730` | T-21～R-24 / V-32～V-39 | complete + device not_observed | `p6-core-20260730/V-32-V-39-provenance-security-after.md` |
| `p6-provider-lifecycle-20260731` | V-31 follow-up | complete + partial device observed | `p6-auxiliary-20260731/V-31-provider-lifecycle-after.md` |
| `p6-selector-except-audit-20260731` | V-40 / T-25～R-25 / V-41 | complete | `p6-auxiliary-20260731/V-40-V-41-selector-except-after.md` |
| `p6-event-before-20260731` | V-42 | complete + device unsupported | `p6-auxiliary-20260731/V-42-event-before.md` |
| `p6-auxiliary-audit-20260731` | T-26～V-48 implementation audit | complete（available-device scope） | `p6-final-device-myron-v024-20260801/V-45-V-63-current-device-closure.json` |
| `p6-export-host-20260801` | T-27～R-27 Host contract | not_observed（production fanotify scope waived） | `p6-auxiliary-20260731/T-27-export-host-contract.md` |
| `p6-status-before-20260801` | V-44 | complete | `p6-auxiliary-20260731/V-44-cli-status-before.md` |
| `p6-runtime-status-20260801` | T-28～R-28 status contract/wiring | complete | `p6-auxiliary-20260731/T-28-runtime-status-host.md` |
| `p6-complete-vfs-gate-20260801` | V-46 | complete | `p6-auxiliary-20260731/V-46-complete-vfs-investment-gate.md` |
| `p6-pattern-gate-20260801` | T-30 matcher gate | complete | `p6-auxiliary-20260731/T-30-pattern-gate-host.md` |
| `p6-runtime-gate-20260801` | T-30～R-30 runtime/soak Host gate | complete | `p6-auxiliary-20260731/T-30-runtime-performance-gate.md` |
| `p6-v48-host-after-20260801` | V-48 Host soak after | complete | `p6-auxiliary-20260731/V-48-host-soak-after.md` |
| `p6-final-host-20260801` | V-49～V-54 cutover/Release Host gate | complete | `p6-final-host-20260801/V-49-V-54-cutover-host.json` |
| `p6-final-host-20260801` | V-55 sanitizer/property/fuzz Host gate | complete | `p6-final-host-20260801/V-55-sanitizer-property-fuzz.json` |
| `p6-final-host-20260801` | V-56 Android NDK/ABI/ELF offline gate | complete | `p6-final-host-20260801/V-56-android-offline-gate.json` |
| `p6-final-host-20260801` | V-59～V-62 Host/final offline audit | complete + available-device closure | `p6-final-host-20260801/V-59-V-62-final-host-audit.json` |
| `p6-final-device-myron-20260801` | V-57～V-62 Android 16/myron device batch | complete（available-device scope） | `p6-final-device-myron-v024-20260801/V-45-V-63-current-device-closure.json` |
| `p6-final-device-myron-v59-20260801` | V-59 myron fault injection and production recovery | complete（available-device scope） | `p6-final-device-myron-v024-20260801/V-45-V-63-current-device-closure.json` |
| `p6-final-device-myron-v024-20260801` | V-45/V-48/V-60 Provider status、restart、retention 与 cold-start 补充 | complete（available-device scope） | `p6-final-device-myron-v024-20260801/V-45-V-60-v024-device.json` |
| `p6-final-device-myron-v024-20260801` | V-45～V-63 current-device closure | complete（available-device scope） | `p6-final-device-myron-v024-20260801/V-45-V-63-current-device-closure.json` |
| `device-matrix-scope-waiver-20260801` | V-48、V-57～V-60 第二设备子矩阵 | not_observed（user-authorized scope waiver） | `device-matrix-scope-waiver-20260801.md` |
| `p6-provider-contract-20260801` | T-34～R-34 Provider contract Host 合同 | complete | `p6-provider-contract-20260801/T-34-R-34-provider-contract-host.md` |
| `p6-provider-contract-20260801` | V-64 Provider 公共操作与模块身份基线 | complete（alioth public-contract scope） | `p6-provider-contract-20260801/V-64-provider-public-contract-alioth.json` |
| `p6-provider-contract-20260801` | T-35～R-35 version-pinned profile 与 mapping Host 合同 | complete | `p6-provider-contract-20260801/T-35-R-35-provider-adapter-profile-host.md` |
| `p6-provider-lsplant-20260802` | T-36～R-36 LSPlant bridge 与 production lifecycle wiring | complete（Host/offline）；V-65 passthrough observed，完整 V-65 pending | `p6-provider-lsplant-20260802/T-36-R-36-lsplant-wiring-host.md` |
| `p6-provider-mapping-20260802` | T-37～R-37 Provider mapping decision Host 合同 | complete（Host）；Java/native mapping wiring pending | `p6-provider-mapping-20260802/T-37-R-37-provider-mapping-host.md` |
| `p6-provider-v65-20260802` | V-65 当前设备 passthrough 与真实映射边界 | partial（passthrough observed；真实映射 unsupported/not_observed） | `p6-provider-v65-20260802/V-65-current-device-passthrough-and-mapping-boundary.json` |
| `p6-provider-v66-20260802` | V-66 当前设备 Provider restart/republication | partial（restart observed；Mainline/provenance recovery unsupported/not_observed） | `p6-provider-v66-20260802/V-66-current-device-restart.json` |
| `p6-provider-callback-20260802` | T-38～R-38 Provider callback safety boundary | complete（dispatcher seam；真实 provenance wiring pending） | `p6-provider-callback-20260802/T-38-R-38-provider-callback-boundary.md` |
| `p6-provider-native-dispatch-20260802` | T-39～R-39 native dispatcher pass-through seam | complete（Host/ABI/device passthrough） | `p6-provider-native-dispatch-20260802/T-39-R-39-native-dispatch-seam.md` |
| `p6-provider-dispatch-spec-20260802` | T-40～R-40 Provider Java dispatch spec | complete（Host/ABI；对象提取 pending） | `p6-provider-dispatch-spec-20260802/T-40-R-40-dispatch-spec-host.md` |
| `p6-provider-open-mode-20260802` | T-41～R-41 Provider open mode bounded extractor | complete（Host/ABI/device passthrough） | `p6-provider-open-mode-20260802/T-41-R-41-open-mode-host.md` |
| `p6-provider-identifier-20260802` | T-42～R-42 Provider URI/document ID extractor | complete（Host/ABI/device passthrough） | `p6-provider-identifier-20260802/T-42-R-42-identifier-host.md` |
| `p6-provider-file-reverse-20260802` | T-43～R-43 Provider File reverse extractor | complete（Host/ABI/device passthrough） | `p6-provider-file-reverse-20260802/T-43-R-43-file-reverse-host.md` |
| `p6-provider-request-20260802` | T-44～R-44 immutable Provider dispatch request | complete（Host/ABI；候选包待真机回归） | `p6-provider-request-20260802/T-44-R-44-request-host.md` |
| `p6-provider-content-values-20260802` | T-45～R-45 ContentValues dynamic operation extractor | complete（Host/ABI/device passthrough） | `p6-provider-content-values-20260802/T-45-R-45-content-values-host.md` |
| `p6-provider-delete-target-20260802` | V-67 delete target feasibility boundary | not_observed（当前设备/ABI 不支持，按 fail-open 跳过） | `p6-provider-delete-target-20260802/V-67-delete-target-boundary.md` |
| `p6-provider-mapping-adapter-20260802` | T-46～R-46 native mapping decision adapter seam | complete（Host/ABI；device passthrough） | `p6-provider-mapping-adapter-20260802/T-46-R-46-mapping-adapter-host.md` |
| `p6-provider-forward-binding-20260802` | T-47～R-47 operation-specific binding validation | complete（Host/ABI/device passthrough） | `p6-provider-forward-binding-20260802/T-47-R-47-forward-binding-host.md` |
| `p6-provider-runtime-resolver-20260802` | T-48～R-48 runtime resolver C ABI/native seam | complete（Host/ABI/device passthrough；真实 mapping unsupported/not_observed） | `p6-provider-runtime-resolver-20260802/T-48-R-48-runtime-resolver-seam-host.md` |
| `p6-provider-java-result-20260802` | T-49～R-49 native-to-Java DispatchResult transport | complete（Host/ABI/device passthrough；真实 rewrite unsupported/not_observed） | `p6-provider-java-result-20260802/T-49-R-49-java-result-transport-host.md` |
| `p6-provider-after-dispatch-20260802` | T-50～R-50 before/backup/after lifecycle | complete（Host/ABI/device passthrough；真实 rewrite unsupported/not_observed） | `p6-provider-after-dispatch-20260802/T-50-R-50-after-dispatch-host.md` |
| `p6-provider-result-observation-20260802` | T-51～R-51 result-kind observation | complete（Host/ABI/device passthrough；真实 rewrite unsupported/not_observed） | `p6-provider-result-observation-20260802/T-51-R-51-result-observation-host.md` |
| `p6-provider-c-abi-facts-20260802` | T-52～R-52 resolver request/facts C ABI | complete（Host/ABI/device passthrough；真实 resolver unsupported/not_observed） | `p6-provider-c-abi-facts-20260802/T-52-R-52-c-abi-facts-host.md` |
| `p6-provider-route-snapshot-20260802` | T-53～R-53 immutable route snapshot registry | complete（Host/ABI contract；production resolver wiring pending） | `p6-provider-route-snapshot-20260802/T-53-R-53-route-snapshot-host.md` |

`p6-auxiliary-20260730/V-31-provider-early-start-before.md` 是上述 Provider 生命周期修复的 before 证据；
原文件曾误标为 V-40，已按任务清单更正为 V-31，避免与 ADR-0015 决策门冲突。

状态只能使用 `pending`、`in_progress`、`complete`、`blocked` 或 `not_observed`。
`complete` 要求对应任务的验收标准和证据均已满足。
最终 Host/离线门和独立设备批次均以 comparison report format 1 JSON 为机器可验证入口；
同名 Markdown 保留执行细节。每份 JSON 均须通过
`tests/baseline/validate_comparison_report.cmake`。
