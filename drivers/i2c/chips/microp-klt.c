/*
    microp-klt.c - i2c chip driver for microp-led

    Joe Hansche <madcoder@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <mach/vreg.h>
#include <mach/msm_iomap.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#include <linux/unistd.h>
#include <linux/earlysuspend.h>

#include <linux/bma150.h>
#include <linux/microp-klt.h>

#include <asm/mach-types.h>
#include "../../../arch/arm/mach-msm/proc_comm_wince.h"

int micropklt_set_lcd_state(int on);

#define MODULE_NAME "microp-klt"

#define I2C_READ_RETRY_TIMES 10
#define I2C_WRITE_RETRY_TIMES 10

#if 0
 #define D(fmt, arg...) printk(KERN_DEBUG "[KLT] %s: " fmt "\n", __FUNCTION__, ## arg);
#else
 #define D(fmt, arg...) do {} while(0)
#endif
#define GP_NS_REG (0x005c)

static int micropklt_read(struct i2c_client *, uint8_t, uint8_t *, int);
static int micropklt_write(struct i2c_client *, uint8_t *, int);
static unsigned int color_led_address;
static unsigned int auto_bl;

extern int bma150_probe(struct microp_klt*);

static void micropklt_led_brightness_set(struct led_classdev *led_cdev,
                                         enum led_brightness brightness)
{
	struct microp_klt *data;
	struct i2c_client *client;
	uint8_t buffer[4] = { 0, 0, 0, 0 };
	int idx, b, state;

	if ( !strcmp(led_cdev->name, "klt::home") )
		idx = 0;
	else if ( !strcmp(led_cdev->name, "klt::back") )
		idx = 1;
	else if ( !strcmp(led_cdev->name, "klt::end") )
		idx = 2;
	else if ( !strcmp(led_cdev->name, "klt::send") )
		idx = 3;
	else if ( !strcmp(led_cdev->name, "klt::action") )
		idx = 4;
	else if ( !strcmp(led_cdev->name, "klt::lcd-bkl") || !strcmp(led_cdev->name, "lcd-backlight"))
		idx = 5;
	else if ( !strcmp(led_cdev->name, "klt::keypad-bkl") || !strcmp(led_cdev->name, "button-backlight"))
		idx = 6;
	else
		return;

	data = container_of(led_cdev, struct microp_klt, leds[idx]);
	client = data->client;

	mutex_lock(&data->lock);

	state = data->led_states;

	// idx 5 and 6 are bits 13 and 14
	if ( idx > 4 )
		idx += 8;

	b = 1U << idx;

	if ( brightness == LED_OFF )
		state &= ~b;
	else
		state |= b;

	if (idx == 6+8) //button-backlight
	{
		if (machine_is_htckovsky())
		{
			buffer[0]=MICROP_KLT_ID_KEYPAD_BRIGHTNESS_KOVS;
			if (brightness)
			{
				buffer[1]=0x80;
				buffer[2]=brightness/4;
			}

			printk(KERN_INFO MODULE_NAME ": Setting %s brightness to %d\n", led_cdev->name, buffer[2]);
			micropklt_write(client, buffer, 3);
		}
	}

	// lcd-backlight lets us do varied brightness
	if ( idx==5+8 /*&& brightness>0*/) {
		//microp version ?
		if(machine_is_htcrhodium()) {
			buffer[0] = MICROP_KLT_ID_LCD_BRIGHTNESS2;
			//Scale from 0 to 9
			buffer[1] = (brightness*9)/255;

			printk(KERN_INFO MODULE_NAME ": Setting %s brightness2 to: %d/10\n",
				led_cdev->name, buffer[1]);
			micropklt_write(client, buffer, 2);
			//msleep(1);
		} else if (machine_is_htckovsky()) {
			buffer[0] = MICROP_KLT_ID_LCD_BRIGHTNESS_KOVS;
			if (brightness == 0) {
				buffer[1] = LED_OFF;
				printk(KERN_INFO MODULE_NAME ": brightness==0, turn %s OFF\n", led_cdev->name);
			} else {
				buffer[1] = LED_FULL; // at least 0x80 is needed
				printk(KERN_INFO MODULE_NAME ": Setting %s brightness to: 0x%02x\n",
				led_cdev->name, brightness);
			}
			buffer[2] = brightness;
			micropklt_write(client, buffer, 3);
		} else {
			buffer[0] = MICROP_KLT_ID_LCD_BRIGHTNESS;
			buffer[1] = brightness/2 & 0xf0;

			printk(KERN_INFO MODULE_NAME ": Setting %s brightness to: 0x%02x\n",
				led_cdev->name, buffer[1]);
			micropklt_write(client, buffer, 2);
		}
	}

	if ( data->led_states != state ) {
		buffer[0] = MICROP_KLT_ID_LED_STATE;
		buffer[1] = 0xff & state;
		buffer[2] = 0xff & (state >> 8);
		data->led_states = state;
		micropklt_write(client, buffer, 3);
	}

	mutex_unlock(&data->lock);
}

