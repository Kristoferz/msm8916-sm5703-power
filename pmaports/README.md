# pmaports integration notes

In the tested setup, the kernel package directory was:

```text
~/.local/var/pmbootstrap/cache_git/pmaports/device/testing/linux-postmarketos-qcom-msm8916/
```

The kernel source checkout was:

```text
~/j5-power-port/linux-msm8916
```

Recommended flow:

1. Copy driver sources into the kernel tree with `scripts/apply-to-kernel-tree.sh`.
2. Enable:

```text
CONFIG_BATTERY_SM5703_FUELGAUGE=m
CONFIG_CHARGER_SM5703_READONLY=m
```

3. Merge `kernel/dts/msm8916-samsung-j5-sm5703.dtsi` into the J5 DTS.
4. Generate pmaports patch files from the kernel tree:

```sh
cd ~/j5-power-port/linux-msm8916
PKGDIR=~/.local/var/pmbootstrap/cache_git/pmaports/device/testing/linux-postmarketos-qcom-msm8916

git add -N drivers/power/supply/sm5703_fuelgauge.c drivers/power/supply/sm5703_charger.c

git diff -- arch/arm64/boot/dts/qcom/msm8916-samsung-j5.dts > "$PKGDIR/0001-arm64-dts-qcom-msm8916-samsung-j5-add-sm5703-nodes.patch"
git diff -- drivers/power/supply/Kconfig drivers/power/supply/Makefile drivers/power/supply/sm5703_fuelgauge.c > "$PKGDIR/0002-power-supply-add-sm5703-fuelgauge-readonly.patch"
git diff -- drivers/power/supply/Kconfig drivers/power/supply/Makefile drivers/power/supply/sm5703_charger.c > "$PKGDIR/0003-power-supply-add-sm5703-charger-charge-control.patch"
```

5. Add patch filenames to `APKBUILD` if not already listed, then run:

```sh
pmbootstrap checksum linux-postmarketos-qcom-msm8916
pmbootstrap build --force linux-postmarketos-qcom-msm8916
```
