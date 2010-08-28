/* drivers/video/msm_fb/mddi_client_epson.c
 *
 * Support for epson TC358720XBG mddi client devices which require no
 * special initialization code.
 *
 * Copyright (C) 2007 Google Incorporated
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <mach/msm_fb.h>
#include <asm/mach-types.h>

#include <linux/delay.h>

#if defined(CONFIG_MACH_HTCBLACKSTONE) || defined(CONFIG_MACH_HTCTOPAZ) || defined(CONFIG_MACH_HTCRHODIUM) || defined(CONFIG_MACH_HTCKOVSKY)
#define LCD_CONTROL_BLOCK_BASE 0x110000
#define CMN         (LCD_CONTROL_BLOCK_BASE|0x10)
#define INTFLG      (LCD_CONTROL_BLOCK_BASE|0x18)
#define HCYCLE      (LCD_CONTROL_BLOCK_BASE|0x34)
#define HDE_START   (LCD_CONTROL_BLOCK_BASE|0x3C)
#define VPOS        (LCD_CONTROL_BLOCK_BASE|0xC0)
#define MPLFBUF     (LCD_CONTROL_BLOCK_BASE|0x20)
#define WAKEUP      (LCD_CONTROL_BLOCK_BASE|0x54)
#define WSYN_DLY    (LCD_CONTROL_BLOCK_BASE|0x58)
#define REGENB      (LCD_CONTROL_BLOCK_BASE|0x5C)

#define BASE5 0x150000
#define BASE6 0x160000
#define BASE7 0x170000

#define GPIOIEV     (BASE5 + 0x10)
#define GPIOIE      (BASE5 + 0x14)
#define GPIORIS     (BASE5 + 0x18)
#define GPIOMIS     (BASE5 + 0x1C)
#define GPIOIC      (BASE5 + 0x20)

#define INTMASK     (BASE6 + 0x0C)
#define INTMASK_VWAKEOUT (1U << 0)
#define INTMASK_VWAKEOUT_ACTIVE_LOW (1U << 8)
#define GPIOSEL     (BASE7 + 0x00)
#define GPIOSEL_VWAKEINT (1U << 0)

#endif

static DECLARE_WAIT_QUEUE_HEAD(epson_vsync_wait);

struct panel_info {
	struct msm_mddi_client_data *client_data;
	struct platform_device pdev;
	struct msm_panel_data panel_data;
	struct msmfb_callback *epson_callback;
	int epson_got_int;
};
static int setup_vsync(struct panel_info *panel, int init);


static void epson_request_vsync(struct msm_panel_data *panel_data,
				  struct msmfb_callback *callback)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	panel->epson_callback = callback;
	if (panel->epson_got_int) {
		panel->epson_got_int = 0;
		client_data->activate_link(client_data);
	}
}

static void epson_clear_vsync(struct msm_panel_data *panel_data) {
	struct panel_info *panel = container_of(panel_data, struct panel_info,
			panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;
	client_data->activate_link(client_data);
}

static void epson_wait_vsync(struct msm_panel_data *panel_data)
{
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	if (panel->epson_got_int) {
		panel->epson_got_int = 0;
		client_data->activate_link(client_data); /* clears interrupt */
	}
	if (wait_event_timeout(epson_vsync_wait, panel->epson_got_int,
				HZ/2) == 0)
		printk(KERN_ERR "timeout waiting for VSYNC\n");
	panel->epson_got_int = 0;
	/* interrupt clears when screen dma starts */
}

static int epson_suspend(struct msm_panel_data *panel_data)
{
printk("epson_suspend\n");return 0;
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;
	int ret;

	ret = bridge_data->uninit(bridge_data, client_data);
	if (ret) {
		printk(KERN_INFO "mddi epson client: non zero return from "
			"uninit\n");
		return ret;
	}
	client_data->suspend(client_data);
	setup_vsync(panel, 0);
	return 0;
}

static int epson_resume(struct msm_panel_data *panel_data)
{
printk("epson_resume\n");return 0;
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;

	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;
	int ret;

	setup_vsync(panel, 1);
	client_data->resume(client_data);
	ret = bridge_data->init(bridge_data, client_data);
	if (ret)
		return ret;
	return 0;
}

static int epson_blank(struct msm_panel_data *panel_data)
{
printk("epson_blank\n");

	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;
	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;

	return bridge_data->blank(bridge_data, client_data);
}

static int epson_unblank(struct msm_panel_data *panel_data)
{
printk("epson_unblank\n");
	struct panel_info *panel = container_of(panel_data, struct panel_info,
						panel_data);
	struct msm_mddi_client_data *client_data = panel->client_data;
	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;

	return bridge_data->unblank(bridge_data, client_data);
}