/**
 * set_misc_states
 * function to set the micropklt misc register.
 * note: experimental and not used yet.
 * note: its used for enabling the LCM clock ( pure guess ) on the HTC RAPH
 * note: can be used to cleanup the "send_command" hacks in micropklt_lcd_ctrl.
 *
 * example:
 * - micropklt_set_misc_states(0xFF, 4) would set bit 3.
 * - micropklt_set_misc_states(0x0, 4) would clear bit 3.
 */
int micropklt_set_misc_states( unsigned mask, unsigned bit_flag )
{
	struct microp_klt *data;
	struct i2c_client *client;
	unsigned state;
	int result;
	uint8_t buffer[2] = { MICROP_I2C_WCMD_MISC, 0x00 };

	data = micropklt_t;
	if ( !data ) return -EAGAIN;
	client = data->client;
	state = data->misc_states;

	mutex_lock(&data->lock);

	if ( bit_flag &  MISC_MICP_ISP) {
		printk( "micropklt, MISC_MICP_ISP bit should NOT be enabled, removing bit!\n" );
		bit_flag = bit_flag & ~MISC_MICP_ISP;
	}

	if ( bit_flag & MISC_CAP_SEN_RES_CTRL1 && bit_flag & MISC_CAP_SEN_RES_CTRL2 ) {
		// its evil to have them both enabled... its one or the other...
		printk( "microp, MISC_CAP_SEN_RES_CTRL1, MISC_CAP_SEN_RES_CTRL2 are both enabled\n" );

		// log the cmd that set this for debug purposes.
		D( "microp, pre bit_flag: 0x%X\n", bit_flag );
		bit_flag = bit_flag & ~( MISC_CAP_SEN_RES_CTRL1 | MISC_CAP_SEN_RES_CTRL2 );
		D( "microp, post bit_flag: 0x%X\n", bit_flag );
	}

	data->misc_states = data->misc_states & ~bit_flag;					// remove the bit
	data->misc_states = data->misc_states | ( bit_flag & mask );		// set or not set the bit

	// only send if it has actualy changed
	if ( data->misc_states != state ) {
		state = data->misc_states;
		buffer[1] = data->misc_states & 0xFF;
		result = micropklt_write(client, buffer, 2);
		D("microp, 0x%X -> 0x%X\n", state, data->misc_states);
		
	} else {
		result = 0;
	}

	// don't store the reset flags
	if( data->misc_states & ( MISC_CAP_SEN_RES_CTRL2 | MISC_CAP_SEN_RES_CTRL1 ) )
		data->misc_states = data->misc_states & ~( MISC_CAP_SEN_RES_CTRL2 | MISC_CAP_SEN_RES_CTRL1 );
	
	mutex_unlock(&data->lock);
	return result;
}

int micropklt_set_led_states(unsigned leds_mask, unsigned leds_values)
{
	struct microp_klt *data;
	struct i2c_client *client;
	unsigned state;
	uint8_t buffer[4] = { 0, 0, 0, 0 };
	int r;

	data = micropklt_t;
	if (!data) return -EAGAIN;
	client = data->client;

	mutex_lock(&data->lock);
	state = data->led_states | (leds_mask & leds_values);
	state &= MICROP_KLT_ALL_LEDS & ~(leds_mask & ~leds_values);
	if (data->led_states != state) {
		data->led_states = state;
		buffer[0] = MICROP_KLT_ID_LED_STATE;
		buffer[1] = 0xff & state;
		buffer[2] = 0xff & (state >> 8);
		data->led_states = state;
		r = micropklt_write(client, buffer, 3);
	} else {
		r = 0;
	}
	mutex_unlock(&data->lock);
	return r;
}
EXPORT_SYMBOL(micropklt_set_led_states);

