#!/system/bin/sh

MODDIR=${0%/*}
echo "PathGuard-Next"
echo "module: $MODDIR"
echo "config: $MODDIR/config/rules.toml"
echo "status:"
if [ -f "$MODDIR/run/pathguardd.pid" ]; then
  cat "$MODDIR/run/pathguardd.pid"
else
  echo "inactive"
fi
if [ -x "$MODDIR/bin/hide1ctl" ]; then
  echo "hide:"
  "$MODDIR/bin/hide1ctl" status 2>/dev/null || true
fi
