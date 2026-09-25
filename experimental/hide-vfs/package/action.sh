#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
echo "boot profile is validated automatically; product_state remains unsupported"
cat "$RUNDIR/boot-state" 2>/dev/null || true
ADMISSION_SOURCE=/sdcard/Download/pathguard-hide1-admission.json
if [ -f "$ADMISSION_SOURCE" ]; then
  mkdir -p "$RUNDIR"
  chmod 0700 "$RUNDIR"
  if cp "$ADMISSION_SOURCE" "$RUNDIR/admission.json.tmp" \
      && chmod 0600 "$RUNDIR/admission.json.tmp" \
      && mv -f "$RUNDIR/admission.json.tmp" "$RUNDIR/admission.json"; then
    echo "admission evidence imported; daemon validates it against current boot"
  else
    rm -f "$RUNDIR/admission.json.tmp"
    echo "admission evidence import failed"
  fi
fi
exec "$MODDIR/bin/hide1ctl" status