int micropklt_set_color_led_state(int state)
{
	struct microp_klt *data;
	struct i2c_client *client;
	uint8_t buffer[5] = { 0, 0, 0, 0, 0 };
	int r;

	data = micropklt_t;
	if (!data) return -EAGAIN;
	client = data->client;

	mutex_lock(&data->lock);
		buffer[0] = color_led_address;
		buffer[1] = 0;
		buffer[2] = state;
		buffer[3] = 0xff;
		buffer[4] = 0xff;
	r = micropklt_write(client, buffer, 5);
	mutex_unlock(&data->lock);
	return 0;
}

int micropklt_set_lcd_state(int on)
{
	return micropklt_set_led_states(1 << MICROP_KLT_BKL_LCD,on ? 1 << MICROP_KLT_BKL_LCD : 0);
}
EXPORT_SYMBOL(micropklt_set_lcd_state);

int micropklt_set_kbd_state(int on)
{
	return micropklt_set_led_states(1 << MICROP_KLT_BKL_KBD,on ? 1 << MICROP_KLT_BKL_KBD : 0);
}
EXPORT_SYMBOL(micropklt_set_kbd_state);

#define send_command(x) micropklt_write(client, x,ARRAY_SIZE(x));

void micropklt_lcd_ctrl(int v)
{
	struct microp_klt *data;
	struct i2c_client *client;

	// for power up
	uint8_t c1[]={MICROP_I2C_WCMD_MISC,0x48};
	uint8_t c2[]={MICROP_I2C_WCMD_MISC,0x0c};
	uint8_t c3[]={MICROP_I2C_WCMD_AUTO_BL_CTL,0,0};

	// for power down
	uint8_t c7[]={MICROP_I2C_WCMD_MISC,0x4c};
	uint8_t c8[]={MICROP_KLT_ID_LED_STATE,0x10,0x00};
	uint8_t c9[]={MICROP_I2C_WCMD_MISC,0x08};
	printk("Something used micropklt_lcd_ctrl. This function should no longer be used.\n");

	// this function is obsolete
	return;

    data = micropklt_t;
    if (!data) return;
    client = data->client;

	mutex_lock(&data->lock);

	switch(v) {
	case 1: // power up
		send_command(c1);
		break;
	case 2:
		send_command(c2);

		break;
	case 3:
		send_command(c3);
		break;
	case 4: // power down
		send_command(c7);
		send_command(c3);
		send_command(c8);
		send_command(c3);
		break;
	case 5:
		send_command(c9);
		break;
	}

	mutex_unlock(&data->lock);
}

EXPORT_SYMBOL(micropklt_lcd_ctrl);

void micropklt_lcd_precess_spi_table(uint16_t spicmd, struct microp_spi_table *spi_table, size_t count)
{
	int i;
	struct microp_klt *data;
	uint16_t delay;
	uint8_t c0[4];

	data = micropklt_t;
	if (!data) return;


	mutex_lock(&data->lock);

	for(i = 0; i < count; i++) {
		delay = spi_table[i].delay;
		c0[0] = spicmd; c0[1] = spi_table[i].value1; c0[2] = spi_table[i].value2; c0[3] = spi_table[i].value3;
		micropklt_write(data->client, c0, ARRAY_SIZE(c0));
		udelay(50);
		if(delay)
			msleep(delay);
	}

	mutex_unlock(&data->lock);
}

