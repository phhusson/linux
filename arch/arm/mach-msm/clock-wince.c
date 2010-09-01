/* arch/arm/mach-msm/clock.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007 QUALCOMM Incorporated
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <mach/msm_rpcrouter.h>

#include <mach/msm_iomap.h>
#include <mach/amss_para.h>
#include <asm/io.h>

#include "clock.h"
#include "proc_comm.h"
//#include "proc_comm_wince.h"


static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clocks_lock);
static LIST_HEAD(clocks);

static int pc_clk_is_enabled(uint32_t id);
static int pc_clk_enable(uint32_t id);
static void pc_clk_disable(uint32_t id);

enum {
	DEBUG_UNKNOWN_ID	= 1<<0,
	DEBUG_UNKNOWN_FREQ	= 1<<1,
	DEBUG_MDNS		= 1<<2,
	DEBUG_UNKNOWN_CMD	= 1<<3,
};
static int debug_mask=0;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#if 1
#define D(x...) printk(KERN_DEBUG "clock-wince: " x)
#else
#define D(x...) do {} while (0)
#endif

struct mdns_clock_params
{
	unsigned long freq;
	uint32_t calc_freq;
	uint32_t md;
	uint32_t ns;
	uint32_t pll_freq;
	uint32_t clk_id;
};

struct msm_clock_params
{
	unsigned clk_id;
	unsigned idx;
	unsigned offset;  // Offset points to .ns register
//	unsigned ns_only; // value to fill in ns register, rather than using mdns_clock_params look-up table
	char	*name;
};

static int max_clk_rate[NR_CLKS], min_clk_rate[NR_CLKS];

#define PLLn_BASE(n)		(MSM_CLK_CTL_BASE + 0x300 + 28 * (n))
#define TCX0			19200000 // Hz
#define PLL_FREQ(l, m, n)	(TCX0 * (l) + TCX0 * (m) / (n))

static unsigned int pll_get_rate(int n)
{
	unsigned int mode, L, M, N, freq;

 if (n == -1) return TCX0;
 if (n > 3)
  return 0;
 else
 {
	mode = readl(PLLn_BASE(n) + 0x0);
	L = readl(PLLn_BASE(n) + 0x4);
	M = readl(PLLn_BASE(n) + 0x8);
	N = readl(PLLn_BASE(n) + 0xc);
	freq = PLL_FREQ(L, M, N);
	printk(KERN_INFO "PLL%d: MODE=%08x L=%08x M=%08x N=%08x freq=%u Hz (%u MHz)\n",
		n, mode, L, M, N, freq, freq / 1000000); \
 }

 return freq;
}

static unsigned int idx2pll(uint32_t idx)
{
 int ret;

 switch(idx)
 {
  case 0: /* TCX0 */
   ret=-1;
  break;
  case 1: /* PLL1 */
   ret=1;
  break;
  case 4: /* PLL0 */
   ret=0;
  break;
  default:
   ret=4; /* invalid */
 }

 return ret;
}

static struct msm_clock_params msm_clock_parameters_6125[] = {

	//{ .clk_id = ACPU_CLK,							.name="ACPU_CLK",},		
	{ .clk_id = ADM_CLK,		.idx =  5,				.name="ADM_CLK",},
	{ .clk_id = ADSP_CLK,				.offset = 0x34,		.name="ADSP_CLK",},
	{ .clk_id = EBI1_CLK,		.idx =  13,	.offset = 0x28,		.name="EBI2_CLK",},
	{ .clk_id = EBI2_CLK,		.idx =  12,	.offset = 0x2C,		.name="EBI2_CLK",},
	{ .clk_id = ECODEC_CLK,				.offset = 0x4C,		.name="ECODEC_CLK",},
	{ .clk_id = EMDH_CLK,			   	.offset = 0x50,		.name="EMDH_CLK"},
	{ .clk_id = GP_CLK,			   	.offset = 0x5c,		.name="GP_CLK" },
	{ .clk_id = GRP_CLK,		.idx = 3,  	.offset = 0x84,		.name="GRP_CLK", },
	{ .clk_id = I2C_CLK,			   	.offset = 0x68,		.name="I2C_CLK"},
	{ .clk_id = ICODEC_RX_CLK,			.offset = 0x70,		.name="ICODEC_RX_CLK"},
	{ .clk_id = ICODEC_TX_CLK,			.offset = 0x78,		.name="ICODEC_RX_CLK"},
	{ .clk_id = IMEM_CLK,		.idx = 3,  	.offset = 0x84,		.name="IMEM_CLK", },
	{ .clk_id = MDC_CLK,			   	.offset = 0x7C,		.name="MDC_CLK", },
	{ .clk_id = MDP_CLK,		.idx = 9,				.name="MDP_CLK",},
	{ .clk_id = PBUS_CLK,		.idx = 14,				.name="PBUS_CLK", },
	{ .clk_id = PCM_CLK,				.offset = 0x88,		.name="PCM_CLK", },
	{ .clk_id = PMDH_CLK,			   	.offset = 0x8c,		.name="PMDH_CLK"},
	{ .clk_id = SDAC_CLK,			   	.offset = 0x9C,		.name="SDAC_CLK"},

