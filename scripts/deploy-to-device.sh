#!/bin/sh
set -eu

PHONE_IP=${1:-192.168.1.21}
PMOS_ROOTFS=${PMOS_ROOTFS:-"$HOME/.local/var/pmbootstrap/chroot_rootfs_samsung-j5"}
PKG=$(ls -t "$HOME"/.local/var/pmbootstrap/packages/edge/aarch64/linux-postmarketos-qcom-msm8916-*.apk | head -n1)
MODDIR="$PMOS_ROOTFS/lib/modules/6.12.1-msm8916/kernel/drivers/power/supply"
DTB="$PMOS_ROOTFS/boot/msm8916-samsung-j5.dtb"

pmbootstrap chroot -r -- apk add --allow-untrusted --force-overwrite "/mnt/pmbootstrap/packages/edge/aarch64/$(basename "$PKG")"

ls -lh "$MODDIR"/sm5703_fuelgauge.ko* "$MODDIR"/sm5703_charger.ko* "$DTB"
scp "$MODDIR"/sm5703_fuelgauge.ko* "$MODDIR"/sm5703_charger.ko* "$DTB" "user@$PHONE_IP:/tmp/"
scp "$(dirname "$0")/install-openrc-service.sh" "$(dirname "$0")/test-charge-control.sh" "user@$PHONE_IP:/tmp/"

cat <<EOF2
Copied files to $PHONE_IP:/tmp.

On the phone as root, run:
  MODDIR=/lib/modules/6.12.1-msm8916/kernel/drivers/power/supply; mkdir -p "\$MODDIR"; rmmod sm5703_charger 2>/dev/null || true; rmmod sm5703_fuelgauge 2>/dev/null || true; cp -f /tmp/sm5703_fuelgauge.ko* /tmp/sm5703_charger.ko* "\$MODDIR"/; depmod -a; cp /boot/msm8916-samsung-j5.dtb /boot/msm8916-samsung-j5.dtb.bak.\$(date +%s); cp -f /tmp/msm8916-samsung-j5.dtb /boot/msm8916-samsung-j5.dtb; sync; reboot
EOF2
