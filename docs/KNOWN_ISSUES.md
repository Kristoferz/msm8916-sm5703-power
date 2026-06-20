# Known issues

## Fuel-gauge current scaling

The fuel-gauge current sign is useful for detecting charge versus discharge, but the absolute value may still be incorrectly scaled in discharge mode. During testing, negative current appeared around `-127945312`, which is not physically plausible as a direct microampere value.

The 40-60% store-mode service does not depend on current magnitude; it uses only:

```text
/sys/class/power_supply/sm5703-fuelgauge/capacity
/sys/class/power_supply/sm5703-charger/charge_control_limit
```

## Not true battery bypass

`charge_control_limit=1` stops charging, but it does not bypass the battery. The phone continues to draw from the battery while charging is disabled. This was observed as negative fuel-gauge current with USB still connected.

## GPIO charge-enable line

The driver uses `SM5703_CNTL` operation mode and optionally `charge-enable-gpios`. On the tested device, charge stop/resume worked with the register path. Verify whether your DTB exposes `charge-enable-gpios` at the node that the driver binds to.

## Upstream status

The code is not upstream-ready. Missing pieces include:

- full DT binding documentation;
- robust current/temperature calibration;
- proper MFD split if desired;
- broader variant testing;
- suspend/resume validation;
- error recovery if I2C reads fail repeatedly.
