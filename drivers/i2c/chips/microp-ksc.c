/*
    microp-ksc.c - i2c chip driver for microp-key

    Joe Hansche <madcoder@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <asm/mach-types.h>
#include <linux/workqueue.h> /* for keyboard LED worker */
#include <linux/microp-ksc.h>

static int micropksc_read(struct i2c_client *, uint8_t, uint8_t *, int);
static int micropksc_write(struct i2c_client *, uint8_t *, int);
static int micropksc_probe(struct i2c_client *, const struct i2c_device_id *id);
static int __devexit micropksc_remove(struct i2c_client *);

#define MODULE_NAME "microp-ksc"

#define I2C_READ_RETRY_TIMES 10
#define I2C_WRITE_RETRY_TIMES 10

#if 0
 #define D(fmt, arg...) printk(KERN_DEBUG "[KSC] %s: " fmt "\n", __FUNCTION__, ## arg);
#else
 #define D(fmt, arg...) do {} while(0)
#endif

static struct microp_ksc {
	struct i2c_client *client;
	struct mutex lock;
	unsigned short version;
	unsigned led_state:2;
	struct work_struct work;
} *micropksc_t = 0;

int micropksc_read_scancode(unsigned char *outkey, unsigned char *outdown)
{
	struct microp_ksc *data;
	struct i2c_client *client;
	unsigned char key, isdown;
	uint8_t buffer[3] = "\0\0\0";

	if (!micropksc_t) {
		if (outkey)
			*outkey = -1;
		return -EAGAIN;
	}

	data = micropksc_t;

	client = data->client;
	key = 0;

	micropksc_read(client, MICROP_KSC_ID_SCANCODE, buffer, 2);

	key = buffer[0] & MICROP_KSC_SCANCODE_MASK;
	isdown = (buffer[0] & MICROP_KSC_RELEASED_BIT) == 0;

	//TODO: Find out what channel 0x11 is for
	//micropksc_read(client, MICROP_KSC_ID_MODIFIER, buffer, 2);
	if (outkey)
		*outkey = key;
	if (outdown)
		*outdown = isdown;

	return 0;
}
EXPORT_SYMBOL(micropksc_read_scancode);

int micropksc_read_scancode_kovsky(unsigned char *outkey, unsigned char *outdown, unsigned char *outclamshell)
{
	struct microp_ksc *data;
	struct i2c_client *client;
	unsigned char key, isdown, clamshell;
	uint8_t buffer[3] = "\0\0\0";

	if (!micropksc_t) {
		if (outkey)
			*outkey = -1;
		return -EAGAIN;
	}

	data = micropksc_t;

	client = data->client;
	key = 0;

	micropksc_read(client, MICROP_KSC_ID_SCANCODE, buffer, 2);

	key = buffer[0] & MICROP_KSC_SCANCODE_MASK;
	isdown = (buffer[0] & MICROP_KSC_RELEASED_BIT) == 0;

	micropksc_read(client, MICROP_KSC_ID_MODIFIER, buffer, 2);
	clamshell = (buffer[1] & MICROP_KSC_RELEASED_BIT) == 0;

	if (outkey)
		*outkey = key;
	if (outdown)
		*outdown = isdown;
	if (outclamshell)
		*outclamshell = clamshell;

	return 0;
}
EXPORT_SYMBOL(micropksc_read_scancode_kovsky);

int micropksc_set_led(unsigned int led, int on)
{
	struct microp_ksc *data;
	struct i2c_client *client;
	uint8_t buffer[3] = { MICROP_KSC_ID_LED, 0, 0 };

	if (!micropksc_t)
		return -EAGAIN;
	if (led >= MICROP_KSC_LED_MAX)
		return -EINVAL;

	data = micropksc_t;
	client = data->client;

	if (led == MICROP_KSC_LED_RESET)
		data->led_state = 0;
	else if (on)
		data->led_state |= led;
	else
		data->led_state &= ~led;

	buffer[1] = 0x16 - (data->led_state << 1);

	micropksc_write(client, buffer, 2);

	return 0;
}
EXPORT_SYMBOL(micropksc_set_led);

int micropksc_set_kbd_led_state(int on)
{
	printk(KERN_INFO MODULE_NAME ": micropksc_set_kbd_led_state (%d)\n", on);
	struct microp_ksc *data;
	struct i2c_client *client;

	data = micropksc_t;
	client = data->client;

	uint8_t buffer_qwerty[10]={MICROP_KSC_ID_QWERTY_BRIGHTNESS_KOVS, on ? 0x60 : 0, on ? 0x40 : 0xFF, on ? 0x60 : 0,0,0,0,0,0,0};
	micropksc_write(client, buffer_qwerty, 10);

	uint8_t buffer[3];
	buffer[0]=MICROP_KSC_ID_QWERTY_ENABLE_KOVS;
	buffer[1]=0;
	buffer[2]=(uint8_t)on;
	micropksc_write(client, buffer, 3);
	return 0;
};
EXPORT_SYMBOL(micropksc_set_kbd_led_state);


