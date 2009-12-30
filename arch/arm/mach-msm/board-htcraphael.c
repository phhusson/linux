/* linux/arch/arm/mach-msm/board-htcraphael.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>,
 * Octavian Voicu, Martijn Stolk
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/i2c.h>
#include <linux/mm.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/mach/mmc.h>
#include <asm/setup.h>

#include <mach/board.h>
#include <mach/msm_iomap.h>
#include <mach/system.h>
#include <mach/msm_fb.h>
#include <mach/msm_hsusb.h>
#include <mach/msm_serial_hs.h>
#include <mach/vreg.h>
#include <mach/htc_battery.h>

#include <mach/gpio.h>
#include <mach/io.h>
#include <mach/board_htc.h>
#include <linux/delay.h>
#ifdef CONFIG_HTC_HEADSET
#include <mach/htc_headset.h>
#endif

#include <linux/microp-keypad.h>
#include <linux/gpio_keys.h>

#include "proc_comm_wince.h"
#include "devices.h"
#include "htc_hw.h"
#include "board-htcraphael.h"

static int adb=0;
module_param(adb, int, S_IRUGO | S_IWUSR | S_IWGRP);

static void htcraphael_device_specific_fixes(void);

extern int init_mmc(void);
extern void msm_init_pmic_vibrator(void);

static struct resource raphael_keypad_resources[] = {
	{ 
		.start = MSM_GPIO_TO_INT(RAPH100_KPD_IRQ), /* Modified in htcraphael_device_specific_fixes() */
		.end = MSM_GPIO_TO_INT(RAPH100_KPD_IRQ), /* Modified in htcraphael_device_specific_fixes() */
		.flags = IORESOURCE_IRQ,
	},
};

static struct microp_keypad_platform_data raphael_keypad_data = {
	.clamshell = { 
		.gpio = RAPH800_CLAMSHELL_IRQ, /* Modified in htcraphael_device_specific_fixes() */
		.irq = MSM_GPIO_TO_INT(RAPH800_CLAMSHELL_IRQ), /* Modified in htcraphael_device_specific_fixes() */
	},
	.backlight_gpio = RAPH100_BKL_PWR, /* Modified in htcraphael_device_specific_fixes() */
};

static struct platform_device raphael_keypad_device = {
	.name = "microp-keypad",
	.id = 0,
	.num_resources = ARRAY_SIZE(raphael_keypad_resources),
	.resource = raphael_keypad_resources,
	.dev = { .platform_data = &raphael_keypad_data, },
};


static int usb_phy_init_seq_raph100[] = {
	0x40, 0x31, /* Leave this pair out for USB Host Mode */
	0x1D, 0x0D,
	0x1D, 0x10,
	-1
};

static int usb_phy_init_seq_raph800[] = {
	0x04, 0x48, /* Host mode is unsure for raph800 */
	0x3A, 0x10,
	0x3B, 0x10,
	-1
};

static void usb_phy_reset(void)
{
	gpio_set_value(RAPH100_USBPHY_RST, 0);
	mdelay(1);
	gpio_set_value(RAPH100_USBPHY_RST, 1);
	mdelay(3);
}

static struct i2c_board_info i2c_devices[] = {
	{
		// LED & Backlight controller
		I2C_BOARD_INFO("microp-klt", 0x66),
	},
	{
		// Keyboard controller
		I2C_BOARD_INFO("microp-ksc", 0x67),
	},
	{		
		I2C_BOARD_INFO("mt9t013", 0x6c>>1),
		/* .irq = TROUT_GPIO_TO_INT(TROUT_GPIO_CAM_BTN_STEP1_N), */
	},
	{
		// Raphael NaviPad (cy8c20434)
		I2C_BOARD_INFO("raph_navi_pad", 0x62),
	},
	{
		// Accelerometer
		I2C_BOARD_INFO("kionix-kxsd9", 0x18),
	},
};

static smem_batt_t msm_battery_pdata = {
	.gpio_battery_detect = RAPH100_BAT_IRQ,
	.gpio_charger_enable = RAPH100_CHARGE_EN_N,
	.gpio_charger_current_select = RAPH100_USB_AC_PWR,
	.smem_offset = 0xfc140,
	.smem_field_size = 4,
};

static struct platform_device raphael_rfkill = {
	.name = "htcraphael_rfkill",
	.id = -1,
};

#define SND(num, desc) { .name = desc, .id = num }
static struct snd_endpoint snd_endpoints_list[] = {
	SND(0, "HANDSET"),
	SND(1, "SPEAKER"),
	SND(2, "HEADSET"),
	SND(3, "BT"),
};
#undef SND

