/*
 * ld6710-fbx.c for ld6710-fbx
 * Created by <nschichan@freebox.fr> on Wed Sep 25 15:01:56 2019
 */

/*
 * Driver for LD6710 power deliverance with freebox specific
 * firmware. The power supply temperature report on the I2C register
 * space is a feature of the ROMed firmware on the chip, which depends
 * on the OEM.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>

#define LD6710_CHIPVER	0x00
#define LD6710_FWVER	0x01

#define LD6710_SINK_CURRENT		0x10
#define LD6710_SINK_CURRENT_MAX		0x11

#define LD6710_SINK_TEMP		0x20
#define LD6710_SINK_TEMP_TURNOFF	0x21
#define LD6710_SINK_TEMP_TURNON		0x22

#define LD6710_SINK_STATUS		0x30
#define  SINK_STATUS_OTP		(1 << 0)
#define  SINK_STATUS_OCP		(1 << 1)
#define  SINK_STATUS_OVP		(1 << 2)

struct ld6710_priv {
	struct device *hwmon_dev;
	struct i2c_client *client;
	struct mutex mutex;
};

static int ld6710_read(struct ld6710_priv *priv, u8 addr)
{
	int ret;

	ret = i2c_smbus_read_byte_data(priv->client, addr);
	if (ret < 0) {
		dev_err(&priv->client->dev, "i2c read error at address %02x\n",
			addr);
		return 0xff;
	}
	return ret;
}

static void ld6710_write(struct ld6710_priv *priv, u8 addr, u8 value)
{
	i2c_smbus_write_byte_data(priv->client, addr, value);
}

static struct ld6710_priv *to_ld6710_priv(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	return i2c_get_clientdata(client);
}

/*
 * chip / fw
 */
static ssize_t version_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ld6710_priv *priv = to_ld6710_priv(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	u32 v;

	mutex_lock(&priv->mutex);
	v = ld6710_read(priv, sattr->nr);
	mutex_unlock(&priv->mutex);

	return sprintf(buf, "0x%02x\n", v);
}

static SENSOR_DEVICE_ATTR_2_RO(chipver, version, LD6710_CHIPVER, 0);
static SENSOR_DEVICE_ATTR_2_RO(fwver, version, LD6710_FWVER, 0);

static struct attribute *ld6710_ver_attrs[] = {
	&sensor_dev_attr_chipver.dev_attr.attr,
	&sensor_dev_attr_fwver.dev_attr.attr,
	NULL,
};

static const struct attribute_group ld6710_ver_group = {
	.attrs = ld6710_ver_attrs,
};

/*
 * sink current (mA)
 */
static ssize_t current_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ld6710_priv *priv = to_ld6710_priv(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	u32 v;

	mutex_lock(&priv->mutex);
	v = ld6710_read(priv, sattr->nr) * 100;
	mutex_unlock(&priv->mutex);

	return sprintf(buf, "%d\n", v);
}

static ssize_t current_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ld6710_priv *priv = to_ld6710_priv(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	val /= 100;
	if (val > 255)
		return -EINVAL;

	mutex_lock(&priv->mutex);
	ld6710_write(priv, sattr->nr, val);
	mutex_unlock(&priv->mutex);

	return count;
}

static SENSOR_DEVICE_ATTR_2_RO(sink_current, current, LD6710_SINK_CURRENT, 0);
static SENSOR_DEVICE_ATTR_2_RO(in1_input, current, LD6710_SINK_CURRENT, 0);
static SENSOR_DEVICE_ATTR_2_RW(sink_current_max, current,
			       LD6710_SINK_CURRENT_MAX, 0);

static struct attribute *ld6710_sink_current_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_sink_current.dev_attr.attr,
	&sensor_dev_attr_sink_current_max.dev_attr.attr,
	NULL,
};

static const struct attribute_group ld6710_sink_current_group = {
	.attrs = ld6710_sink_current_attrs,
};

/*
 * sink temperature (1/1000th degree)
 */
static ssize_t temperature_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct ld6710_priv *priv = to_ld6710_priv(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	u32 v;

	mutex_lock(&priv->mutex);
	v = ld6710_read(priv, sattr->nr) * 1000;
	mutex_unlock(&priv->mutex);

	return sprintf(buf, "%d\n", v);
}