void micropklt_lcd_precess_cmd(char* cmd, size_t count)
{
	struct microp_klt *data;
	data = micropklt_t;
	if (!data) return;

	msleep(1);

	mutex_lock(&data->lock);
	micropklt_write(data->client, cmd, count);
	mutex_unlock(&data->lock);
	udelay(50);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void micropklt_early_suspend(struct early_suspend *h)
{
	if(machine_is_htctopaz())
		micropklt_set_led_states(0x1, 0x0);
}

void micropklt_early_resume(struct early_suspend *h)
{
	if(machine_is_htctopaz())
		micropklt_set_led_states(0x1, 0x1);
}
#endif

static int micropklt_remove(struct i2c_client *client)
{
	struct microp_klt *data;
	int i;

	data = i2c_get_clientdata(client);

	micropklt_set_led_states(MICROP_KLT_ALL_LEDS, MICROP_KLT_DEFAULT_LED_STATES);

	for (i=0; i<ARRAY_SIZE(data->leds); i++) {
		led_classdev_unregister(&data->leds[i]);
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	if (data->enable_early_suspend) {
		unregister_early_suspend(&data->early_suspend);
	}
#endif

	kfree(data);
	micropklt_t = NULL;
	return 0;
}

static int micropklt_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct microp_klt *data;
	int supported, r, i;
	uint8_t buf[3] = { 0, 0, 0 };
	int id_version;

	auto_bl = 0;

	printk(KERN_INFO MODULE_NAME ": Initializing MicroP-LED chip driver at "
	       "addr: 0x%02x\n", client->addr);


	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		printk(KERN_ERR MODULE_NAME ": i2c bus not supported\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof *data, GFP_KERNEL);
	if (data < 0) {
		printk(KERN_ERR MODULE_NAME ": Not enough memory\n");
		return -ENOMEM;
	}

	// maybe seperate the read and write mutex.
	mutex_init(&data->lock);
	mutex_lock(&data->lock);

	data->client = client;
	i2c_set_clientdata(client, data);
	data->enable_early_suspend=1;
	micropklt_t = data;

	// Read version
	if (machine_is_htckovsky()) {
		id_version = MICROP_KLT_ID_VERSION_KOVS;
	} else {
		id_version = MICROP_KLT_ID_VERSION;
	}
	if( micropklt_read(client, id_version, buf, 2) < 0 ) {
		printk( KERN_ERR MODULE_NAME" : unable to read microp firmware version\n" );
		r = -EIO;
		goto fail;
	}

	/* Check version against what we think we should support
	 * Known supported versions:
	 *   0105, 0205, 0a05, 0b05, 0d02
	 *   8182, 8185
	 *   0c82, 0c85
	 *   060d
	 *   kovsky:
	 *   0787
	 */
	supported = 0;
	data->version = (buf[0] << 8) | buf[1];
	switch (buf[0]) {
	case 0x01:
	case 0x02:
	case 0x0a:
	case 0x0b:
		switch (buf[1])	{
		case 0x0e: /* topa100 */
			bma150_probe(data);
		case 0x88: /* rhod210 */
		case 0x01:
		case 0x05:
		case 0x81: /* diam500 */
			supported = 1;
			break;
		}
		break;
	case 0x0d:
		switch (buf[1])	{
		case 0x02:
			supported = 1;
			break;
		case 0x0a: /* raph300 */
			supported = 1;
			break;
		}
		break;
	case 0x81:
	case 0x0c:
		switch (buf[1])	{
		case 0x82:
		case 0x85:
			supported = 1;
			break;
		}
		break;
	case 0x06:
		switch (buf[1])	{
		case 0x0d:
			supported = 1;
			break;
		}
		break;
	case 0x07:
		switch (buf[1])	{
		case 0x87:
			supported = 1;
			break;
		}
		break;
	}

	// microp misc state init value.
	data->misc_states = 0x0C;

	/* if for some reason the hardware should support it, but it simply shows the wrong I2C id, its possible the device has been
	 * set into bootloader mode. See HTC Hero's microp driver for a way to fix this.
	 */
	if (!supported) {
		printk(KERN_WARNING MODULE_NAME ": This hardware is not yet supported: %04x\n", data->version);
		r = -ENOTSUPP;
		goto fail;
	}

	data->led_states = MICROP_KLT_DEFAULT_LED_STATES;

	data->leds[0].name = "klt::home";
	data->leds[0].brightness = LED_OFF;
	data->leds[0].brightness_set = micropklt_led_brightness_set;

	data->leds[1].name = "klt::back";
	data->leds[1].brightness = LED_OFF;
	data->leds[1].brightness_set = micropklt_led_brightness_set;

	data->leds[2].name = "klt::end";
	data->leds[2].brightness = LED_OFF;
	data->leds[2].brightness_set = micropklt_led_brightness_set;

	data->leds[3].name = "klt::send";
	data->leds[3].brightness = LED_OFF;
	data->leds[3].brightness_set = micropklt_led_brightness_set;

	data->leds[4].name = "klt::action";
	data->leds[4].brightness = LED_OFF;
	data->leds[4].brightness_set = micropklt_led_brightness_set;

	data->leds[5].name = "klt::lcd-bkl";
	data->leds[5].brightness = 0x90;
	data->leds[5].brightness_set = micropklt_led_brightness_set;

	data->leds[6].name = "klt::keypad-bkl";
	data->leds[6].brightness = LED_OFF;
	data->leds[6].brightness_set = micropklt_led_brightness_set;

#ifdef CONFIG_ANDROID_PMEM
	data->leds[5].name = "lcd-backlight";
	data->leds[6].name = "button-backlight";
#endif

	for (i=0; i<ARRAY_SIZE(data->leds); i++) {
		r = led_classdev_register(&client->dev, &data->leds[i]);
		if (r < 0) {
			goto err_led_classdev_register_failed;
			break;
		}
	}

	if(machine_is_htctopaz())
		color_led_address = 0x51;
	else if (machine_is_htcrhodium())
		color_led_address = 0x50;
	else if (machine_is_htckovsky())
		color_led_address = 0x20;
	else
		color_led_address = 0x0;

	mutex_unlock(&data->lock);

	// Set default LED state
	micropklt_set_led_states(MICROP_KLT_ALL_LEDS, MICROP_KLT_DEFAULT_LED_STATES);

	if(machine_is_htctopaz())	// Enable keypadled with device
		micropklt_set_led_states(0x1, 0x1);

	#ifdef CONFIG_HAS_EARLYSUSPEND
	if (data->enable_early_suspend) {
		data->early_suspend.level =
				EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
		data->early_suspend.suspend = micropklt_early_suspend;
		data->early_suspend.resume = micropklt_early_resume;
		register_early_suspend(&data->early_suspend);
	}
	#endif

	printk(KERN_INFO MODULE_NAME ": Initialized MicroP-LED chip revision v%04x\n", data->version);

	return 0;

err_led_classdev_register_failed:
	printk(KERN_ERR MODULE_NAME ": led_classdev_register(%d) failed: %d\n", i, r);
	for (i=i; i>=0; i--) {
		led_classdev_unregister(&data->leds[i]);
	}
fail:
	mutex_unlock(&data->lock);
	kfree(data);
	micropklt_t = 0;
	return r;
}

static int micropklt_write(struct i2c_client *client, uint8_t *sendbuf, int len)
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
		printk(KERN_WARNING "micropklt, i2c write retry\n");
	}
	printk(KERN_ERR "micropklt_write, i2c_write_block retry over %d\n",
			I2C_WRITE_RETRY_TIMES);
	return rc;
}

