/* drivers/input/touchscreen/msm_blackstone_pad.c
 *
 * By Nicolas Velasquez <gnicolax@gmail.com>
 * Heavily based on msm_blackstone_pad.c by Octavian Voicu <octavian@voicu.gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/font.h>
#include <linux/fb.h>
#include <linux/fb_helper.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>

#include <mach/msm_iomap.h>
#include <mach/msm_fb.h>
#include <../../.././arch/arm/mach-msm/proc_comm_wince.h>
#include <linux/delay.h>


#define TP_X_MIN 0
#define TP_X_MAX 1023
#define TP_Y_MIN 0
#define TP_Y_MAX 1023
#define MSM_BLACKSTONE_PAD_NBUTTONS	4
#define MSM_BLACKSTONE_PAD_BWIDTH	256


int vibrate=0;
module_param_named(vibrate, vibrate, int, S_IRUGO | S_IWUSR | S_IWGRP);

/* returns whether position was inside blackstone_pad, so as to eat event */
typedef int msm_ts_handler_t(int, int, int);
extern msm_ts_handler_t *msm_ts_handler_pad;

static struct input_dev *msm_blackstone_pad_dev;

static int pad_keymap[MSM_BLACKSTONE_PAD_NBUTTONS] =
{
	KEY_F3,
	KEY_MENU,
	KEY_BACK,
	KEY_F4,
};


void do_vibrate(void)
{
	if(vibrate) {
		struct msm_dex_command vibra = { .cmd = 0, };
		vibra.cmd = PCOM_VIBRA_ON;
		msm_proc_comm_wince(&vibra, 0);
		mdelay(50);
		vibra.cmd = PCOM_VIBRA_OFF;
		msm_proc_comm_wince(&vibra, 0);
	}	
}

int msm_blackstone_pad_handle_ts_event(int x, int y, int touched)
{
	/*button_pressed ensures, that the buttons
	 * do not get pressed multiple times */
	static bool button_pressed = false;
	static int prev_button = -1;
	int button;
	
	/* no touch button pressed*/
	if (touched && !button_pressed && y<TP_Y_MAX)
		return 0;
		
	/* the user moved his finger to somewhere else,
	 * but he did not release it -> ignore the touch */
	if (touched && button_pressed && prev_button==-1)
		return 1;
	
	/*the user no loger touches the button
	 * release the button*/
	if (!touched) {
		if (button_pressed && prev_button!=-1) {
			input_event(msm_blackstone_pad_dev, EV_KEY, pad_keymap[prev_button], 0);
			input_sync(msm_blackstone_pad_dev);
		}
		button_pressed = false;
		prev_button = -1;
		return 1; 
	}

	button = x / MSM_BLACKSTONE_PAD_BWIDTH;
	
	/*first touch*/
	if (touched && !button_pressed) {
		input_event(msm_blackstone_pad_dev, EV_KEY, pad_keymap[button], 1);
		input_sync(msm_blackstone_pad_dev);
		do_vibrate();
		prev_button = button;
		button_pressed = true;
		return 1;
	}
	
	/*the user is holding the button
	 * check if he is still on the button
	 * otherwise release the button */
	if (touched && button_pressed) {
		if (button != prev_button || y!=TP_Y_MAX) {
			input_event(msm_blackstone_pad_dev, EV_KEY, pad_keymap[prev_button], 0);
			input_sync(msm_blackstone_pad_dev);
			prev_button = -1;
		}
		return 1;
	}
	
	
	return 0;
}


static int __init msm_blackstone_pad_init(void)
{
	int err, i;
	//Do we want that only on black?
	if(!machine_is_htcblackstone())
		return 0;

	printk(KERN_INFO "msm_blackstone_pad: init\n");

	/* Setup the input device */
	msm_blackstone_pad_dev = input_allocate_device();
	if (!msm_blackstone_pad_dev)
		return -ENOMEM;

//	msm_blackstone_pad_dev->evbit[0] = BIT_MASK(EV_KEY);

	msm_blackstone_pad_dev->name = "MSM Blackstone pad";
	msm_blackstone_pad_dev->phys = "msm_blackstone_pad/input0";

	for (i = 0; i < MSM_BLACKSTONE_PAD_NBUTTONS ; i++) {
		input_set_capability(msm_blackstone_pad_dev, EV_KEY, pad_keymap[i]);
	}


	/* Register the input device */
	err = input_register_device(msm_blackstone_pad_dev);
	if (err) {
		input_free_device(msm_blackstone_pad_dev);
		return err;
	}

	/* We're depending on input from the touchscreen driver */
	msm_ts_handler_pad = msm_blackstone_pad_handle_ts_event;

	/* Done */
	printk(KERN_INFO "msm_blackstone_pad: loaded\n");

	return 0;
}

static void __exit msm_blackstone_pad_exit(void)
{
	msm_ts_handler_pad = NULL;
	input_unregister_device(msm_blackstone_pad_dev);
}

module_init(msm_blackstone_pad_init);
module_exit(msm_blackstone_pad_exit);

MODULE_AUTHOR("Nicolas Velasquez, gnicolax@gmail.com");
MODULE_DESCRIPTION("MSM Blackstone pad");
MODULE_LICENSE("GPL");
