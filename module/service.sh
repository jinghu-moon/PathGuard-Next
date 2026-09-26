#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
DAEMON="$MODDIR/bin/pathguardd"
PIDFILE="$RUNDIR/pathguardd.pid"

mkdir -p "$RUNDIR"
mkdir -p "$RUNDIR/deny-anchor"
chown 0:0 "$RUNDIR/deny-anchor"
chmod 0000 "$RUNDIR/deny-anchor"
exec >>"$RUNDIR/service.log" 2>&1

load_hide_lkm() {
  local profile="$MODDIR/config/hide1_device_profile.json"
  local ko="$MODDIR/bin/pathguard_hide1.ko"
  [ -s "$profile" ] && [ -s "$ko" ] || return 0
  local device
  device=$(getprop ro.product.device 2>/dev/null)
  if [ "$device" != "myron" ]; then
    echo "hide unsupported: device=$device; supported_device=myron"
    return 0
  fi
  local expected_device expected_arch expected_release expected_fingerprint expected_hash
  expected_device=$(sed -n 's/^[[:space:]]*"device"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$profile" | head -n 1)
  expected_arch=$(sed -n 's/^[[:space:]]*"arch"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$profile" | head -n 1)
  expected_release=$(sed -n 's/^[[:space:]]*"kernel_release"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$profile" | head -n 1)
  expected_fingerprint=$(sed -n 's/^[[:space:]]*"fingerprint"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$profile" | head -n 1)
  expected_hash=$(sed -n 's/^[[:space:]]*"module_sha256"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$profile" | head -n 1)
  local actual_arch actual_hash
  actual_arch=$(getprop ro.product.cpu.abi 2>/dev/null)
  case "$actual_arch" in arm64|arm64-v8a) actual_arch=aarch64 ;; esac
  actual_hash=$(sha256sum "$ko" 2>/dev/null | awk '{print $1}')
  if [ "$(getprop ro.product.device 2>/dev/null)" != "$expected_device" ] \
    || [ "$actual_arch" != "$expected_arch" ] \
    || [ "$(uname -r 2>/dev/null)" != "$expected_release" ] \
    || [ "$(getprop ro.build.fingerprint 2>/dev/null)" != "$expected_fingerprint" ] \
    || [ "$actual_hash" != "$expected_hash" ]; then
    echo "hide lkm load denied: fixed-device profile mismatch (supported_device=myron)"
    return 0
  fi
  if ! grep -q '^pathguard_hide1 ' /proc/modules 2>/dev/null; then
    local ksu=''
    if [ -x /data/adb/ksu/bin/ksud ]; then ksu=/data/adb/ksu/bin/ksud
    elif [ -x /data/adb/ksud ]; then ksu=/data/adb/ksud
    elif command -v ksud >/dev/null 2>&1; then ksu=$(command -v ksud)
    fi
    if [ -n "$ksu" ]; then
      "$ksu" insmod "$ko" shadow_mode=0 diagnostic_probes=0 symlink_errno_bridge=0 \
        || echo "hide lkm load failed"
    else
      echo "hide lkm load denied: SukiSU loader unavailable"
    fi
  fi
  if grep -q '^pathguard_hide1 ' /proc/modules 2>/dev/null \
    && [ -e /sys/module/pathguard_hide1 ] && [ -e /dev/pathguard_hide1 ]; then
    echo "hide lkm live: fixed-device profile matched"
  else
    echo "hide lkm inactive: module or control device is not live"
  fi
}

load_hide_lkm

if [ ! -x "$DAEMON" ]; then
  echo "pathguardd is not installed; module remains inactive"
  exit 0
fi

if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null; then
  exit 0
fi

rm -f "$PIDFILE"
"$DAEMON" --module-dir "$MODDIR" >>"$RUNDIR/daemon.log" 2>&1 &
echo $! > "$PIDFILE"
