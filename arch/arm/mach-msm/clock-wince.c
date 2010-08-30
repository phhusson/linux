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

static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clocks_lock);
static LIST_HEAD(clocks);

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
	unsigned ns_only; // value to fill in ns register, rather than using mdns_clock_params look-up table
	char	*name;
};

static int max_clk_rate[NR_CLKS], min_clk_rate[NR_CLKS];

#define PLLn_BASE(n)		(MSM_CLK_CTL_BASE + 0x300 + 28 * (n))
#define TCX0			19200000 // Hz
#define PLL_FREQ(l, m, n)	(TCX0 * (l) + TCX0 * (m) / (n))

void clock_poop() {
	int i=0;
	do {
		D("%8.8x | %8.8x %8.8x %8.8x %8.8x\n",MSM_CLK_CTL_PHYS+i, \
		readl(MSM_CLK_CTL_BASE+i),readl(MSM_CLK_CTL_BASE+(i+0x04)), \
		readl(MSM_CLK_CTL_BASE+(i+0x08)),readl(MSM_CLK_CTL_BASE+(i+0x0C)));
		i=i+16;
	} while(i<1024);
}


unsigned int get_amss_s_version(void)
{
	char amss_ver[20];
	char amss_dump[20];
	char *dot1, *dot2;
	int len = 0;

	/* Detection doesn't work on 'old' CDMA, there's no
	 * version string to be found anywhere in SHARED_RAM_BASE
	 
	if (machine_is_htcdiamond_cdma() || machine_is_htcraphael_cdma() || machine_is_htcraphael_cdma500())
		return 6150;
	*/

	// Dump AMMS version
	*(unsigned int *) (amss_dump + 0x0) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0x0);
	*(unsigned int *) (amss_dump + 0x4) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0x4);
	*(unsigned int *) (amss_dump + 0x8) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0x8);
	*(unsigned int *) (amss_dump + 0xc) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0xc);
	*(unsigned int *) (amss_dump + 0x10) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0x10);
	amss_dump[19] = '\0';

	dot1 = strchr(amss_dump, '.');
	if(dot1 == NULL) {	// CDMA
		dot1 = strchr(amss_dump, '-');
		if(dot1 == NULL)
			return 0;
		strncpy(amss_ver, dot1+1, 4);
		amss_ver[4] = '\0';
		return  simple_strtoul(amss_ver, NULL, 10);
	}
	else { // GSM
		len = (dot1-amss_dump);
		strncpy(amss_ver, amss_dump, len);
		dot1 = strchr(dot1+1, '.');
		if(dot1 == NULL)
			return 0;
		dot2 = strchr(dot1+1, '.');
		if(dot2 == NULL)
			return 0;
		strncpy(amss_ver+len, dot1+1, (dot2-dot1)-1);
		len+= (int)(dot2-dot1)-1;
		amss_ver[len] = '\0';
		return  simple_strtoul(amss_ver, NULL, 10);
	}
}


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

static struct msm_clock_params msm_clock_parameters_def[] = {
	// Full ena/md/ns clock
	{ .clk_id = SDC1_CLK,		.idx =  7,	.offset =	0xa4,	.name=	"SDC1_CLK",},
	{ .clk_id = SDC2_CLK,		.idx =  8,	.offset =	0xac,	.name=	"SDC2_CLK",},
	{ .clk_id = SDC3_CLK,		.idx = 27,	.offset =	0xb4,	.name=	"SDC3_CLK",},
	{ .clk_id = SDC4_CLK,		.idx = 28,	.offset =	0xbc,	.name=	"SDC4_CLK",},
	{ .clk_id = UART1DM_CLK,	.idx = 17,	.offset = 	0xd4,	.name=	"UART1DM_CLK",},
	{ .clk_id = UART2DM_CLK,	.idx = 26,	.offset = 	0xdc,	.name=	"UART2DM_CLK",},