static int micropklt_read(struct i2c_client *client, uint8_t id, uint8_t *buf, int len)
{
	int retry;
	int rc;
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
			.buf = buf,
		}
	};
	for (retry= 0; retry <= I2C_READ_RETRY_TIMES; retry++) {
		rc = i2c_transfer(client->adapter, msgs, 2);
		if (rc == 2) {
			return 0;
		}
		msleep(10);
		printk(KERN_WARNING "micropklt, i2c read retry\n");
	}
	dev_err(&client->dev, "i2c_read_block retry over %d\n",
			I2C_READ_RETRY_TIMES);
	return -EIO;
}

static u16 sleep_state=0,old_state;
#if CONFIG_PM
static int micropklt_suspend(struct i2c_client *client, pm_message_t mesg)
{
	D("suspending device...");

//	char cmd[]={0x20,0x08};
//	send_command(cmd);
	if(sleep_state==-1) {
	if(micropklt_t)
		old_state=micropklt_t->led_states;
	else
		old_state=0;
	}
	
	micropklt_set_led_states(0xffff, sleep_state);
	//micropklt_set_led_states(0xffff, 0); old_state=0;
	//Sleeping is GOOD !
	//It's GREEN ! (on topa/rhod.)
	if ( machine_is_htctopaz() || machine_is_htcrhodium() )
		micropklt_set_color_led_state(1);
	return 0;
}

