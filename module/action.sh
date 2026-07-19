#!/system/bin/sh

MODDIR=${0%/*}
echo "PathGuard Next"
echo "module: $MODDIR"
echo "config: $MODDIR/config/rules.ini"
echo "status:"
if [ -f "$MODDIR/run/pathguardd.pid" ]; then
  cat "$MODDIR/run/pathguardd.pid"
else
  echo "inactive"
fi
