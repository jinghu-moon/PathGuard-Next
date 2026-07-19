# PathGuard Next Workspace

## Components

- `core/`: host-buildable policy and binary library.
- `daemon/`: long-lived `pathguardd` control and companion service.
- `zygisk/`: thin application-startup module.
- `cli/`: `pathguardctl` control client.
- `module/`: Magisk/KernelSU packaging template and runtime configuration.
- `protocol/`: shared control and bootstrap wire contracts.
- `tests/`: host, integration, and device tests.

The module scripts only manage lifecycle and packaging. They must not parse
rules or perform periodic process scans; those responsibilities belong to the
daemon and the Zygisk/companion path.
