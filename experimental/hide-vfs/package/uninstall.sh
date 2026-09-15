#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
KO_NAME="pathguard_hide1"

# Never attempt ENABLE or CLEAR during uninstall. Disable first so a future
# manual recovery can inspect the last state if the kernel refuses rmmod.
if grep -q "^$KO_NAME " /proc/modules 2>/dev/null; then
  "$MODDIR/bin/hide1ctl" disable >> "$RUNDIR/uninstall.log" 2>&1 || true
  "$MODDIR/bin/hide1ctl" clear >> "$RUNDIR/uninstall.log" 2>&1 || true
  rmmod "$KO_NAME" >> "$RUNDIR/uninstall.log" 2>&1 || true
fi
