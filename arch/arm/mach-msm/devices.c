/* linux/arch/arm/mach-msm/devices.c
 *
 * Copyright (C) 2008 Google, Inc.
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
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <mach/msm_iomap.h>
#include <mach/dma.h>
#include <asm/mach-types.h>

#include <asm/mach/flash.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/usb/mass_storage_function.h>
#include <asm/mach/mmc.h>
#include <mach/msm_hsusb.h>
#include <asm/setup.h>

#include "devices.h"
#include "clock.h"
#include "proc_comm.h"

static struct resource resources_uart1[] = {
	{
		.start	= INT_UART1,
		.end	= INT_UART1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART1_PHYS,
		.end	= MSM_UART1_PHYS + MSM_UART1_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource resources_uart2[] = {
	{
		.start	= INT_UART2,
		.end	= INT_UART2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART2_PHYS,
		.end	= MSM_UART2_PHYS + MSM_UART2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource resources_uart3[] = {
	{
		.start	= INT_UART3,
		.end	= INT_UART3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART3_PHYS,
		.end	= MSM_UART3_PHYS + MSM_UART3_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device msm_device_uart1 = {
	.name	= "msm_serial",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_uart1),
	.resource	= resources_uart1,
};

struct platform_device msm_device_uart2 = {
	.name	= "msm_serial",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_uart2),
	.resource	= resources_uart2,
};

struct platform_device msm_device_uart3 = {
	.name	= "msm_serial",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_uart3),
	.resource	= resources_uart3,
};

static struct resource msm_uart1_dm_resources[] = {
	{
		.start = MSM_UART1DM_PHYS,
		.end   = MSM_UART1DM_PHYS + PAGE_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = INT_UART1DM_IRQ,
		.end   = INT_UART1DM_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = INT_UART1DM_RX,
		.end   = INT_UART1DM_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = DMOV_HSUART1_TX_CHAN,
		.end   = DMOV_HSUART1_RX_CHAN,
		.name  = "uartdm_channels",
		.flags = IORESOURCE_DMA,
	},
	{
		.start = DMOV_HSUART1_TX_CRCI,
		.end   = DMOV_HSUART1_RX_CRCI,
		.name  = "uartdm_crci",
		.flags = IORESOURCE_DMA,
	},
};

static u64 msm_uart_dm1_dma_mask = DMA_BIT_MASK(32);

struct platform_device msm_device_uart_dm1 = {
	.name = "msm_serial_hs",
	.id = 0,
	.num_resources = ARRAY_SIZE(msm_uart1_dm_resources),
	.resource = msm_uart1_dm_resources,
	.dev		= {
		.dma_mask = &msm_uart_dm1_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource msm_uart2_dm_resources[] = {
	{
		.start = MSM_UART2DM_PHYS,
		.end   = MSM_UART2DM_PHYS + PAGE_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = INT_UART2DM_IRQ,
		.end   = INT_UART2DM_IRQ,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = INT_UART2DM_RX,
		.end   = INT_UART2DM_RX,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = DMOV_HSUART2_TX_CHAN,
		.end   = DMOV_HSUART2_RX_CHAN,
		.name  = "uartdm_channels",
		.flags = IORESOURCE_DMA,
	},
	{
		.start = DMOV_HSUART2_TX_CRCI,
		.end   = DMOV_HSUART2_RX_CRCI,
		.name  = "uartdm_crci",
		.flags = IORESOURCE_DMA,
	},
};

static u64 msm_uart_dm2_dma_mask = DMA_BIT_MASK(32);

struct platform_device msm_device_uart_dm2 = {
	.name = "msm_serial_hs",
	.id = 1,
	.num_resources = ARRAY_SIZE(msm_uart2_dm_resources),
	.resource = msm_uart2_dm_resources,
	.dev		= {
		.dma_mask = &msm_uart_dm2_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource resources_i2c[] = {
	{
		.start	= MSM_I2C_PHYS,
		.end	= MSM_I2C_PHYS + MSM_I2C_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_PWB_I2C,
		.end	= INT_PWB_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device msm_device_i2c = {
	.name		= "msm_i2c",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(resources_i2c),
	.resource	= resources_i2c,
};

#define GPIO_I2C_CLK 60
#define GPIO_I2C_DAT 61
void msm_set_i2c_mux(bool gpio, int *gpio_clk, int *gpio_dat)
{
#if !defined(CONFIG_MSM_AMSS_VERSION_WINCE)
	unsigned id;
	if (gpio) {
		id = PCOM_GPIO_CFG(GPIO_I2C_CLK, 0, GPIO_OUTPUT,
				   GPIO_NO_PULL, GPIO_2MA);
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &id, 0);
		id = PCOM_GPIO_CFG(GPIO_I2C_DAT, 0, GPIO_OUTPUT,
				   GPIO_NO_PULL, GPIO_2MA);
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &id, 0);
		*gpio_clk = GPIO_I2C_CLK;
		*gpio_dat = GPIO_I2C_DAT;
	} else {
		id = PCOM_GPIO_CFG(GPIO_I2C_CLK, 1, GPIO_INPUT,
				   GPIO_NO_PULL, GPIO_2MA);
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &id, 0);
		id = PCOM_GPIO_CFG(GPIO_I2C_DAT , 1, GPIO_INPUT,
				   GPIO_NO_PULL, GPIO_2MA);
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &id, 0);
	}
#endif
}

struct flash_platform_data msm_nand_data = {
	.parts		= NULL,
	.nr_parts	= 0,
};

static struct resource resources_nand[] = {
	[0] = {
		.start	= 7,
		.end	= 7,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device msm_device_nand = {
	.name		= "msm_nand",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_nand),
	.resource	= resources_nand,
	.dev		= {
		.platform_data	= &msm_nand_data,
	},
};

/* adjust eye diagram, disable vbusvalid interrupts */
static int hsusb_phy_init_seq[] = { 0x40, 0x31, 0x1, 0x0D, 0x1, 0x10, -1 };

