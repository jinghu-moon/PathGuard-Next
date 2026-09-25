#!/system/bin/sh

MODDIR=${0%/*}
RUNDIR="$MODDIR/run"
mkdir -p "$RUNDIR"
PROFILE="$MODDIR/config/hide1_device_profile.json"
{
  echo "service=observed"
  echo "automatic_load=profile-gated"
  echo "automatic_enable=daemon-controlled"
} >> "$RUNDIR/service.log"

profile_value() {
  sed -n 's/^[[:space:]]*"'"$1"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$PROFILE" | head -n 1
}

EXPECTED_DEVICE=$(profile_value device)
EXPECTED_ARCH=$(profile_value arch)
EXPECTED_RELEASE=$(profile_value kernel_release)
EXPECTED_FINGERPRINT=$(profile_value fingerprint)
EXPECTED_KMI=$(profile_value kmi)
EXPECTED_KO_SHA256=$(profile_value module_sha256)
ACTUAL_SHA256=$(sha256sum "$MODDIR/bin/pathguard_hide1.ko" 2>/dev/null | awk '{print $1}')
ACTUAL_ARCH=$(getprop ro.product.cpu.abi 2>/dev/null)
case "$ACTUAL_ARCH" in arm64|arm64-v8a) ACTUAL_ARCH=aarch64 ;; esac
if [ ! -s "$PROFILE" ] || [ -z "$EXPECTED_KO_SHA256" ] || [ -z "$EXPECTED_KMI" ] \
  || [ "$(getprop ro.product.device 2>/dev/null)" != "$EXPECTED_DEVICE" ] \
  || [ "$ACTUAL_ARCH" != "$EXPECTED_ARCH" ] \
  || [ "$(uname -r 2>/dev/null)" != "$EXPECTED_RELEASE" ] \
  || [ "$(getprop ro.build.fingerprint 2>/dev/null)" != "$EXPECTED_FINGERPRINT" ] \
  || [ "$ACTUAL_SHA256" != "$EXPECTED_KO_SHA256" ]; then
  echo "automatic load denied: device profile mismatch" >> "$RUNDIR/service.log"
  exit 0
fi

if ! grep -q '^pathguard_hide1 ' /proc/modules 2>/dev/null; then
  if [ -x /data/adb/ksu/bin/ksud ]; then
    KSU=/data/adb/ksu/bin/ksud
  elif [ -x /data/adb/ksud ]; then
    KSU=/data/adb/ksud
  elif command -v ksud >/dev/null 2>&1; then
    KSU=$(command -v ksud)
  else
    echo "automatic load denied: SukiSU loader unavailable" >> "$RUNDIR/service.log"
    exit 0
  fi
  if ! "$KSU" insmod "$MODDIR/bin/pathguard_hide1.ko" \
      shadow_mode=0 diagnostic_probes=0 symlink_errno_bridge=0 \
      >> "$RUNDIR/service.log" 2>&1; then
    echo "automatic load failed" >> "$RUNDIR/service.log"
    exit 0
  fi
fi
if ! grep -q '^pathguard_hide1 ' /proc/modules 2>/dev/null \
  || [ ! -e /sys/module/pathguard_hide1 ] \
  || [ ! -e /dev/pathguard_hide1 ]; then
  echo "automatic load denied: module or control device is not live" >> "$RUNDIR/service.log"
  exit 0
fi
echo "automatic load accepted: fixed device profile matched" >> "$RUNDIR/service.log"

DAEMON="$MODDIR/bin/pathguardd"
PIDFILE="$RUNDIR/pathguardd.pid"
if [ ! -x "$DAEMON" ]; then
  echo "pathguardd is not installed; daemon disabled" >> "$RUNDIR/service.log"
  exit 0
fi
if [ -f "$PIDFILE" ]; then
  OLD_PID=$(cat "$PIDFILE" 2>/dev/null)
  case "$OLD_PID" in
    ''|*[!0-9]*) OLD_PID= ;;
  esac
  if [ -n "$OLD_PID" ] && kill -0 "$OLD_PID" 2>/dev/null; then
    OLD_CMDLINE=$(tr '\000' ' ' < "/proc/$OLD_PID/cmdline" 2>/dev/null)
    case "$OLD_CMDLINE" in
      *"$MODDIR/bin/pathguardd --module-dir $MODDIR"*) exit 0 ;;
    esac
  fi
fi
rm -f "$PIDFILE"
"$DAEMON" --module-dir "$MODDIR" >>"$RUNDIR/daemon.log" 2>&1 &
echo $! > "$PIDFILE"
