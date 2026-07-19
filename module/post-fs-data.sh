#!/system/bin/sh

MODDIR=${0%/*}
mkdir -p "$MODDIR/run"

if [ -x "$MODDIR/bin/pathguardd" ]; then
  "$MODDIR/bin/pathguardd" --module-dir "$MODDIR" --compile \
    >"$MODDIR/run/compile.log" 2>&1
fi