static char *usb_functions[] = {
#if defined(CONFIG_USB_FUNCTION_ETHER)
	"ether",
#endif
#if defined(CONFIG_USB_FUNCTION_MASS_STORAGE) 
	"usb_mass_storage",
#endif
#if defined(CONFIG_USB_FUNCTION_UMS)
	"ums",
#endif
#if defined(CONFIG_USB_FUNCTION_ADB)
	"adb",
#endif
#if defined(CONFIG_USB_FUNCTION_FSYNC)
	"fsync",
#endif
#if defined(CONFIG_USB_FUNCTION_DIAG)
	"diag",
#endif
#if defined(CONFIG_USB_FUNCTION_SERIAL)
	"fserial",
#endif
#if defined(CONFIG_USB_FUNCTION_PROJECTOR)
	"projector",
#endif
#if defined(CONFIG_USB_FUNCTION_MTP_TUNNEL)
	"mtp_tunnel",
#endif

};

/* about .functions variable, please refer: drivers/usb/function/usb_function.h  */
static struct msm_hsusb_product usb_products[] = {
	{
		.product_id	= 0x0c01,
		.functions	= 0x00000001, /* usb_mass_storage */
	},
	{
		.product_id	= 0x0c02,
		.functions	= 0x00000003, /* usb_mass_storage + adb */
	},
	{
		.product_id	= 0x0c03,
		.functions	= 0x00000011, /* fserial + mass_storage */
	},
	{
		.product_id	= 0x0c04,
		.functions	= 0x00000013, /* fserial + adb + mass_storage */
	},
	{
		.product_id = 0x0c05,
		.functions	= 0x00000021, /* Projector + mass_storage */
	},
	{
		.product_id = 0x0c06,
		.functions	= 0x00000023, /* Projector + adb + mass_storage */
	},
	{
		.product_id	= 0x0c07,
		.functions	= 0x0000000B, /* diag + adb + mass_storage */
	},
	{
		.product_id = 0x0c08,
		.functions	= 0x00000009, /* diag + mass_storage */
	},
	{
		.product_id = 0x0FFE,
		.functions	= 0x00000004, /* internet sharing */
	},

};