irqreturn_t epson_vsync_interrupt(int irq, void *data)
{
	struct panel_info *panel = data;

	panel->epson_got_int = 1;
	if (panel->epson_callback) {
		panel->epson_callback->func(panel->epson_callback);
		panel->epson_callback = 0;
	}
	wake_up(&epson_vsync_wait);
	return IRQ_HANDLED;
}

static int setup_vsync(struct panel_info *panel,
		       int init)
{
	printk("setup_vsync\n");
	int ret;
	int gpio = 97;
	unsigned int irq;

	if (!init) {
		ret = 0;
		goto uninit;
	}
	ret = gpio_request(gpio, "vsync");
	if (ret)
		goto err_request_gpio_failed;

	ret = gpio_direction_input(gpio);
	if (ret)
		goto err_gpio_direction_input_failed;

	ret = irq = gpio_to_irq(gpio);
	if (ret < 0)
		goto err_get_irq_num_failed;

	ret = request_irq(irq, epson_vsync_interrupt, IRQF_TRIGGER_RISING,
			  "vsync", panel);
	if (ret)
		goto err_request_irq_failed;
	printk(KERN_INFO "vsync on gpio %d now %d\n",
	       gpio, gpio_get_value(gpio));
	return 0;

uninit:
	free_irq(gpio_to_irq(gpio), panel);
err_request_irq_failed:
err_get_irq_num_failed:
err_gpio_direction_input_failed:
	gpio_free(gpio);
err_request_gpio_failed:
printk("XXX: vsync on panel failed!\n");
	return ret;
}

static int mddi_epson_probe(struct platform_device *pdev)
{
printk("mddi_epson_probe\n");

	int ret;
	struct msm_mddi_client_data *client_data = pdev->dev.platform_data;
	struct msm_mddi_bridge_platform_data *bridge_data =
		client_data->private_client_data;
	struct panel_info *panel =
		kzalloc(sizeof(struct panel_info), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;
	platform_set_drvdata(pdev, panel);


#if defined(CONFIG_MACH_HTCBLACKSTONE) || defined(CONFIG_MACH_HTCKOVSKY)
       /* mddi_remote_write(mddi, 0, WAKEUP); */
	if(machine_is_htcblackstone()) {
	       client_data->remote_write(client_data, GPIOSEL_VWAKEINT, GPIOSEL);
	       client_data->remote_write(client_data, INTMASK_VWAKEOUT, INTMASK);

		client_data->remote_write(client_data,0x8000,0x400);//rgb565 input
		client_data->remote_write(client_data,0x8000,0x680);//main memory rgb565
		client_data->remote_write(client_data,0x4000,0x600);//reset
	}
#endif	//ORUX

	ret = setup_vsync(panel, 1);
	if (ret) {
		dev_err(&pdev->dev, "mddi_bridge_setup_vsync failed\n");
		return ret;
	}

	panel->client_data = client_data;
	panel->panel_data.suspend = epson_suspend;
	panel->panel_data.resume = epson_resume;
	panel->panel_data.wait_vsync = epson_wait_vsync;
	panel->panel_data.request_vsync = epson_request_vsync;
	panel->panel_data.clear_vsync = epson_clear_vsync;
	panel->panel_data.blank = epson_blank;
	panel->panel_data.unblank = epson_unblank;
	panel->panel_data.fb_data =  &bridge_data->fb_data;

	if(machine_is_htcblackstone() || machine_is_htckovsky()) {
		panel->panel_data.fb_data->width = 480;
		panel->panel_data.fb_data->height = 800;
	} else {
		panel->panel_data.fb_data->width = 480;
		panel->panel_data.fb_data->height = 640;
	}

	panel->pdev.name = "msm_panel";
	panel->pdev.id = pdev->id;
	panel->pdev.resource = client_data->fb_resource;
	panel->pdev.num_resources = 1;
	panel->pdev.dev.platform_data = &panel->panel_data;
	bridge_data->init(bridge_data, client_data);
	platform_device_register(&panel->pdev);

	return 0;
}

static int mddi_epson_remove(struct platform_device *pdev)
{
	struct panel_info *panel = platform_get_drvdata(pdev);

	setup_vsync(panel, 0);
	kfree(panel);
	return 0;
}

static struct platform_driver mddi_client_4ca3_0000 = {
	.probe = mddi_epson_probe,
	.remove = mddi_epson_remove,
	.driver = { .name = "S1D13774" },
};

static int __init mddi_client_epson_init(void)
{
	platform_driver_register(&mddi_client_4ca3_0000);
	return 0;
}

module_init(mddi_client_epson_init);
