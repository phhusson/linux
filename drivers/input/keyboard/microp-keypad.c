/*
    microp-keypad.c - i2c keyboard driver found on certain HTC Phones
    Depends on microp-ksc and microp-klt i2c chip drivers

    Joe Hansche <madcoder@gmail.com>
    Based in part on htc-spi-kbd.c from Kevin2

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <asm/mach-types.h>

#include <linux/microp-keypad.h>
#include <linux/microp-ksc.h>
#include <linux/microp-klt.h>

#define MODULE_NAME "microp-keypad"

#define MICROP_DEBUG 0

#if defined(MICROP_DEBUG) && MICROP_DEBUG
 #define DLOG(fmt, arg...) printk(fmt, ## arg);
#else
 #define DLOG(fmt, arg...) do {} while(0)
#endif

static int microp_keypad_led_event(struct input_dev *dev, unsigned int type, unsigned int code, int value);
static void microp_backlight_timeout(struct work_struct *work);

// Rhodium keymapping by VicBush
// Right now its for all Rhodium models. Tested on RHOD400
static int microp_keymap_rhodium[] = {
        KEY_B, //KEY_RESERVED, // invalid
        KEY_BACK, //Back key
        KEY_Q,
        KEY_HOME, //Mute button on back of device
        KEY_CAMERA,
        KEY_A,
        KEY_F,
        KEY_S,
        KEY_D,
        KEY_SEND, //Send Key
        KEY_MENU, //Windows Key
        KEY_S, //KEY_RESERVED, // 0x0b   //Unknown
        KEY_I,
        KEY_K,
        KEY_J,
        KEY_H,
        KEY_G,
        KEY_A,
        KEY_4,
        KEY_1, //KEY_RESERVED, // 0x13  //Unknown
        KEY_2, //KEY_RESERVED, // 0x14	//Unknown
        KEY_L,
        KEY_I,
        KEY_P,
        KEY_O,
        KEY_B,
        KEY_9,
        KEY_8,
        KEY_N,
        KEY_ENTER,
        KEY_M,
        KEY_C,
        KEY_V,
        KEY_0,
        KEY_U,
        KEY_E,
        KEY_R,
        KEY_Q,
        KEY_T,
        KEY_Y,
        KEY_W,
        KEY_UP,  //ARROW KEY
        KEY_1,
        KEY_2,
        KEY_DOWN, //KEY_LEFT,	//KEY_DOWN,   //ITS KEY DOWN FOR SURE!!!! STILL DOESN't GO DOWN THO
        KEY_3,
        KEY_4,
        KEY_5,
        KEY_LEFT, 	//KEY_LEFT,	//ARROW KEY
        KEY_6,
        KEY_2,			//KEY_RESERVED, // 0x32 //Unknown
        KEY_SPACE,
        KEY_BACKSPACE,
        KEY_7,
        KEY_RIGHT,		//KEY_UNKNOWN,  // ARROW KEY
        KEY_SPACE, //UNKNOWN
        KEY_X,	//KEY_COMMA, //UNKNOWN
        KEY_EMAIL,
        KEY_DOT,
        KEY_FN,  //Doesn't do anything?
        KEY_LEFTSHIFT,
        KEY_Z,
        KEY_X,
        KEY_COMMA,
        KEY_COMPOSE, //Brings up search box?
        KEY_C, //KEY_SLASH,  //Unknown
        KEY_COMMA,
        KEY_6, 			//Unknown
        KEY_8,			//Unknown
        KEY_1, //KEY_RESERVED, // 0x45	//Unknown
        KEY_2, //KEY_RESERVED, // 0x46	//Unknown
        KEY_P, //KEY_EMAIL,	//Unknown
};

// This is raph800's default keymap.  can be remapped by userland
static int microp_keymap_raph800[] = {
        KEY_RESERVED, // invalid
        KEY_TAB,
        KEY_Q,
        KEY_W,
        KEY_E,
        KEY_R,
        KEY_T,
        KEY_Y,
        KEY_1,
        KEY_RESERVED, // 0x09
        KEY_RESERVED, // 0x0a
        KEY_U, // 0x0b
        KEY_I,
        KEY_O,
        KEY_P,
        KEY_BACKSPACE,
        KEY_CAPSLOCK,
        KEY_A,
        KEY_4,
        KEY_RESERVED, // 0x13
        KEY_RESERVED, // 0x14
        KEY_S,
        KEY_D,
        KEY_F,
        KEY_G,
        KEY_H,
        KEY_J,
        KEY_K,
        KEY_7,
        KEY_RESERVED, // 0x1d
        KEY_RESERVED, // 0x1e
        KEY_L,
        KEY_ENTER,
        KEY_LEFTSHIFT,
        KEY_Z,
        KEY_X,
        KEY_C,
        KEY_V,
        KEY_9,
        KEY_RESERVED, // 0x27
        KEY_RESERVED, // 0x28
        KEY_B,
        KEY_N,
        KEY_M,
        KEY_RIGHTSHIFT,
        KEY_UP,
        KEY_0,
        KEY_LEFTCTRL,
        KEY_2,
        KEY_RESERVED, // 0x31
        KEY_RESERVED, // 0x32
        KEY_LEFTALT,
        KEY_COMPOSE,  // TXT/SMS
        KEY_MINUS,
        KEY_UNKNOWN,  // SYM/Data
        KEY_SPACE,
        KEY_COMMA,
        KEY_DOT,
        KEY_5,
        KEY_RESERVED, // 0x3b
        KEY_RESERVED, // 0x3c
        KEY_RIGHT,
        KEY_DOWN,
        KEY_LEFT,
        KEY_EQUAL,
        KEY_SLASH,
        KEY_3,
        KEY_6,
        KEY_8,
        KEY_RESERVED, // 0x45
        KEY_RESERVED, // 0x46
        KEY_EMAIL,
};

// This is raph100's keymap.  can be remapped by userland
static int microp_keymap_raph100[] = {
        KEY_RESERVED, // invalid
        KEY_CAPSLOCK,
        KEY_TAB,
        KEY_Q,
        KEY_W,
        KEY_E,
        KEY_R,
        KEY_T,
        KEY_1,
        KEY_RESERVED,
        KEY_RESERVED,
        KEY_Y,
        KEY_U,
        KEY_I,
        KEY_O,
        KEY_P,
        KEY_EQUAL,
        KEY_A,
        KEY_4,
        KEY_RESERVED,
        KEY_RESERVED,
        KEY_S,
        KEY_D,
        KEY_F,
        KEY_G,
        KEY_H,
        KEY_J,
        KEY_K,
        KEY_7,
        KEY_RESERVED,
        KEY_RESERVED,
        KEY_L,
        KEY_ENTER,
        KEY_LEFTSHIFT,
        KEY_Z,
        KEY_X,
        KEY_C,
        KEY_V,
        KEY_9,
        KEY_RESERVED,
        KEY_RESERVED,
        KEY_B,
        KEY_N,
        KEY_M,
        KEY_RIGHTSHIFT,
        KEY_UP,
        KEY_0,
        KEY_LEFTCTRL,
        KEY_2,
        KEY_RESERVED, // 0x31
        KEY_RESERVED, // 0x32
        KEY_LEFTALT,
        KEY_TEXT,     // TXT/SMS
        KEY_MINUS,
        KEY_RIGHTALT,  // SYM/Data
        KEY_SPACE,
        KEY_COMMA,
        KEY_DOT,
        KEY_5,
        KEY_RESERVED, // 0x3b
        KEY_RESERVED, // 0x3c
        KEY_RIGHT,
        KEY_DOWN,
        KEY_LEFT,
        KEY_BACKSPACE,
        KEY_SLASH,
        KEY_3,
        KEY_6,
        KEY_8,
        KEY_RESERVED, // 0x45
        KEY_RESERVED, // 0x46
        KEY_EMAIL,
};

// This is htckovsky keymap.  can be remapped by userland
static int microp_keymap_htckovsky[] = {
	KEY_RESERVED, // invalid
	KEY_ENTER,
	KEY_LEFT,
	KEY_F1, // NAVI_SILVER_LEFT; Menu-Key for Android
	KEY_SEND, // NAVI_GREEN
	KEY_A,
	KEY_F,
	KEY_S,
	KEY_D,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_POWER, // NAVI_SILVER_RIGHT; Power-Key for Android
	KEY_DOWN,
	KEY_RIGHT,
	KEY_HOME, // NAVI_XPANEL; Home-Key for Android
	KEY_K,
	KEY_J,
	KEY_H,
	KEY_G,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_END, // NAVI_RED
	KEY_BACK, // NAVI_OK; Back-Key for Android
	KEY_UP,
	KEY_RESERVED,
	KEY_L,
	KEY_I,
	KEY_P,
	KEY_O,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_B,
	KEY_APOSTROPHE,
	KEY_SEMICOLON,
	KEY_N,
	KEY_ENTER,
	KEY_M,
	KEY_C,
	KEY_V,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_ESC,
	KEY_U,
	KEY_E,
	KEY_R,
	KEY_Q,
	KEY_T,
	KEY_Y,
	KEY_W,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_SPACE,
	KEY_SPACE,
	KEY_SPACE,
	KEY_BACKSPACE, // CLOSE
	KEY_DOT,
	KEY_BACK, // OK ; same as front OK
	KEY_SLASH,
	KEY_COMMA,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_2,
	KEY_TAB,
	KEY_RIGHTALT,
	KEY_LEFTSHIFT,
	KEY_Z,
	KEY_X,
	KEY_LEFTCTRL,
	KEY_LEFTALT,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_VOLUMEUP,
	KEY_VOLUMEDOWN,
};

struct microp_key_led {
	unsigned led:4;
	unsigned on:1;
};

static struct microp_keypad {
	struct mutex lock;
	struct work_struct keypad_work;
	struct work_struct clamshell_work;
	struct work_struct led_work;
	struct delayed_work backlight_work;

	struct microp_keypad_platform_data *pdata;
	struct platform_device *pdev;

	struct input_dev *input;

	struct microp_key_led led_status;
	int *keymap;
	int keycount;
	int keypress_irq;
	int clamshell_irq;
} * microp_keypad_t;

static irqreturn_t microp_keypad_interrupt(int irq, void *handle)
{
	struct microp_keypad *data;
	data = (struct microp_keypad *)handle;

	disable_irq(data->keypress_irq);
	schedule_work(&data->keypad_work);

	return IRQ_HANDLED;
}

static void microp_keypad_work(struct work_struct *work)
{
	struct microp_keypad *data;
	struct microp_keypad_platform_data *pdata;
	unsigned char key, isdown, clamshell;

	data = container_of(work, struct microp_keypad, keypad_work);
	pdata = data->pdata;
	key = 0;

	mutex_lock(&data->lock);

	// on key press enable backlight
	if (pdata->backlight_gpio > 0)
		gpio_set_value(pdata->backlight_gpio, 1);

	if ( work_pending( &data->backlight_work.work ) )
		cancel_delayed_work_sync(&data->backlight_work);
	schedule_delayed_work(&data->backlight_work, HZ * 5); // 5 seconds?

	do
	{
		if (machine_is_htckovsky() || machine_is_htcrhodium()) {
			micropksc_read_scancode_kovsky(&key, &isdown, &clamshell);
		} else {
			micropksc_read_scancode(&key, &isdown);
		}
		if (key != 0)
		{
			DLOG(KERN_INFO " :::   Scancode = %02x; currently pressed: %01x\n", key, isdown);
			// Allow input subsystem to use a scancode even if our keymap doesn't define it
			input_event(data->input, EV_MSC, MSC_SCAN, key);

			if (key < data->keycount)
			{
				input_report_key(data->input, data->keymap[key], isdown);
				input_sync(data->input);

				DLOG(KERN_INFO "       Input keycode = %d, scancode = %d\n", data->keymap[key], key);
			}
		}
		if (machine_is_htckovsky() || machine_is_htcrhodium()) {
			DLOG(KERN_WARNING "%s: clamshell is %s\n", __func__,
						!clamshell ? "closed" : "open");

			micropklt_set_kbd_state(!clamshell);
			if (machine_is_htckovsky())
				micropksc_set_kbd_led_state(clamshell);
			input_report_switch(data->input, SW_LID, !clamshell);
		}
	} while ( key != 0 );

	mutex_unlock(&data->lock);

	enable_irq(data->keypress_irq);
}

static irqreturn_t microp_keypad_clamshell_interrupt(int irq, void *handle)
{
	struct microp_keypad *data;
	data = (struct microp_keypad *)handle;

	disable_irq(data->clamshell_irq);
	schedule_work(&data->clamshell_work);
	return IRQ_HANDLED;
}

static void microp_keypad_clamshell_work(struct work_struct *work)
{
	struct microp_keypad *data;
	int closed;

	data = container_of(work, struct microp_keypad, clamshell_work);

	mutex_lock(&data->lock);
	closed = !gpio_get_value(data->pdata->clamshell.gpio);
	DLOG(KERN_WARNING "%s: clamshell is %s\n", __func__,
			closed ? "closed" : "open");
	micropklt_set_kbd_state(!closed);
	input_report_switch(data->input, SW_LID, closed);
	input_sync(data->input);
	mutex_unlock(&data->lock);
	enable_irq(data->clamshell_irq);
}

static int microp_keypad_remove(struct platform_device *pdev)
{
	struct microp_keypad *data;
	struct microp_keypad_platform_data *pdata;

	pdata = pdev->dev.platform_data;
	data = platform_get_drvdata(pdev);

	if (pdata->backlight_gpio > 0)
		gpio_set_value(pdata->backlight_gpio, 0);

	if (machine_is_htckovsky())
		micropksc_set_kbd_led_state(0);

	if (data->keypress_irq > 0)
		free_irq(data->keypress_irq, data);

	if ( pdata->clamshell.irq > 0 )
		free_irq(pdata->clamshell.irq, data);

	flush_scheduled_work();
	kfree(data);
	printk("microp_keypad_remove\n");
	return 0;
}

static void microp_led_work(struct work_struct *work)
{
	struct microp_keypad *data;

	data = container_of(work, struct microp_keypad, led_work);

	mutex_lock(&data->lock);
	micropksc_set_led(data->led_status.led, data->led_status.on);
	mutex_unlock(&data->lock);
}

static int microp_keypad_led_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	unsigned int led;
	struct microp_keypad *data = microp_keypad_t;

	if (type == EV_LED)
	{
		switch (code)
		{
			case LED_CAPSL:
				if (machine_is_htcraphael_cdma()) {
					led = MICROP_KSC_LED_CAPS_RAPH800;
				} else {
					led = MICROP_KSC_LED_CAPS;
				}
				break;
			case LED_MISC:
				if (machine_is_htcraphael_cdma()) {
					led = MICROP_KSC_LED_FN_RAPH800;
				} else {
					led = MICROP_KSC_LED_FN;
				}
				break;
			default:
				return -1;
		}

		data->led_status.led = led;
		data->led_status.on = value;
		schedule_work(&data->led_work);
		return 0;
	}
	return -1;
}

static void microp_backlight_timeout(struct work_struct *work)
{
	struct microp_keypad *data;
	struct microp_keypad_platform_data *pdata;
	data = container_of(work, struct microp_keypad, backlight_work.work);

	pdata = data->pdata;

	if (pdata->backlight_gpio > 0)
		gpio_set_value(pdata->backlight_gpio, 0);

	if (machine_is_htckovsky())
		micropksc_set_kbd_led_state(0);
}

static int microp_keypad_probe(struct platform_device *pdev)
{
	struct microp_keypad *data;
	struct input_dev *input = NULL;
	struct microp_keypad_platform_data *pdata = pdev->dev.platform_data;
	int r, i;

	printk(KERN_INFO MODULE_NAME ": Initializing MicroP keypad driver\n");

	data = kzalloc(sizeof *data, GFP_KERNEL);
	if (data < 0)
	{
		printk(KERN_ERR MODULE_NAME ": Not enough memory\n");
		return -ENOMEM;
	}

	mutex_init(&data->lock);
	INIT_WORK(&data->keypad_work, microp_keypad_work);
	INIT_WORK(&data->led_work, microp_led_work);
	INIT_DELAYED_WORK(&data->backlight_work, microp_backlight_timeout);

	// Initialize input device
	input = input_allocate_device();
	if (!input)
		goto fail;
	input->name = MODULE_NAME;

	// Tell input subsystem we can provide KEYs
	set_bit(EV_KEY, input->evbit);

	// Tell input subsystem we can set our own LEDs
	set_bit(EV_LED, input->evbit);
	set_bit(LED_CAPSL, input->ledbit);
	set_bit(LED_MISC, input->ledbit); // Fn-lock?

	// Tell input subsystem to handle auto-repeat of keys for us
	set_bit(EV_REP, input->evbit);

	// Tell input subsystem we can provide overridable scancodes
	set_bit(EV_MSC, input->evbit);
	set_bit(MSC_SCAN, input->mscbit);

	// Use our handler for LED-set callbacks
	input->event = microp_keypad_led_event;

	input->keycodesize = sizeof(data->keymap[0]);
	if (machine_is_htcraphael_cdma()) {
		input->keycodemax = ARRAY_SIZE(microp_keymap_raph800);
		input->keycode = data->keymap = microp_keymap_raph800;
	}
	else if (machine_is_htcraphael_cdma500()) {
		/* clone raph800 for now */
		input->keycodemax = ARRAY_SIZE(microp_keymap_raph800);
		input->keycode = data->keymap = microp_keymap_raph800;
	}
	else if (machine_is_htcraphael()) {
		input->keycodemax = ARRAY_SIZE(microp_keymap_raph100);
		input->keycode = data->keymap = microp_keymap_raph100;
	}
	else if (machine_is_htckovsky()) {
		input->keycodemax = ARRAY_SIZE(microp_keymap_htckovsky);
		input->keycode = data->keymap = microp_keymap_htckovsky;
	}
	else if (machine_is_htcrhodium()) {
		input->keycodemax = ARRAY_SIZE(microp_keymap_rhodium);
		input->keycode = data->keymap = microp_keymap_rhodium;
	}
	else {
		goto fail;
	}

	for (i = 0; i < input->keycodemax; i++)
	{
		if (data->keymap[i] != KEY_RESERVED)
		{
			set_bit(data->keymap[i], input->keybit);
		}
	}
	data->keycount = i;

	r = input_register_device(input);
	if (r)
		goto fail;

	data->pdev = pdev;
	data->input = input;
	data->pdata = pdata;
	platform_set_drvdata(pdev, data);

	data->keypress_irq = 0;
	for (i = 0; i < pdev->num_resources; i++)
	{
		if (pdev->resource[i].flags == IORESOURCE_IRQ &&
			pdev->resource[i].start > 0)
		{
			data->keypress_irq = pdev->resource[i].start;
			r = request_irq(data->keypress_irq, microp_keypad_interrupt,
		                IRQF_TRIGGER_FALLING | IRQF_SAMPLE_RANDOM,
		                MODULE_NAME, data);
			if (r < 0)
			{
				printk(KERN_ERR "Couldn't request IRQ %d; error: %d\n", data->keypress_irq, r);
				goto fail;
			}
			break;
		}
	}
	if (!data->keypress_irq)
	{
		printk(KERN_ERR MODULE_NAME ": not using IRQ!  polling not implemented\n");
		goto fail;
	}
	if ( pdata->clamshell.gpio > 0 )
 	{
		gpio_request(pdata->clamshell.gpio, "microp-keypad-sw");
		gpio_direction_input(pdata->clamshell.gpio);
		data->clamshell_irq = gpio_to_irq(pdata->clamshell.gpio);
		r = request_irq(data->clamshell_irq, microp_keypad_clamshell_interrupt,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"microp-keypad-sw", data);
		if (r < 0) {
			printk(KERN_ERR "Couldn't request IRQ %d; error: %d\n", data->keypress_irq, r);
			goto fail;
		}
		set_irq_wake(data->clamshell_irq, 1);
		disable_irq(data->clamshell_irq);

		// Tell input subsystem we can provide lid switch event
		set_bit(EV_SW, input->evbit);
		input_set_capability(input, EV_SW, SW_LID);

		INIT_WORK(&data->clamshell_work, microp_keypad_clamshell_work);
		schedule_work(&data->clamshell_work);
	}
	if(machine_is_htcrhodium() || machine_is_htckovsky()) {
		set_bit(EV_SW, input->evbit);
		input_set_capability(input, EV_SW, SW_LID);
	}

	if ( pdata->backlight_gpio > 0 )
		gpio_direction_output( pdata->backlight_gpio, 0 );

	microp_keypad_t = data;

	printk("microp_keypad_probe done\n");
	return 0;
