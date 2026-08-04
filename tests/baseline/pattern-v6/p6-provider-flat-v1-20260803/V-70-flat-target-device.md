# V-70 flat target device validation

Date: 2026-08-03

Device scope: alioth, Android 13, the same available-device scope used by V-68/V-69.

Candidate:

- module version: `0.1.47-dev` (`versionCode=48`)
- package SHA-256: `4e561232c800ce03e994b82f8f33b5f5469a3911df686d18d70f1664ea4df5b1`
- normal universal package contains both LSPlant ABIs and `provider-hooker.dex`
- redirect runtime does not activate the LSPlant Java bridge

User workflow:

1. LocalSend received `test.txt` and `test1.jpg` with Save to gallery disabled.
2. LocalSend received `test2.jpg` with Save to gallery enabled.
3. The same `test2.jpg` was received again.
4. `collect_flat_redirect_status.ps1` was run after all operations.

Observed physical target:

```text
/storage/emulated/0/Download/localsend-redirect/test.txt       75494 bytes
/storage/emulated/0/Download/localsend-redirect/test1.jpg     125478 bytes
/storage/emulated/0/Download/localsend-redirect/test2.jpg     125478 bytes
/storage/emulated/0/Download/localsend-redirect/test2 (1).jpg 125478 bytes
```

The three JPG files have the same SHA-256,
`f2b5bb6a1851eb6db3fbd610df6f0d73610642739c7de3c6fc1b37cfeb205357`.
No file exists below the archived `_pg` layout. Empty legacy directories, if any,
are not production data and are not interpreted by the current policy.

LocalSend logs report `Saved` for every transfer and `Received all files` for all
three sessions. With Save to gallery disabled it used the SAF logical source. With
Save to gallery enabled it first saved to its private cache; the final Android path
operation still reached the same flat target. The repeated name became `test2 (1).jpg`.
PathGuard did not add this suffix and only preserved the caller/Provider behavior.

Both `com.android.providers.media.module` and `com.android.externalstorage` reported:

- `enforcement=active`
- two admitted Provider redirect actions
- `provider_bridge_library_loaded=false`
- `provider_bridge_lsplant_initialized=false`
- zero installed Java hooks

Direct `/proc/<pid>/maps` reads succeeded for both Provider processes and contained
no LSPlant or Provider Hooker mapping. No Provider fatal exception, JNI warning, or
PathGuard/LSPlant failure was observed.

Authoritative evidence:

- `build/device-evidence/provider-flat-v1/20260803-210013/`
- `20260803-205822` established the corrected per-process maps result manually;
  `20260803-210013` is the final automated rerun with maps read failures promoted to
  hard failures. The earlier `20260803-205700` run is not authoritative for the maps subcheck: its
  shell loop had a syntax error that the first collector revision did not reject.

Conclusion: V-70 passes in the current available-device scope. The production path
is a flat shared target, native Provider redirect remains active, and LSPlant is
packaged but decoupled from ordinary redirect.