	{ .clk_id = SDC1_CLK,			 	.offset = 0xa4,		.name="SDC1_CLK",},
	{ .clk_id = SDC1_PCLK,		.idx =  7, 				.name="SDC1_PCLK",},
 	{ .clk_id = SDC2_CLK,			 	.offset = 0xac,		.name="SDC2_CLK",},
	{ .clk_id = SDC2_PCLK,		.idx =  8, 				.name="SDC2_PCLK",},
	{ .clk_id = SDC3_CLK,			 	.offset = 0xb4,		.name="SDC3_CLK",},
	{ .clk_id = SDC3_PCLK,		.idx = 27, 				.name="SDC3_PCLK",},
	{ .clk_id = SDC4_CLK,			 	.offset = 0xbc,		.name="SDC4_CLK",},
	{ .clk_id = SDC4_PCLK,		.idx = 28, 				.name="SDC4_PCLK",},


	
	{ .clk_id = TSIF_CLK,		.idx = 18,	.offset = 0xc4,		.name="TSIF_CLK",},
	{ .clk_id = TSIF_REF_CLK,	.idx = 18,	.offset = 0xc4,		.name="TSIF_REF_CLK",},
	{ .clk_id = TV_DAC_CLK,				.offset = 0xcc,		.name="TV_DAC_CLK",},
	{ .clk_id = TV_ENC_CLK,				.offset = 0xcc,		.name="TV_ENC_CLK",},
	{ .clk_id = UART1_CLK,				.offset = 0xe0,		.name="UART1",},
	{ .clk_id = UART2_CLK,				.offset = 0xe0,		.name="UART2",},
	{ .clk_id = UART3_CLK,				.offset = 0xe0,		.name="UART3",},
	{ .clk_id = UART1DM_CLK,	.idx = 17, 	.offset = 0xd4,		.name="UART1DM_CLK",},
	{ .clk_id = UART2DM_CLK,	.idx = 26, 	.offset = 0xdc,		.name="UART2DM_CLK",},
	{ .clk_id = USB_HS_CLK,				.offset = 0x2c0,	.name="USB_HS_CLK",},
	{ .clk_id = USB_HS_PCLK,	.idx = 25,				.name="USB_HS_PCLK",},
	//{ .clk_id = USB_OTG_CLK,						.name="USB_HS_PCLK",},
//	{ .clk_id = VDC_CLK,			   	.offset = 0xF0,		.name="VDC_CLK"},
//	{ .clk_id = VFE_CLK, 				.offset = 0x44,		.name="VFE_CLK", },
//	{ .clk_id = VFE_MDC_CLK, 			.offset = 0x44,		.name="VFE_MDC_CLK", },
};
static struct msm_clock_params* msm_clock_parameters;

void fix_mddi_clk_black(void) {
}


// This formula is used to generate md and ns reg values
#define MSM_CLOCK_REG(frequency,M,N,D,PRE,a5,SRC,MNE,pll_frequency) { \
	.freq = (frequency), \
	.md = ((0xffff & (M)) << 16) | (0xffff & ~((D) << 1)), \
	.ns = ((0xffff & ~((N) - (M))) << 16) \
	    | ((0xff & (0xa | (MNE))) << 8) \
	    | ((0x7 & (a5)) << 5) \
	    | ((0x3 & (PRE)) << 3) \
	    | (0x7 & (SRC)), \
	.pll_freq = (pll_frequency), \
	.calc_freq = (pll_frequency*M/((PRE+1)*N)), \
}

static struct mdns_clock_params *msm_clock_freq_parameters;

// CDMA phones typically use a 196 MHz PLL0
struct mdns_clock_params msm_clock_freq_parameters_pll0_196[] = {