	{ .clk_id = USB_HS_CLK,		.idx = 25,.offset=0x2c0,.ns_only =0xb00,.name=	"USB_HS_CLK",},
	{ .clk_id = GRP_CLK,		.idx = 3,				.name=	"GRP_CLK", },
	{ .clk_id = IMEM_CLK,		.idx =	3,				.name=	"IMEM_CLK", },


	// MD/NS only; offset = Ns reg
	{ .clk_id = VFE_CLK,		.offset = 0x44,				.name=	"VFE_CLK", },

	// Enable bit only; bit = 1U << idx
	{ .clk_id = MDP_CLK,		.idx = 9,				.name=	"MDP_CLK",},

	// NS-reg only; offset = Ns reg, ns_only = Ns value
	{ .clk_id = GP_CLK,			.offset = 0x5c,	.ns_only = 0xa06,.name=	"GP_CLK" },
/*#if defined(CONFIG_MACH_HTCBLACKSTONE) || defined(CONFIG_MACH_HTCKOVSKY)
	{ .clk_id = PMDH_CLK,		.offset = 0x8c,.ns_only = 0xa19,	.name=	"PMDH_CLK"},
#else*/
	{ .clk_id = PMDH_CLK,		.offset = 0x8c,	.ns_only = 0xa0c,	.name=	"PMDH_CLK"},
//#endif

	{ .clk_id = I2C_CLK,		.offset = 0x68,	.ns_only = 0xa00,	.name=	"I2C_CLK"},
//	{ .clk_id = UART1_CLK,		.offset = 0xe0,	.ns_only = 0xa00,	.name=	"UART1_CLK"},
};

/*
static struct msm_clock_params msm_clock_parameters_6125[] = {
	// Full ena/md/ns clock
	{ .clk_id = SDC1_CLK,		.idx =  7, .offset = 0xa4,				.name="SDC1_CLK",},
	{ .clk_id = SDC2_CLK,		.idx =  8, .offset = 0xac,	.ns_only = 0xB44,	.name="SDC2_CLK",},
	{ .clk_id = SDC3_CLK,		.idx = 27, .offset = 0xb4,	.ns_only = 0x5C,	.name="SDC3_CLK",},
	{ .clk_id = SDC4_CLK,		.idx = 28, .offset = 0xbc,	.ns_only = 0x5C,	.name="SDC4_CLK",},

	{ .clk_id = UART1DM_CLK,	.idx = 17, .offset = 0xd4,				.name="UART1DM_CLK",},
	{ .clk_id = UART2DM_CLK,	.idx = 26, .offset = 0xdc,				.name="UART2DM_CLK",},

	{ .clk_id = USB_HS_CLK,		.idx = 25, .offset = 0x2c0,	.ns_only = 0xb59,	.name="USB_HS_CLK",},
	{ .clk_id = USB_HS_PCLK,	.idx = 25, .offset = 0xE8,	.ns_only = 0xfa,	.name="USB_HS_PCLK",},
	

	// these both enable the GRP and IMEM clocks.
	{ .clk_id = GRP_CLK,		.idx = 3,  .offset = 0x84,	.ns_only = 0xa80,	.name="GRP_CLK", }, 
	{ .clk_id = IMEM_CLK,		.idx = 3,  .offset = 0x84,	.ns_only = 0xa80,	.name="IMEM_CLK", },

	// MD/NS only; offset = Ns reg
	{ .clk_id = VFE_CLK,			   .offset = 0x44,	.ns_only = (1<<9),	.name="VFE_CLK", },
	{ .clk_id = VFE_MDC_CLK,		   .offset = 0x44,	.ns_only = (1<<11),	.name="VFE_MDC_CLK", },

	// Enable bit only; bit = 1U << idx
	{ .clk_id = MDP_CLK,		.idx = 9,						.name="MDP_CLK",},
	
	// NS-reg only; offset = Ns reg, ns_only = Ns value
	{ .clk_id = GP_CLK,			   .offset = 0x5c,	.ns_only = 0x000,	.name="GP_CLK" },
	{ .clk_id = PMDH_CLK,			   .offset = 0x8c,	.ns_only = 0xa21,	.name="PMDH_CLK"},
	{ .clk_id = I2C_CLK,			   .offset = 0x68,	.ns_only = 0xa00,	.name="I2C_CLK"},

	// experimental 
	{ .clk_id = EMDH_CLK,			   .offset = 0x50,	.ns_only = 0x814,	.name="EMDH_CLK"},
	{ .clk_id = SDAC_CLK,			   .offset = 0x9C,	.ns_only = 0x040,	.name="SDAC_CLK"},
	{ .clk_id = VDC_CLK,			   .offset = 0xF0,	.ns_only = 0x01C,	.name="VDC_CLK"},
//	{ .clk_id = SDC1_PCLK,		.idx =  7, 	.name="SDC1_PCLK",},
//	{ .clk_id = SDC2_PCLK,		.idx =  8, 	.name="SDC2_PCLK",},
//	{ .clk_id = SDC3_PCLK,		.idx = 27, 	.name="SDC3_PCLK",},
//	{ .clk_id = SDC4_PCLK,		.idx = 28, 	.name="SDC4_PCLK",},	
};
*/

