# Rules Compiler RF9 Verification

> Date: 2026-07-27
>
> Result: Passed

## Scope

RF9 adds optional control-plane tools without changing rules syntax, Canonical
Policy semantics, policy format v5, or the Zygisk data path:

- `pathguardctl lint`, `plan`, and `explain --path`;
- daemon-owned Manager save with optimistic `source_digest` concurrency;
- an explicit YAGNI decision not to build `fmt` or a persistent CST before a
  real lossless-formatting requirement exists.

## TDD evidence

- `pathguard_rules_tools_test`: lint warnings, deterministic add/remove/modify
  plan, longest-prefix explanation, and shadowed parents.
- `pathguard_rules_tools_cli_integration`: CLI output and absence of policy
  writes.
- `pathguard_rules_manager_save_test`: stale digest, invalid candidate,
  unsupported admission, successful save/publish, repeated stale digest, and
  a deterministic concurrent-write race.
- The Manager test first failed at the missing `Reconciler::SaveRules` link
  symbol before the smallest production implementation was added.

All tools reuse `RulesBuildResult`, `OriginMap`, Resolved Policy, Canonical
Policy, the existing compiler validators, and the existing Reconciler. There
is no second parser, validator, rules syntax, or policy publisher.

## Verification

```text
Host MSVC Release:              47/47 passed
Android arm64-v8a build:        passed
Host/Android compiler parity:   passed
Zygisk ELF/link-map isolation:  passed
Alioth/R1 vertical path:        passed
```

The device path compiled and published generation `5668809052303656865`, then
read `Source -> Target` through the mmap/index/redirect-plan data path.

## Boundary result

`pathguard_rules_compiler` remains the only production rules compiler target.
The compiler and diagnostics are linked into daemon/CLI control-plane targets
only. `libpathguard_zygisk.so` remains free of toml++, desugarer, compiler, and
diagnostic dependencies. RF0 through RF9 are complete.