	MSM_CLOCK_REG(  144000,   3, 0x64, 0x32, 3, 3, 0, 1, 19200000), /* SD, 144kHz */
	MSM_CLOCK_REG( 7372800,   3, 0x50, 0x28, 0, 2, 4, 1, 196608000), /*  460800*16, will be divided by 4 for 115200 */
	MSM_CLOCK_REG(12000000,   1, 0x20, 0x10, 1, 3, 1, 1, 768000000), /* SD, 12MHz */
	MSM_CLOCK_REG(14745600,   3, 0x28, 0x14, 0, 2, 4, 1, 196608000), /* BT, 921600 (*16)*/
	MSM_CLOCK_REG(19200000,   1, 0x0a, 0x05, 3, 3, 1, 1, 768000000), /* SD, 19.2MHz */
	MSM_CLOCK_REG(24000000,   1, 0x10, 0x08, 1, 3, 1, 1, 768000000), /* SD, 24MHz */
	MSM_CLOCK_REG(32000000,   1, 0x0c, 0x06, 1, 3, 1, 1, 768000000), /* SD, 32MHz */
	MSM_CLOCK_REG(58982400,   3, 0x0a, 0x05, 0, 2, 4, 1, 196608000), /* BT, 3686400 (*16) */
	MSM_CLOCK_REG(64000000,0x7d, 0x180, 0xC0, 0, 2, 4, 1, 196608000), /* BT, 4000000 (*16) */
	{0, 0, 0, 0, 0, 0},
};

// defines from MSM7500_Core.h

// often used defines
#define MSM_PRPH_WEB_NS_REG	( MSM_CLK_CTL_BASE+0x80 )
#define MSM_GRP_NS_REG				( MSM_CLK_CTL_BASE+0x84 )
#define MSM_AXI_RESET 					( MSM_CLK_CTL_BASE+0x208 )
#define MSM_ROW_RESET 				( MSM_CLK_CTL_BASE+0x214 )
#define MSM_VDD_GRP_GFS_CTL	( MSM_CLK_CTL_BASE+0x284 )
#define MSM_VDD_VDC_GFS_CTL	( MSM_CLK_CTL_BASE+0x288 )
#define MSM_RAIL_CLAMP_IO			( MSM_CLK_CTL_BASE+0x290 )

#define REG_OR( reg, value ) do { u32 i = readl( (reg) ); writel( i | (value), (reg) ); } while(0)
#define REG_AND( reg, value ) do {	u32 i = readl( reg ); writel( i & ~value, reg); } while(0)
#define REG_SET( reg, value ) do { writel( value, reg ); } while(0)

static void set_grp_clk( int on ) {
	int timeout=100;
	if ( on != 0 ) {
		REG_OR( MSM_AXI_RESET, 0x20 );
		REG_OR( MSM_ROW_RESET, 0x20000 );
		REG_SET( MSM_VDD_GRP_GFS_CTL, 0x11f );
		mdelay( 20 );// very rough delay

		REG_OR( MSM_GRP_NS_REG, 0x800 );
		REG_OR( MSM_GRP_NS_REG, 0x80 );
		REG_OR( MSM_GRP_NS_REG, 0x200 );
		REG_OR( MSM_CLK_CTL_BASE, 0x8 );					// grp idx

		REG_AND( MSM_RAIL_CLAMP_IO, 0x4 );
		REG_AND( MSM_PRPH_WEB_NS_REG, 0x1 );			// Suppress bit 0 of grp MD

		//writel(readl(MSM_AXI_BASE+0x10080) &~ 0x1,MSM_AXI_BASE+0x10080);
		REG_AND( MSM_AXI_RESET, 0x20 );
		REG_AND( MSM_ROW_RESET, 0x20000 );

		//void __iomem	*mmio;
	        //mmio = ioremap(0xa0010200, 0x0C);
		//D("0x%8.8x | 0x%8.8x 0x%8.8x \n",0xa0010200, readl(mmio),readl(mmio+0x04));
		//REG_OR(mmio, 0x40000);
		//iounmap(mmio);

	} else {
		REG_OR( MSM_GRP_NS_REG, 0x800 );
		REG_OR( MSM_GRP_NS_REG, 0x80 );
		REG_OR( MSM_GRP_NS_REG, 0x200 );

		REG_OR(  MSM_CLK_CTL_BASE, 0x8 );					// grp idx

		REG_OR( MSM_PRPH_WEB_NS_REG, 0x1 );			// grp MD
		//writel(readl(MSM_AXI_BASE+0x10080) | 0x1,MSM_AXI_BASE+0x10080);
		//while(((readl(MSM_AXI_BASE+0x10084) & 1)==0) && timeout--);

		int i = 0;
		int status = 0;
		while ( status == 0 && i < 100) {
			i++;
			status = readl( MSM_GRP_NS_REG ) & 0x1;
		}

		REG_OR( MSM_AXI_RESET, 0x20 );
		REG_OR( MSM_ROW_RESET, 0x20000 );
		REG_AND( MSM_GRP_NS_REG, 0x800 );
		REG_AND( MSM_GRP_NS_REG, 0x80 );
		REG_AND( MSM_GRP_NS_REG, 0x200 );
		REG_OR( MSM_RAIL_CLAMP_IO, 0x4 );					// grp clk ramp
		REG_SET( MSM_VDD_GRP_GFS_CTL, 0x1f );

		int control = readl( MSM_VDD_VDC_GFS_CTL );

		if ( control & 0x100 ) {
			REG_AND( MSM_CLK_CTL_BASE, 0x8 );				// grp idx
		}
	}
}