fail:
	input_unregister_device(input);
	kfree(data);
	return -ENOSYS;
}

#if CONFIG_PM
static int microp_keypad_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct microp_keypad_platform_data *pdata = pdev->dev.platform_data;
	if (pdata->backlight_gpio > 0)
		gpio_set_value(pdata->backlight_gpio, 0);
	if (machine_is_htckovsky())
		micropksc_set_kbd_led_state(0);
	flush_scheduled_work();
	return 0;
}

static int microp_keypad_resume(struct platform_device *pdev)
{
	return 0;
}
#else
 #define microp_keypad_suspend NULL
 #define microp_keypad_resume NULL
#endif

static struct platform_driver microp_keypad_driver = {
	.driver = {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
	.probe = microp_keypad_probe,
	.remove = microp_keypad_remove,
#if defined(CONFIG_PM)
	.suspend = microp_keypad_suspend,
	.resume = microp_keypad_resume,
#endif
};

static int __init microp_keypad_init(void)
{
	printk(KERN_INFO "Registering MicroP keypad driver\n");
	return platform_driver_register(&microp_keypad_driver);
}

static void __exit microp_keypad_exit(void)
{
	printk(KERN_INFO "Unregistered MicroP keypad driver\n");
	platform_driver_unregister(&microp_keypad_driver);
}

MODULE_AUTHOR("Joe Hansche <madcoder@gmail.com>");
MODULE_DESCRIPTION("MicroP keypad driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

late_initcall(microp_keypad_init);
module_exit(microp_keypad_exit);