static ssize_t temperature_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct ld6710_priv *priv = to_ld6710_priv(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	val /= 1000;
	if (val > 255)
		return -EINVAL;

	mutex_lock(&priv->mutex);
	ld6710_write(priv, sattr->nr, val);
	mutex_unlock(&priv->mutex);

	return count;
}

static SENSOR_DEVICE_ATTR_2_RO(temp1_input, temperature, LD6710_SINK_TEMP, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_turnoff, temperature,
			       LD6710_SINK_TEMP_TURNOFF, 0);
static SENSOR_DEVICE_ATTR_2_RW(temp1_turnon, temperature,
			       LD6710_SINK_TEMP_TURNON, 0);


static struct attribute *ld6710_sink_temp_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_turnoff.dev_attr.attr,
	&sensor_dev_attr_temp1_turnon.dev_attr.attr,
	NULL,
};

static const struct attribute_group ld6710_sink_temp_group = {
	.attrs = ld6710_sink_temp_attrs,
};

/*
 * status
 */
static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return version_show(dev, attr, buf);
}

static SENSOR_DEVICE_ATTR_2_RO(status, status, LD6710_SINK_STATUS, 0);

static struct attribute *ld6710_status_attrs[] = {
	&sensor_dev_attr_status.dev_attr.attr,
	NULL,
};

static const struct attribute_group ld6710_status_group = {
	.attrs = ld6710_status_attrs,
};


static void ld6710_fbx_remove_files(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &ld6710_ver_group);
	sysfs_remove_group(&client->dev.kobj, &ld6710_sink_current_group);
	sysfs_remove_group(&client->dev.kobj, &ld6710_sink_temp_group);
	sysfs_remove_group(&client->dev.kobj, &ld6710_status_group);
}

static int ld6710_fbx_probe(struct i2c_client *client)
{
	struct ld6710_priv *priv;
	u8 chipver, fwver;
	int error;

	dev_info(&client->dev, "probe\n");

	priv = devm_kzalloc(&client->dev, sizeof (*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	mutex_init(&priv->mutex);
	i2c_set_clientdata(client, priv);

	/*
	 * read chipver and fwver and check that they look sane.
	 */
	chipver = ld6710_read(priv, LD6710_CHIPVER);
	fwver = ld6710_read(priv, LD6710_FWVER);
	if (chipver == 0xff || fwver == 0xff) {
		dev_err(&client->dev, "invalid chip version of firmware "
			"version.\n");
		return -ENXIO;
	}

	dev_info(&client->dev, "LD6710 chip %02x, fw %02x\n",
		 chipver, fwver);

	/*
	 * create attributes
	 */
	error = sysfs_create_group(&client->dev.kobj, &ld6710_ver_group);
	if (error)
		goto remove_files;

	error = sysfs_create_group(&client->dev.kobj,
				   &ld6710_sink_current_group);
	if (error)
		goto remove_files;

	error = sysfs_create_group(&client->dev.kobj, &ld6710_sink_temp_group);
	if (error)
		goto remove_files;

	error = sysfs_create_group(&client->dev.kobj, &ld6710_status_group);
	if (error)
		goto remove_files;

	/*
	 * register hwmon device.
	 */
	priv->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(priv->hwmon_dev)) {
		dev_err(&client->dev, "unable to register hwmon device.\n");
		error = PTR_ERR(priv->hwmon_dev);
		goto remove_files;
	}

	return 0;

remove_files:
	ld6710_fbx_remove_files(client);
	return error;
}

static void ld6710_fbx_remove(struct i2c_client *client)
{
	struct ld6710_priv *priv = i2c_get_clientdata(client);

	dev_info(&client->dev, "remove\n");

	hwmon_device_unregister(priv->hwmon_dev);
	ld6710_fbx_remove_files(priv->client);
}

static const struct of_device_id ld6710_fbx_of_match[] = {
	{ .compatible	= "leadtrend,ld6710-fbx" },
	{ },
};
MODULE_DEVICE_TABLE(of, ld6710_fbx_of_match);


static const unsigned short ld6710_addrs[] = { 0x68, /* maybe some others ? */
					       I2C_CLIENT_END };

static struct i2c_driver ld6710_fbx_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "ld6710_fbx",
		.of_match_table = of_match_ptr(ld6710_fbx_of_match),
	},
	.probe		= ld6710_fbx_probe,
	.remove		= ld6710_fbx_remove,
	.address_list	= ld6710_addrs,
};

module_i2c_driver(ld6710_fbx_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Schichan <nschichan@freebox.fr>");
