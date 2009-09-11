/* linux/arch/arm/mach-msm/gpio.c
 *
 * Copyright (C) 2007 Google, Inc.
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

#include <asm/io.h>
#include <asm/gpio.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/delay.h>

#include "gpio_chip.h"
#include "gpio_hw.h"

#include "smd_private.h"

#include "proc_comm_wince.h"

enum {
	GPIO_DEBUG_SLEEP = 1U << 0,
};
static int msm_gpio_debug_mask = 1;
module_param_named(debug_mask, msm_gpio_debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define MSM_GPIO_BROKEN_INT_CLEAR 1

/* private gpio_configure flags */
#define MSM_GPIOF_ENABLE_INTERRUPT      0x10000000
#define MSM_GPIOF_DISABLE_INTERRUPT     0x20000000
#define MSM_GPIOF_ENABLE_WAKE           0x40000000
#define MSM_GPIOF_DISABLE_WAKE          0x80000000

static DEFINE_SPINLOCK(msm_gpio_lock);

static int msm_gpio_configure(struct gpio_chip *chip, unsigned int gpio, unsigned long flags);
static int msm_gpio_get_irq_num(struct gpio_chip *chip, unsigned int gpio, unsigned int *irqp, unsigned long *irqnumflagsp);
static int msm_gpio_read(struct gpio_chip *chip, unsigned n);
static int msm_gpio_write(struct gpio_chip *chip, unsigned n, unsigned on);
static int msm_gpio_read_detect_status(struct gpio_chip *chip, unsigned int gpio);
static int msm_gpio_clear_detect_status(struct gpio_chip *chip, unsigned int gpio);

struct msm_gpio_regs
{
	void __iomem *out;
	void __iomem *in;
	void __iomem *int_status;
	void __iomem *int_clear;
	void __iomem *int_en;
	void __iomem *int_edge;
	void __iomem *int_pos;
	void __iomem *oe;
	void __iomem *owner;
};

struct msm_gpio_chip {
	struct gpio_chip        chip;
	struct msm_gpio_regs    regs;
#if MSM_GPIO_BROKEN_INT_CLEAR
	unsigned                int_status_copy;
#endif
	unsigned int            both_edge_detect;
	unsigned int            int_enable[2]; /* 0: awake, 1: sleep */
};

