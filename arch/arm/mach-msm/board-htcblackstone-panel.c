/* linux/arch/arm/mach-msm/board-htcblackstone-panel.c*/

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
#include "board-htcraphael.h"
#include "proc_comm_wince.h"
#include "devices.h"

static struct clk *gp_clk;

static int no_bkl_off;
module_param_named(no_bkl_off, no_bkl_off, int, S_IRUGO | S_IWUSR | S_IWGRP);

static struct microp_spi_table spi_init_table[] = {
	{0x0f,0x01},
	{0x05,0x01},
	{0x07,0x10},
	{0x09,0x1e},
	{0x0a,0x04},
	{0x11,0xff},
	{0x15,0x8a},
	{0x16,0x00},
	{0x17,0x82},
	{0x18,0x24},
	{0x19,0x22},
	{0x1a,0x6d},
	{0x1b,0xeb},
	{0x1c,0xb9},
	{0x1d,0x3a},
	{0x31,0x1a},
	{0x32,0x16},
	{0x33,0x05},
	{0x37,0x7f},
	{0x38,0x15},
	{0x39,0x7b},
	{0x3c,0x05},
	{0x3d,0x0c},
	{0x3e,0x80},
	{0x3f,0x00},
	{0x5c,0x90},
	{0x61,0x01},
	{0x62,0xff},
	{0x71,0x11},
	{0x72,0x02},
	{0x73,0x08},
	{0x7b,0xab},
	{0x7c,0x04},
	{0x06,0x02},
	{0x85,0x00},
	{0x86,0xf0},
	{0x87,0x22},
	{0x88,0x0b},
	{0x89,0xff},
	{0x8a,0x0f},
	{0x8b,0x00},
	{0x8c,0xf5},
	{0x8d,0x22},
	{0x8e,0x0b},
	{0x8f,0xff},
	{0x90,0x0f},
	{0x91,0x00},
	{0x92,0xfe},
	{0x93,0x22},
	{0x94,0x0b},
	{0x95,0xff},
	{0x96,0x0f},
	{0xca,0x30},
	{0x06,0x02},
	{0x08,0x00},
	{0x81,0x53},
	{0x82,0x35},
	{0x83,0x3c},
	{0x84,0x01},
	{0x99,0x00},
	{0x9a,0x00},
	{0x9b,0x81},
	{0x9c,0xa5},
	{0x9d,0xab},
	{0x9e,0xb1},
	{0x9f,0xb7},
	{0xa0,0xbd},
	{0xa1,0xc3},
	{0xa2,0xc9},
	{0xa3,0xcf},
	{0xa4,0xd5},
	{0xa5,0xdb},
	{0xa6,0xe1},
	{0xa7,0xe7},
	{0xa8,0xed},
	{0xa9,0xf3},
	{0xaa,0xf9},
	{0xab,0xff},
	{0x1e,0x01},
	{0x04,0x01},
	{0x1f,0x41},
	{0x1f,0xc1},
	{0x1f,0xd9},
	{0x1f,0xdf},
	{0x80,0x01},
	{0x97,0x01},
	{0x98,0xff},
	{0x99,0x01},
	{0x9a,0x09},
	{0x80,0x01},
	{0x97,0x01},
	{0x98,0xff},
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    
};


static struct microp_spi_table spi_deinit_table[] = {
	{0x99,0x00},
	{0x04,0x00},
	{0x1f,0xc1},
	{0x1f,0x00},
	{0x00,0x00},
	{0x80,0x00},
	{0x97,0x00},
	{0x98,0x08},
};
extern int micropklt_lcd_precess_spi_table(uint32_t, struct microp_spi_table*, size_t);
extern int micropklt_lcd_precess_cmd(char*, size_t);


static struct vreg *vreg_mddi_1v5;
static struct vreg *vreg_lcm_2v85;
extern int micropklt_set_misc_states( unsigned mask, unsigned bit_flag );
static void htcblackstone_mddi_power_client(struct msm_mddi_client_data *client_data,
				    int on)
{
	printk("htcblackstone_mddi_power_client(%d)\n", on);
}

