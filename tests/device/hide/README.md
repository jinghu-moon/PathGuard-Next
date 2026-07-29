# Hide H0 device probes

This directory contains isolated research probes. They do not implement `hide`
and are not linked into the daemon or Zygisk module.

## Current scope

`pathguard_hide_vfs_probe` records JSONL observations for:

- `stat`, `lstat`, `access`, `open`, and `opendir`;
- libc `readdir` and raw `getdents64`;
- relative `fstatat`, `openat`, `openat2`, and `faccessat2`;
- a symlink alias and a known descendant;
- sandbox-only `O_CREAT`, `O_TRUNC`, `mkdirat`, `unlinkat`, `renameat`, and
  `linkat` mutations;
- `/proc/self/mountinfo`, `mounts`, and `mountstats` changes.

All mutations are rejected unless `--sandbox` starts with
`/data/local/tmp/pathguard-hide-h0-`, has a non-empty suffix, contains no
control characters or `..`, is owned by the current effective UID, and is
empty. The probe removes only fixture names that it created. The runner uses
`rmdir` for the sandbox and never recursively deletes it.

## Execution contexts

`run_hide_h0_baseline.ps1` executes under the ADB shell UID and SELinux domain.
Its results validate the native probe and establish a topology/syscall
baseline, but do not establish target-app behavior.

`app-probe/` provides that isolated debug APK. It is not part of the PathGuard
module and packages only `libpathguard_hide_app_probe.so`. It covers the
untrusted-app mount namespace, Java/NIO, JNI/native, MediaStore, Photo Picker,
and SAF surfaces. Installing it and granting its test-only media permissions
changes device state and therefore requires explicit approval before running
`run_hide_h0_app_probe.ps1`.

## Build

Use NDK r27d and API 31, matching the production native baseline:

```powershell
./scripts/build-native.ps1 -Abi arm64-v8a
```

The probe is generated at:

```text
native/libs/arm64-v8a/pathguard_hide_vfs_probe
```

## Run

Running the probe performs create/truncate/rename/unlink operations only in
the dedicated remote sandbox. Obtain explicit approval before running it on a
device.

```powershell
./tests/device/hide/run_hide_h0_baseline.ps1 `
  -Probe native/libs/arm64-v8a/pathguard_hide_vfs_probe `
  -ObservePath /storage/emulated/0/Pictures/Nagram
```

Each run writes `metadata.json` and `observations.jsonl` below
`build/device-evidence/hide-h0/<timestamp>/`.

## Build and run the app-domain probe

Build the shared native probe first, then the debug APK:

```powershell
./scripts/build-native.ps1 -Abi arm64-v8a
Push-Location ./tests/device/hide/app-probe
./gradlew.bat testDebugUnitTest assembleDebug
Pop-Location
```

After explicit approval for APK installation, run with the permissions already
configured on the device:

```powershell
./tests/device/hide/run_hide_h0_app_probe.ps1 `
  -Apk ./tests/device/hide/app-probe/app/build/outputs/apk/debug/app-debug.apk
```

Permission profiles are opt-in and must be tested separately. Add
`-GrantReadMediaImages` for the ordinary media-reader profile or add both
`-GrantReadMediaImages -GrantAllFilesAccess` for the all-files profile. These
switches change device permission state and require explicit approval.

The app runner performs mutations only below its private `no_backup` directory.
The configured shared-storage paths are read-only observations. Evidence is
written below `build/device-evidence/hide-h0-app/<timestamp>/`.

The APK also exposes Photo Picker, SAF image, Pictures tree, and DCIM tree
commands. Each result is appended to `selector-observations.jsonl`. After the
manual system UI interactions, collect all current evidence without changing
permissions or reinstalling the APK:

```powershell
./tests/device/hide/collect_hide_h0_app_evidence.ps1
```