struct msm_gpio_chip msm_gpio_chips[] = {
	{
		.regs = {
			.out =         GPIO_OUT_0,
			.in =          GPIO_IN_0,
			.int_status =  GPIO_INT_STATUS_0,
			.int_clear =   GPIO_INT_CLEAR_0,
			.int_en =      GPIO_INT_EN_0,
			.int_edge =    GPIO_INT_EDGE_0,
			.int_pos =     GPIO_INT_POS_0,
			.oe =          GPIO_OE_0,
			.owner =       GPIO_OWNER_0,
		},
		.chip = {
			.start = 0,
			.end = 15,
			.configure = msm_gpio_configure,
			.get_irq_num = msm_gpio_get_irq_num,
			.read = msm_gpio_read,
			.write = msm_gpio_write,
			.read_detect_status = msm_gpio_read_detect_status,
			.clear_detect_status = msm_gpio_clear_detect_status
		}
	},
	{
		.regs = {
			.out =         GPIO_OUT_1,
			.in =          GPIO_IN_1,
			.int_status =  GPIO_INT_STATUS_1,
			.int_clear =   GPIO_INT_CLEAR_1,
			.int_en =      GPIO_INT_EN_1,
			.int_edge =    GPIO_INT_EDGE_1,
			.int_pos =     GPIO_INT_POS_1,
			.oe =          GPIO_OE_1,
			.owner =       GPIO_OWNER_1,
		},
		.chip = {
			.start = 16,
			.end = 42,
			.configure = msm_gpio_configure,
			.get_irq_num = msm_gpio_get_irq_num,
			.read = msm_gpio_read,
			.write = msm_gpio_write,
			.read_detect_status = msm_gpio_read_detect_status,
			.clear_detect_status = msm_gpio_clear_detect_status
		}
	},
	{
		.regs = {
			.out =         GPIO_OUT_2,
			.in =          GPIO_IN_2,
			.int_status =  GPIO_INT_STATUS_2,
			.int_clear =   GPIO_INT_CLEAR_2,
			.int_en =      GPIO_INT_EN_2,
			.int_edge =    GPIO_INT_EDGE_2,
			.int_pos =     GPIO_INT_POS_2,
			.oe =          GPIO_OE_2,
			.owner =       GPIO_OWNER_2,
		},
		.chip = {
			.start = 43,
			.end = 67,
			.configure = msm_gpio_configure,
			.get_irq_num = msm_gpio_get_irq_num,
			.read = msm_gpio_read,
			.write = msm_gpio_write,
			.read_detect_status = msm_gpio_read_detect_status,
			.clear_detect_status = msm_gpio_clear_detect_status
		}
	},
	{
		.regs = {
			.out =         GPIO_OUT_3,
			.in =          GPIO_IN_3,
			.int_status =  GPIO_INT_STATUS_3,
			.int_clear =   GPIO_INT_CLEAR_3,
			.int_en =      GPIO_INT_EN_3,
			.int_edge =    GPIO_INT_EDGE_3,
			.int_pos =     GPIO_INT_POS_3,
			.oe =          GPIO_OE_3,
			.owner =       GPIO_OWNER_3,
		},
		.chip = {
			.start = 68,
			.end = 94,
			.configure = msm_gpio_configure,
			.get_irq_num = msm_gpio_get_irq_num,
			.read = msm_gpio_read,
			.write = msm_gpio_write,
			.read_detect_status = msm_gpio_read_detect_status,
			.clear_detect_status = msm_gpio_clear_detect_status
		}
	},
	{
		.regs = {
			.out =         GPIO_OUT_4,
			.in =          GPIO_IN_4,
			.int_status =  GPIO_INT_STATUS_4,
			.int_clear =   GPIO_INT_CLEAR_4,
			.int_en =      GPIO_INT_EN_4,
			.int_edge =    GPIO_INT_EDGE_4,
			.int_pos =     GPIO_INT_POS_4,
			.oe =          GPIO_OE_4,
			.owner =       GPIO_OWNER_4,
		},
		.chip = {
			.start = 95,
			.end = 106,
			.configure = msm_gpio_configure,
			.get_irq_num = msm_gpio_get_irq_num,
			.read = msm_gpio_read,
			.write = msm_gpio_write,
			.read_detect_status = msm_gpio_read_detect_status,
			.clear_detect_status = msm_gpio_clear_detect_status
		}
	},
	{
		.regs = {
			.out =         GPIO_OUT_5,
			.in =          GPIO_IN_5,
			.int_status =  GPIO_INT_STATUS_5,
			.int_clear =   GPIO_INT_CLEAR_5,
			.int_en =      GPIO_INT_EN_5,
			.int_edge =    GPIO_INT_EDGE_5,
			.int_pos =     GPIO_INT_POS_5,
			.oe =          GPIO_OE_5,
			.owner =       GPIO_OWNER_5,
		},
		.chip = {
			.start = 107,
			.end = 121,
			.configure = msm_gpio_configure,
			.get_irq_num = msm_gpio_get_irq_num,
			.read = msm_gpio_read,
			.write = msm_gpio_write,
			.read_detect_status = msm_gpio_read_detect_status,
			.clear_detect_status = msm_gpio_clear_detect_status
		}
	}
};

static void msm_gpio_update_both_edge_detect(struct msm_gpio_chip *msm_chip)
{
	int loop_limit = 100;
	unsigned pol, val, val2, intstat;
	do {
		val = readl(msm_chip->regs.in);
		pol = readl(msm_chip->regs.int_pos);
		pol = (pol & ~msm_chip->both_edge_detect) | (~val & msm_chip->both_edge_detect);
		writel(pol, msm_chip->regs.int_pos);
		intstat = readl(msm_chip->regs.int_status);
		val2 = readl(msm_chip->regs.in);
		if (((val ^ val2) & msm_chip->both_edge_detect & ~intstat) == 0)
			return;
	} while (loop_limit-- > 0);
	printk(KERN_ERR "msm_gpio_update_both_edge_detect, failed to reach stable state %x != %x\n", val, val2);
}