static inline struct msm_clock_params msm_clk_get_params(uint32_t id)
{
	int i;
	struct msm_clock_params empty = { };
	for (i = 0; i < ARRAY_SIZE(msm_clock_parameters_6125); i++) {
		if (id == msm_clock_parameters[i].clk_id) {
			return msm_clock_parameters[i];
		}
	}
	return empty;
}

static inline uint32_t msm_clk_enable_bit(uint32_t id)
{
	struct msm_clock_params params;
	params = msm_clk_get_params(id);
	if (!params.idx) return 0;
	return 1U << params.idx;
}

static inline unsigned msm_clk_reg_offset(uint32_t id)
{
	struct msm_clock_params params;
	params = msm_clk_get_params(id);
	return params.offset;
}

static int set_mdns_host_clock(uint32_t id, unsigned long freq)
{
	int n;
	unsigned offset;
	int retval;
	bool found, enabled;
	struct msm_clock_params params;
	uint32_t nsreg;
	found = 0;
	retval = -EINVAL;

	params = msm_clk_get_params(id);
	offset = params.offset;

        enabled = pc_clk_is_enabled(id);
        if (enabled)
                pc_clk_disable(id);

	if (offset >0)
	{
		n = 0;
		while (msm_clock_freq_parameters[n].freq) {
			n++;
		}

		for (n--; n >= 0; n--) {
			if (freq >= msm_clock_freq_parameters[n].freq) {
				// This clock requires MD and NS regs to set frequency:
				writel(msm_clock_freq_parameters[n].md, MSM_CLK_CTL_BASE + offset - 4);
				writel(msm_clock_freq_parameters[n].ns, MSM_CLK_CTL_BASE + offset);
				if(debug_mask&DEBUG_MDNS)
					D("%s: %u, freq=%lu calc_freq=%u pll%d=%u expected pll =%u\n", __func__, id,
				  msm_clock_freq_parameters[n].freq,
				  msm_clock_freq_parameters[n].calc_freq,
				  msm_clock_freq_parameters[n].ns&7,
				  pll_get_rate(idx2pll(msm_clock_freq_parameters[n].ns&7)),
				  msm_clock_freq_parameters[n].pll_freq );
				retval = 0;
				found = 1;
				break;
			}
		}
	}
	else printk(KERN_WARNING "%s: FIXME! How to mdns host clock %u ?\n", __func__, id);

        if (enabled) pc_clk_enable(id);
       return 0;
}

static unsigned long get_mdns_host_clock(uint32_t id)
{
	int n;
	unsigned offset;
	uint32_t mdreg;
	uint32_t nsreg;
	unsigned long freq = 0;

	offset = msm_clk_reg_offset(id);
	if (offset == 0)
		return -EINVAL;

	mdreg = readl(MSM_CLK_CTL_BASE + offset - 4);
	nsreg = readl(MSM_CLK_CTL_BASE + offset);

	n = 0;
	while (msm_clock_freq_parameters[n].freq) {
		if (msm_clock_freq_parameters[n].md == mdreg &&
			msm_clock_freq_parameters[n].ns == nsreg) {
			freq = msm_clock_freq_parameters[n].freq;
			break;
		}
		n++;
	}

	return freq;
}

#ifndef CONFIG_MSM_AMSS_VERSION_WINCE
static inline int pc_clk_enable(unsigned id)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_ENABLE, &id, NULL);
}