//orig clocks
static struct msm_clock_params msm_clock_parameters_6125[] = {
	// Full ena/md/ns clock
	{ .clk_id = SDC1_CLK,		.idx =  7,	.offset = 0xa4,	.name="SDC1_CLK",},
	{ .clk_id = SDC2_CLK,		.idx =  8,	.offset = 0xac,	.name="SDC2_CLK",},
	{ .clk_id = SDC3_CLK,		.idx = 27,	.offset = 0xb4,	.name="SDC3_CLK",},
	{ .clk_id = SDC4_CLK,		.idx = 28,	.offset = 0xbc,	.name="SDC4_CLK",},
	{ .clk_id = UART1DM_CLK,	.idx = 17,	.offset = 0xd4,	.name="UART1DM_CLK",},
	{ .clk_id = UART2DM_CLK,	.idx = 26,	.offset = 0xdc,	.name="UART2DM_CLK",},

	{ .clk_id = USB_HS_CLK,		.idx = 25,.offset = 0x2c0,.ns_only = 0xb59,.name="USB_HS_CLK",},
	{ .clk_id = GRP_CLK,		.idx = 3,			.name="GRP_CLK", },
	{ .clk_id = IMEM_CLK,		.idx = 3,			.name="IMEM_CLK", },


	// MD/NS only; offset = Ns reg
	{ .clk_id = VFE_CLK,				.offset = 0x44,	.name="VFE_CLK", },

	// Enable bit only; bit = 1U << idx
	{ .clk_id = MDP_CLK,		.idx = 9,			.name="MDP_CLK",},


	// NS-reg only; offset = Ns reg, ns_only = Ns value
	{ .clk_id = GP_CLK,		.offset = 0x5c,	.ns_only = 0xa06,.name="GP_CLK" },
//#if defined(CONFIG_MACH_HTCBLACKSTONE) || defined(CONFIG_MACH_HTCKOVSKY)
//	{ .clk_id = PMDH_CLK,		.offset = 0x8c,	.ns_only = 0xa19,.name="PMDH_CLK"},
//#else
	{ .clk_id = PMDH_CLK,		.offset = 0x8c,	.ns_only = 0xa21,.name="PMDH_CLK"},
//#endif

	{ .clk_id = I2C_CLK,		.offset = 0x68,	.ns_only = 0xa00,.name="I2C_CLK"},
//	{ .clk_id = UART1_CLK,		.offset = 0xe0,	.ns_only = 0xa00,.name="UART1_CLK"},
};


static struct msm_clock_params* msm_clock_parameters;