static int micropklt_resume(struct i2c_client *client)
{
	D("resuming device...");
//	char cmd[]={0x20,0x48};
//	send_command(cmd);
	micropklt_set_led_states(0xffff,old_state);
	//Being awake is BAD !
	//It's RED ! (on topa/rhod.) (ok it's amber.)

	if ( machine_is_htctopaz() || machine_is_htcrhodium() )
		micropklt_set_color_led_state(2);
	return 0;
}
#else
 #define micropklt_suspend NULL
 #define micropklt_resume NULL
#endif

static const struct i2c_device_id microp_klt_ids[] = {
	{ "microp-klt", 0 },
	{ }
};

static struct i2c_driver micropklt_driver = {
	.driver = {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
	.id_table = microp_klt_ids,
	.probe = micropklt_probe,
	.remove = micropklt_remove,
	.suspend = micropklt_suspend,
	.resume = micropklt_resume,
};

static int __init micropklt_init(void)
{
	printk(KERN_INFO "microp-klt: Registering MicroP-LED driver\n");
	return i2c_add_driver(&micropklt_driver);
}

static void __exit micropklt_exit(void)
{
	printk(KERN_INFO "microp-klt: Unregistered MicroP-LED driver\n");
	i2c_del_driver(&micropklt_driver);
}

MODULE_AUTHOR("Joe Hansche <madcoder@gmail.com>");
MODULE_DESCRIPTION("MicroP-LED chip driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

module_init(micropklt_init);
module_exit(micropklt_exit);

#if defined(CONFIG_DEBUG_FS)
static int micropklt_dbg_leds_set(void *dat, u64 val)
{
	struct microp_klt *data;
	struct i2c_client *client;
	uint8_t buffer[4] = { 0, 0, 0, 0 };
	int r;

	data = micropklt_t;
	if (!data) return -EAGAIN;
	client = data->client;

	mutex_lock(&data->lock);
		buffer[0] = MICROP_KLT_ID_LED_STATE;
		buffer[1] = 0xff & val;
		buffer[2] = 0xff & (val >> 8);

	r = micropklt_write(client, buffer, 3);
	
	mutex_unlock(&data->lock);
	return r;
}

static int micropklt_dbg_leds_get(void *data, u64 *val) {
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_leds_fops,
		micropklt_dbg_leds_get,
		micropklt_dbg_leds_set, "%llu\n");

static int micropklt_dbg_light_set(void *data, u64 val)
{
	return 0;
}

static int micropklt_dbg_light_get(void *dat, u64 *val) {
	struct microp_klt *data;
	struct i2c_client *client;
	int r;
	u64 lcd_brgh;
	unsigned long long d, d2;
	uint8_t buffer[4] = { 0, 0, 0, 0 };
	data = micropklt_t;
	if (!data) return -EAGAIN;

	client = data->client;
	mutex_lock(&data->lock);
	if(!auto_bl && machine_is_htctopaz()) {
		buffer[0] = MICROP_I2C_WCMD_AUTO_BL_CTL;
		buffer[1] = 0x1;
		buffer[2] = 0x0;
		r = micropklt_write(client, buffer, 3);
		msleep(2);
	}
	// the typecast is a bit evil... could be done better.
	r = micropklt_read(client, MICROP_KLT_ID_LIGHT_SENSOR, (uint8_t*)&d, 4);
	*val=(d&0xff00);
	if(machine_is_htctopaz()) {
		if(!auto_bl) {
			buffer[0] = MICROP_I2C_WCMD_AUTO_BL_CTL;
			buffer[1] = 0x0;
			buffer[2] = 0x0;
			r = micropklt_write(client, buffer, 3);
			msleep(2);
		}
		r = micropklt_read(client, MICROP_KLT_ID_GET_LCD_BRHTNS, (uint8_t*)&d2, 4);
		lcd_brgh = (d2&0xff00);
	}
	mutex_unlock(&data->lock);
	return r;
}

DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_light_fops,
		micropklt_dbg_light_get,
		micropklt_dbg_light_set, "%llu\n");

static int micropklt_dbg_auto_bl_get(void *dat, u64 *val) {
	*val=0;
	return 0;
}

static int micropklt_dbg_auto_bl_set(void *dat, u64 val)
{
	struct microp_klt *data;
	uint8_t buffer[4] = { 0, 0, 0, 0 };
	struct i2c_client *client;
	int r;
	auto_bl = val;

	data = micropklt_t;
	if (!data) return -EAGAIN;
	client = data->client;

	mutex_lock(&data->lock);
		buffer[0] = MICROP_I2C_WCMD_AUTO_BL_CTL;
		if(machine_is_htctopaz())
			buffer[1] = val;
		  else
			buffer[1] = 0x2;
		buffer[2] = val;
	r = micropklt_write(client, buffer, 3);
	mutex_unlock(&data->lock);
	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_auto_bl_fops,
		micropklt_dbg_auto_bl_get,
		micropklt_dbg_auto_bl_set, "%llu\n");

static int micropklt_dbg_sleep_get(void *dat, u64 *val) {
	*val=sleep_state;
	return 0;
}

static int micropklt_dbg_sleep_set(void *dat, u64 val)
{
	sleep_state=val;
	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_sleep_fops,
		micropklt_dbg_sleep_get,
		micropklt_dbg_sleep_set, "%llu\n");

static int micropklt_dbg_effects_get(void *dat, u64 *val) {
	switch( (micropklt_t->led_states&0x1f00)>>8) {
		case 1:
			//Ring
			*val=1;
			break;
		case 2:
			//Blink
			*val=2;
			break;
		case 4:
			//Breathe
			*val=3;
			break;
		case 5:
			//Fade
			*val=4;
			break;
		case 8:
			//Rotate
			*val=5;
			break;
		case 0x10:
			//Vertical
			*val=6;
			break;
		default:
			*val=0;
			break;
	}

	return 0;
}

static int micropklt_dbg_effects_set(void *dat, u64 val)
{
	switch(val) {
		default:
		case 0:
			//Clear
			micropklt_set_led_states(0x1f00, 0);
			break;
		case 1:
			//Ring
			micropklt_set_led_states(0x1f00, MICROP_KLT_SYSLED_RING);
			break;
		case 2:
			//Blink
			micropklt_set_led_states(0x1f00, MICROP_KLT_SYSLED_BLINK);
			break;
		case 3:
			//Breathe
			micropklt_set_led_states(0x1f00, MICROP_KLT_SYSLED_BREATHE);
			break;
		case 4:
			//Fade
			micropklt_set_led_states(0x1f00, MICROP_KLT_SYSLED_FADE);
			break;
		case 5:
			//Rotate
			micropklt_set_led_states(0x1f00, MICROP_KLT_SYSLED_ROTATE);
			break;
		case 6:
			//Vertical
			micropklt_set_led_states(0x1f00, MICROP_KLT_SYSLED_VERTICAL);
			break;
	}
	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_effects_fops,
		micropklt_dbg_effects_get,
		micropklt_dbg_effects_set, "%llu\n");

static int micropklt_dbg_sl_eff_get(void *dat, u64 *val) {
	switch( (sleep_state&0x1f00)>>8) {
		case 1:
			//Ring
			*val=1;
			break;
		case 2:
			//Blink
			*val=2;
			break;
		case 4:
			//Breathe
			*val=3;
			break;
		case 5:
			//Fade
			*val=4;
			break;
		case 8:
			//Rotate
			*val=5;
			break;
		case 0x10:
			//Vertical
			*val=6;
			break;
		default:
			*val=0;
			break;
	}

	return 0;
}

static int micropklt_dbg_sl_eff_set(void *dat, u64 val)
{
	switch(val) {
		default:
		case 0:
			//Clear
			sleep_state=0;
			break;
		case 1:
			//Ring
			sleep_state=MICROP_KLT_SYSLED_RING;
			break;
		case 2:
			//Blink
			sleep_state=MICROP_KLT_SYSLED_BLINK;
			break;
		case 3:
			//Breathe
			sleep_state=MICROP_KLT_SYSLED_BREATHE;
			break;
		case 4:
			//Fade
			sleep_state=MICROP_KLT_SYSLED_FADE;
			break;
		case 5:
			//Rotate
			sleep_state=MICROP_KLT_SYSLED_ROTATE;
			break;
		case 6:
			//Vertical
			sleep_state=MICROP_KLT_SYSLED_VERTICAL;
			break;
	}
	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_sl_eff_fops,
		micropklt_dbg_sl_eff_get,
		micropklt_dbg_sl_eff_set, "%llu\n");

static int micropklt_dbg_gpi_get(void *dat, u64 *val) {

	struct microp_klt *data;
	struct i2c_client *client;
	uint8_t *buff;
	int r;

	data = micropklt_t;
	if (!data) return -EAGAIN;
	client = data->client;
	buff = (uint8_t*)val;

	mutex_lock(&data->lock);
	r = micropklt_read(client, MICROP_I2C_RCMD_GPI_STATUS, buff, 2);
	mutex_unlock(&data->lock);

	return r;
}

static int micropklt_dbg_gpi_set(void *dat, u64 val)
{
	return -EPERM;
}

DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_gpi_fops,
		micropklt_dbg_gpi_get,
		micropklt_dbg_gpi_set, "%llu\n");