static int msm_gpio_write(struct gpio_chip *chip, unsigned n, unsigned on)
{
	struct msm_gpio_chip *msm_chip = container_of(chip, struct msm_gpio_chip, chip);
	unsigned b = 1U << (n - chip->start);
	unsigned v;

	v = readl(msm_chip->regs.out);
	if (on) {
		writel(v | b, msm_chip->regs.out);
	} else {
		writel(v & (~b), msm_chip->regs.out);
	}
	return 0;
}

static int msm_gpio_read(struct gpio_chip *chip, unsigned n)
{
	struct msm_gpio_chip *msm_chip = container_of(chip, struct msm_gpio_chip, chip);
	unsigned b = 1U << (n - chip->start);

	return (readl(msm_chip->regs.in) & b) ? 1 : 0;
}

static int msm_gpio_read_detect_status(struct gpio_chip *chip, unsigned int gpio)
{
	struct msm_gpio_chip *msm_chip = container_of(chip, struct msm_gpio_chip, chip);
	unsigned b = 1U << (gpio - chip->start);
	unsigned v;

	v = readl(msm_chip->regs.int_status);
#if MSM_GPIO_BROKEN_INT_CLEAR
	v |= msm_chip->int_status_copy;
#endif
	return (v & b) ? 1 : 0;
}

static int msm_gpio_clear_detect_status(struct gpio_chip *chip, unsigned int gpio)
{
	struct msm_gpio_chip *msm_chip = container_of(chip, struct msm_gpio_chip, chip);
	unsigned b = 1U << (gpio - chip->start);

#if MSM_GPIO_BROKEN_INT_CLEAR
	/* Save interrupts that already triggered before we loose them. */
	/* Any interrupt that triggers between the read of int_status */
	/* and the write to int_clear will still be lost though. */
	msm_chip->int_status_copy |= readl(msm_chip->regs.int_status);
	msm_chip->int_status_copy &= ~b;
#endif
	writel(b, msm_chip->regs.int_clear);
	msm_gpio_update_both_edge_detect(msm_chip);
	return 0;
}

int msm_gpio_configure(struct gpio_chip *chip, unsigned int gpio, unsigned long flags)
{
	struct msm_gpio_chip *msm_chip = container_of(chip, struct msm_gpio_chip, chip);
	unsigned b = 1U << (gpio - chip->start);
	unsigned v;

	if (flags & (GPIOF_OUTPUT_LOW | GPIOF_OUTPUT_HIGH))
		msm_gpio_write(chip, gpio, flags & GPIOF_OUTPUT_HIGH);

	if (flags & (GPIOF_INPUT | GPIOF_DRIVE_OUTPUT)) {
		v = readl(msm_chip->regs.oe);
		if (flags & GPIOF_DRIVE_OUTPUT) {
			writel(v | b, msm_chip->regs.oe);
		} else {
			writel(v & (~b), msm_chip->regs.oe);
		}
	}

	if (flags & (GPIOF_OWNER_ARM9 | GPIOF_OWNER_ARM11)) {
		v = readl(msm_chip->regs.owner);
		if (flags & GPIOF_OWNER_ARM11) {
			writel(v | b, msm_chip->regs.owner);
		} else {
			writel(v & ~b, msm_chip->regs.owner);
		}
	}

	if (flags & (IRQF_TRIGGER_MASK | GPIOF_IRQF_TRIGGER_NONE)) {
		v = readl(msm_chip->regs.int_edge);
		if (flags & (IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING)) {
			writel(v | b, msm_chip->regs.int_edge);
			irq_desc[MSM_GPIO_TO_INT(gpio)].handle_irq = handle_edge_irq;
		} else {
			writel(v & (~b), msm_chip->regs.int_edge);
			irq_desc[MSM_GPIO_TO_INT(gpio)].handle_irq = handle_level_irq;
		}
		if ((flags & (IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING)) == (IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING)) {
			msm_chip->both_edge_detect |= b;
			msm_gpio_update_both_edge_detect(msm_chip);
		} else {
			msm_chip->both_edge_detect &= ~b;
			v = readl(msm_chip->regs.int_pos);
			if (flags & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_HIGH)) {
				writel(v | b, msm_chip->regs.int_pos);
			} else {
				writel(v & (~b), msm_chip->regs.int_pos);
			}
		}
	}

	/* used by msm_gpio_irq_mask and msm_gpio_irq_unmask */
	if (flags & (MSM_GPIOF_ENABLE_INTERRUPT | MSM_GPIOF_DISABLE_INTERRUPT)) {
		v = readl(msm_chip->regs.int_edge);
		/* level triggered interrupts are also latched */
		if (!(v & b))
			msm_gpio_clear_detect_status(chip, gpio);
		if (flags & MSM_GPIOF_ENABLE_INTERRUPT) {
			msm_chip->int_enable[0] |= b;
		} else {
			msm_chip->int_enable[0] &= ~b;
		}
		writel(msm_chip->int_enable[0], msm_chip->regs.int_en);
	}

	if (flags & (MSM_GPIOF_ENABLE_WAKE | MSM_GPIOF_DISABLE_WAKE)) {
		if (flags & MSM_GPIOF_ENABLE_WAKE)
			msm_chip->int_enable[1] |= b;
		else
			msm_chip->int_enable[1] &= ~b;
	}

	return 0;
}

