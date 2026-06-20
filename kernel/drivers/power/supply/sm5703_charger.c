// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal SiliconMitus SM5703 charger driver for Samsung Galaxy J5 2015.
 *
 * This driver is intentionally small. It exposes enough power_supply state
 * for postmarketOS and provides a writable charge_control_limit property:
 *
 *   charge_control_limit = 0  charging enabled
 *   charge_control_limit = 1  charging disabled
 *
 * Validated experimentally on Samsung SM-J500FN / MSM8916.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

#define SM5703_STATUS2		0x09
#define SM5703_STATUS3		0x0a
#define SM5703_CNTL		0x0c
#define SM5703_VBUSCNTL		0x0d
#define SM5703_CHGCNTL2		0x0f
#define SM5703_CHGCNTL3		0x10
#define SM5703_STATUS5		0x16

#define SM5703_STATUS2_VBUSOK	BIT(3)
#define SM5703_STATUS2_BATPRES	BIT(4)

#define SM5703_OPERATION_MODE_MASK		0x07
#define SM5703_OPERATION_MODE_SUSPEND		0x00
#define SM5703_OPERATION_MODE_CHARGING_OFF	0x04
#define SM5703_OPERATION_MODE_CHARGING_ON	0x05

struct sm5703_charger {
	struct i2c_client *client;
	struct power_supply *psy;
	struct power_supply_desc desc;
	struct gpio_desc *chg_en_gpiod;
	bool charge_disabled;
};

static int sm5703_chg_read(struct sm5703_charger *chg, u8 reg)
{
	struct i2c_client *client = chg->client;
	u8 tx = reg;
	u8 rx = 0;
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
			.len = 1,
			.buf = &rx,
		},
	};
	int ret;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msg))
		return -EIO;

	return rx;
}

static int sm5703_chg_write(struct sm5703_charger *chg, u8 reg, u8 val)
{
	struct i2c_client *client = chg->client;
	u8 tx[2] = { reg, val };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = sizeof(tx),
		.buf = tx,
	};
	int ret;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	return 0;
}

static int sm5703_chg_update_bits(struct sm5703_charger *chg, u8 reg,
					  u8 mask, u8 val)
{
	int old;
	u8 new;

	old = sm5703_chg_read(chg, reg);
	if (old < 0)
		return old;

	new = (old & ~mask) | (val & mask);
	if (new == old)
		return 0;

	return sm5703_chg_write(chg, reg, new);
}

static int sm5703_chg_online(struct sm5703_charger *chg)
{
	int status2;

	status2 = sm5703_chg_read(chg, SM5703_STATUS2);
	if (status2 < 0)
		return status2;

	return !!(status2 & SM5703_STATUS2_VBUSOK);
}

static int sm5703_chg_present(struct sm5703_charger *chg)
{
	int status2;

	status2 = sm5703_chg_read(chg, SM5703_STATUS2);
	if (status2 < 0)
		return status2;

	/* Vendor code treats this bit as active-low battery absent on this board. */
	return !(status2 & SM5703_STATUS2_BATPRES);
}

static int sm5703_chg_status(struct sm5703_charger *chg)
{
	int online, status3;

	online = sm5703_chg_online(chg);
	if (online < 0)
		return online;

	if (!online)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	if (chg->charge_disabled)
		return POWER_SUPPLY_STATUS_NOT_CHARGING;

	status3 = sm5703_chg_read(chg, SM5703_STATUS3);
	if (status3 < 0)
		return status3;

	/* On tested J5, STATUS3=0x03 while connected and full/top-off. */
	if (status3 & 0x03)
		return POWER_SUPPLY_STATUS_FULL;

	return POWER_SUPPLY_STATUS_CHARGING;
}

static int sm5703_chg_input_current_uA(struct sm5703_charger *chg)
{
	int reg;

	reg = sm5703_chg_read(chg, SM5703_VBUSCNTL);
	if (reg < 0)
		return reg;

	reg &= 0x3f;
	return (100 + reg * 50) * 1000;
}

static int sm5703_chg_fast_current_uA(struct sm5703_charger *chg)
{
	int reg;

	reg = sm5703_chg_read(chg, SM5703_CHGCNTL2);
	if (reg < 0)
		return reg;

	reg &= 0x3f;
	return (100 + reg * 50) * 1000;
}

static int sm5703_chg_voltage_max_uV(struct sm5703_charger *chg)
{
	int reg;

	reg = sm5703_chg_read(chg, SM5703_CHGCNTL3);
	if (reg < 0)
		return reg;

	reg &= 0x3f;
	return (4120 + reg * 10) * 1000;
}

