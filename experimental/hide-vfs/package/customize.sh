#!/system/bin/sh

ui_print "- PathGuard Hide 1.0 Lab"
ui_print "- Experimental fixed-device boot admission"

case "$ARCH" in
  arm64) ;;
  *) abort "! Unsupported architecture: $ARCH (arm64 required)" ;;
esac

EXPECTED_RELEASE="6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"
ACTUAL_RELEASE="$(uname -r 2>/dev/null || true)"
if [ "$ACTUAL_RELEASE" != "$EXPECTED_RELEASE" ]; then
  abort "! Unsupported kernel release: $ACTUAL_RELEASE"
fi

set_perm "$MODPATH/post-fs-data.sh" 0 0 0755
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/action.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
set_perm "$MODPATH/bin/hide1ctl" 0 0 0755
set_perm "$MODPATH/bin/pathguard_hide1.ko" 0 0 0644
set_perm "$MODPATH/bin/hide1_control" 0 0 0755
set_perm "$MODPATH/bin/pathguardd" 0 0 0755
set_perm "$MODPATH/config/rules.toml" 0 0 0644
set_perm "$MODPATH/config/hide1_device_profile.json" 0 0 0644
ui_print "- Kernel release accepted; profile and module hash are revalidated at boot"
ui_print "- LKM loads only on exact profile match; daemon applies configured Hide rules"
