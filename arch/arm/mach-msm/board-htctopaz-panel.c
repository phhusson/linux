/* linux/arch/arm/mach-msm/board-htctopaz-panel.c
 * Based on board-trout-panel.c by: Brian Swetland <swetland@google.com>
 * Remodelled based on board-supersonic-panel.c by: Jay Tu <jay_tu@htc.com>
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
#include <linux/microp-klt.h>

#include "board-htctopaz.h"
#include "proc_comm_wince.h"
#include "devices.h"

#define REG_WAIT	(0xffff)

static struct microp_spi_table spi_init_table[] = {
	{0xc0,0x00,0x86},
	{0xc0,0x01,0x00},
	{0xc0,0x02,0x86},
	{0xc0,0x03,0x00},
	{0xc1,0x00,0x40},
	{0xc2,0x00,0x21},
	{0xc2,0x02,0x02},
	{0xc7,0x00,0x91},
	{0x35,0x00,0x02},
	{0x44,0x00,0x00},
	{0x44,0x01,0x4f},
	{0xe0,0x00,0x01},
	{0xe0,0x01,0x05},
	{0xe0,0x02,0x1c},
	{0xe0,0x03,0x33},
	{0xe0,0x04,0x21},
	{0xe0,0x05,0x35},
	{0xe0,0x06,0x60},
	{0xe0,0x07,0x33, 25},
	{0xe0,0x08,0x24},
	{0xe0,0x09,0x26},
	{0xe0,0x0a,0x84},
	{0xe0,0x0b,0x15},
	{0xe0,0x0c,0x3a},
	{0xe0,0x0d,0x4f},
	{0xe0,0x0e,0x8c},
	{0xe0,0x0f,0xaf},
	{0xe0,0x10,0x4b},
	{0xe0,0x11,0x4d},
	{0xe1,0x00,0x01},
	{0xe1,0x01,0x05},
	{0xe1,0x02,0x1c},
	{0xe1,0x03,0x33},
	{0xe1,0x04,0x21},
	{0xe1,0x05,0x35},
	{0xe1,0x06,0x62},
	{0xe1,0x07,0x33},
	{0xe1,0x08,0x26},
	{0xe1,0x09,0x26, 25},
	{0xe1,0x0a,0x84},
	{0xe1,0x0b,0x11},
	{0xe1,0x0c,0x38},
	{0xe1,0x0d,0x4d},
	{0xe1,0x0e,0x8a},
	{0xe1,0x0f,0xad},
	{0xe1,0x10,0x49},
	{0xe1,0x11,0x4d},
	{0xe2,0x00,0x08},
	{0xe2,0x01,0x19},
	{0xe2,0x02,0x28},
	{0xe2,0x03,0x3e},
	{0xe2,0x04,0x1f},
	{0xe2,0x05,0x34},
	{0xe2,0x06,0x68},
	{0xe2,0x07,0x43},
	{0xe2,0x08,0x2b},
	{0xe2,0x09,0x2a},
	{0xe2,0x0a,0x88},
	{0xe2,0x0b,0x19, 25},
	{0xe2,0x0c,0x3c},
	{0xe2,0x0d,0x52},
	{0xe2,0x0e,0x8b},
	{0xe2,0x0f,0xad},
	{0xe2,0x10,0x4d},
	{0xe2,0x11,0x4d},
	{0xe3,0x00,0x08},
	{0xe3,0x01,0x19},
	{0xe3,0x02,0x28},
	{0xe3,0x03,0x3e},
	{0xe3,0x04,0x1f},
	{0xe3,0x05,0x34},
	{0xe3,0x06,0x68},
	{0xe3,0x07,0x43},
	{0xe3,0x08,0x2d},
	{0xe3,0x09,0x2a},
	{0xe3,0x0a,0x88},
	{0xe3,0x0b,0x15},
	{0xe3,0x0c,0x3a},
	{0xe3,0x0d,0x50, 25},
	{0xe3,0x0e,0x89},
	{0xe3,0x0f,0xab},
	{0xe3,0x10,0x4b},
	{0xe3,0x11,0x4d},
	{0xe4,0x00,0x7a},
	{0xe4,0x01,0x76},
	{0xe4,0x02,0x7d},
	{0xe4,0x03,0x90},
	{0xe4,0x04,0x3b},
	{0xe4,0x05,0x40},
	{0xe4,0x06,0x6a},
	{0xe4,0x07,0x61},
	{0xe4,0x08,0x29},
	{0xe4,0x09,0x29},
	{0xe4,0x0a,0x94},
	{0xe4,0x0b,0x15},
	{0xe4,0x0c,0x35},
	{0xe4,0x0d,0x4f},
	{0xe4,0x0e,0x9b},
	{0xe4,0x0f,0xcc, 25},
	{0xe4,0x10,0x4d},
	{0xe4,0x11,0x4d},
	{0xe5,0x00,0x7a},
	{0xe5,0x01,0x76},
	{0xe5,0x02,0x7d},
	{0xe5,0x03,0x90},
	{0xe5,0x04,0x3b},
	{0xe5,0x05,0x40},
	{0xe5,0x06,0x6c},
	{0xe5,0x07,0x61},
	{0xe5,0x08,0x2b},
	{0xe5,0x09,0x29},
	{0xe5,0x0a,0x94},
	{0xe5,0x0b,0x11},
	{0xe5,0x0c,0x33},
	{0xe5,0x0d,0x4d},
	{0xe5,0x0e,0x99},
	{0xe5,0x0f,0xca},
	{0xe5,0x10,0x4b},
	{0xe5,0x11,0x4d, 25},
	{0xf4,0x02,0x14},
	{0xf1,0x00,0x0c, 25},
	{0xb6,0x00,0x10},
};

static struct microp_spi_table spi_deinit_table[] = {
	{0x53,0x00,0x00},
	{0x53,0x00,0x00, 25},
	{0x28,0x00,0x00},
	{0x10,0x00,0x00},
};

static struct clk *gp_clk;
static struct vreg *vreg_lcd_1;	/* LCD1 */
static struct vreg *vreg_lcd_2;	/* LCD2 */