static inline void pc_clk_disable(unsigned id)
{
	msm_proc_comm(PCOM_CLKCTL_RPC_DISABLE, &id, NULL);
}

static inline int pc_clk_set_rate(unsigned id, unsigned rate)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_SET_RATE, &id, &rate);
}

static inline int pc_clk_set_min_rate(unsigned id, unsigned rate)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_MIN_RATE, &id, &rate);
}

static inline int pc_clk_set_max_rate(unsigned id, unsigned rate)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_MAX_RATE, &id, &rate);
}

static inline int pc_clk_set_flags(unsigned id, unsigned flags)
{
	return msm_proc_comm(PCOM_CLKCTL_RPC_SET_FLAGS, &id, &flags);
}

static inline unsigned pc_clk_get_rate(unsigned id)
{
	if (msm_proc_comm(PCOM_CLKCTL_RPC_RATE, &id, NULL))
		return 0;
	else
		return id;
}

static inline unsigned pc_clk_is_enabled(unsigned id)
{
	if (msm_proc_comm(PCOM_CLKCTL_RPC_ENABLED, &id, NULL))
		return 0;
	else
		return id;
}

static inline int pc_pll_request(unsigned id, unsigned on)
{
	on = !!on;
	return msm_proc_comm(PCOM_CLKCTL_RPC_PLL_REQUEST, &id, &on);
}
#else

void print_clocks()
{
	int i=0;
	do { 
	D("%8.8x | %8.8x %8.8x %8.8x %8.8x\n",MSM_CLK_CTL_PHYS+i, \
		readl(MSM_CLK_CTL_BASE+i),readl(MSM_CLK_CTL_BASE+(i+0x04)), \
		readl(MSM_CLK_CTL_BASE+(i+0x08)),readl(MSM_CLK_CTL_BASE+(i+0x0C)));
		i=i+16;
	} 
	while(i<1024);
}

/*
static int pc_clk_enable(uint32_t id)
{
	struct msm_dex_command setclock;
	setclock.cmd = PCOM_SET_CLOCK_ON;
	setclock.has_data = 1;
	setclock.data = id;
	msm_proc_comm_wince(&setclock, 0);
	if (id==GRP_CLK) print_clocks();		
}

static void pc_clk_disable(uint32_t id)
{
	struct msm_dex_command setclock;
	setclock.cmd = PCOM_SET_CLOCK_OFF;
	setclock.has_data = 1;
	setclock.data = id;
	msm_proc_comm_wince(&setclock, 0);
	if (id==GRP_CLK) print_clocks();				
}
*/