static struct msm_snd_endpoints raphael_snd_endpoints = {
        .endpoints = snd_endpoints_list,
        .num = ARRAY_SIZE(snd_endpoints_list),
};

static struct platform_device raphael_snd = {
	.name = "msm_snd",
	.id = -1,
	.dev	= {
		.platform_data = &raphael_snd_endpoints,
	},
};

static struct platform_device raphael_gps = {
    .name       = "raphael_gps",
};

#ifdef CONFIG_HTC_HEADSET

static void h2w_config_cpld(int route);
static void h2w_init_cpld(void);
static struct h2w_platform_data raphael_h2w_data = {
	.cable_in1              = 18,//TROUT_GPIO_CABLE_IN1,
	.cable_in2              = 45,//TROUT_GPIO_CABLE_IN2,
	.h2w_clk                = 46,//TROUT_GPIO_H2W_CLK_GPI,
	.h2w_data               = 31,//TROUT_GPIO_H2W_DAT_GPI,
	.debug_uart             = H2W_UART3,
	.config_cpld            = h2w_config_cpld,
	.init_cpld              = h2w_init_cpld,
	.headset_mic_35mm       = 17,
};

static void h2w_config_cpld(int route)
{
	switch (route) {
		case H2W_UART3:
			gpio_set_value(0, 1);
			break;
		case H2W_GPIO:
			gpio_set_value(0, 0);
			break;
	}
}

static void h2w_init_cpld(void)
{
	h2w_config_cpld(H2W_UART3);
	gpio_set_value(raphael_h2w_data.h2w_clk, 0);
	gpio_set_value(raphael_h2w_data.h2w_data, 0);
}

static struct platform_device raphael_h2w = {
	.name           = "h2w",
	.id             = -1,
	.dev            = {
		.platform_data  = &raphael_h2w_data,
	},
};
#endif

static struct platform_device *devices[] __initdata = {
	&raphael_keypad_device,
	&raphael_rfkill,
	&msm_device_smd,
	&msm_device_nand,
	&msm_device_i2c,
	&msm_device_rtc,
	&msm_device_htc_hw,
#if !defined(CONFIG_MSM_SERIAL_DEBUGGER) && !defined(CONFIG_HTC_HEADSET)
//	&msm_device_uart1,
#endif
#ifdef CONFIG_SERIAL_MSM_HS
	&msm_device_uart_dm2,
#endif
	&msm_device_htc_battery,
	&raphael_snd,
	//&raphael_gps,
#ifdef CONFIG_HTC_HEADSET
	&raphael_h2w,
#endif
};

extern struct sys_timer msm_timer;

static void __init halibut_init_irq(void)
{
	msm_init_irq();
}

static struct msm_acpu_clock_platform_data halibut_clock_data = {
	.acpu_switch_time_us = 50,
	.max_speed_delta_khz = 256000,
	.vdd_switch_time_us = 62,
	.power_collapse_khz = 19200000,
	.wait_for_irq_khz = 128000000,
};

void msm_serial_debug_init(unsigned int base, int irq, 
			   const char *clkname, int signal_irq);

#ifdef CONFIG_SERIAL_MSM_HS
static struct msm_serial_hs_platform_data msm_uart_dm2_pdata = {
	.wakeup_irq = MSM_GPIO_TO_INT(RAPH100_UART2DM_RX),
	.inject_rx_on_wakeup = 1,
	.rx_to_inject = 0x32,
};
#endif

static void htcraphael_reset(void)
{
	struct msm_dex_command dex = { .cmd = PCOM_RESET_ARM9 };
//	struct msm_dex_command dex = { .cmd = PCOM_NOTIFY_ARM9_REBOOT };
	msm_proc_comm_wince(&dex, 0);
	msleep(0x15e);
	gpio_configure(RAPH100_ARM9_SOFTRST, GPIOF_OWNER_ARM11);
	gpio_direction_output(RAPH100_ARM9_SOFTRST, 0);
	printk(KERN_INFO "%s: Soft reset done.\n", __func__);
}

static void htcraphael_set_vibrate(uint32_t val)
{
	struct msm_dex_command vibra;

	if (val == 0) {
		vibra.cmd = PCOM_VIBRA_OFF;
		msm_proc_comm_wince(&vibra, 0);
	} else if (val > 0) {
		if (val == 1 || val > 0xb22)
			val = 0xb22;
		writel(val, MSM_SHARED_RAM_BASE + 0xfc130);
		vibra.cmd = PCOM_VIBRA_ON;
		msm_proc_comm_wince(&vibra, 0);
	}
}