static int msm_gpio_get_irq_num(struct gpio_chip *chip, unsigned int gpio, unsigned int *irqp, unsigned long *irqnumflagsp)
{
	*irqp = MSM_GPIO_TO_INT(gpio);
	if (irqnumflagsp)
		*irqnumflagsp = 0;
	return 0;
}


static void msm_gpio_irq_ack(unsigned int irq)
{
	gpio_clear_detect_status(irq - NR_MSM_IRQS);
}

static void msm_gpio_irq_mask(unsigned int irq)
{
	gpio_configure(irq - NR_MSM_IRQS, MSM_GPIOF_DISABLE_INTERRUPT);
}

static void msm_gpio_irq_unmask(unsigned int irq)
{
	gpio_configure(irq - NR_MSM_IRQS, MSM_GPIOF_ENABLE_INTERRUPT);
}

static int msm_gpio_irq_set_wake(unsigned int irq, unsigned int on)
{
	return gpio_configure(irq - NR_MSM_IRQS, on ? MSM_GPIOF_ENABLE_WAKE : MSM_GPIOF_DISABLE_WAKE);
}


static int msm_gpio_irq_set_type(unsigned int irq, unsigned int flow_type)
{
	return gpio_configure(irq - NR_MSM_IRQS, flow_type);
}

static void msm_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	int i, j, m;
	unsigned v;

	for (i = 0; i < ARRAY_SIZE(msm_gpio_chips); i++) {
		struct msm_gpio_chip *msm_chip = &msm_gpio_chips[i];
		v = readl(msm_chip->regs.int_status);
		v &= msm_chip->int_enable[0];
		while (v) {
			m = v & -v;
			j = fls(m) - 1;
			/* printk("msm_gpio_irq_handler %08x %08x bit %d gpio %d irq %d\n", v, m, j, msm_chip->chip.start + j, NR_MSM_IRQS + msm_chip->chip.start + j); */
			v &= ~m;
			generic_handle_irq(NR_MSM_IRQS + msm_chip->chip.start + j);
		}
	}
	desc->chip->ack(irq);
}

static struct irq_chip msm_gpio_irq_chip = {
	.name      = "msmgpio",
	.ack       = msm_gpio_irq_ack,
	.mask      = msm_gpio_irq_mask,
	.unmask    = msm_gpio_irq_unmask,
	.set_wake  = msm_gpio_irq_set_wake,
	.set_type  = msm_gpio_irq_set_type,
};

#define NUM_GPIO_INT_REGISTERS 6
#define GPIO_SMEM_NUM_GROUPS 2
#define GPIO_SMEM_MAX_PC_INTERRUPTS 8
struct tramp_gpio_smem
{
	uint16_t num_fired[GPIO_SMEM_NUM_GROUPS];
	uint16_t fired[GPIO_SMEM_NUM_GROUPS][GPIO_SMEM_MAX_PC_INTERRUPTS];
	uint32_t enabled[NUM_GPIO_INT_REGISTERS];
	uint32_t detection[NUM_GPIO_INT_REGISTERS];
	uint32_t polarity[NUM_GPIO_INT_REGISTERS];
};