void fix_mddi_clk_black(void) {
		int i;
		for (i = 0; i < ARRAY_SIZE(msm_clock_parameters_def); i++)
				if (msm_clock_parameters_def[i].clk_id == PMDH_CLK) {
					msm_clock_parameters_def[i].ns_only=0xa19;
					break;
		}
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

// GSM phones typically use a 245 MHz PLL0
struct mdns_clock_params msm_clock_freq_parameters_pll0_245[] = {

	MSM_CLOCK_REG(  144000,   3, 0x64, 0x32, 3, 3, 0, 1, 19200000), /* SD, 144kHz */
#if 0 /* wince uses this clock setting for UART2DM */
	MSM_CLOCK_REG( 1843200,     3, 0x64, 0x32, 3, 2, 4, 1, 245760000), /*  115200*16=1843200 */
//	MSM_CLOCK_REG(            , 2, 0xc8, 0x64, 3, 2, 1, 1, 768888888), /* 1.92MHz for 120000 bps */
#else
	MSM_CLOCK_REG( 7372800,   3, 0x64, 0x32, 0, 2, 4, 1, 245760000), /*  460800*16, will be divided by 4 for 115200 */
#endif
	MSM_CLOCK_REG(12000000,   1, 0x20, 0x10, 1, 3, 1, 1, 768000000), /* SD, 12MHz */
	MSM_CLOCK_REG(14745600,   3, 0x32, 0x19, 0, 2, 4, 1, 245760000), /* BT, 921600 (*16)*/
	MSM_CLOCK_REG(19200000,   1, 0x0a, 0x05, 3, 3, 1, 1, 768000000), /* SD, 19.2MHz */
	MSM_CLOCK_REG(24000000,   1, 0x10, 0x08, 1, 3, 1, 1, 768000000), /* SD, 24MHz */
	MSM_CLOCK_REG(32000000,   1, 0x0c, 0x06, 1, 3, 1, 1, 768000000), /* SD, 32MHz */
	MSM_CLOCK_REG(58982400,   6, 0x19, 0x0c, 0, 2, 4, 1, 245760000), /* BT, 3686400 (*16) */
	MSM_CLOCK_REG(64000000,0x19, 0x60, 0x30, 0, 2, 4, 1, 245760000), /* BT, 4000000 (*16) */
	{0, 0, 0, 0, 0, 0},
};

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
	if ( on != 0 ) {
		REG_OR( MSM_AXI_RESET, 0x20 );
		REG_OR( MSM_ROW_RESET, 0x20000 );
		REG_SET( MSM_VDD_GRP_GFS_CTL, 0x11f );
		mdelay( 20 );						// very rough delay

		REG_OR( MSM_GRP_NS_REG, 0x800 );
		REG_OR( MSM_GRP_NS_REG, 0x80 );
		REG_OR( MSM_GRP_NS_REG, 0x200 );

		REG_OR( MSM_CLK_CTL_BASE, 0x8 );					// grp idx

		REG_AND( MSM_RAIL_CLAMP_IO, 0x4 );
		REG_AND( MSM_PRPH_WEB_NS_REG, 0x1 );			// Suppress bit 0 of grp MD
		REG_AND( MSM_AXI_RESET, 0x20 );
		REG_AND( MSM_ROW_RESET, 0x20000 );
	} else {
		REG_OR( MSM_GRP_NS_REG, 0x800 );
		REG_OR( MSM_GRP_NS_REG, 0x80 );
		REG_OR( MSM_GRP_NS_REG, 0x200 );

		REG_OR(  MSM_CLK_CTL_BASE, 0x8 );					// grp idx

		REG_OR( MSM_PRPH_WEB_NS_REG, 0x1 );			// grp MD

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

		REG_SET( MSM_VDD_GRP_GFS_CTL, 0x11f );

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
	for (i = 0; i < ARRAY_SIZE(msm_clock_parameters_def); i++) {
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
	bool found;
	struct msm_clock_params params;
	uint32_t nsreg;
	found = 0;
	retval = -EINVAL;

	params = msm_clk_get_params(id);
	offset = params.offset;

	if(debug_mask&DEBUG_MDNS)
		D("set mdns: %u, %lu; bitidx=%u, offset=%x, ns=%x\n", id, freq,
	  params.idx, params.offset, params.ns_only);

	if (!params.offset)
	{
		printk(KERN_WARNING "%s: FIXME! Don't know how to set clock %u - no known Md/Ns reg\n", __func__, id);
		return -ENOTSUPP;
	}

	// Turn off clock-enable bit if supported
	if (params.idx > 0)
		writel(readl(MSM_CLK_CTL_BASE) & ~(1U << params.idx), MSM_CLK_CTL_BASE);

	if (params.ns_only > 0)
	{
		nsreg = readl(MSM_CLK_CTL_BASE + offset) & 0xfffff000;
		writel( nsreg | params.ns_only, MSM_CLK_CTL_BASE + offset);

		found = 1;
		retval = 0;

	} else {
		n = 0;
		while (msm_clock_freq_parameters[n].freq) {
			n++;
		}

		for (n--; n >= 0; n--) {
			if (freq >= msm_clock_freq_parameters[n].freq) {
				// This clock requires MD and NS regs to set frequency:
				writel(msm_clock_freq_parameters[n].md, MSM_CLK_CTL_BASE + offset - 4);
				writel(msm_clock_freq_parameters[n].ns, MSM_CLK_CTL_BASE + offset);
//				msleep(5);
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

	// Turn clock-enable bit back on, if supported
	if (params.idx > 0)
		writel(readl(MSM_CLK_CTL_BASE) | (1U << params.idx), MSM_CLK_CTL_BASE);

	if (!found && debug_mask&DEBUG_UNKNOWN_FREQ) {
		printk(KERN_WARNING "clock-wince: FIXME! set_sdcc_host_clock could not "
		       "find suitable parameter for freq %lu\n", freq);
	}

//     return retval;
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

static int pc_clk_enable(uint32_t id)
{
	struct msm_clock_params params;
	int done=0;
	params = msm_clk_get_params(id);

	//XXX: too spammy, extreme debugging only: D(KERN_DEBUG "%s: %d\n", __func__, id);

	if ( id == IMEM_CLK || id == GRP_CLK )
	{
		//clock_poop( 0 );		
		set_grp_clk( 1 );
		//writel(readl(MSM_CLK_CTL_BASE) | (1U << params.idx), MSM_CLK_CTL_BASE);
		done=1;
	}

	if (params.idx)
	{
		writel(readl(MSM_CLK_CTL_BASE) | (1U << params.idx), MSM_CLK_CTL_BASE);
		done=1;
	}
	if (params.ns_only > 0 && params.offset)
	{
		writel((readl(MSM_CLK_CTL_BASE + params.offset) &0xfffff000) | params.ns_only, MSM_CLK_CTL_BASE + params.offset);
		done=1;
	}
	if(!done && debug_mask&DEBUG_UNKNOWN_ID)
		printk(KERN_WARNING "%s: FIXME! enabling a clock that doesn't have an ena bit "
		       "or ns-only offset: %u\n", __func__, id);

	return 0;
}

static void pc_clk_disable(uint32_t id)
{
	struct msm_clock_params params;
	params = msm_clk_get_params(id);

	//XXX: D(KERN_DEBUG "%s: %d\n", __func__, id);

	if ( id == IMEM_CLK || id == GRP_CLK )
	{
		set_grp_clk( 0 );
		writel( readl( MSM_CLK_CTL_BASE ) & ~( 1U << params.idx ), MSM_CLK_CTL_BASE );
		return 0;
	}

	if (params.idx)
	{
		writel(readl(MSM_CLK_CTL_BASE) & ~(1U << params.idx), MSM_CLK_CTL_BASE);
	} else if (params.ns_only > 0 && params.offset)
	{
		writel(readl(MSM_CLK_CTL_BASE + params.offset) & 0xfffff000, MSM_CLK_CTL_BASE + params.offset);
	} else {
		if(debug_mask&DEBUG_UNKNOWN_ID)
			printk(KERN_WARNING "%s: FIXME! disabling a clock that doesn't have an "
			       "ena bit: %u\n", __func__, id);
	}
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
	//XXX: is this necessary?
	if (id==SDC1_PCLK || id==SDC2_PCLK || id==SDC3_PCLK || id==SDC4_PCLK)
		is_enabled = 1;
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

int amss_s_version;
amss_s_version = get_amss_s_version();

	struct clk *clk;
	switch(amss_s_version) {
		case 6125:
			msm_clock_parameters = msm_clock_parameters_6125;
			printk(KERN_ERR "amss: Using 6125 clocks\n");
		break;
		default:
			msm_clock_parameters = msm_clock_parameters_def;
			printk(KERN_ERR "amss: Using default clocks\n");
		break;
	}
	
	spin_lock_init(&clocks_lock);
	mutex_lock(&clocks_mutex);
	for (clk = msm_clocks; clk && clk->name; clk++) {
		list_add_tail(&clk->list, &clocks);
	}

	if (pll_get_rate(0) == 196608000) {
		// cdma pll0 = 196 MHz
		msm_clock_freq_parameters = msm_clock_freq_parameters_pll0_196;
	} else {
		// default gsm pll0 = 245 MHz
		msm_clock_freq_parameters = msm_clock_freq_parameters_pll0_245;
	}

	mutex_unlock(&clocks_mutex);
}

/* The bootloader and/or AMSS may have left various clocks enabled.
 * Disable any clocks that belong to us (CLKFLAG_AUTO_OFF) but have
 * not been explicitly enabled by a clk_enable() call.
 */



#define PMIC_API_PROG		0x30000055
#define PMIC_API_VERS 		0x0
#define PMIC_API_GET_KHZ_PROC	0x1

#define GRP_RAIL_ON 0x2C
#define GRP_RAIL_OFF 0x2B

void clk_regime_sec(int i) {
	struct msm_rpc_endpoint *ept=NULL;
	struct {
		struct rpc_request_hdr hdr;
		uint32_t data[1];
	} req;
	BUG_ON(ept=msm_rpc_connect(0x3000000f, 0x1c5ace2a, 0x0));
	if (IS_ERR(ept)) {
		printk(KERN_ERR "%s: init rpc failed! error: %ld\n",
		__func__, PTR_ERR(ept));
	}
	else {
		req.data[0]=cpu_to_be32(0x1);
		msm_rpc_call(ept, i, &req, sizeof(req), 15*HZ);
	}
}

static void get_clk_khz(void)
{
	struct msm_rpc_endpoint *pmic_ep;
	int rc;
	struct {
		struct rpc_request_hdr hdr;
		uint32_t data[1];
	} req;
	
	pmic_ep = msm_rpc_connect(PMIC_API_PROG, PMIC_API_VERS, 0);
	if (IS_ERR(pmic_ep)) {
		printk(KERN_ERR "%s: init rpc failed! error: %ld\n",
				__func__, PTR_ERR(pmic_ep));
		goto close;
	}
	unsigned i;
	pr_info("IMEM OLD: VAL = %d\n", readl(MSM_IMEM_BASE ));
	req.data[0] = cpu_to_be32(1);
	rc = msm_rpc_call(pmic_ep, PMIC_API_GET_KHZ_PROC, &req, sizeof(req), 5 * HZ);
	if (rc < 0)
		printk(KERN_ERR "%s: rpc call failed! (%d)\n", __func__, rc);
	
	msleep(100),
	pr_info("IMEM NEW: VAL = %d\n", readl(MSM_IMEM_BASE ));

close:
	msm_rpc_close(pmic_ep);
}

static int __init clock_late_init(void)
{
	unsigned long flags;
	struct clk *clk;
	unsigned count = 0;

	//clock_poop(0);

	mutex_lock(&clocks_mutex);
	list_for_each_entry(clk, &clocks, list) {
		if (clk->flags & CLKFLAG_AUTO_OFF) {
			spin_lock_irqsave(&clocks_lock, flags);
			if (!clk->count) {
				count++;
				// pr_info("disabling %d \n", clk->id);
				pc_clk_disable(clk->id);
			}
			spin_unlock_irqrestore(&clocks_lock, flags);
		}
	}
	mutex_unlock(&clocks_mutex);
	pr_info("clock_late_init() disabled %d unused clocks\n", count);

	return 0;
}

late_initcall(clock_late_init);