static htc_hw_pdata_t msm_htc_hw_pdata = {
	.set_vibrate = htcraphael_set_vibrate,
	.battery_smem_offset = 0xfc140, //XXX: raph800
	.battery_smem_field_size = 4,
};

static void __init halibut_init(void)
{
	int i;

	// Fix data in arrays depending on GSM/CDMA version
	htcraphael_device_specific_fixes();

	msm_acpu_clock_init(&halibut_clock_data);
	msm_proc_comm_wince_init();

	// Register hardware reset hook
	msm_hw_reset_hook = htcraphael_reset;

	// Device pdata overrides
	msm_device_htc_hw.dev.platform_data = &msm_htc_hw_pdata;
	msm_device_htc_battery.dev.platform_data = &msm_battery_pdata;

	if(machine_is_htcraphael())
		msm_add_usb_devices(usb_phy_reset, NULL, usb_phy_init_seq_raph100);
	else
		msm_add_usb_devices(usb_phy_reset, NULL, usb_phy_init_seq_raph800);

#ifdef CONFIG_SERIAL_MSM_HS
	msm_device_uart_dm2.dev.platform_data = &msm_uart_dm2_pdata;
#endif

	msm_init_pmic_vibrator();

	// Register devices
	platform_add_devices(devices, ARRAY_SIZE(devices));

	// Register I2C devices
	i2c_register_board_info(0, i2c_devices, ARRAY_SIZE(i2c_devices));

	// Initialize SD controllers
	init_mmc();

	/* TODO: detect vbus and correctly notify USB about its presence 
	 * For now we just declare that VBUS is present at boot and USB
	 * copes, but this is not ideal.
	 */
	msm_hsusb_set_vbus_state(1);

	/* A little vibrating welcome */
	for (i=0; i<2; i++) {
		htcraphael_set_vibrate(1);
		mdelay(150);
		htcraphael_set_vibrate(0);
		mdelay(75);
	}
}

static void __init halibut_map_io(void)
{
	msm_map_common_io();
	msm_clock_init();
}

static void __init htcraphael_fixup(struct machine_desc *desc, struct tag *tags,
                                    char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PAGE_ALIGN(PHYS_OFFSET);
	mi->bank[0].node = PHYS_TO_NID(mi->bank[0].start);
	mi->bank[0].size = 107*1024*1024;
#if 0
	/* TODO: detect whether a 2nd memory bank is actually present, not all devices have it */
	mi->nr_banks++;
	mi->bank[1].start = PAGE_ALIGN(PHYS_OFFSET + 0x10000000);
	mi->bank[1].node = PHYS_TO_NID(mi->bank[1].start);
	mi->bank[1].size = (128 * 1024 * 1024);
#endif
	printk(KERN_INFO "fixup: nr_banks = %d\n", mi->nr_banks);
	printk(KERN_INFO "fixup: bank0 start=%08lx, node=%08x, size=%08lx\n", mi->bank[0].start, mi->bank[0].node, mi->bank[0].size);
	if (mi->nr_banks > 1)
		printk(KERN_INFO "fixup: bank1 start=%08lx, node=%08x, size=%08lx\n", mi->bank[1].start, mi->bank[1].node, mi->bank[1].size);
}

static void htcraphael_cdma_fixup(struct machine_desc *desc, struct tag *tags, char **cmdline, struct meminfo *mi) {
	mi->nr_banks = 1;
	mi->bank[0].start = PAGE_ALIGN(PHYS_OFFSET);
	mi->bank[0].node = PHYS_TO_NID(mi->bank[0].start);
	mi->bank[0].size = (89 * 1024 * 1024);
};

static void htcraphael_cdma500_fixup(struct machine_desc *desc, struct tag *tags, char **cmdline, struct meminfo *mi) {
	mi->nr_banks = 1;
	mi->bank[0].start = PAGE_ALIGN(PHYS_OFFSET);
	mi->bank[0].node = PHYS_TO_NID(mi->bank[0].start);
	mi->bank[0].size = (89 * 1024 * 1024);
};

static void treopro_fixup(struct machine_desc *desc, struct tag *tags, char **cmdline, struct meminfo *mi) {
	mi->nr_banks = 1;
	mi->bank[0].start = PAGE_ALIGN(PHYS_OFFSET);
	mi->bank[0].node = PHYS_TO_NID(mi->bank[0].start);
	mi->bank[0].size = (89 * 1024 * 1024);
};

