#!/system/bin/sh

MODDIR=${0%/*}
exec "$MODDIR/bin/hide1ctl" status