/**
 * The i2c buffer holds all the keys that are pressed,
 * even when microp-ksc isn't listening. It's safe to assume
 * we don't care about those bytes, so we need to flush
 * the i2c buffer by reading scancodes until it's empty.
 */
int micropksc_flush_buffer(void)
{
	unsigned char key;
	int r, i;

	i = 0;

	if (!micropksc_t) {
		printk(KERN_WARNING MODULE_NAME ": not initialized yet..\n");
		return -EAGAIN;
	}
	if (machine_is_htckovsky()) {
		r = micropksc_read_scancode_kovsky(&key, 0, 0);
	} else {
		r = micropksc_read_scancode(&key, 0);
	}

	if (key != 0) {
		do {
			mdelay(5);
			if (machine_is_htckovsky()) {
				r = micropksc_read_scancode_kovsky(&key, 0, 0);
			} else {
				r = micropksc_read_scancode(&key, 0);
			}
		} while (++i < 50 && key != 0);
		printk(KERN_INFO MODULE_NAME ": Keyboard buffer was dirty! "
		                      "Flushed %d byte(s) from buffer\n", i);
	}
	return i;
}
EXPORT_SYMBOL(micropksc_flush_buffer);

static int micropksc_remove(struct i2c_client *client)
{
	struct microp_ksc *data;

	data = i2c_get_clientdata(client);

	kfree(data);
	return 0;
}

static int micropksc_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct microp_ksc *data;
	uint8_t buf[3] = { 0, 0, 0 };

	printk(KERN_INFO MODULE_NAME ": Initializing MicroP-KEY chip driver at addr: 0x%02x\n", client->addr);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		printk(KERN_ERR MODULE_NAME ": i2c bus not supported\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof *data, GFP_KERNEL);
	if (data < 0) {
		printk(KERN_ERR MODULE_NAME ": Not enough memory\n");
		return -ENOMEM;
	}

	micropksc_t = data;
	data->client = client;
	i2c_set_clientdata(client, data);

	// Read version
	micropksc_read(client, MICROP_KSC_ID_VERSION, buf, 2);
	data->version = buf[0] << 8 | buf[1];
	//TODO: Check version against known good revisions, and fail if it's not supported

	micropksc_flush_buffer();

	printk(MODULE_NAME ": Initialized MicroP-KEY chip revision v%04x\n", data->version);
	return 0;
#if 0 // See TODO above
fail:
	kfree(data);
	return -ENOSYS;
#endif
}

static int micropksc_write(struct i2c_client *client, uint8_t *sendbuf, int len)
{
	int rc;
	int retry;

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = len,
			.buf = sendbuf,
		},
	};

	for (retry = 0; retry <= I2C_WRITE_RETRY_TIMES; retry++) {
		rc = i2c_transfer(client->adapter, msg, 1);
		if (rc == 1)
			return 0;
		msleep(10);
		printk(KERN_WARNING "micropksc, i2c write retry\n");
	}
	printk(KERN_ERR "micropksc_write, i2c_write_block retry over %d\n",
			I2C_WRITE_RETRY_TIMES);
	return rc;
}

static int micropksc_read(struct i2c_client *client, uint8_t id,
						uint8_t *recvbuf, int len)
{
	int retry;
	int ret;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &id,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = recvbuf,
		}
	};
	for (retry = 0; retry <= I2C_READ_RETRY_TIMES; retry++) {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2)
			return 0;
		msleep(10);
		printk(KERN_WARNING "micropksc, i2c read retry\n");
	}
	dev_err(&client->dev, "i2c_read_block retry over %d\n",
			I2C_READ_RETRY_TIMES);
	return -EIO;
}

#if CONFIG_PM
static int micropksc_suspend(struct i2c_client *client, pm_message_t mesg)
{
	D("suspending device...");
	return 0;
}

static int micropksc_resume(struct i2c_client *client)
{
	D("resuming device...");
	return 0;
}
#else
 #define micropksc_suspend NULL
 #define micropksc_resume NULL
#endif

static const struct i2c_device_id microp_ksc_ids[] = {
	{ "microp-ksc", 0 },
	{ }
};

static struct i2c_driver micropksc_driver = {
	.driver = {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
	.id_table = microp_ksc_ids,
	.probe = micropksc_probe,
	.remove = micropksc_remove,
	.suspend = micropksc_suspend,
	.resume = micropksc_resume,
};

static int __init micropksc_init(void)
{
	micropksc_t = NULL;
	printk(KERN_INFO "microp-ksc: Registering MicroP-KEY driver\n");

	return i2c_add_driver(&micropksc_driver);
}

static void __exit micropksc_exit(void)
{
	printk(KERN_INFO "microp-ksc: Unregistered MicroP-KEY driver\n");
	i2c_del_driver(&micropksc_driver);
}

MODULE_AUTHOR("Joe Hansche <madcoder@gmail.com>");
MODULE_DESCRIPTION("MicroP-KEY chip driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

module_init(micropksc_init);
module_exit(micropksc_exit);