static int htcblackstone_mddi_epson_panel_init(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{
	//struct msm_dex_command dex;
	int ret=0;
	char c0[]={MICROP_KLT_ID_SPICTRL, 0x01};
	//char con[2];
	printk("blackstone panel init\n");
	client_data->auto_hibernate(client_data, 0);
	/*
	mdelay(50);
	
	gpio_configure(RAPH100_LCD_PWR1, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_HIGH);
	gpio_set_value(RAPH100_LCD_PWR1, 1);
	gpio_configure(BLACK_LCD_PWR3, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
	gpio_set_value(BLACK_LCD_PWR3,1);
	
	msleep(5);
	dex.cmd=PCOM_PMIC_REG_ON;
	dex.has_data=1;
	dex.data=0x80;
	msm_proc_comm_wince(&dex,0);
	
	msleep(5);
	dex.data=0x2000;
	msm_proc_comm_wince(&dex,0);
	msleep(5);
	
	gpio_configure(RAPH100_LCD_PWR2, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_HIGH);
	gpio_set_value(RAPH100_LCD_PWR2, 1);
	msleep(10);
	micropklt_set_misc_states( 0xFF, 4 );
	*/
	
	if(!no_bkl_off) {
		msleep(50);
		micropklt_lcd_precess_cmd(c0, ARRAY_SIZE(c0));
		msleep(50);
		micropklt_lcd_precess_spi_table(MICROP_KLT_ID_SPILCMDDATA, spi_init_table, ARRAY_SIZE(spi_init_table));
		msleep(50);
	}
	
	client_data->auto_hibernate(client_data, 1);
	return ret;
  
}
static int htcblackstone_mddi_epson_panel_deinit(
					     struct msm_mddi_bridge_platform_data *bridge_data,
					      struct msm_mddi_client_data *client_data)
{
  	 int ret=0;
	// struct msm_dex_command dex;
	//char con[2];
	char c1[]={MICROP_KLT_ID_SPICTRL, 0x00};
	if(!no_bkl_off) {
		micropklt_lcd_precess_spi_table(MICROP_KLT_ID_SPILCMDDATA, spi_deinit_table, ARRAY_SIZE(spi_deinit_table));
		micropklt_lcd_precess_cmd(c1, ARRAY_SIZE(c1));
		printk("blackstone panel deinit\n");
		msleep(50);
	}
	client_data->auto_hibernate(client_data, 0);
	mdelay(2);
	
	/*
	msleep(50);
	gpio_configure(RAPH100_LCD_PWR2, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
	gpio_set_value(RAPH100_LCD_PWR2, 0);
	msleep(10);
	
	
	dex.cmd=PCOM_PMIC_REG_OFF;
	dex.has_data=1;
	dex.data=0x2000;
	msm_proc_comm_wince(&dex,0);
	msleep(5);
	dex.data=0x80;
	msm_proc_comm_wince(&dex,0);
	
	gpio_configure(BLACK_LCD_PWR3, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
	gpio_set_value(BLACK_LCD_PWR3, 0);
	gpio_configure(RAPH100_LCD_PWR1, GPIOF_DRIVE_OUTPUT | GPIOF_OUTPUT_LOW);
	gpio_set_value(RAPH100_LCD_PWR1, 0);
	*/
	client_data->auto_hibernate(client_data, 1);
	 return ret;
}


static int htcblackstone_mddi_epson_client_init(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{



	return 0;
}

static int htcblackstone_mddi_epson_client_uninit(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{

	return 0;
}



static int htcblackstone_mddi_panel_unblank(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{

	int ret = 0;
	printk("init epson panel\n");
	htcblackstone_mddi_epson_panel_init(bridge_data,client_data);
	return ret;

}

static int htcblackstone_mddi_panel_blank(
	struct msm_mddi_bridge_platform_data *bridge_data,
	struct msm_mddi_client_data *client_data)
{
	int ret=0;
	printk("init epson blank\n");
	htcblackstone_mddi_epson_panel_deinit(bridge_data,client_data);
	return ret;
}


extern struct resource resources_msm_fb[];


static struct msm_mddi_bridge_platform_data epson_client_data = {
	.init = htcblackstone_mddi_epson_client_init,
	.uninit = htcblackstone_mddi_epson_client_uninit,
	.blank = htcblackstone_mddi_panel_blank,
	.unblank = htcblackstone_mddi_panel_unblank,
	.fb_data = {
		.xres = 480,
		.yres = 800, //orux
		.output_format = 0,
	},
};

static struct msm_mddi_platform_data mddi_pdata = {
	.clk_rate = 122880000,
	.power_client = htcblackstone_mddi_power_client,
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

int __init htcblackstone_init_panel(void)
{
	int rc;
	
	printk(KERN_INFO "%s: Initializing panel\n", __func__);

	if (!machine_is_htcblackstone() && !machine_is_htckovsky()) {
		printk(KERN_INFO "%s: panel does not apply to this device, aborted\n", __func__);
		return 0;
	}

	vreg_mddi_1v5 = vreg_get(0, "gp2");
	if (IS_ERR(vreg_mddi_1v5))
		return PTR_ERR(vreg_mddi_1v5);
	vreg_lcm_2v85 = vreg_get(0, "gp4");
	if (IS_ERR(vreg_lcm_2v85))
		return PTR_ERR(vreg_lcm_2v85);

#if 0
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
#endif

	rc = platform_device_register(&msm_device_mdp);
	if (rc)
		return rc;
	msm_device_mddi0.dev.platform_data = &mddi_pdata;
	return platform_device_register(&msm_device_mddi0);
}

device_initcall(htcblackstone_init_panel);
