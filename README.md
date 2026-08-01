# PathGuard Next

PathGuard Next is an experimental Android storage-isolation module for Magisk Zygisk / KernelSU + ZygiskNext.

The current R1 prototype provides per-application selective path redirect through
namespace-local VFS mounts and Provider path-I/O adapters. It uses rules format 2,
verified policy format v6 snapshots, a shared bounded Glob v1 selector runtime,
capability admission, transactional rollback, strict-FD directory deny, and route
provenance. Provider query/insert/reverse, asynchronous Export, CompleteVfs, system
Photo Picker filtering, and legacy-backend deny remain unsupported or prototype-only.
It is not production-ready; the remaining device, fault, compatibility, and performance
matrices must pass before use.

## Build

```powershell
./scripts/build.ps1
```

The architecture baseline and performance plan are documented in:

- [Architecture design](docs/00-architecture-design.md)
- [Reference projects](docs/01-reference-projects.md)
- [Performance audit and optimization plan](docs/02-performance-audit-and-optimization-plan.md)
- [Redirect subsystem design](docs/03-redirect-subsystem-design.md)
- [Rule file refactoring design](docs/04-rule-file-refactoring-design.md)
- [Arrow desugarer and rules compiler D0](docs/05-rule-arrow-desugarer-design.md)
- [Rules refactoring TDD checklist](docs/06-rule-file-refactoring-and-desugarer-tdd-implementation-checklist.md)
- [Pattern redirect design](docs/08-pattern-redirect-design.md)
- [Pattern redirect TDD checklist](docs/09-pattern-redirect-tdd-task-list.md)

## Rules

`module/config/rules.toml` is the only configuration source. Rules format 2 uses
TOML 1.0 tables with unified selectors and actions. The C++20 control-plane compiler
uses toml++ v3.4.0 and publishes verified policy format v6 bytes; Zygisk and Provider
adapters only read that frozen binary.

```powershell
pathguardctl validate module/config/rules.toml --host
pathguardctl compile module/config/rules.toml output-policy.bin
pathguardctl lint module/config/rules.toml
pathguardctl plan old-rules.toml new-rules.toml
pathguardctl explain --path module/config/rules.toml com.example.app Download/file
pathguardctl explain module/run/policy.bin com.example.app --json
pathguardctl reload module
pathguardctl status module
pathguardctl status module --json
```

The daemon is the only writer of `module/run/policy.bin`. Invalid source,
unsupported device admission, verification, fsync, or rename failures retain
the previous valid generation.

Manager saves use the daemon control layer with an expected `source_digest`.
Stale, invalid, or unsupported candidates are rejected before `rules.toml` is
replaced; successful saves still use the daemon's single publisher.

## Tests

```powershell
cmake -S . -B build -DPATHGUARD_BUILD_TESTS=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
./scripts/build-native.ps1 -Abi arm64-v8a
```

## License

This project is licensed under the GNU Affero General Public License, version 3 or any later version. See [LICENSE](LICENSE).
