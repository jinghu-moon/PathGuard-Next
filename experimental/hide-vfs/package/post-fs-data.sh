#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
mkdir -p "$RUNDIR"
chmod 0700 "$RUNDIR"
{
  echo "phase=post-fs-data"
  echo "kernel=$(uname -r 2>/dev/null || echo unknown)"
  echo "fingerprint=$(getprop ro.build.fingerprint 2>/dev/null || echo unknown)"
  echo "module_sha256=$(sha256sum "$MODDIR/bin/pathguard_hide1.ko" 2>/dev/null | awk '{print $1}')"
  echo "automatic_load=no"
  echo "automatic_enable=no"
} > "$RUNDIR/boot-state"
