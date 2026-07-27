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