static int micropklt_dbg_color_led_get(void *dat, u64 *val) {
	*val=0;
	return 0;
}

static int micropklt_dbg_color_led_set(void *dat, u64 val)
{
	struct microp_klt *data;
	struct i2c_client *client;
	uint8_t buffer[5] = { 0, 0, 0, 0, 0 };
	int r;

	if(color_led_address==0) return -ENODEV;

	data = micropklt_t;
	if (!data) return -EAGAIN;
	client = data->client;

	mutex_lock(&data->lock);
		buffer[0] = color_led_address;
		buffer[1] = val&0xff;
		buffer[2] = (val>>8)&0xff;
		buffer[3] = (val>>16)&0xff;
		buffer[4] = (val>>32)&0xff;
	r = micropklt_write(client, buffer, 5);
	mutex_unlock(&data->lock);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_color_led_fops,
		micropklt_dbg_color_led_get,
		micropklt_dbg_color_led_set, "%llu\n");

static int micropklt_dbg_brightness_get(void *dat, u64 *val) {
	return -EIO;
}

static int micropklt_dbg_brightness_set(void *dat, u64 val)
{
	struct microp_klt *data;
	struct i2c_client *client;
	uint8_t buffer[2] = { 0, 0, };
	int r;

	data = micropklt_t;
	if (!data) return -EAGAIN;
	client = data->client;

	mutex_lock(&data->lock);
		buffer[0] = 0xcc;
		buffer[1] = val;//time ?
	r = micropklt_write(client, buffer, 2);
	mutex_unlock(&data->lock);
	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(micropklt_dbg_brightness_fops,
		micropklt_dbg_brightness_get,
		micropklt_dbg_brightness_set, "%llu\n");

static int __init micropklt_dbg_init(void)
{
	struct dentry *dent;

	dent = debugfs_create_dir("micropklt_dbg", 0);
	if (IS_ERR(dent))
		return PTR_ERR(dent);

	debugfs_create_file("leds", 0444, dent, NULL,
			&micropklt_dbg_leds_fops);
	debugfs_create_file("light", 0444, dent, NULL,
			&micropklt_dbg_light_fops);
	debugfs_create_file("auto_backlight", 0666, dent, NULL,
			&micropklt_dbg_auto_bl_fops);
	debugfs_create_file("sleep_leds", 0666, dent, NULL,
			&micropklt_dbg_sleep_fops);
	debugfs_create_file("sleep_effects", 0666, dent, NULL,
			&micropklt_dbg_sl_eff_fops);
	debugfs_create_file("effects", 0666, dent, NULL,
			&micropklt_dbg_effects_fops);
	debugfs_create_file("color_led", 0666, dent, NULL,
			&micropklt_dbg_color_led_fops);
	debugfs_create_file("brightness", 0666, dent, NULL,
			&micropklt_dbg_brightness_fops);

	debugfs_create_file("gpi", 0444, dent, NULL,
			&micropklt_dbg_gpi_fops);

	return 0;
}

device_initcall(micropklt_dbg_init);
#endif /* CONFIG_DEBUG_FS */
