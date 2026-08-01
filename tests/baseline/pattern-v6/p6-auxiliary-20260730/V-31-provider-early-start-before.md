# V-31 Provider early-start regression baseline

## Affected core scenario

- Package scope: `org.localsend.localsend_app`, user 0, UID 10382.
- LocalSend receives one ordinary file and one image through its configured SAF tree.
- Provider redirect rules route both `Download/localsend-source/**` and `Pictures/**`
  to `Download/localsend-redirect`.
- Known-good comparison: module built from commit
  `41df39529f01049ec5a0bb2c2d4b9a4ced3e2d79` routes both file types to the
  redirect target.

## Current before-change replay

On module `v0.1.16-dev`, after installation and reboot:

| Input | LocalSend result | Physical result |
| --- | --- | --- |
| `test7.jpg` | success | `Download/localsend-redirect/test7.jpg`, 125478 bytes, opens normally |
| `test7.txt` | success | `Download/localsend-source/test7.txt`, 75494 bytes |

The image path logged MediaProvider rewrites for caller UID 10382. The text
operation started `com.android.externalstorage` as PID 8431 at 23:12:21, but
that process emitted no PathGuard module or provider-hook installation record.
The active policy had already been published at 23:12:07.

After PID 8431 exited, a read-only provider query started PID 18605. That later
instance logged all expected lifecycle stages:

```text
module_onload pid=18605
provider virtual hooks: images=5 registrations=45 committed=1 active=1
path policy installed: scopes=1 domain=2 caller_scope=binder_uid
provider redirect specialize: process=external_storage installed=1
```

The LocalSend process restarted as PID 15799 and had the expected literal mount:

```text
/0/Download/localsend-redirect ->
    /storage/emulated/0/Download/localsend-source
```

## Root-cause boundary and planned breakage

Magisk's module lifecycle executes module `post-fs-data` scripts before it
recollects Zygisk module file descriptors. A Provider forked in that interval
cannot receive the PathGuard module and remains stale for its process lifetime.

The planned behavior change is limited to late-start reconciliation: when the
active policy contains Provider-domain actions, a supported storage Provider
that predates Zygisk availability and does not map the active `policy.bin` is
terminated once so Android can start a hook-ready instance on demand.

No changes are planned for selector semantics, caller UID isolation, target
mapping, LocalSend-specific behavior, or already hook-ready Provider processes.
