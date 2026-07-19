#!/system/bin/sh

# KernelSU boot-completed hook. The service lifecycle owns daemon startup.
MODDIR=${0%/*}
PIDFILE="$MODDIR/run/pathguardd.pid"

[ -f "$PIDFILE" ] || exit 0
kill -0 "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null || rm -f "$PIDFILE"
