#!/system/bin/sh

MODDIR=${0%/*}
PIDFILE="$MODDIR/run/pathguardd.pid"

if [ -f "$PIDFILE" ]; then
  kill "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null
  rm -f "$PIDFILE"
fi

if [ -x "$MODDIR/bin/hide1ctl" ]; then
  "$MODDIR/bin/hide1ctl" disable >> "$MODDIR/run/uninstall.log" 2>&1 || true
  "$MODDIR/bin/hide1ctl" clear >> "$MODDIR/run/uninstall.log" 2>&1 || true
fi
if grep -q '^pathguard_hide1 ' /proc/modules 2>/dev/null; then
  rmmod pathguard_hide1 >> "$MODDIR/run/uninstall.log" 2>&1 || true
fi
