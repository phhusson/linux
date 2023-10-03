/*
 * Driver found for joysticks made by Softwin
 * Found on JXD S7800b, GPD Q9, GPD XD
 *
 * Copyright (C) 2017 Pierre-Hugues Husson
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/of_device.h>

#define ADC_BASE 0x55

struct softwin_js {
	struct i2c_client *client;
	struct input_dev *input;
	int irq;
};

static const struct of_device_id softwin_js_of_match[] = {
	{ .compatible = "softwin,joystick" },
	{ }
};
MODULE_DEVICE_TABLE(of, softwin_js_of_match);

static int calibrate_value(int value) {
	value = 0x7f - value;
	if(value <= 16 && value >= -16)
		return 0;
	if(value < -16)
		value =  (value + 16)*60/10;
	else /*if(value > 16)*/
		value = (value - 16)*60/10;
	if(value > 0x7f) value = 0x7f;
	if(value < -0x7f) value = -0x7f;
	return value;
}

static irqreturn_t softwin_js_irq(int irq, void *private) {
	struct softwin_js *priv = private;
	struct i2c_client *client = priv->client;
	u8 data[4];
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, ADC_BASE, 4, data);
	if(ret != 4) {
		dev_err(&client->dev, "i2c read failed %d", ret);
		goto err;
	}

	input_report_abs(priv->input, ABS_X, calibrate_value(data[1]));
	input_report_abs(priv->input, ABS_Y, calibrate_value(data[0]));
	input_report_abs(priv->input, ABS_Z, calibrate_value(data[3]));
	input_report_abs(priv->input, ABS_RZ, calibrate_value(data[2]));
	input_sync(priv->input);
err:
	return IRQ_HANDLED;
}

static int softwin_js_i2c_probe(struct i2c_client *client)
{
	const struct of_device_id *of_id;
	struct input_dev *input = NULL;
	struct softwin_js *priv;
	int ret = 0;
#if 0
extern struct input_dev *gpio_keys_inputdev;
#endif

	of_id = of_match_device(softwin_js_of_match, &client->dev);
	if (!of_id) {
		dev_err(&client->dev, "Failed to find matching DT ID\n");
		return -EINVAL;
	}

	if(client->irq < 0) {
		dev_err(&client->dev, "Device doesn't work without an IRQ\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	i2c_set_clientdata(client, priv);

#if 1
	input = devm_input_allocate_device(&client->dev);
	if (!input) {
		dev_err(&client->dev, "input_allocate_device fail\n");
		return -ENOMEM;
	}

	input->name = "softwin-js";
	set_bit(EV_ABS, input->evbit);
	input_set_capability(input, EV_ABS, ABS_X);
	input_set_abs_params(input, ABS_X, -0x7f, 0x7f, 0, 0);
	input_set_capability(input, EV_ABS, ABS_Y);
	input_set_abs_params(input, ABS_Y, -0x7f, 0x7f, 0, 0);
	input_set_capability(input, EV_ABS, ABS_Z);
	input_set_abs_params(input, ABS_Z, -0x7f, 0x7f, 0, 0);
	input_set_capability(input, EV_ABS, ABS_RZ);
	input_set_abs_params(input, ABS_RZ, -0x7f, 0x7f, 0, 0);

	/* Needed so that Android subsystem understand this is a gamepad */
	set_bit(EV_KEY, input->evbit);
	input_set_capability(input, EV_KEY, BTN_JOYSTICK);

	ret = input_register_device(input);
	if(ret) {
		dev_err(&client->dev, "Unable to register input device, error: %d\n", ret);
		return ret;
	}
#else
	if(!gpio_keys_inputdev)
		return -EPROBE_DEFER;
	input = gpio_keys_inputdev;

	set_bit(EV_ABS, input->evbit);
	input_set_capability(input, EV_ABS, ABS_X);
	input_set_abs_params(input, ABS_X, -0x7f, 0x7f, 0, 0);
	input_set_capability(input, EV_ABS, ABS_Y);
	input_set_abs_params(input, ABS_Y, -0x7f, 0x7f, 0, 0);
	input_set_capability(input, EV_ABS, ABS_Z);
	input_set_abs_params(input, ABS_Z, -0x7f, 0x7f, 0, 0);
	input_set_capability(input, EV_ABS, ABS_RZ);
	input_set_abs_params(input, ABS_RZ, -0x7f, 0x7f, 0, 0);
#endif

	priv->input = input;

	ret = devm_request_threaded_irq(&client->dev, client->irq,
			NULL, softwin_js_irq,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"softwin_js", priv);
	if(ret < 0) {
		return ret;
	}

	return 0;
}

static const struct i2c_device_id softwin_js_i2c_id[] = {
	{ }
};
MODULE_DEVICE_TABLE(i2c, softwin_js_i2c_id);

static struct i2c_driver softwin_js_i2c_driver = {
	.driver = {
		.name = "softwin_js",
		.of_match_table = of_match_ptr(softwin_js_of_match),
	},
	.probe = softwin_js_i2c_probe,
	.remove = NULL,
	.id_table = softwin_js_i2c_id,
};

module_i2c_driver(softwin_js_i2c_driver);

MODULE_AUTHOR("Pierre-Hugues Husson <phh@phh.me>");
MODULE_DESCRIPTION("Softwin Joystick driver");
MODULE_LICENSE("GPL v2");