static int pc_clk_enable(uint32_t id) {
	switch (id)
	{
	case SDC1_PCLK:
		writel(readl(MSM_CLK_CTL_BASE)|(1<<7),MSM_CLK_CTL_BASE);
		break;
	case SDC2_PCLK:
		writel(readl(MSM_CLK_CTL_BASE)|(1<<8),MSM_CLK_CTL_BASE);
		break;
	case SDC3_PCLK:
		writel(readl(MSM_CLK_CTL_BASE)|(1<<27),MSM_CLK_CTL_BASE);
		break;
	case SDC4_PCLK:
		writel(readl(MSM_CLK_CTL_BASE)|(1<<28),MSM_CLK_CTL_BASE);
		break;
	case SDC1_CLK:		
		writel((readl(MSM_CLK_CTL_BASE + 0xa4) &0xfff0f000) | 0xB0044, MSM_CLK_CTL_BASE + 0xa4);
		writel(readl(MSM_CLK_CTL_BASE+0xa4)|0xB00,MSM_CLK_CTL_BASE+0xa4);
		break;
	case SDC2_CLK:
		//writel((readl(MSM_CLK_CTL_BASE + 0xac) &0xfffff000) | 0x44, MSM_CLK_CTL_BASE + 0xac);
		//writel(readl(MSM_CLK_CTL_BASE+0xac)|0xB00,MSM_CLK_CTL_BASE+0xac);
		
		break;
	case SDC3_CLK:
		writel((readl(MSM_CLK_CTL_BASE + 0xb4) &0xfffff000) | 0x5C, MSM_CLK_CTL_BASE + 0xb4);		
		writel(readl(MSM_CLK_CTL_BASE+0xb4)|0xB00,MSM_CLK_CTL_BASE+0xb4);
		break;
	case SDC4_CLK:
		writel((readl(MSM_CLK_CTL_BASE + 0xbc) &0xfffff000) | 0x5C, MSM_CLK_CTL_BASE + 0xbc);
		writel(readl(MSM_CLK_CTL_BASE+0xbc)|0xB00,MSM_CLK_CTL_BASE+0xbc);
		break;
	case MDP_CLK:
		writel(readl(MSM_CLK_CTL_BASE)|(1<<9),MSM_CLK_CTL_BASE);
		break;
	case PMDH_CLK:
		writel((readl(MSM_CLK_CTL_BASE + 0x8c) &0xfffff000) | 0x21, MSM_CLK_CTL_BASE + 0x8c);
		writel(readl(MSM_CLK_CTL_BASE+0x8c)| 0xa00 ,MSM_CLK_CTL_BASE+0x8c);
		break;
	case GRP_CLK:
		//msm_clk_grp_rail(1);
		set_grp_clk(1);
		//print_clocks();
		break;
	case IMEM_CLK:
		//writel(readl(MSM_CLK_CTL_BASE+0x84)| 0x280,MSM_CLK_CTL_BASE+0x84);
		break;
	case UART1_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0xe0)| 0x30,MSM_CLK_CTL_BASE+0xe0);
		break;
	case UART2_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0xe0)| 0xC00,MSM_CLK_CTL_BASE+0xe0);
		break;
	case UART3_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0xe0)| 0x30000,MSM_CLK_CTL_BASE+0xe0);
		break;
	case UART1DM_CLK:
		writel(readl(MSM_CLK_CTL_BASE) | (1<<17),MSM_CLK_CTL_BASE);
		writel(readl(MSM_CLK_CTL_BASE+0xd4)| 0xB00,MSM_CLK_CTL_BASE+0xd4);
		break;
	case UART2DM_CLK:
		writel(readl(MSM_CLK_CTL_BASE) | (1<<26),MSM_CLK_CTL_BASE);
		writel(readl(MSM_CLK_CTL_BASE+0xdc)| 0xB00,MSM_CLK_CTL_BASE+0xdc);
		break;
	case I2C_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0x64)| 0xa00,MSM_CLK_CTL_BASE+0x64);
		break;
	case USB_HS_PCLK:
		writel(readl(MSM_CLK_CTL_BASE) | (1<<25),MSM_CLK_CTL_BASE);
		break;
	case USB_HS_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0x2c0)| 0xB00,MSM_CLK_CTL_BASE+0x2c0);
		break;
	case SDAC_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0x9C)| 0xB00,MSM_CLK_CTL_BASE+0x9C);
		break;
	default:
		printk(KERN_WARNING "%s: FIXME! How to enable clock %u ?\n", __func__, id);
        	return -1;  
    	}
	return 0;
}