/*
struct tramp_gpio_smem sleep_settings = {
	.enabled={0x10040000,0x00002030,0x44080000},
	.polarity={0,0,0x40000000},
};
*/

static void msm_gpio_sleep_int(unsigned long arg)
{
	int i, j;
	struct tramp_gpio_smem *smem_gpio;
	BUILD_BUG_ON(ARRAY_SIZE(msm_gpio_chips) != ARRAY_SIZE(smem_gpio->enabled));
	smem_gpio = smem_alloc(SMEM_GPIO_INT, sizeof(*smem_gpio)); 
	if (smem_gpio == NULL)
		return;

	local_irq_disable();
	for(i = 0; i < GPIO_SMEM_NUM_GROUPS; i++) {
		int count = smem_gpio->num_fired[i];
		for(j = 0; j < count; j++) {
			/* TODO: Check mask */
			printk("msm_gpio_sleep_int %d\n",smem_gpio->fired[i][j]);
			generic_handle_irq(MSM_GPIO_TO_INT(smem_gpio->fired[i][j]));
		}
	}
	local_irq_enable();
}

static DECLARE_TASKLET(msm_gpio_sleep_int_tasklet, msm_gpio_sleep_int, 0);


static void dumpgpios(void) {
	int i;
	for (i = 0; i < ARRAY_SIZE(msm_gpio_chips); i++) {
		printk("GPIO %d\n",i);
		printk("out:%x in:%x sta:%x ena:%x \nedg:%x pos:%x oe:%x own:%x\n",
		       readl(msm_gpio_chips[i].regs.out),
			     readl(msm_gpio_chips[i].regs.in),
				   readl(msm_gpio_chips[i].regs.int_status),
					 readl(msm_gpio_chips[i].regs.int_en),
					       readl(msm_gpio_chips[i].regs.int_edge),
							       readl(msm_gpio_chips[i].regs.int_pos),
									       readl(msm_gpio_chips[i].regs.oe),
										readl(msm_gpio_chips[i].regs.owner));
	}
}

void msm_gpio_enter_sleep(int from_idle)
{
	int i,j;
	struct tramp_gpio_smem *smem_gpio;

	BUILD_BUG_ON(ARRAY_SIZE(msm_gpio_chips) != ARRAY_SIZE(smem_gpio->enabled));

	smem_gpio = smem_alloc(SMEM_GPIO_INT, sizeof(*smem_gpio));
	
//	writel(0,msm_gpio_chips[4].regs.int_en);
	writel(4,msm_gpio_chips[4].regs.int_clear);
	
	udelay(10);
	if (msm_gpio_debug_mask & GPIO_DEBUG_SLEEP)
		dumpgpios();
	if (smem_gpio) {
		for (i = 0; i < ARRAY_SIZE(smem_gpio->enabled); i++) {
			smem_gpio->enabled[i] = 0;
			smem_gpio->detection[i] = 0;
			smem_gpio->polarity[i] = 0;
		}
	}
//	writel(0,msm_gpio_chips[3].regs.int_edge);
	for (i = 0; i < ARRAY_SIZE(msm_gpio_chips); i++) {
		writel(msm_gpio_chips[i].int_enable[!from_idle], msm_gpio_chips[i].regs.int_en);
		if (smem_gpio) {
			uint32_t tmp;
			int start, index, shiftl, shiftr;
			start = msm_gpio_chips[i].chip.start;
			index = start / 32;
			shiftl = start % 32;
			shiftr = 32 - shiftl;
			tmp = msm_gpio_chips[i].int_enable[!from_idle];
			smem_gpio->enabled[index] |= tmp << shiftl;
			smem_gpio->enabled[index+1] |= tmp >> shiftr;
			smem_gpio->detection[index] |= (
			readl(msm_gpio_chips[i].regs.int_edge) & tmp) << shiftl;
			smem_gpio->detection[index+1] |= (readl(msm_gpio_chips[i].regs.int_edge) & tmp) >> shiftr;
			smem_gpio->polarity[index] |= (readl(msm_gpio_chips[i].regs.int_pos) & tmp) << shiftl;
			smem_gpio->polarity[index+1] |= (readl(msm_gpio_chips[i].regs.int_pos) & tmp) >> shiftr;
		}
		
	}

	if (smem_gpio) {
		if (msm_gpio_debug_mask & GPIO_DEBUG_SLEEP)
			for (i = 0; i < ARRAY_SIZE(smem_gpio->enabled); i++) {
				printk("msm_gpio_enter_sleep gpio %d-%d: enable"
				       " %08x, edge %08x, polarity %08x\n",
				       i * 32, i * 32 + 31,
				       smem_gpio->enabled[i],
				       smem_gpio->detection[i],
				       smem_gpio->polarity[i]);
			}
		for(i = 0; i < GPIO_SMEM_NUM_GROUPS; i++) {
			smem_gpio->num_fired[i] = 0;
			for(j=0;j<GPIO_SMEM_MAX_PC_INTERRUPTS;j++)
				smem_gpio->fired[i][j]=0;
		}
			
	}
}