static int sm5703_chg_set_charge_enabled(struct sm5703_charger *chg, bool enable)
{
	int ret;

	if (enable) {
		ret = sm5703_chg_update_bits(chg, SM5703_CNTL,
					     SM5703_OPERATION_MODE_MASK,
					     SM5703_OPERATION_MODE_CHARGING_ON);
		if (ret < 0)
			return ret;

		if (chg->chg_en_gpiod)
			gpiod_set_value_cansleep(chg->chg_en_gpiod, 1);

		chg->charge_disabled = false;
	} else {
		if (chg->chg_en_gpiod)
			gpiod_set_value_cansleep(chg->chg_en_gpiod, 0);

		ret = sm5703_chg_update_bits(chg, SM5703_CNTL,
					     SM5703_OPERATION_MODE_MASK,
					     SM5703_OPERATION_MODE_CHARGING_OFF);
		if (ret < 0)
			return ret;

		chg->charge_disabled = true;
	}

	power_supply_changed(chg->psy);
	return 0;
}

static enum power_supply_property sm5703_chg_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
};

static int sm5703_chg_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct sm5703_charger *chg = power_supply_get_drvdata(psy);
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = sm5703_chg_online(chg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_STATUS:
		ret = sm5703_chg_status(chg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		return 0;
	case POWER_SUPPLY_PROP_PRESENT:
		ret = sm5703_chg_present(chg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = sm5703_chg_input_current_uA(chg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		ret = sm5703_chg_fast_current_uA(chg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		ret = sm5703_chg_voltage_max_uV(chg);
		if (ret < 0)
			return ret;
		val->intval = ret;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = chg->charge_disabled ? 1 : 0;
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		val->intval = 1;
		return 0;
	default:
		return -EINVAL;
	}
}

static int sm5703_chg_set_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	struct sm5703_charger *chg = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return sm5703_chg_set_charge_enabled(chg, val->intval ? false : true);
	default:
		return -EINVAL;
	}
}

static int sm5703_chg_property_is_writeable(struct power_supply *psy,
					    enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		return 0;
	}
}

static int sm5703_chg_probe(struct i2c_client *client)
{
	struct sm5703_charger *chg;
	struct power_supply_config psy_cfg = {};
	int status2, status3, vbuscntl, chgcntl2, chgcntl3;

	chg = devm_kzalloc(&client->dev, sizeof(*chg), GFP_KERNEL);
	if (!chg)
		return -ENOMEM;

	chg->client = client;
	i2c_set_clientdata(client, chg);

	chg->chg_en_gpiod = devm_gpiod_get_optional(&client->dev,
						    "charge-enable", GPIOD_OUT_HIGH);
	if (IS_ERR(chg->chg_en_gpiod))
		return PTR_ERR(chg->chg_en_gpiod);

	chg->charge_disabled = false;

	chg->desc.name = "sm5703-charger";
	chg->desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	chg->desc.properties = sm5703_chg_props;
	chg->desc.num_properties = ARRAY_SIZE(sm5703_chg_props);
	chg->desc.get_property = sm5703_chg_get_property;
	chg->desc.set_property = sm5703_chg_set_property;
	chg->desc.property_is_writeable = sm5703_chg_property_is_writeable;

	psy_cfg.drv_data = chg;
	psy_cfg.of_node = client->dev.of_node;

	chg->psy = devm_power_supply_register(&client->dev, &chg->desc, &psy_cfg);
	if (IS_ERR(chg->psy))
		return dev_err_probe(&client->dev, PTR_ERR(chg->psy),
				     "failed to register power supply\n");

	status2 = sm5703_chg_read(chg, SM5703_STATUS2);
	status3 = sm5703_chg_read(chg, SM5703_STATUS3);
	vbuscntl = sm5703_chg_read(chg, SM5703_VBUSCNTL);
	chgcntl2 = sm5703_chg_read(chg, SM5703_CHGCNTL2);
	chgcntl3 = sm5703_chg_read(chg, SM5703_CHGCNTL3);

	dev_info(&client->dev,
		 "SM5703 charger registered: status2=0x%02x status3=0x%02x vbuscntl=0x%02x chgcntl2=0x%02x chgcntl3=0x%02x\n",
		 status2, status3, vbuscntl, chgcntl2, chgcntl3);

	return 0;
}

static const struct of_device_id sm5703_chg_of_match[] = {
	{ .compatible = "siliconmitus,sm5703mfd" },
	{ .compatible = "siliconmitus,sm5703-charger" },
	{ }
};
MODULE_DEVICE_TABLE(of, sm5703_chg_of_match);

static const struct i2c_device_id sm5703_chg_id[] = {
	{ "sm5703-charger" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sm5703_chg_id);

static struct i2c_driver sm5703_chg_driver = {
	.driver = {
		.name = "sm5703-charger",
		.of_match_table = sm5703_chg_of_match,
	},
	.probe = sm5703_chg_probe,
	.id_table = sm5703_chg_id,
};
module_i2c_driver(sm5703_chg_driver);

MODULE_DESCRIPTION("SM5703 charger driver with charge control limit");
MODULE_AUTHOR("postmarketOS J5 porting experiment");
MODULE_LICENSE("GPL");
