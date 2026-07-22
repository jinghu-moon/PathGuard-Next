#!/system/bin/sh

SKIPUNZIP=0

ui_print "- Installing PathGuard Next"

if [ -z "$API" ]; then
  API="$(getprop ro.build.version.sdk)"
fi

if [ -z "$API" ] || [ "$API" -lt 31 ]; then
  abort "! Android 12 (API 31) or newer is required"
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/config" 0 0 0755 0644
mkdir -p "$MODPATH/run"
set_perm_recursive "$MODPATH/run" 0 0 0755 0644

for script in post-fs-data.sh service.sh boot-completed.sh action.sh uninstall.sh; do
  [ -f "$MODPATH/$script" ] && set_perm "$MODPATH/$script" 0 0 0755
done

case "$ARCH" in
  arm64) ABI=arm64-v8a ;;
  arm) ABI=armeabi-v7a ;;
  x64) ABI=x86_64 ;;
  x86) ABI=x86 ;;
  *) abort "! Unsupported ABI: $ARCH" ;;
esac

if [ "$IS64BIT" = true ]; then
  case "$ARCH" in
    arm64)
      [ -f "$MODPATH/zygisk/arm64-v8a.so" ] || abort "! Missing arm64 Zygisk library"
      [ -f "$MODPATH/zygisk/armeabi-v7a.so" ] || abort "! Missing arm32 Zygisk library"
      ;;
    x64)
      [ -f "$MODPATH/zygisk/x86_64.so" ] || abort "! Missing x64 Zygisk library"
      [ -f "$MODPATH/zygisk/x86.so" ] || abort "! Missing x86 Zygisk library"
      ;;
  esac
fi

for binary in pathguardd pathguardctl; do
  source="$MODPATH/bin/$ABI/$binary"
  [ -f "$source" ] || abort "! Missing $binary for $ABI"
  cp -f "$source" "$MODPATH/bin/$binary"
  set_perm "$MODPATH/bin/$binary" 0 0 0755
done
