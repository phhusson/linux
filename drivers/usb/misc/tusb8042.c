// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for TI TUSB8042 USB 3.1 SuperSpeed Hub Controller
 * Configuration via SMBus.
 *
 * Copyright (c) 2022 Freebox SAS
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#define DRIVER_NAME	"tusb8042"
#define DRIVER_DESC	"TI TUSB8042 USB 3.1 SuperSpeed Hub Controller"

#define TUSB8042_REG_VID		0x01
#define TUSB8042_REG_PID		0x03
#define TUSB8042_REG_SMBUS_STATUS	0xf8

struct tusb8042 {
	struct device *dev;
	struct i2c_client *i2c;
	struct gpio_desc *gpio_reset;
	struct regulator_bulk_data supplies[2];
	u16 vendor_id;
	u16 product_id;
};

static void tusb8042_reset(struct tusb8042 *hub)
{
	if (!hub->gpio_reset)
		return;

	i2c_lock_bus(hub->i2c->adapter, I2C_LOCK_SEGMENT);

	gpiod_set_value_cansleep(hub->gpio_reset, 1);
	usleep_range(3000, 3500);
	gpiod_set_value_cansleep(hub->gpio_reset, 0);

	/* wait for hub recovery/stabilization */
	usleep_range(1, 10);

	i2c_unlock_bus(hub->i2c->adapter, I2C_LOCK_SEGMENT);
}

static int tusb8042_connect(struct tusb8042 *hub)
{
	struct device *dev = hub->dev;
	int ret;

	tusb8042_reset(hub);

	ret = i2c_smbus_read_word_data(hub->i2c, TUSB8042_REG_VID);
	if (ret < 0) {
		dev_err(dev, "failed to read vendor id: %d\n", ret);
		return ret;
	}

	hub->vendor_id = ret;

	ret = i2c_smbus_read_word_data(hub->i2c, TUSB8042_REG_PID);
	if (ret < 0) {
		dev_err(dev, "failed to read product id: %d\n", ret);
		return ret;
	}

	hub->product_id = ret;

	ret = i2c_smbus_write_byte_data(hub->i2c,
					TUSB8042_REG_SMBUS_STATUS, 1);
	if (ret) {
		dev_err(dev, "failed to commit hub config: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id tusb8042_of_match[] = {
	{ .compatible = "ti,tusb8020" },
	{ .compatible = "ti,tusb8041" },
	{ }
};
MODULE_DEVICE_TABLE(of, tusb8042_of_match);

static void tusb8042_regulator_disable_action(void *data)
{
	struct tusb8042 *hub = data;

	regulator_bulk_disable(ARRAY_SIZE(hub->supplies), hub->supplies);
}

static int tusb8042_probe(struct tusb8042 *hub)
{
	struct device *dev = hub->dev;
	int ret;

	hub->supplies[0].supply = "vdd";
	hub->supplies[1].supply = "vdd33";
	ret = devm_regulator_bulk_get(dev, 2, hub->supplies);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev,
				       tusb8042_regulator_disable_action, hub);
	if (ret)
		return ret;

	ret = tusb8042_connect(hub);
	if (ret)
		return ret;

	dev_info(dev, "hub probed successfully, vid:%04x pid:%04x\n",
		 hub->vendor_id, hub->product_id);

	return 0;
}

static int tusb8042_i2c_probe(struct i2c_client *i2c)
{
	struct tusb8042 *hub;

	hub = devm_kzalloc(&i2c->dev, sizeof(struct tusb8042), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;

	i2c_set_clientdata(i2c, hub);
	hub->dev = &i2c->dev;
	hub->i2c = i2c;

	return tusb8042_probe(hub);
}

static int __maybe_unused tusb8042_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tusb8042 *hub = i2c_get_clientdata(client);

	return regulator_bulk_disable(ARRAY_SIZE(hub->supplies), hub->supplies);

}

static int __maybe_unused tusb8042_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tusb8042 *hub = i2c_get_clientdata(client);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(hub->supplies), hub->supplies);
	if (ret)
		return ret;

	return tusb8042_connect(hub);
}

static SIMPLE_DEV_PM_OPS(tusb8042_pm_ops, tusb8042_suspend, tusb8042_resume);

static const struct i2c_device_id tusb8042_id[] = {
	{ "tusb8020", 0 },
	{ "tusb8042", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, tusb8042_id);

static struct i2c_driver tusb8042_i2c_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(tusb8042_of_match),
		.pm = &tusb8042_pm_ops,
	},
	.probe    = tusb8042_i2c_probe,
	.id_table = tusb8042_id,
};

module_i2c_driver(tusb8042_i2c_driver);

MODULE_AUTHOR("Arnaud Vrac <avrac@freebox.fr>");
MODULE_DESCRIPTION("TI TUSB8042 USB 3.1 Hub Controller Driver");
MODULE_LICENSE("GPL");
