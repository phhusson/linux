/* arch/arm/mach-msm/qdsp5/external.c
 *
 * Handle power speaker/mic bias for devices not controlled by AMSS
 *
 * Copyright (C) 2010 HUSSON Pierre-Hugues <phhusson@free.fr>
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/wakelock.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <mach/amss_para.h>

// called from snd.c
void headphone_amp_power(int status);

//from board-htcrhodium-audio
void enable_speaker_rhod(void);
void disable_speaker_rhod(void);
void speaker_vol_rhod(int);

static int force_rhod_speaker=0;
module_param_named(rhod_speaker, force_rhod_speaker, int, S_IRUGO | S_IWUSR | S_IWGRP);

void enable_speaker(void) {
	switch(__machine_arch_type) {
		case MACH_TYPE_HTCBLACKSTONE:
			gpio_set_value(57,1);
			break;
		case MACH_TYPE_HTCRHODIUM:
			//Needs userland fix
			enable_speaker_rhod();
			break;
		case MACH_TYPE_HTCKOVSKY:
			gpio_configure(0x41, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_HIGH);
			gpio_set_value(0x41, 1);
			break;
		default:
			break;
	};
}

void disable_speaker(void) {
	switch(__machine_arch_type) {
		case MACH_TYPE_HTCBLACKSTONE:
			gpio_set_value(57, 0);
			break;
		case MACH_TYPE_HTCRHODIUM:
			disable_speaker_rhod();
			break;
		case MACH_TYPE_HTCKOVSKY:
			gpio_configure(0x41, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
			gpio_set_value(0x41, 0);
			break;
		default:
			break;
	};
}

void speaker_vol(int arg) {
	if(machine_is_htcrhodium())
		speaker_vol_rhod(arg);
}

void headphone_amp_power(int status)
{
	unsigned int gpio;
	switch (__machine_arch_type) {
		case MACH_TYPE_HTCRAPHAEL_CDMA500:
		case MACH_TYPE_HTCRAPHAEL_CDMA:
		case MACH_TYPE_HTCDIAMOND_CDMA:
			gpio = 0x54;
			break;
		case MACH_TYPE_HTCKOVSKY:
			gpio = 0x42;
			break;
		default:
			/* We should test this on more machines */
			return;
	}

	if (status)
	{
		/* Power up headphone amp */
		gpio_configure(gpio, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_HIGH);
		gpio_set_value(gpio, 1);
	}
	else
	{
		/* Power down headphone amp */
		gpio_configure(gpio, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
		gpio_set_value(gpio, 0);
	}
}