static void htcraphael_device_specific_fixes(void)
{
	if (machine_is_htcraphael() || machine_is_treopro()) {
		raphael_keypad_resources[0].start = MSM_GPIO_TO_INT(RAPH100_KPD_IRQ);
		raphael_keypad_resources[0].end = MSM_GPIO_TO_INT(RAPH100_KPD_IRQ);
		raphael_keypad_data.clamshell.gpio = RAPH100_CLAMSHELL_IRQ;
		raphael_keypad_data.clamshell.irq = MSM_GPIO_TO_INT(RAPH100_CLAMSHELL_IRQ);
		raphael_keypad_data.backlight_gpio = RAPH100_BKL_PWR;
		msm_htc_hw_pdata.battery_smem_offset = 0xfc110;
		msm_htc_hw_pdata.battery_smem_field_size = 2;
		msm_battery_pdata.smem_offset = 0xfc110;
		msm_battery_pdata.smem_field_size = 2;
	}
	if (machine_is_htcraphael_cdma()) {
		raphael_keypad_resources[0].start = MSM_GPIO_TO_INT(RAPH100_KPD_IRQ);
		raphael_keypad_resources[0].end = MSM_GPIO_TO_INT(RAPH100_KPD_IRQ);
		raphael_keypad_data.clamshell.gpio = RAPH800_CLAMSHELL_IRQ;
		raphael_keypad_data.clamshell.irq = MSM_GPIO_TO_INT(RAPH800_CLAMSHELL_IRQ);
		raphael_keypad_data.backlight_gpio = RAPH100_BKL_PWR;
		msm_htc_hw_pdata.battery_smem_offset = 0xfc140;
		msm_htc_hw_pdata.battery_smem_field_size = 4;
		msm_battery_pdata.smem_offset = 0xfc140;
		msm_battery_pdata.smem_field_size = 4;
	}
	if (machine_is_htcraphael_cdma500()) {
		/* copied from raph800, fix the bt rfkill */
                raphael_keypad_resources[0].start = MSM_GPIO_TO_INT(RAPH100_KPD_IRQ);
                raphael_keypad_resources[0].end = MSM_GPIO_TO_INT(RAPH100_KPD_IRQ);
                raphael_keypad_data.clamshell.gpio = RAPH800_CLAMSHELL_IRQ;
                raphael_keypad_data.clamshell.irq = MSM_GPIO_TO_INT(RAPH800_CLAMSHELL_IRQ);
                raphael_keypad_data.backlight_gpio = RAPH100_BKL_PWR;
                msm_htc_hw_pdata.battery_smem_offset = 0xfc140;
                msm_htc_hw_pdata.battery_smem_field_size = 4;
                msm_battery_pdata.smem_offset = 0xfc140;
                msm_battery_pdata.smem_field_size = 4;
	}

}

MACHINE_START(HTCRAPHAEL, "HTC Raphael GSM phone (aka HTC Touch Pro)")
	.fixup 		= htcraphael_fixup,
	.boot_params	= 0x10000100,
	.map_io		= halibut_map_io,
	.init_irq	= halibut_init_irq,
	.init_machine	= halibut_init,
	.timer		= &msm_timer,
MACHINE_END

MACHINE_START(HTCRAPHAEL_CDMA, "HTC Raphael CDMA phone (aka HTC Touch Pro)")
	.fixup 		= htcraphael_cdma_fixup,
	.boot_params	= 0x10000100,
	.map_io		= halibut_map_io,
	.init_irq	= halibut_init_irq,
	.init_machine	= halibut_init,
	.timer		= &msm_timer,
MACHINE_END

MACHINE_START(HTCRAPHAEL_CDMA500, "HTC Raphael CDMA phone (Touch Pro) raph500")
	.fixup		= htcraphael_cdma500_fixup,
	.boot_params	= 0x10000100,
	.map_io		= halibut_map_io,
	.init_irq	= halibut_init_irq,
	.init_machine	= halibut_init,
	.timer		= &msm_timer,
MACHINE_END

MACHINE_START(TREOPRO, "Sprint Treo Pro(HTC Raphael alike)")
	.fixup		= treopro_fixup,
	.boot_params	= 0x10000100,
	.map_io		= halibut_map_io,
	.init_irq	= halibut_init_irq,
	.init_machine	= halibut_init,
	.timer		= &msm_timer,
MACHINE_END