struct msm_hsusb_platform_data msm_hsusb_pdata = {
	.phy_init_seq = hsusb_phy_init_seq,
	.vendor_id = 0x0bb4,
	.product_id = 0x0c01,
	.version = 0x0100,
	.product_name = "Smartphone",
	.manufacturer_name = "HTC",

	.functions = usb_functions,
	.num_functions = ARRAY_SIZE(usb_functions),
	.products = usb_products,
	.num_products = ARRAY_SIZE(usb_products),
};

static struct resource resources_hsusb[] = {
	{
		.start	= MSM_HSUSB_PHYS,
		.end	= MSM_HSUSB_PHYS + MSM_HSUSB_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_HS,
		.end	= INT_USB_HS,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device msm_device_hsusb = {
	.name		= "msm_hsusb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_hsusb),
	.resource	= resources_hsusb,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
		.platform_data = &msm_hsusb_pdata,
	},
};

#ifdef CONFIG_USB_FUNCTION_MASS_STORAGE
static struct usb_mass_storage_platform_data mass_storage_pdata = {
	.nluns = 1,
	.buf_size = 16384,
	.vendor = "HTC     ",
	.product = "Android Phone   ",
	.release = 0x0100,
};

static struct platform_device msm_device_usb_mass_storage = {
	.name = "usb_mass_storage",
	.id = -1,
	.dev = {
		.platform_data = &mass_storage_pdata,
	},
};
#endif
void __init msm_add_usb_devices(void (*phy_reset) (void), void (*phy_shutdown) (void), int *init_seq)
{
	// get size of init_seq
	int size=0;
	while (!(init_seq[size++]<0));

	if (phy_reset)
		msm_hsusb_pdata.phy_reset = phy_reset;

	if (phy_shutdown)
		msm_hsusb_pdata.phy_shutdown = phy_shutdown;
	if(init_seq)
		memcpy(msm_hsusb_pdata.phy_init_seq, init_seq, size*sizeof(int));

	msm_hsusb_pdata.serial_number="000000000000";
	platform_device_register(&msm_device_hsusb);
#ifdef CONFIG_USB_FUNCTION_MASS_STORAGE
	platform_device_register(&msm_device_usb_mass_storage);
#endif
}

struct platform_device msm_device_smd = {
	.name	= "msm_smd",
	.id	= -1,
};

static struct resource resources_sdc1[] = {
	{
		.start	= MSM_SDC1_PHYS,
		.end	= MSM_SDC1_PHYS + MSM_SDC1_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_SDC1_0,
		.end	= INT_SDC1_0,
		.flags	= IORESOURCE_IRQ,
		.name	= "cmd_irq",
	},
	{
		.start	= INT_SDC1_1,
		.end	= INT_SDC1_1,
		.flags	= IORESOURCE_IRQ,
		.name	= "pio_irq",
	},
	{
		.flags	= IORESOURCE_IRQ | IORESOURCE_DISABLED,
		.name	= "status_irq"
	},
	{
		.start	= DMOV_SDC1_CHAN,
		.end	= DMOV_SDC1_CHAN,
		.flags	= IORESOURCE_DMA,
	},
};

static struct resource resources_sdc2[] = {
	{
		.start	= MSM_SDC2_PHYS,
		.end	= MSM_SDC2_PHYS + MSM_SDC2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_SDC2_0,
		.end	= INT_SDC2_0,
		.flags	= IORESOURCE_IRQ,
		.name	= "cmd_irq",
	},
		{
		.start	= INT_SDC2_1,
		.end	= INT_SDC2_1,
		.flags	= IORESOURCE_IRQ,
		.name	= "pio_irq",
	},
	{
		.flags	= IORESOURCE_IRQ | IORESOURCE_DISABLED,
		.name	= "status_irq"
	},
	{
		.start	= DMOV_SDC2_CHAN,
		.end	= DMOV_SDC2_CHAN,
		.flags	= IORESOURCE_DMA,
	},
};

static struct resource resources_sdc3[] = {
	{
		.start	= MSM_SDC3_PHYS,
		.end	= MSM_SDC3_PHYS + MSM_SDC3_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_SDC3_0,
		.end	= INT_SDC3_0,
		.flags	= IORESOURCE_IRQ,
		.name	= "cmd_irq",
	},
		{
		.start	= INT_SDC3_1,
		.end	= INT_SDC3_1,
		.flags	= IORESOURCE_IRQ,
		.name	= "pio_irq",
	},
	{
		.flags	= IORESOURCE_IRQ | IORESOURCE_DISABLED,
		.name	= "status_irq"
	},
	{
		.start	= DMOV_SDC3_CHAN,
		.end	= DMOV_SDC3_CHAN,
		.flags	= IORESOURCE_DMA,
	},
};

static struct resource resources_sdc4[] = {
	{
		.start	= MSM_SDC4_PHYS,
		.end	= MSM_SDC4_PHYS + MSM_SDC4_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_SDC4_0,
		.end	= INT_SDC4_0,
		.flags	= IORESOURCE_IRQ,
		.name	= "cmd_irq",
	},
		{
		.start	= INT_SDC4_1,
		.end	= INT_SDC4_1,
		.flags	= IORESOURCE_IRQ,
		.name	= "pio_irq",
	},
	{
		.flags	= IORESOURCE_IRQ | IORESOURCE_DISABLED,
		.name	= "status_irq"
	},
	{
		.start	= DMOV_SDC4_CHAN,
		.end	= DMOV_SDC4_CHAN,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device msm_device_sdc1 = {
	.name		= "msm_sdcc",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(resources_sdc1),
	.resource	= resources_sdc1,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

struct platform_device msm_device_sdc2 = {
	.name		= "msm_sdcc",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(resources_sdc2),
	.resource	= resources_sdc2,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

struct platform_device msm_device_sdc3 = {
	.name		= "msm_sdcc",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(resources_sdc3),
	.resource	= resources_sdc3,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

struct platform_device msm_device_sdc4 = {
	.name		= "msm_sdcc",
	.id		= 4,
	.num_resources	= ARRAY_SIZE(resources_sdc4),
	.resource	= resources_sdc4,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

static struct platform_device *msm_sdcc_devices[] __initdata = {
	&msm_device_sdc1,
	&msm_device_sdc2,
	&msm_device_sdc3,
	&msm_device_sdc4,
};

int __init msm_add_sdcc(unsigned int controller, struct mmc_platform_data *plat,
			unsigned int stat_irq, unsigned long stat_irq_flags)
{
	struct platform_device	*pdev;
	struct resource *res;

	if (controller < 1 || controller > 4)
		return -EINVAL;

	pdev = msm_sdcc_devices[controller-1];
	pdev->dev.platform_data = plat;

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "status_irq");
	if (!res)
		return -EINVAL;
	else if (stat_irq) {
		res->start = res->end = stat_irq;
		res->flags &= ~IORESOURCE_DISABLED;
		res->flags |= stat_irq_flags;
	}

	return platform_device_register(pdev);
}

static struct resource resources_mddi0[] = {
	{
		.start	= MSM_PMDH_PHYS,
		.end	= MSM_PMDH_PHYS + MSM_PMDH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_MDDI_PRI,
		.end	= INT_MDDI_PRI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource resources_mddi1[] = {
	{
		.start	= MSM_EMDH_PHYS,
		.end	= MSM_EMDH_PHYS + MSM_EMDH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_MDDI_EXT,
		.end	= INT_MDDI_EXT,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device msm_device_mddi0 = {
	.name = "msm_mddi",
	.id = 0,
	.num_resources = ARRAY_SIZE(resources_mddi0),
	.resource = resources_mddi0,
};

struct platform_device msm_device_mddi1 = {
	.name = "msm_mddi",
	.id = 1,
	.num_resources = ARRAY_SIZE(resources_mddi1),
	.resource = resources_mddi1,
};

static struct resource resources_mdp[] = {
	{
		.start	= MSM_MDP_PHYS,
		.end	= MSM_MDP_PHYS + MSM_MDP_SIZE - 1,
		.name	= "mdp",
		.flags	= IORESOURCE_MEM
	},
	{
		.start	= INT_MDP,
		.end	= INT_MDP,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device msm_device_mdp = {
	.name = "msm_mdp",
	.id = 0,
	.num_resources = ARRAY_SIZE(resources_mdp),
	.resource = resources_mdp,
};

#define CLOCK(clk_name, clk_id, clk_dev, clk_flags, clk_arch) {	\
	.name = clk_name, \
	.id = clk_id, \
	.flags = (clk_flags) | ((clk_arch) & CLKFLAG_ARCH_ALL), \
	.dev = clk_dev, \
	}

#define CLK_ALL(name, id, dev, flags) \
		CLOCK(name, id, dev, flags, CLKFLAG_ARCH_ALL)
#define CLK_7X00A(name, id, dev, flags) \
		CLOCK(name, id, dev, flags, CLKFLAG_ARCH_MSM7X00A)

#define OFF CLKFLAG_AUTO_OFF
#define MINMAX (CLKFLAG_USE_MIN_TO_SET | CLKFLAG_USE_MAX_TO_SET)
#define USE_MIN CLKFLAG_USE_MIN_TO_SET

struct clk msm_clocks[] = {
	CLK_ALL("adm_clk", ADM_CLK, NULL, 0),
	CLK_ALL("adsp_clk", ADSP_CLK, NULL, 0),
	CLK_ALL("ebi1_clk", EBI1_CLK, NULL, USE_MIN),
	CLK_ALL("ebi2_clk", EBI2_CLK, NULL, 0),
	CLK_ALL("ecodec_clk", ECODEC_CLK, NULL, 0),
	CLK_ALL("mddi_clk", EMDH_CLK, &msm_device_mddi1.dev, OFF),
	CLK_ALL("gp_clk", GP_CLK, NULL, 0),
	CLK_ALL("grp_clk", GRP_CLK, NULL, OFF),
	CLK_ALL("i2c_clk", I2C_CLK, &msm_device_i2c.dev, 0),
	CLK_ALL("icodec_rx_clk", ICODEC_RX_CLK, NULL, 0),
	CLK_ALL("icodec_tx_clk", ICODEC_TX_CLK, NULL, 0),
	CLK_ALL("imem_clk", IMEM_CLK, NULL, OFF),
	CLK_ALL("mdc_clk", MDC_CLK, NULL, 0),
	CLK_ALL("mdp_clk", MDP_CLK, &msm_device_mdp.dev, OFF),
	CLK_ALL("pbus_clk", PBUS_CLK, NULL, 0),
	CLK_ALL("pcm_clk", PCM_CLK, NULL, 0),
	CLK_ALL("mddi_clk", PMDH_CLK, &msm_device_mddi0.dev, OFF | MINMAX),
	CLK_ALL("sdac_clk", SDAC_CLK, NULL, OFF),
	CLK_ALL("sdc_clk", SDC1_CLK, &msm_device_sdc1.dev, OFF),
	CLK_ALL("sdc_pclk", SDC1_PCLK, &msm_device_sdc1.dev, OFF),
	CLK_ALL("sdc_clk", SDC2_CLK, &msm_device_sdc2.dev, OFF),
	CLK_ALL("sdc_pclk", SDC2_PCLK, &msm_device_sdc2.dev, OFF),
	CLK_ALL("sdc_clk", SDC3_CLK, &msm_device_sdc3.dev, OFF),
	CLK_ALL("sdc_pclk", SDC3_PCLK, &msm_device_sdc3.dev, OFF),
	CLK_ALL("sdc_clk", SDC4_CLK, &msm_device_sdc4.dev, OFF),
	CLK_ALL("sdc_pclk", SDC4_PCLK, &msm_device_sdc4.dev, OFF),
	CLK_ALL("tsif_clk", TSIF_CLK, NULL, 0),
	CLK_ALL("tsif_ref_clk", TSIF_REF_CLK, NULL, 0),
	CLK_ALL("tv_dac_clk", TV_DAC_CLK, NULL, 0),
	CLK_ALL("tv_enc_clk", TV_ENC_CLK, NULL, 0),
	CLK_ALL("uart_clk", UART1_CLK, &msm_device_uart1.dev, OFF),
//	CLK_ALL("uart_clk", UART2_CLK, &msm_device_uart2.dev, 0),
	CLK_ALL("uart_clk", UART2_CLK, &msm_device_uart2.dev, OFF),
	CLK_ALL("uart_clk", UART3_CLK, &msm_device_uart3.dev, OFF),
//	CLK_ALL("uartdm_clk", UART1DM_CLK, &msm_device_uart_dm1.dev, OFF),
//	CLK_ALL("uartdm_clk", UART2DM_CLK, &msm_device_uart_dm2.dev, 0),
	CLK_ALL("uartdm_clk", UART1DM_CLK, &msm_device_uart_dm1.dev, 0),
	CLK_ALL("uartdm_clk", UART2DM_CLK, &msm_device_uart_dm2.dev, OFF),
	CLK_ALL("usb_hs_clk", USB_HS_CLK, &msm_device_hsusb.dev, OFF),
	CLK_ALL("usb_hs_pclk", USB_HS_PCLK, &msm_device_hsusb.dev, OFF),
	CLK_ALL("usb_otg_clk", USB_OTG_CLK, NULL, 0),
	CLK_ALL("vdc_clk", VDC_CLK, NULL, OFF | MINMAX),
	CLK_ALL("vfe_clk", VFE_CLK, NULL, OFF),
	CLK_ALL("vfe_mdc_clk", VFE_MDC_CLK, NULL, OFF),

	CLOCK(NULL, 0, NULL, 0, 0),
};

struct platform_device msm_device_rtc = {
	.name = "msm_rtc",
	.id = -1,
};

struct platform_device msm_device_htc_hw = {
        .name = "htc_hw",
        .id = -1,
};

struct platform_device msm_device_htc_battery = {
        .name = "htc_battery",
        .id = -1,
};

// Our tree is a mixture of many it seems. Source-wise this should
// go into devices_htc.c but then it would have to be built, too.
// On the opposite, the desirec source only has devices.c.
#define ATAG_MONODIE 0x4d534D76
static int mono_die;
int __init parse_tag_monodie(const struct tag *tags)
{
#if 0
	struct tag *t = (struct tag *)tags;
	int find = 0;
	for (; t->hdr.size; t = tag_next(t)) {
		if (t->hdr.tag == ATAG_MONODIE) {
			printk(KERN_DEBUG "find the flash id tag\n");
			find = 1;
			break;
		}
	}

	if (find)
		mono_die = t->u.revision.rev;
	printk(KERN_DEBUG "parse_tag_monodie: mono-die = 0x%x\n", mono_die);
	return mono_die;
#endif
	int skuid_pcba;

	// restrict to htctopaz for now
	if (!machine_is_htctopaz())
		return mono_die;

	/* Dynamic memory die detection.
	 * We can't use:
	 * - HaRET's passed ATAG_MEM as it is just one entry and too
	 *   uninformal as of (2010-08-29) (option would be to locate and
	 *   and parse OEMAddressTable dynamically
	 * - SMEM PCB XC (base+0xfc048) or PCB ID (base+0xfc0ef >> 24)
	 *   as all values are identical for dualdie and monodie
	 *   pcb_xc 0x1ffc048=0x00000001
	 *   pcb_id 0x1ffc0ef=0x00000000
	 * We use:
	 * - SKUID of PCBA at phys 0x081C00
	 *   dualdie: 0x050001ed
	 *   monodie: 0x190001ed
	 */
	skuid_pcba = readl(0x00081c00);

	printk(KERN_DEBUG "%s: SKUID_PCBA=0x%08x\n", __func__, skuid_pcba);

	mono_die = skuid_pcba == 0x190001ed;

	printk(KERN_DEBUG "%s: mono-die = 0x%x\n", __func__, mono_die);
	return mono_die;
}
__tagtable(ATAG_MONODIE, parse_tag_monodie);

int __init board_mcp_monodie(void)
{
	return mono_die;
}
