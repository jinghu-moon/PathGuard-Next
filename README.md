# PathGuard Next

PathGuard Next is an experimental Android storage-isolation module for Magisk Zygisk / KernelSU + ZygiskNext.

The current prototype provides per-application deny rules, namespace-local mounts, policy snapshot compilation, and optional MediaStore query filtering. It is not production-ready; device and ROM compatibility must be validated before use.

## Build

```powershell
./scripts/build.ps1
```

The architecture baseline and performance plan are documented in:

- [Architecture design](docs/00-architecture-design.md)
- [Reference projects](docs/01-reference-projects.md)
- [Performance audit and optimization plan](docs/02-performance-audit-and-optimization-plan.md)
- [Redirect subsystem design](docs/03-redirect-subsystem-design.md)

## License

This project is licensed under the GNU Affero General Public License, version 3 or any later version. See [LICENSE](LICENSE).
