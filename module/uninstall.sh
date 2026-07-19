#!/system/bin/sh

MODDIR=${0%/*}
PIDFILE="$MODDIR/run/pathguardd.pid"

if [ -f "$PIDFILE" ]; then
  kill "$(cat "$PIDFILE" 2>/dev/null)" 2>/dev/null
  rm -f "$PIDFILE"
fi
