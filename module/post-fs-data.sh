#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
POLICY="$RUNDIR/policy.bin"
BOOTSTRAP="$RUNDIR/.policy.bin.bootstrap.$$"
CTL="$MODDIR/bin/pathguardctl"

mkdir -p "$MODDIR/run"
mkdir -p "$MODDIR/run/deny-anchor"
chown 0:0 "$MODDIR/run/deny-anchor"
chmod 0000 "$MODDIR/run/deny-anchor"

if [ -x "$CTL" ] && "$CTL" compile "$MODDIR/config/rules.toml" "$BOOTSTRAP" \
    >"$RUNDIR/compile.log" 2>&1; then
  chmod 0644 "$BOOTSTRAP"
  chown 0:0 "$BOOTSTRAP"
  mv -f "$BOOTSTRAP" "$POLICY"
else
  rm -f "$BOOTSTRAP"
fi

if [ -s "$MODDIR/config/hide1_device_profile.json" ] && [ -s "$MODDIR/bin/pathguard_hide1.ko" ]; then
  {
    echo "phase=post-fs-data"
    echo "hide_support_scope=myron-only"
    echo "device=$(getprop ro.product.device 2>/dev/null)"
    ARCH=$(getprop ro.product.cpu.abi 2>/dev/null)
    case "$ARCH" in arm64|arm64-v8a) ARCH=aarch64 ;; esac
    echo "arch=$ARCH"
    echo "kernel=$(uname -r 2>/dev/null)"
    echo "kmi=$(sed -n 's/^[[:space:]]*\"kmi\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p' "$MODDIR/config/hide1_device_profile.json" | head -n 1)"
    echo "boot_id=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null || echo unknown)"
    echo "fingerprint=$(getprop ro.build.fingerprint 2>/dev/null)"
    echo "module_sha256=$(sha256sum "$MODDIR/bin/pathguard_hide1.ko" 2>/dev/null | awk '{print $1}')"
  } > "$RUNDIR/boot-state"
  chmod 0600 "$RUNDIR/boot-state"
fi