void msm_gpio_exit_sleep(void)
{
	int i;
	struct tramp_gpio_smem *smem_gpio;

	BUILD_BUG_ON(ARRAY_SIZE(msm_gpio_chips) != ARRAY_SIZE(smem_gpio->enabled));

	smem_gpio = smem_alloc(SMEM_GPIO_INT, sizeof(*smem_gpio)); 

	for (i = 0; i < ARRAY_SIZE(msm_gpio_chips); i++) {
		writel(msm_gpio_chips[i].int_enable[0], msm_gpio_chips[i].regs.int_en);
	}
	if (smem_gpio && (smem_gpio->num_fired[0] || smem_gpio->num_fired[1])) {
		if (msm_gpio_debug_mask & GPIO_DEBUG_SLEEP)
			printk(KERN_INFO "gpio: fired %x %x\n",
			      smem_gpio->num_fired[0], smem_gpio->num_fired[1]);
		tasklet_schedule(&msm_gpio_sleep_int_tasklet);
	}
//	msm_gpio_set_function(DEX_GPIO_CFG(97,1,GPIO_INPUT,GPIO_NO_PULL,GPIO_2MA,0));
}

void msm_gpio_set_function(struct msm_gpio_config cfg)
{
        unsigned addr;
        unsigned config;
        unsigned long flags;
        spin_lock_irqsave(&msm_gpio_lock, flags);

        if (cfg.gpio < 16 || cfg.gpio > 42)
                addr = (unsigned)(MSM_GPIOCFG1_BASE + 0x20);
        else
                addr = (unsigned)(MSM_GPIOCFG2_BASE + 0x410);

        if ( ! addr || ! (addr+0x04) )
        {
                printk(KERN_WARNING "%s: could not find addr\n", __func__);
                spin_unlock_irqrestore(&msm_gpio_lock, flags);
                return;
        }
        writel(cfg.gpio, addr);
        config = (cfg.drvstr<<6) | (cfg.func<<2) | (cfg.pull);
        writel(config, addr + 0x04);
	printk("msm_gpio_set_function(%x, %x)\n",cfg.gpio, config);
        if (readl(addr) != cfg.gpio)
                printk(KERN_WARNING "%s: could not set alt func %u => %u\n", __func__, cfg.gpio, cfg.func);
        if (cfg.dir)
                gpio_direction_output(cfg.gpio, cfg.out_op);
        else
                gpio_direction_input(cfg.gpio);
        spin_unlock_irqrestore(&msm_gpio_lock, flags);
}

static int __init msm_init_gpio(void)
{
	int i;

	for (i = NR_MSM_IRQS; i < NR_MSM_IRQS + NR_GPIO_IRQS; i++) {
		set_irq_chip(i, &msm_gpio_irq_chip);
		set_irq_handler(i, handle_edge_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	for (i = 0; i < ARRAY_SIZE(msm_gpio_chips); i++) {
		writel(0, msm_gpio_chips[i].regs.int_en);
		register_gpio_chip(&msm_gpio_chips[i].chip);
	}

	set_irq_chained_handler(INT_GPIO_GROUP1, msm_gpio_irq_handler);
	set_irq_chained_handler(INT_GPIO_GROUP2, msm_gpio_irq_handler);
	set_irq_wake(INT_GPIO_GROUP1, 1);
	set_irq_wake(INT_GPIO_GROUP2, 2);
	return 0;
}

postcore_initcall(msm_init_gpio);
