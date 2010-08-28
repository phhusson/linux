/* linux/arch/arm/mach-msm/devices.c
 *
 * Authors: Alexander Tarasikov <alexander.tarasikov@gmail.com>
 * Brian Swetland <swetland@google.com> (board-trout-panel.c)
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/io.h>
#include <asm/gpio.h>
#include <asm/mach-types.h>

#include <mach/msm_fb.h>
#include <mach/vreg.h>

#include "board-htckovsky.h"
#include "proc_comm_wince.h"
#include "devices.h"

#ifdef CONFIG_DEBUG_INFO
#define _D printk
#else
#define _D while(0) printk
#endif

struct mddi_table {
	uint32_t reg;
	uint32_t value;
};

struct spi_table {
	uint16_t reg;
	uint16_t value;
	uint16_t delay;
};

static struct spi_table epson_spi_init_table[] = {
	{2, 0},
	{3, 0},
	{4, 0},
	{0x10, 0x250},
	{0x20, 0},
	{0x21, 0x2831},
	{0x22, 0x3e},
	{0x23, 0x7400},
	{0x24, 0x7400},
	{0x25, 0x6a06},
	{0x26, 0x7400},
	{0x27, 0x1c06},
	{0x28, 0x1c28},
	{0x29, 0x1c4a},
	{0x2a, 0x666},
	{0x100, 0x33},
	{0x101, 0x3},
	{0x300, 0x7777},
	{0x301, 0x2116},
	{0x302, 0xc114},
	{0x303, 0x2177},
	{0x304, 0x7110},
	{0x305, 0xc316},
	{0x402, 0},
	{0x501, 0xffff},
	{0x502, 0xffff},
	{0x503, 0xffff},
	{0x504, 0xff},
};

static struct mddi_table mddi_epson_deinit_table[] = {
	{0x504, 0x8001},
	{0x324, 0x3800},
	{1, 0x64},
};

static struct mddi_table mddi_epson_init_table_1[] = {
	{0x30, 0},
	{0x18, 0x3BF},
	{0x20, 0x3034},
	{0x24, 0x4011},
	{0x28, 0x32A},
	{0x2c, 0x1},
	{0x2c, 0x1},
	{0x84, 0x208},
	{0x88, 0x37},
	{0x8C, 0x2113},
	{0x90, 0x0},
	{0x2c, 0x2},
	{0x2c, 0x3},
	{0x100, 0x3402},
	{0x104, 0x380},
	{0x140, 0x3E},
	{0x144, 0xEF},
	{0x148, 0x9},
	{0x14c, 0x4},
	{0x150, 0x0},
	{0x154, 0x333},
	{0x158, 0x31F},
	{0x15c, 0x8},
	{0x160, 0x4},
	{0x164, 0x0},
	{0x168, 0xE3},
	{0x180, 0x57},
	{0x184, 0xDB},
	{0x188, 0xE3},
	{0x18c, 0x0},
	{0x190, 0x0},
	{0x200, 0xA6},
	{0x204, 0x0},
	{0x208, 0x13F},
	{0x20c, 0x0},
	{0x210, 0x0},
	{0x214, 0x0},
	{0x218, 0x0},
	{0x21C, 0x1DF},
	{0x220, 0x0},
	{0x224, 0x0},
	{0x400, 0x8000},
	{0x404, 0x10FF},
	{0x480, 0x4001},
	{0x484, 0x62},
	{0x500, 0x0},
	{0x504, 0x8000},
	{0x508, 0x0},
	{0x50c, 0x0},
	{0x510, 0x0},
	{0x514, 0x0},
	{0x518, 0x1E},
	{0x51C, 0xC7},
	{0x520, 0x1DF},
	{0x524, 0x31F},
//	{0xFFFD, 0x10},
	{0x528, 0x0},
	{0x52c, 0x0},
	{0x580, 0x0},
	{0x584, 0x0},
	{0x588, 0xD2C},
	{0x58C, 0xFA0},
	{0x590, 0x10CC},
	{0x594, 0x34},
	{0x598, 0x77},
	{0x59C, 0x13F},
	{0x5A0, 0xEF},
	{0x5a4, 0x0},
	{0x5a8, 0x0},
	{0x600, 0x0},
//	{0xFFFD, 0x20},
	{0x604, 0x101},
	{0x60C, 0x80},
	{0x680, 0x8000},
	{0x684, 0x0},
	{0x688, 0x1E},
	{0x68C, 0xC7},
	{0x690, 0x1DF},
	{0x694, 0x31F},
	{0x698, 0x0},
	{0x69c, 0x0},
	{0x6a0, 0x0},
	{0x6a4, 0x0},
	{0x6a8, 0x0},
	{0x6AC, 0x34},
	{0x6B0, 0x77},
	{0x6B4, 0x12B},
	{0x6B8, 0x31},
	{0x6BC, 0xF},
	{0x6C0, 0xF0},
	{0x6c4, 0x0},
	{0x6C8, 0x96},
	{0x700, 0x0},
	{0x704, 0x20A},
	{0x708, 0x400},
	{0x70C, 0x400},
	{0x714, 0xC00},
	{0x718, 0xD2C},
	{0x71C, 0xFA0},
	{0x720, 0x10CC},
	{0x724, 0x14},
	{0x728, 0x3B},
	{0x72C, 0x63},
	{0x730, 0x63},
	{0x73C, 0x32},
	{0x740, 0x32},
	{0x744, 0x95},
	{0x748, 0x95},
	{0x800, 0x0},
	{0x804, 0x20A},
	{0x808, 0x400},
	{0x80C, 0x400},
	{0x814, 0xC00},
	{0x818, 0xD2C},
	{0x81C, 0xFA0},
	{0x820, 0x10CC},
	{0x824, 0x14},
	{0x828, 0x3B},
	{0x82C, 0x63},
	{0x830, 0x63},
	{0x834, 0x64},
	{0x838, 0x64},
	{0x83C, 0x64},
	{0x840, 0x64},
	{0x844, 0xC7},
	{0x848, 0xC7},
	{0x900, 0x0},
	{0x904, 0x0},
	{0x908, 0x13F},
	{0x90C, 0xEF},
	{0x910, 0x0},
	{0x914, 0x13F},
	{0x918, 0x0},
	{0x91C, 0xEF},
//	{0xFFFD, 0xC000},
	{0x600, 0x4000},
	{0xb00, 0x3},
	{0x300, 0x3000},
	{0x304, 0x0},
	{0x308, 0x0},
	{0x30C, 0x4FFF},
	{0x310, 0x7FFF},
	{0x314, 0xFF},
	{0x318, 0x7FFF},
	{0x31C, 0xFFFF},
	{0x320, 0xFF},
	{0x324, 0x6FFF},
	{0x328, 0x0},
	{0x32c, 0x0},
	{0x600, 0x4000},{1, 0x2},
	{0x614, 0},
	{0x618, 0},
	{0x61c, 0},
	{0x620, 0},
	{0x628, 0},
	{0x62c, 0},
	{0x630, 0},
	{0x634, 0},
	{0x638, 0},
	{0x63c, 0},
	{0x640, 0},
	{0x644, 0},
	{0x324, 0x3800},
	{1, 0x64},
};

static struct mddi_table mddi_epson_init_table_2[] = {
	{0x324, 0x2800},
	{1, 0x104},
	{0x504, 0x1},
	{1, 0xa},
};

static inline void htckovsky_process_spi_table(struct msm_mddi_client_data *client_data,
					struct spi_table *table,
					size_t count) {
	int i;
	for (i = 0; i < count; i++) {	
		client_data->remote_write(client_data, 0x10, 0x110);
		client_data->remote_write(client_data, table[i].reg, 0x108);
		mdelay(1);
		client_data->remote_write(client_data, 0x12, 0x110);
		client_data->remote_write(client_data, table[i].value, 0x108);
	}
}

static void htckovsky_process_mddi_table(struct msm_mddi_client_data
					  *client_data,
					  struct mddi_table *table,
					  size_t count)
{
	int i;
	for (i = 0; i < count; i++) {
		uint32_t reg = table[i].reg;
		uint32_t value = table[i].value;

		if (reg == 0)
			udelay(value);
		else if (reg == 1)
			msleep(value);
		else
			client_data->remote_write(client_data, value, reg);
	}
}

static void htckovsky_mddi_power_client(struct msm_mddi_client_data
					 *client_data, int on)
{
	struct msm_dex_command dex;

	printk("htckovsky_mddi_power_client(%d)\n", on);
//      printk("XC=%x\n", i=readl(MSM_SHARED_RAM_BASE + 0xfc048));

	if (on) {
		_D("%s: +powering up panel\n", __func__);
		msm_gpio_set_function(DEX_GPIO_CFG(KOVS100_LCD_PWR, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA, 1));
		gpio_set_value(KOVS100_LCD_PWR, 1);

		_D("%s: enabled panel power\n", __func__);

		dex.cmd = PCOM_PMIC_REG_ON;
		dex.has_data = 1;
		dex.data = 0x80;
		msm_proc_comm_wince(&dex, 0);
		mdelay(50);
		_D("%s: enabled register 0x80\n", __func__);

		dex.data = 0x40000;
		msm_proc_comm_wince(&dex, 0);
		mdelay(50);
		_D("%s: enabled register 0x40000\n", __func__);
		
		dex.data = 0x400000;
		msm_proc_comm_wince(&dex, 0);
		mdelay(50);
		_D("%s: enabled register 0x400000\n", __func__);

//		The screen never works with this, probably clocks need a fix
//		msm_gpio_set_function(DEX_GPIO_CFG(KOVS100_MDDI_PWR, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA, 0));
//		gpio_set_value(KOVS100_MDDI_PWR, 0);
//		mdelay(10);
//		msm_gpio_set_function(DEX_GPIO_CFG(KOVS100_MDDI_PWR, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA, 1));
//		gpio_set_value(KOVS100_MDDI_PWR, 1);
//		mdelay(50);		

		_D("%s: enabled mddi bridge power\n", __func__);
		_D("%s: -powering up panel\n", __func__);
	} else {

		_D("%s: +shutting down panel\n", __func__);
//		msm_gpio_set_function(DEX_GPIO_CFG(KOVS100_MDDI_PWR, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA, 0));
//		_D("%s: disabled mddi bridge power\n", __func__);
//		mdelay(50);


		dex.cmd = PCOM_PMIC_REG_ON;
		dex.has_data = 1;
		dex.data = 0x400000;
		msm_proc_comm_wince(&dex, 0);
		mdelay(50);
		_D("%s: disabled register 0x400000\n", __func__);

		dex.data = 0x40000;
		msm_proc_comm_wince(&dex, 0);
		mdelay(50);
		_D("%s: disabled register 0x40000\n", __func__);
		
		dex.data = 0x80;
		msm_proc_comm_wince(&dex, 0);
		mdelay(50);
		_D("%s: disabled register 0x80\n", __func__);

		msm_gpio_set_function(DEX_GPIO_CFG(KOVS100_LCD_PWR, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA, 0));
		gpio_set_value(0x62, 0);
		mdelay(10);
		_D("%s: disabled panel power\n", __func__);
		_D("%s: -shutting down panel\n", __func__);
	}
}

static int htckovsky_mddi_panel_unblank(struct msm_mddi_bridge_platform_data
					 *bridge_data, struct msm_mddi_client_data
					 *client_data)
{
	_D("%s: +unblank panel\n", __func__);
	client_data->auto_hibernate(client_data, 0);
	htckovsky_process_mddi_table(client_data,
				mddi_epson_init_table_1,
				ARRAY_SIZE(mddi_epson_init_table_1));

	htckovsky_process_spi_table(client_data,
				epson_spi_init_table,
				ARRAY_SIZE(epson_spi_init_table));

	htckovsky_process_mddi_table(client_data,
				mddi_epson_init_table_2,
				ARRAY_SIZE(mddi_epson_init_table_2));

	client_data->auto_hibernate(client_data, 1);
	_D("%s: -unblank panel\n", __func__);
	return 0;

}

static int htckovsky_mddi_panel_blank(struct msm_mddi_bridge_platform_data
				       *bridge_data,
				       struct msm_mddi_client_data *client_data)
{
	_D("%s: +blank panel\n", __func__);
	client_data->auto_hibernate(client_data, 0);
	printk("deinit epson panel\n");
        htckovsky_process_mddi_table(client_data,
        		mddi_epson_deinit_table,
        		ARRAY_SIZE(mddi_epson_deinit_table));
	client_data->auto_hibernate(client_data, 1);
	_D("%s: -blank panel\n", __func__);
	return 0;
}

extern struct resource resources_msm_fb[];

static struct msm_mddi_bridge_platform_data epson_client_data = {
	.init = htckovsky_mddi_panel_unblank,
	.uninit = htckovsky_mddi_panel_blank,
	.blank = htckovsky_mddi_panel_blank,
	.unblank = htckovsky_mddi_panel_unblank,
	.fb_data = {
		    .xres = 480,
		    .yres = 800,
		    .output_format = 0,
		    },
};

static struct msm_mddi_platform_data mddi_pdata = {
	.clk_rate = 122880000,
	.power_client = htckovsky_mddi_power_client,
	.fb_resource = resources_msm_fb,
	.num_clients = 1,
	.client_platform_data = {
	{
		.product_id = (0x4ca3 << 16 | 0),
		.name = "S1D13774",
		.id = 0,
		.client_data = &epson_client_data,
		.clk_rate = 0,
	},
	},
};

int __init htckovsky_init_panel(void)
{
	int rc;

	printk(KERN_INFO "%s: Initializing panel\n", __func__);

	if (!machine_is_htckovsky()) {
		printk(KERN_INFO "%s: disabling kovsky panel\n", __func__);
		return 0;
	}

	rc = platform_device_register(&msm_device_mdp);
	if (rc)
		return rc;
	msm_device_mddi0.dev.platform_data = &mddi_pdata;
	return platform_device_register(&msm_device_mddi0);
}

device_initcall(htckovsky_init_panel);
