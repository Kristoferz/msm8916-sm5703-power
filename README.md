# Samsung Galaxy J5 2015 SM5703 power-supply port for postmarketOS

Experimental mainline/postmarketOS power-supply work for the Samsung Galaxy J5 2015 (`samsung-j5`, `j5nlte`, SM-J500FN, MSM8916/Snapdragon 410).

This repository contains the code and notes needed to reproduce the work:

- SM5703 fuel-gauge read-only Linux power-supply driver.
- SM5703 charger Linux power-supply driver.
- Writable `charge_control_limit` support for stopping/resuming charging.
- Device-tree fragment for the SM5703 fuel gauge and charger I2C nodes.
- OpenRC userspace store-mode service that keeps the battery between 40% and 60%.
- pmbootstrap build/deploy helper scripts.

The tested result was:

```text
/sys/class/power_supply/sm5703-fuelgauge
/sys/class/power_supply/sm5703-charger
```

and manual charge control:

```sh
echo 1 > /sys/class/power_supply/sm5703-charger/charge_control_limit  # stop charging
echo 0 > /sys/class/power_supply/sm5703-charger/charge_control_limit  # resume charging
```

When charging was stopped with the USB cable connected, the charger reported `status=Not charging` and the fuel-gauge current became negative. When charging was resumed, the current became positive again.

## Repository layout

```text
kernel/drivers/power/supply/
  sm5703_fuelgauge.c        Minimal read-only fuel-gauge driver
  sm5703_charger.c          Minimal charger driver with charge_control_limit

kernel/dts/
  msm8916-samsung-j5-sm5703.dtsi

openrc/
  sm5703-store-mode.conf    /etc/conf.d file
  sm5703-store-mode         daemon script
  sm5703-store-mode.init    OpenRC init script

scripts/
  apply-to-kernel-tree.sh   Copy driver sources and edit Kconfig/Makefile
  install-openrc-service.sh Install 40-60% store-mode service on target
  build-pmbootstrap.sh      Rebuild the postmarketOS kernel package
  deploy-to-device.sh       Copy modules and DTB to a running phone over SSH
  test-charge-control.sh    Manual runtime tests

docs/
  TESTING.md
  PORTING_NOTES.md
  KNOWN_ISSUES.md
```

## Important status

This is not an upstream-ready driver. It is a practical experimental port that was validated on one SM-J500FN.

Known caveats:

- Fuel-gauge discharge-current scaling still needs calibration. The sign is useful, but the negative value may be off by a scale factor.
- The charger control is confirmed to stop/resume charging, but it is not true battery bypass.
- The device-tree insertion script is intentionally conservative. Always inspect the resulting DTS/DTB.
- The drivers are written for the Linux 6.12.1 msm8916 postmarketOS kernel tree used during testing.

## Quick start

On the PC, point `KERNEL_TREE` at the msm8916 kernel checkout:

```sh
export KERNEL_TREE="$HOME/j5-power-port/linux-msm8916"
export PMOS_KERNEL_PACKAGE="$HOME/.local/var/pmbootstrap/cache_git/pmaports/device/testing/linux-postmarketos-qcom-msm8916"
./scripts/apply-to-kernel-tree.sh "$KERNEL_TREE"
./scripts/build-pmbootstrap.sh "$PMOS_KERNEL_PACKAGE"
```

Then deploy to a running phone over Wi-Fi SSH:

```sh
./scripts/deploy-to-device.sh 192.168.1.21
```

On the phone, after reboot:

```sh
sudo su
./tmp/test-charge-control.sh
```

Install the 40-60% OpenRC store-mode service:

```sh
sudo su
sh /tmp/install-openrc-service.sh
```

## Runtime checks

```sh
lsmod | grep sm5703
ls /sys/class/power_supply/sm5703-fuelgauge
ls /sys/class/power_supply/sm5703-charger
```

Check state:

```sh
echo "cap=$(cat /sys/class/power_supply/sm5703-fuelgauge/capacity) limit=$(cat /sys/class/power_supply/sm5703-charger/charge_control_limit) current=$(cat /sys/class/power_supply/sm5703-fuelgauge/current_now) temp=$(cat /sys/class/power_supply/sm5703-fuelgauge/temp)"
```

## License

Kernel driver files are GPL-2.0-only, matching Linux kernel module conventions. Helper scripts and documentation are MIT unless otherwise noted.
