# PathGuard Next

PathGuard Next is an experimental Android storage-isolation module for Magisk Zygisk / KernelSU + ZygiskNext.

The current R1 prototype provides per-application selective directory redirect through
namespace-local VFS mounts, policy format v5 snapshot compilation, strict/legacy
capability selection, transactional rollback, and experimental opt-in SAF Provider
virtualization. Deny, isolate/allow, event automation, and MediaStore filtering are
compile-gated until their executors are complete. It is not production-ready; the
owner-death, topology-remount, rollback-failure, device, and ROM matrices must pass
before use.

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

## Rules

`module/config/rules.toml` is the only configuration source. Format 1 uses
TOML 1.0 plus the local array-element syntax `"source" -> "target"` for
redirects. The C++20 control-plane compiler uses toml++ v3.4.0 and publishes
verified policy format v5 bytes; Zygisk only reads that frozen binary.

```powershell
pathguardctl validate module/config/rules.toml --host
pathguardctl compile module/config/rules.toml output-policy.bin
pathguardctl lint module/config/rules.toml
pathguardctl plan old-rules.toml new-rules.toml
pathguardctl explain --path module/config/rules.toml com.example.app Download/file
pathguardctl reload module
pathguardctl status module
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
