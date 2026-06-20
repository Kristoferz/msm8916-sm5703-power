// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal SiliconMitus SM5703 fuel-gauge driver for Samsung Galaxy J5 2015.
 *
 * This exposes read-only power_supply properties sufficient for postmarketOS.
 * The current conversion is experimental; use the sign for charge/discharge
 * detection, but verify scaling before relying on absolute current values.
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

#define SM5703_REG_STATUS		0x04
#define SM5703_REG_SOC			0x05
#define SM5703_REG_OCV			0x06
#define SM5703_REG_VOLTAGE		0x07
#define SM5703_REG_CURRENT		0x08
#define SM5703_REG_TEMPERATURE		0x09

struct sm5703_fg {
	struct i2c_client *client;
	struct power_supply *psy;
	struct power_supply_desc desc;
};

static int sm5703_fg_read16(struct sm5703_fg *fg, u8 reg)
{
	struct i2c_client *client = fg->client;
	u8 tx = reg;
	u8 rx[2] = { 0, 0 };
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &tx,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 2,
			.buf = rx,
		},
	};
	int ret;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msg))
		return -EIO;

	/* The vendor driver treats these as little-endian 16-bit registers. */
	return rx[0] | (rx[1] << 8);
}

static int sm5703_fg_get_voltage_uV(struct sm5703_fg *fg)
{
	int raw;

	raw = sm5703_fg_read16(fg, SM5703_REG_VOLTAGE);
	if (raw < 0)
		return raw;

	/* raw * 1000 / 256 gives mV; expose uV. */
	return DIV_ROUND_CLOSEST(raw * 1000000LL, 256);
}

static int sm5703_fg_get_soc(struct sm5703_fg *fg)
{
	int raw, soc;

	raw = sm5703_fg_read16(fg, SM5703_REG_SOC);
	if (raw < 0)
		return raw;

	soc = raw >> 8;
	return clamp(soc, 0, 100);
}

static int sm5703_fg_get_temperature_decic(struct sm5703_fg *fg)
{
	int raw, temp;

	raw = sm5703_fg_read16(fg, SM5703_REG_TEMPERATURE);
	if (raw < 0)
		return raw;

	/* Approximate signed 8.8 fixed-point Celsius -> deci-Celsius. */
	temp = sign_extend32(raw, 15);
	return DIV_ROUND_CLOSEST(temp * 10, 256);
}

static int sm5703_fg_get_current_uA(struct sm5703_fg *fg, int *uA)
{
	int raw, cur;

	raw = sm5703_fg_read16(fg, SM5703_REG_CURRENT);
	if (raw < 0)
		return raw;

	/* Experimental signed conversion. Verify with known load before relying on absolute value. */
	cur = sign_extend32(raw, 15);
	*uA = DIV_ROUND_CLOSEST(cur * 1000000LL, 2048);
	return 0;
}

static enum power_supply_property sm5703_fg_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
};

static int sm5703_fg_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct sm5703_fg *fg = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
		return 0;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		return 0;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		return 0;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = sm5703_fg_get_soc(fg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sm5703_fg_get_voltage_uV(fg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = sm5703_fg_get_current_uA(fg, &val->intval);
		if (ret < 0)
			return ret;
		return 0;
	case POWER_SUPPLY_PROP_TEMP:
		ret = sm5703_fg_get_temperature_decic(fg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	default:
		return -EINVAL;
	}
}

static int sm5703_fg_probe(struct i2c_client *client)
{
	struct sm5703_fg *fg;
	struct power_supply_config psy_cfg = {};

	fg = devm_kzalloc(&client->dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;

	fg->client = client;
	i2c_set_clientdata(client, fg);

	fg->desc.name = "sm5703-fuelgauge";
	fg->desc.type = POWER_SUPPLY_TYPE_BATTERY;
	fg->desc.properties = sm5703_fg_props;
	fg->desc.num_properties = ARRAY_SIZE(sm5703_fg_props);
	fg->desc.get_property = sm5703_fg_get_property;

	psy_cfg.drv_data = fg;
	psy_cfg.of_node = client->dev.of_node;

	fg->psy = devm_power_supply_register(&client->dev, &fg->desc, &psy_cfg);
	if (IS_ERR(fg->psy))
		return dev_err_probe(&client->dev, PTR_ERR(fg->psy),
				     "failed to register power supply\n");

	dev_info(&client->dev, "SM5703 fuel gauge registered\n");
	return 0;
}

static const struct of_device_id sm5703_fg_of_match[] = {
	{ .compatible = "sm5703-fuelgauge,i2c" },
	{ .compatible = "siliconmitus,sm5703-fuelgauge" },
	{ }
};
MODULE_DEVICE_TABLE(of, sm5703_fg_of_match);

static const struct i2c_device_id sm5703_fg_id[] = {
	{ "sm5703-fuelgauge" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sm5703_fg_id);

static struct i2c_driver sm5703_fg_driver = {
	.driver = {
		.name = "sm5703-fuelgauge",
		.of_match_table = sm5703_fg_of_match,
	},
	.probe = sm5703_fg_probe,
	.id_table = sm5703_fg_id,
};
module_i2c_driver(sm5703_fg_driver);

MODULE_DESCRIPTION("SM5703 fuel gauge driver");
MODULE_AUTHOR("postmarketOS J5 porting experiment");
MODULE_LICENSE("GPL");