static void pc_clk_disable(uint32_t id)
{
	switch (id)
	{
	case SDC1_PCLK:
		writel(readl(MSM_CLK_CTL_BASE)& ~(1<<7),MSM_CLK_CTL_BASE);
		break;
	case SDC2_PCLK:
		writel(readl(MSM_CLK_CTL_BASE)& ~(1<<8),MSM_CLK_CTL_BASE);
		break;
	case SDC3_PCLK:
		writel(readl(MSM_CLK_CTL_BASE)& ~(1<<27),MSM_CLK_CTL_BASE);
		break;
	case SDC4_PCLK:
		writel(readl(MSM_CLK_CTL_BASE)& ~(1<<28),MSM_CLK_CTL_BASE);
		break;
	case SDC1_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0xa4)& ~0xB00,MSM_CLK_CTL_BASE+0xa4);
		break;
	case SDC2_CLK:
		//writel(readl(MSM_CLK_CTL_BASE+0xac)& ~0xB00,MSM_CLK_CTL_BASE+0xac);
		break;
	case SDC3_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0xb4)& ~0xB00,MSM_CLK_CTL_BASE+0xb4);
		break;
	case SDC4_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0xbc)& ~0xB00,MSM_CLK_CTL_BASE+0xbc);
		break;
	case MDP_CLK:
		writel(readl(MSM_CLK_CTL_BASE)& ~(1<<9),MSM_CLK_CTL_BASE);
		break;
	case PMDH_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0x8c)&~(0xa00),MSM_CLK_CTL_BASE+0x8c);
		break;
	case GRP_CLK:
		//msm_clk_grp_rail(0);
		set_grp_clk(0);
		//print_clocks();
		break;
	case IMEM_CLK:
		//writel(readl(MSM_CLK_CTL_BASE+0x84)& ~0x280,MSM_CLK_CTL_BASE+0x84);
		break;
	case UART1_CLK:
                writel(readl(MSM_CLK_CTL_BASE+0xe0)& ~0x30,MSM_CLK_CTL_BASE+0xe0);
		break;
	case UART2_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0xe0)& ~0xC00,MSM_CLK_CTL_BASE+0xe0);
		break;
	case UART3_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0xe0)& ~0x30000,MSM_CLK_CTL_BASE+0xe0);
		break;
	case UART1DM_CLK:
		writel(readl(MSM_CLK_CTL_BASE) &~ (1<<17),MSM_CLK_CTL_BASE);
		writel(readl(MSM_CLK_CTL_BASE+0xd4)& ~0xB00,MSM_CLK_CTL_BASE+0xd4);
		break;
	case UART2DM_CLK:
		writel(readl(MSM_CLK_CTL_BASE) &~ (1<<26),MSM_CLK_CTL_BASE);
		writel(readl(MSM_CLK_CTL_BASE+0xdc)& ~0xB00,MSM_CLK_CTL_BASE+0xdc);
		break;
	case I2C_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0x64)& ~0xa00,MSM_CLK_CTL_BASE+0x64);
		break;
	case USB_HS_PCLK:
		writel(readl(MSM_CLK_CTL_BASE) & ~(1<<25),MSM_CLK_CTL_BASE);
		break;
	case USB_HS_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0x2c0)& ~0xB00,MSM_CLK_CTL_BASE+0x2c0);
		break;
	case SDAC_CLK:
		writel(readl(MSM_CLK_CTL_BASE+0x9C)& ~0xB00,MSM_CLK_CTL_BASE+0x9C);
		break;
	default:
		printk(KERN_WARNING "%s: FIXME! How to disable clock %u ?\n", __func__, id);
    	}
	return;
}

static int pc_clk_set_rate(uint32_t id, unsigned long rate)
{
	int retval;
	retval = 0;

	if(DEBUG_MDNS)
		D("%s: id=%u rate=%lu\n", __func__, id, rate);

	retval = set_mdns_host_clock(id, rate);

	return retval;
}

static int pc_clk_set_min_rate(uint32_t id, unsigned long rate)
{
	if (id < NR_CLKS)
	 min_clk_rate[id]=rate;
	else if(debug_mask&DEBUG_UNKNOWN_ID)
	 printk(KERN_WARNING " FIXME! clk_set_min_rate not implemented; %u:%lu NR_CLKS=%d\n", id, rate, NR_CLKS);

	return 0;
}

static int pc_clk_set_max_rate(uint32_t id, unsigned long rate)
{
	if (id < NR_CLKS)
	 max_clk_rate[id]=rate;
	else if(debug_mask&DEBUG_UNKNOWN_ID)
	 printk(KERN_WARNING " FIXME! clk_set_min_rate not implemented; %u:%lu NR_CLKS=%d\n", id, rate, NR_CLKS);

	return 0;
}

static unsigned long pc_clk_get_rate(uint32_t id)
{
	unsigned long rate = 0;

	switch (id) {
		/* known MD/NS clocks, MSM_CLK dump and arm/mach-msm/clock-7x30.c */
		case SDC1_CLK:
		case SDC2_CLK:
		case SDC3_CLK:
		case SDC4_CLK:
		case UART1DM_CLK:
		case UART2DM_CLK:
		case USB_HS_CLK:
		case SDAC_CLK:
		case TV_DAC_CLK:
		case TV_ENC_CLK:
		case USB_OTG_CLK:
			rate = get_mdns_host_clock(id);
			break;

		case SDC1_PCLK:
		case SDC2_PCLK:
		case SDC3_PCLK:
		case SDC4_PCLK:
			rate = 64000000; /* g1 value */
			break;

		default:
			//TODO: support all clocks
			if(debug_mask&DEBUG_UNKNOWN_ID)
				printk("%s: unknown clock: id=%u\n", __func__, id);
			rate = 0;
	}

	return rate;
}

static int pc_clk_set_flags(uint32_t id, unsigned long flags)
{
	if(debug_mask&DEBUG_UNKNOWN_CMD)
		printk(KERN_WARNING "%s not implemented for clock: id=%u, flags=%lu\n", __func__, id, flags);
	return 0;
}

