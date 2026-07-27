#!/system/bin/sh

MODDIR=${0%/*}
mkdir -p "$MODDIR/run"
mkdir -p "$MODDIR/run/deny-anchor"
chown 0:0 "$MODDIR/run/deny-anchor"
chmod 0000 "$MODDIR/run/deny-anchor"

if [ -x "$MODDIR/bin/pathguardd" ]; then
  "$MODDIR/bin/pathguardd" --module-dir "$MODDIR" --compile \
    >"$MODDIR/run/compile.log" 2>&1
fi
