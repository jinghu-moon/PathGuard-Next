#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
mkdir -p "$RUNDIR"
{
  echo "service=observed"
  echo "automatic_load=no"
  echo "automatic_enable=no"
  echo "hint=use $MODDIR/bin/hide1ctl load"
} >> "$RUNDIR/service.log"
