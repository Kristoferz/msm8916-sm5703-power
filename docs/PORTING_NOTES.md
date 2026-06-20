# Porting notes

## Tested device

- Samsung Galaxy J5 2015 SM-J500FN (`j5nlte`)
- SoC: Qualcomm MSM8916 / Snapdragon 410
- postmarketOS edge, Linux `6.12.1-msm8916`
- Boot: lk2nd + extlinux

## Runtime DT path

The running boot setup used `/boot/extlinux/extlinux.conf` with:

```text
kernel /vmlinuz
fdt /msm8916-samsung-j5.dtb
initrd /initramfs
```

Therefore, updating only the kernel image is not enough when changing device tree. The updated `msm8916-samsung-j5.dtb` must be copied to `/boot/msm8916-samsung-j5.dtb` on the phone.

## I2C devices observed after adding DT nodes

```text
2-0071  sm5703-fuelgauge compatible sm5703-fuelgauge,i2c
3-0049  sm5703 charger/MFD compatible siliconmitus,sm5703mfd
```

## Charger online detection

Empirical register reads showed that `STATUS5` did not change between cable connected and disconnected, while `STATUS2` bit 3 did. The charger driver therefore reports `online` using:

```c
SM5703_STATUS2 & BIT(3)
```

## Charge control

The vendor driver enables charging by setting operation mode to `SM5703_OPERATION_MODE_CHARGING_ON` and drives `nCHG` low. It disables charging by driving `nCHG` high and can set operation mode to a non-charging mode. The minimal mainline driver uses `SM5703_CNTL` operation mode and optionally a `charge-enable-gpios` line when present.

## Userspace store mode

The vendor Samsung battery driver contains a store-mode policy with hysteresis. This repository implements the same idea in userspace with configurable thresholds. The default is 40-60%.

