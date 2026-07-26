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

## License

This project is licensed under the GNU Affero General Public License, version 3 or any later version. See [LICENSE](LICENSE).
