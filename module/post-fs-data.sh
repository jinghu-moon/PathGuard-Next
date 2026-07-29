#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
POLICY="$RUNDIR/policy.bin"
BOOTSTRAP="$RUNDIR/.policy.bin.bootstrap.$$"
CTL="$MODDIR/bin/pathguardctl"

mkdir -p "$MODDIR/run"
mkdir -p "$MODDIR/run/deny-anchor"
chown 0:0 "$MODDIR/run/deny-anchor"
chmod 0000 "$MODDIR/run/deny-anchor"

if [ -x "$CTL" ] && "$CTL" compile "$MODDIR/config/rules.toml" "$BOOTSTRAP" \
    >"$RUNDIR/compile.log" 2>&1; then
  chmod 0644 "$BOOTSTRAP"
  chown 0:0 "$BOOTSTRAP"
  mv -f "$BOOTSTRAP" "$POLICY"
else
  rm -f "$BOOTSTRAP"
fi
