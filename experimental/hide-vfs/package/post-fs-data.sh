#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
PROFILE="$MODDIR/config/hide1_device_profile.json"
mkdir -p "$RUNDIR"
chmod 0700 "$RUNDIR"

profile_value() {
  sed -n 's/^[[:space:]]*"'"$1"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$PROFILE" | head -n 1
}

DEVICE=$(getprop ro.product.device 2>/dev/null)
ARCH=$(getprop ro.product.cpu.abi 2>/dev/null)
case "$ARCH" in arm64|arm64-v8a) ARCH=aarch64 ;; esac
FINGERPRINT=$(getprop ro.build.fingerprint 2>/dev/null)
KERNEL=$(uname -r 2>/dev/null)
KMI=$(profile_value kmi)
MODULE_SHA256=$(sha256sum "$MODDIR/bin/pathguard_hide1.ko" 2>/dev/null | awk '{print $1}')
{
  echo "phase=post-fs-data"
  echo "device=${DEVICE:-unknown}"
  echo "arch=${ARCH:-unknown}"
  echo "kernel=${KERNEL:-unknown}"
  echo "kmi=${KMI:-unknown}"
  echo "boot_id=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null || echo unknown)"
  echo "fingerprint=${FINGERPRINT:-unknown}"
  echo "module_sha256=${MODULE_SHA256:-unknown}"
  echo "automatic_load=service-profile-gated"
  echo "automatic_enable=daemon-rules-and-profile-gated"
} > "$RUNDIR/boot-state"
chmod 0600 "$RUNDIR/boot-state"
