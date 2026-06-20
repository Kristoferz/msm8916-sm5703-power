#!/bin/sh
set -eu

if [ $# -ne 1 ]; then
	echo "Usage: $0 /path/to/linux-msm8916" >&2
	exit 2
fi

KDIR=$1
REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ ! -d "$KDIR/drivers/power/supply" ]; then
	echo "Not a Linux kernel tree: $KDIR" >&2
	exit 1
fi

cp "$REPO_DIR/kernel/drivers/power/supply/sm5703_fuelgauge.c" "$KDIR/drivers/power/supply/"
cp "$REPO_DIR/kernel/drivers/power/supply/sm5703_charger.c" "$KDIR/drivers/power/supply/"

KCONFIG="$KDIR/drivers/power/supply/Kconfig"
MAKEFILE="$KDIR/drivers/power/supply/Makefile"

if ! grep -q 'BATTERY_SM5703_FUELGAUGE' "$KCONFIG"; then
	cat >> "$KCONFIG" <<'KCONFIG_EOF'

config BATTERY_SM5703_FUELGAUGE
	tristate "SiliconMitus SM5703 fuel gauge"
	depends on I2C
	help
	  Minimal SM5703 fuel gauge driver used by the Samsung Galaxy J5 2015
	  postmarketOS port.

config CHARGER_SM5703_READONLY
	tristate "SiliconMitus SM5703 charger"
	depends on I2C
	help
	  Minimal SM5703 charger driver with writable charge_control_limit.
KCONFIG_EOF
fi

if ! grep -q 'sm5703_fuelgauge.o' "$MAKEFILE"; then
	cat >> "$MAKEFILE" <<'MAKE_EOF'
obj-$(CONFIG_BATTERY_SM5703_FUELGAUGE) += sm5703_fuelgauge.o
obj-$(CONFIG_CHARGER_SM5703_READONLY) += sm5703_charger.o
MAKE_EOF
fi

printf '\nDriver files copied. Now enable these kernel config symbols:\n'
printf '  CONFIG_BATTERY_SM5703_FUELGAUGE=m\n'
printf '  CONFIG_CHARGER_SM5703_READONLY=m\n\n'
printf 'Then merge kernel/dts/msm8916-samsung-j5-sm5703.dtsi into your J5 DTS.\n'
