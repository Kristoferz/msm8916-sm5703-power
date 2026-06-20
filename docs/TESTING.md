# Testing notes

## Baseline power-supply checks

```sh
lsmod | grep sm5703
ls -la /sys/class/power_supply
ls /sys/class/power_supply/sm5703-fuelgauge
ls /sys/class/power_supply/sm5703-charger
```

Expected supplies:

```text
sm5703-fuelgauge
sm5703-charger
```

## Read charger and fuel-gauge state

```sh
echo "=== charger ==="; for f in online status health present current_now current_max voltage_max charge_type charge_control_limit charge_control_limit_max; do echo -n "$f="; cat /sys/class/power_supply/sm5703-charger/$f 2>/dev/null; done; echo "=== fuelgauge ==="; for f in capacity voltage_now current_now temp present technology status; do echo -n "$f="; cat /sys/class/power_supply/sm5703-fuelgauge/$f 2>/dev/null; done
```

## Manual charge-control test

Run with USB cable connected:

```sh
echo "=== before ==="; for f in online status charge_control_limit charge_type; do echo -n "$f="; cat /sys/class/power_supply/sm5703-charger/$f; done; echo -n "fg_current="; cat /sys/class/power_supply/sm5703-fuelgauge/current_now; echo 1 > /sys/class/power_supply/sm5703-charger/charge_control_limit; sleep 5; echo "=== limit=1 ==="; for f in online status charge_control_limit charge_type; do echo -n "$f="; cat /sys/class/power_supply/sm5703-charger/$f; done; echo -n "fg_current="; cat /sys/class/power_supply/sm5703-fuelgauge/current_now; echo 0 > /sys/class/power_supply/sm5703-charger/charge_control_limit; sleep 5; echo "=== limit=0 ==="; for f in online status charge_control_limit charge_type; do echo -n "$f="; cat /sys/class/power_supply/sm5703-charger/$f; done; echo -n "fg_current="; cat /sys/class/power_supply/sm5703-fuelgauge/current_now
```

Validated output on the tested phone:

```text
=== before ===
online=1
status=Full
charge_control_limit=0
charge_type=Trickle
fg_current=257812
=== limit=1 ===
online=1
status=Not charging
charge_control_limit=1
charge_type=Trickle
fg_current=-127945312
=== limit=0 ===
online=1
status=Full
charge_control_limit=0
charge_type=Trickle
fg_current=250000
```

`online=1` while `status=Not charging` is expected: VBUS is still physically present, but charging is disabled.

## Store-mode service checks

```sh
/etc/init.d/sm5703-store-mode status
echo "cap=$(cat /sys/class/power_supply/sm5703-fuelgauge/capacity) limit=$(cat /sys/class/power_supply/sm5703-charger/charge_control_limit) current=$(cat /sys/class/power_supply/sm5703-fuelgauge/current_now) temp=$(cat /sys/class/power_supply/sm5703-fuelgauge/temp)"
```

With default 40-60% limits:

- `cap >= 60` should set `limit=1`.
- `cap <= 40` should set `limit=0`.
- Between 41 and 59, it should keep the previous state.