static int htctopaz_mddi_client_init(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{
	int i;
	unsigned reg, val;

	if(machine_is_htcrhodium())
		return 0;

	printk(KERN_DEBUG "%s\n", __func__);

	client_data->auto_hibernate(client_data, 0);

	for (i = 0; i < ARRAY_SIZE(spi_init_table); i++) {
		reg = cpu_to_le32(spi_init_table[i].value1 << 8 | spi_init_table[i].value2);
		val = cpu_to_le32(spi_init_table[i].value3);
		if (reg == REG_WAIT)
			msleep(val);
		else
			client_data->remote_write(client_data, val, reg);
	}

	client_data->auto_hibernate(client_data, 1);

	return 0;
}

static int htctopaz_mddi_client_uninit(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{
	int i;
	unsigned reg, val;

	if(machine_is_htcrhodium())
		return 0;

	printk(KERN_DEBUG "%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(spi_deinit_table); i++) {
		reg = cpu_to_le32(spi_deinit_table[i].value1 << 8 | spi_deinit_table[i].value2);
		val = cpu_to_le32(spi_deinit_table[i].value3);
		if (reg == REG_WAIT)
			msleep(val);
		else
			client_data->remote_write(client_data, val, reg);
	}

	return 0;
}

static int htctopaz_mddi_panel_blank(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{
	if(machine_is_htcrhodium())
		return 0;

	printk(KERN_DEBUG "%s\n", __func__);

	// nothing to do here

	return 0;
}

static int htctopaz_mddi_panel_unblank(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{
	if(machine_is_htcrhodium())
		return 0;

	printk(KERN_DEBUG "%s\n", __func__);

	// when issueing 0x1100, 0x00 on client init (like in supersonic
	// init code) we sometimes get short garbage or distorted colors;
	// when issued here everything is fine
	client_data->remote_write(client_data, 0x00, 0x1100);
	client_data->remote_write(client_data, 0x00, 0x2900);
	client_data->remote_write(client_data, 0x2c, 0x5300);

	return 0;
}

static void htctopaz_mddi_power_client(
	struct msm_mddi_client_data *client_data,
	int on)
{
	struct msm_dex_command dex;

	if (machine_is_htcrhodium())
		return;

	printk(KERN_DEBUG "%s (%s)\n", __func__, on ? "on" : "off");

	if (on) {
		vreg_enable(vreg_lcd_1);
		vreg_enable(vreg_lcd_2);
		mdelay(50);

		gpio_configure(57, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
		gpio_set_value(57, 0);
		gpio_configure(58, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
		gpio_set_value(58, 0);
		msleep(5);

		dex.cmd=PCOM_PMIC_REG_ON;
		dex.has_data=1;
		dex.data=0x80;
		msm_proc_comm_wince(&dex,0);
		msleep(5);

		dex.data=0x2000;
		msm_proc_comm_wince(&dex,0);
		msleep(5);

		gpio_configure(87, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_HIGH);
		gpio_set_value(87, 1);
		msleep(10);
	} else {
		gpio_configure(87, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
		gpio_set_value(87, 0);
		msleep(10);

		dex.cmd=PCOM_PMIC_REG_OFF;
		dex.has_data=1;
		dex.data=0x2000;
		msm_proc_comm_wince(&dex,0);
		msleep(5);

		dex.data=0x80;
		msm_proc_comm_wince(&dex,0);
		msleep(5);

		gpio_configure(57, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_HIGH);
		gpio_set_value(57, 1);
		gpio_configure(58, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_HIGH);
		gpio_set_value(58, 1);
		msleep(5);

		vreg_disable(vreg_lcd_1);
		vreg_disable(vreg_lcd_2);
		mdelay(50);
	}
}

extern struct resource resources_msm_fb[];

struct msm_mddi_bridge_platform_data novatec_client_data = {
	.init = htctopaz_mddi_client_init,
	.uninit = htctopaz_mddi_client_uninit,
	.blank = htctopaz_mddi_panel_blank,
	.unblank = htctopaz_mddi_panel_unblank,
	.fb_data = {
		.xres = 480,
		.yres = 800,
		.output_format = 0,
	},
#if 0
	.panel_conf = {
		.caps = MSMFB_CAP_CABC,
	},
#endif
};

struct msm_mddi_platform_data mddi_pdata = {
	.power_client = htctopaz_mddi_power_client,
	.fb_resource = resources_msm_fb,
	.num_clients = 2,
	.client_platform_data = {
		{
			// rhod
			.product_id = (0xb9f6 << 16 | 0x5580),
			.name = "mddi_c_b9f6_5582",
			.id = 0,
			.client_data = &novatec_client_data,
			.clk_rate = 0,
		},
		{
			// topa
			.product_id = (0xb9f6 << 16 | 0x5582),
			.name = "mddi_c_b9f6_5582",
			.id = 0,
			.client_data = &novatec_client_data,
			.clk_rate = 0,
		},
	},
};

int __init htctopaz_init_panel(void)
{
	int rc;

	if(!machine_is_htctopaz() && !machine_is_htcrhodium()) {
		printk(KERN_INFO "%s: panel does not apply to this device, aborted\n", __func__);
		return 0;
	}

	printk(KERN_INFO "%s: Initializing panel\n", __func__);


	gp_clk = clk_get(NULL, "gp_clk");
	if (IS_ERR(gp_clk)) {
		printk(KERN_ERR "%s: could not get gp clock\n", __func__);
		gp_clk = NULL;
	}
	rc = clk_set_rate(gp_clk, 19200000);
	if (rc)
	{
		printk(KERN_ERR "%s: set clock rate failed\n", __func__);
	}

	vreg_lcd_1 = vreg_get(0, "gp2");
	if (IS_ERR(vreg_lcd_1))
		return PTR_ERR(vreg_lcd_1);

	vreg_lcd_2 = vreg_get(0, "gp4");
	if (IS_ERR(vreg_lcd_2))
		return PTR_ERR(vreg_lcd_2);

	rc = platform_device_register(&msm_device_mdp);
	if (rc)
		return rc;
	msm_device_mddi0.dev.platform_data = &mddi_pdata;
	return platform_device_register(&msm_device_mddi0);
}

device_initcall(htctopaz_init_panel);