static int pc_clk_is_enabled(uint32_t id)
{
	int is_enabled = 0;
	unsigned bit;
	bit = msm_clk_enable_bit(id);
	if (bit > 0)
	{
		is_enabled = (readl(MSM_CLK_CTL_BASE) & bit) != 0;
	}
	return is_enabled;
}

static int pc_pll_request(unsigned id, unsigned on)
{
	if(debug_mask&DEBUG_UNKNOWN_CMD)
		printk(KERN_WARNING "%s not implemented for PLL=%u\n", __func__, id);

	return 0;
}

#endif

/*
 * Standard clock functions defined in include/linux/clk.h
 */
struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *clk;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(clk, &clocks, list)
		if (!strcmp(id, clk->name) && clk->dev == dev)
			goto found_it;

	list_for_each_entry(clk, &clocks, list)
		if (!strcmp(id, clk->name) && clk->dev == NULL)
			goto found_it;

	clk = ERR_PTR(-ENOENT);
found_it:
	mutex_unlock(&clocks_mutex);
	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	if (clk->id == ACPU_CLK)
	{
		return -ENOTSUPP;
	}
	spin_lock_irqsave(&clocks_lock, flags);
	clk->count++;
	if (clk->count == 1)
		pc_clk_enable(clk->id);
	spin_unlock_irqrestore(&clocks_lock, flags);
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;
	spin_lock_irqsave(&clocks_lock, flags);
	BUG_ON(clk->count == 0);
	clk->count--;
	if (clk->count == 0)
		pc_clk_disable(clk->id);
	spin_unlock_irqrestore(&clocks_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return pc_clk_get_rate(clk->id);
}
EXPORT_SYMBOL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret;
	if (clk->flags & CLKFLAG_USE_MAX_TO_SET) {
		ret = pc_clk_set_max_rate(clk->id, rate);
		if (ret)
			return ret;
	}
	if (clk->flags & CLKFLAG_USE_MIN_TO_SET) {
		ret = pc_clk_set_min_rate(clk->id, rate);
		if (ret)
			return ret;
	}

	if (clk->flags & CLKFLAG_USE_MAX_TO_SET ||
		clk->flags & CLKFLAG_USE_MIN_TO_SET)
		return ret;

	return pc_clk_set_rate(clk->id, rate);
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	return -ENOSYS;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	return ERR_PTR(-ENOSYS);
}
EXPORT_SYMBOL(clk_get_parent);

int clk_set_flags(struct clk *clk, unsigned long flags)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;
	return pc_clk_set_flags(clk->id, flags);
}
EXPORT_SYMBOL(clk_set_flags);


void __init msm_clock_init(void)
{
	struct clk *clk;
	msm_clock_parameters = msm_clock_parameters_6125;	
	spin_lock_init(&clocks_lock);
	mutex_lock(&clocks_mutex);
	for (clk = msm_clocks; clk && clk->name; clk++) {
		list_add_tail(&clk->list, &clocks);
	}

		msm_clock_freq_parameters = msm_clock_freq_parameters_pll0_196;
	
	


	mutex_unlock(&clocks_mutex);
}

/* The bootloader and/or AMSS may have left various clocks enabled.
 * Disable any clocks that belong to us (CLKFLAG_AUTO_OFF) but have
 * not been explicitly enabled by a clk_enable() call.
 */

static int __init clock_late_init(void)
{
	unsigned long flags;
	struct clk *clk;
	unsigned count = 0;

	//msm_clk_rpc_connect();
	mutex_lock(&clocks_mutex);
	list_for_each_entry(clk, &clocks, list) {
		if (clk->flags & CLKFLAG_AUTO_OFF) {
			spin_lock_irqsave(&clocks_lock, flags);
			if (!clk->count) {					
				count++;
					pr_info("disabling %d\n", clk->id);
					pc_clk_disable(clk->id);
			}
			spin_unlock_irqrestore(&clocks_lock, flags);
		}
	}



	mutex_unlock(&clocks_mutex);
	pr_info("clock_late_init() disabled %d unused clocks\n", count);
print_clocks(); //data
	/* some nand poop.. may not be needed 
	REG_AND(MSM_CLK_CTL_BASE+0x34, 0xA0000);
	REG_SET(MSM_CLK_CTL_BASE+0x5C,0);
	REG_OR(MSM_CLK_CTL_BASE, 0x208);
	REG_SET(MSM_CLK_CTL_BASE+0x8C,0xA21);
	REG_OR(MSM_CLK_CTL_BASE+0xAC,0xB44); */
	return 0;
}

late_initcall(clock_late_init);

