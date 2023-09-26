// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define US5151_DRV_NAME "us5151"

#define US5151_REG_CONTROL  1
#define US5151_REG_DATAL    2
#define US5151_REG_DATAH    3

#define US5151_CONTROL_ON		0x80
#define US5151_CONTROL_OFF		0x00

struct us5151_data {
	struct i2c_client *client;
};

static const struct iio_chan_spec us5151_channels[] = {
	{
		.type	= IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}
};

static int us5151_set_pwr(struct i2c_client *client, bool pwr)
{
	u8 val = pwr ? US5151_CONTROL_ON : US5151_CONTROL_OFF;
	return i2c_smbus_write_byte_data(client, US5151_REG_CONTROL, val);
}

static void us5151_set_pwr_off(void *_data)
{
	struct us5151_data *data = _data;

	us5151_set_pwr(data->client, false);
}

static int us5151_init(struct us5151_data *data)
{
	return us5151_set_pwr(data->client, true);
}

static int us5151_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	struct us5151_data *data = iio_priv(indio_dev);
    s32 ret;
	u8 buf[2];

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
        ret = i2c_smbus_read_i2c_block_data(data->client,
                           US5151_REG_DATAL, 2, buf);
		if (ret < 0)
			return ret;
        *val = (buf[1] >> 7) | (buf[0] << 1);
		return IIO_VAL_INT;
	}
	return -EINVAL;
}

static const struct iio_info us5151_info = {
	.read_raw	= us5151_read_raw,
};

static int us5151_probe(struct i2c_client *client)
{
	struct us5151_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	indio_dev->info = &us5151_info;
	indio_dev->name = US5151_DRV_NAME;
	indio_dev->channels = us5151_channels;
	indio_dev->num_channels = ARRAY_SIZE(us5151_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = us5151_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "us5151 chip init failed\n");
		return ret;
	}

	ret = devm_add_action_or_reset(&client->dev,
					us5151_set_pwr_off,
					data);
	if (ret < 0)
		return ret;

	return devm_iio_device_register(&client->dev, indio_dev);
}

static int us5151_suspend(struct device *dev)
{
	return us5151_set_pwr(to_i2c_client(dev), false);
}

static int us5151_resume(struct device *dev)
{
	return us5151_set_pwr(to_i2c_client(dev), true);
}

static DEFINE_SIMPLE_DEV_PM_OPS(us5151_pm_ops, us5151_suspend, us5151_resume);

static const struct of_device_id us5151_of_match[] = {
	{ .compatible = "upisemi,us5151", },
	{},
};
MODULE_DEVICE_TABLE(of, us5151_of_match);

static struct i2c_driver us5151_driver = {
	.driver = {
		.name = US5151_DRV_NAME,
		.of_match_table = us5151_of_match,
		.pm = pm_sleep_ptr(&us5151_pm_ops),
	},
	.probe		= us5151_probe,
};
module_i2c_driver(us5151_driver);

MODULE_AUTHOR("Pierre-Hugues Husson <phh@phh@me>");
MODULE_DESCRIPTION("US5151 Ambient Light Sensor driver");
MODULE_LICENSE("GPL v2");
