/* drivers/input/touchscreen/msm_ts.c
 *
 * By Octavian Voicu <octavian@voicu.gmail.com>
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

#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/fb_helper.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/msm_iomap.h>
#include <mach/msm_fb.h>

#define MSM_TS_DEBUG		0

#define MSM_TS_ABS_X_MIN	0
#define MSM_TS_ABS_X_MAX	479
#define MSM_TS_ABS_Y_MIN	0
#if defined(CONFIG_MACH_HTCBLACKSTONE) || defined(CONFIG_MACH_HTCKOVSKY)
#define MSM_TS_ABS_Y_MAX	799
#else
#define MSM_TS_ABS_Y_MAX	639
#endif
#define MSM_TS_ABS_PRESSURE_MIN 0
#define MSM_TS_ABS_PRESSURE_MAX 1
#define MSM_TS_ABS_SIZE_MIN 0
#define MSM_TS_ABS_SIZE_MAX 15

#define MSM_TS_LCD_WIDTH	480
#if defined(CONFIG_MACH_HTCBLACKSTONE) || defined(CONFIG_MACH_HTCKOVSKY)
#define MSM_TS_LCD_HEIGHT	800
#else
#define MSM_TS_LCD_HEIGHT	640
#endif

/* Touchscreen registers */
#define TSSC_CTL		0x100
#define TSSC_STATUS		0x10c
#define TSSC_DATA_LO_AVE	0x110
#define TSSC_DATA_UP_AVE	0x114


/* returns whether position was inside vkeyb, so as to eat event */

typedef int msm_ts_handler_t(int, int, int);
msm_ts_handler_t *msm_ts_handler; // virtual keyboard handler
extern msm_ts_handler_t *msm_ts_handler_pad; // blackstone handler

  
/* Work used by the polling mechanism after an interrupt has fired */
static void msm_ts_process_irq1(struct work_struct *work);
static void msm_ts_process_irq2(struct work_struct *work);
static void msm_ts_process_timeout(struct work_struct *work);
static DECLARE_WORK(msm_ts_work_irq1, msm_ts_process_irq1);
static DECLARE_WORK(msm_ts_work_irq2, msm_ts_process_irq2);
static DECLARE_DELAYED_WORK(msm_ts_work_timeout, msm_ts_process_timeout);

/* Function which processes the touchscreen data */
static void msm_ts_process_data(int irq);

static struct input_dev *msm_ts_dev;

static int calib_step, calib_xmin, calib_xmax, calib_ymin, calib_ymax, rawts=0;

/* lavender.t: (temporarily) add a kernel option to set calibration. */
static int __init msmts_calib_setup(char *str)
{
	int xmin, ymin, xmax, ymax;
	char* endp;
	int ok = 0;

	endp = NULL;
	xmin = simple_strtol(str, &endp, 0);

	if (endp > str && *endp == '.')
	{
		str = endp + 1;
		endp = NULL;
		ymin = simple_strtol(str, &endp, 0);

		if (endp > str && *endp == '.')
		{
			str = endp + 1;
			endp = NULL;
			xmax = simple_strtol(str, &endp, 0);

			if (endp > str && *endp == '.')
			{
				str = endp + 1;
				endp = NULL;
				ymax = simple_strtol(str, &endp, 0);

				if (endp > str)
				{
					calib_step = 3;
					calib_xmin = xmin;
					calib_ymin = ymin;
					calib_xmax = xmax;
					calib_ymax = ymax;
					ok = 1;
				}
			}
		}
	}

	return ok;
}

__setup("msmts_calib=", msmts_calib_setup);
module_param_named(raw, rawts, int, S_IRUGO | S_IWUSR | S_IWGRP);


static void msm_ts_predma_callback(struct fb_info *fb, struct msmfb_update_area *area)
{
	int x, y;

	/* Draw two red dots during calibration (in upper-left and lower-right corners) */
	if (calib_step > 0 && calib_step < 3) {
		struct fb_info *fb = fb_helper_get_first_fb();
		
		for (x = 0;x < 3;x++) {
			for (y = 0;y < 3;y++) {
				fb_helper_plot(fb, x,y,0xF800);
				fb_helper_plot(fb, MSM_TS_LCD_WIDTH - 1 - x, MSM_TS_LCD_HEIGHT - 1 - y, 0xF800);
			}
		}

		/* Make it a full screen update */
		area->x = 0;
		area->y = 0;
		area->width = fb->var.xres;
		area->height = fb->var.yres;
	}
}

static void msm_ts_process_irq1(struct work_struct *tmp_work)
{
	msm_ts_process_data(INT_TCHSCRN1);
}

static void msm_ts_process_irq2(struct work_struct *tmp_work)
{
	msm_ts_process_data(INT_TCHSCRN2);
}

static void msm_ts_process_timeout(struct work_struct *tmp_work)
{
#if MSM_TS_DEBUG
	printk(KERN_DEBUG "msm_ts: release interrupt didn't fire, sending pen up event manually\n");
#endif
	msm_ts_process_data(0);
}

static void msm_ts_process_data(int irq)
{
	unsigned long status, data;
	int absx, absy, touched, x=0, y=0;
	int vkey=0, bspad=0;
	static int prev_absx = -1, prev_absy = -1, prev_touched = -1;
	unsigned long data2;
	int x2,y2,ax,bx,ay,by,cx,cy,size;

	/* Read status and data */
	status = readl(MSM_TS_BASE + TSSC_STATUS);
	data = readl(MSM_TS_BASE + TSSC_DATA_LO_AVE);

	/* Get stylus position and press state */
	absx = data & 0xFFFF;
	absy = data >> 16;
	//touched = status & 0x100 ? 1 : 0;
	touched = status & 0x200 ? 0 : 1; // The 0x200 flag seems to be more reliable

	/* When pen released, we can disable our timeout */
	if (!touched) {
		cancel_delayed_work(&msm_ts_work_timeout);
	}

	/* When an interrupt timeout was reached, we assume pen released */
	if (!irq) {
		absx = absy = touched = 0;
	}

	/* Filter invalid/unreliable data. Based on:
	 * - When touched, the 0x800 flag must be present in CTL. We remove the 0x800 flag when we're done with it.
	 * - When touched, we verify that the 0x2000 flag is present in STATUS. When this is not the case, it has been seen there are some weird
	 *   x/y values. Perhaps the hardware is updating while that flag is gone? The 0x800 flag is present in this case, which is why
	 *   that check is not sufficient.
	 */
	if (touched && (readl(MSM_TS_BASE + TSSC_CTL) & 0x800) == 0) {
#if MSM_TS_DEBUG
		printk(KERN_DEBUG "msm_ts: invalid data (no 0x800 flag in CTL)\n");
#endif
		goto skip;
	}
	if (touched && (status & 0x2000) == 0) {
#if MSM_TS_DEBUG
		printk(KERN_DEBUG "msm_ts: invalid data (no 0x2000 flag in STATUS)\n");
#endif
		goto skip;
	}

	/* Only do something when the touch state or position have changed */
	if (absx != prev_absx || absy != prev_absy || touched != prev_touched) {
		/* First three tap are to calibrate upper-left and lower-right corners */
		if (calib_step < 3 && !rawts) {
			if (touched && !prev_touched) {
				if (calib_step == 1) {
					/* This is upper-left corner */
					calib_xmax = absx;
					calib_ymin = absy;
					printk(KERN_WARNING "msm_ts: maxx=%04x miny=%04x\n", calib_xmax, calib_ymin);
					printk(KERN_WARNING "Calibration step 2: tap red dot in lower-right corner\n");
				} else if(calib_step == 2) {
					/* This is lower-right corner */
					calib_xmin = absx;
					calib_ymax = absy;
					printk(KERN_WARNING "msm_ts: minx=%04x maxy=%04x\n", calib_xmin, calib_ymax);
					printk(KERN_WARNING "Calibration successful!\n\n# ");
				} else {
					printk(KERN_WARNING "Calibration step 1: tap red dot in upper-left corner\n");
				}
				calib_step++;
			}
		} else {
			/* On stylus up, use same coordinates as last position we had (since they're 0 otherwise) */
			if (touched) {
				data2 = readl(MSM_TS_BASE + TSSC_DATA_UP_AVE);
				x2 = data2 & 0xFFFF;
				y2 = data2 >> 16;
				
				if (!x2 || !y2)
					goto skip;

				ax =  300 - 100 * (absx +  6 * y2 / 11 -  200) / x2;
				ay = -240 + 100 * (absy +  7 * x2 / 10 + 1100) / y2;
				bx = -110 - 200 * (absx -  3 * x2      -  200) / y2;
				by =   70 + 100 * (absy - 12 * y2 /  5 + 1100) / x2;
				cx =  200 -        absx +  3 * x2      -    6  * y2 / 11;
				cy = 1100 +        absy - 12 * y2 /  5 +    7  * x2 / 10;
								
				size = (ax+bx+cx+ay+by+cy - 200) / 64;
				if (size <= MSM_TS_ABS_SIZE_MIN)
					size = MSM_TS_ABS_SIZE_MIN + 1;
				else if (size > MSM_TS_ABS_SIZE_MAX)
					size = MSM_TS_ABS_SIZE_MAX;
				
//				printk(KERN_INFO "TS: %3d %3d %3d  %3d %3d %3d  %4d\n",
//						ax,bx,cx,ay,by,cy,size);
			} else {
				absx = prev_absx;
				absy = prev_absy;
				size = MSM_TS_ABS_SIZE_MIN;
			}

			/* Calculate coordinates in pixels, clip to screen and make sure we don't to divisions by zero */
			if(!rawts) {
				x = MSM_TS_LCD_WIDTH - 1 - (absx - calib_xmin) * MSM_TS_LCD_WIDTH / (calib_xmax == calib_xmin ? MSM_TS_LCD_HEIGHT : calib_xmax - calib_xmin);
				if (x < 0) x = 0;
				if (x >= MSM_TS_LCD_WIDTH) x = MSM_TS_LCD_WIDTH - 1;
				y = (absy - calib_ymin) * MSM_TS_LCD_HEIGHT / (calib_ymax == calib_ymin ? MSM_TS_LCD_HEIGHT : calib_ymax - calib_ymin);
				if (y < 0) y = 0;
				if (y >= MSM_TS_LCD_HEIGHT) y = MSM_TS_LCD_HEIGHT - 1;

				/* Call our handler if it's registered -- the virtual keyboards gets data from this */
				if (msm_ts_handler_pad && !rawts) {
					bspad = (*msm_ts_handler_pad)(x, y, prev_touched);
				} else {
					bspad = 0;
				}
				if (msm_ts_handler && !rawts) {
					vkey = (*msm_ts_handler)(x, y, touched);
				} else {
					vkey = 0;
				}

#if MSM_TS_DEBUG
				printk(KERN_DEBUG "msm_ts: x=%d,y=%d,t=%d,vkeyb=%d\n", x, y, touched, vkey);
#endif
			} else {
#if MSM_TS_DEBUG
				printk(KERN_DEBUG "msm_ts: absx=%d,absy=%d,t=%d\n", absx, absy, touched);
#endif
			}
			
			/* Send data to linux input system, if not eaten by vkeyb */
			if(rawts) {
				input_report_abs(msm_ts_dev, ABS_X, absx);
				input_report_abs(msm_ts_dev, ABS_Y, absy);
				input_report_abs(msm_ts_dev, ABS_PRESSURE, touched ? MSM_TS_ABS_PRESSURE_MAX : MSM_TS_ABS_PRESSURE_MIN);
				input_report_abs(msm_ts_dev, ABS_TOOL_WIDTH, size);
				input_report_key(msm_ts_dev, BTN_TOUCH, touched);
				input_sync(msm_ts_dev);
			} else if (!(vkey || bspad)) {
				input_report_abs(msm_ts_dev, ABS_X, x);
				input_report_abs(msm_ts_dev, ABS_Y, y);
				input_report_abs(msm_ts_dev, ABS_PRESSURE, touched ? MSM_TS_ABS_PRESSURE_MAX : MSM_TS_ABS_PRESSURE_MIN);
				input_report_abs(msm_ts_dev, ABS_TOOL_WIDTH, size);
				input_report_key(msm_ts_dev, BTN_TOUCH, touched);
				input_sync(msm_ts_dev);
			}
		}

		/* Save the state so we won't report the same position and state twice */
		prev_absx = touched ? absx : 0;
		prev_absy = touched ? absy : 0;
		prev_touched = touched;
	}

skip:
	/* Indicate to the hardware that we have handled the data and the touchscreen may update it again */
	msleep(10);
	if (irq) {
		enable_irq(irq);
		writel(readl(MSM_TS_BASE + TSSC_CTL) & ~0x800, MSM_TS_BASE + TSSC_CTL);
	}
}

static irqreturn_t msm_ts_interrupt(int irq, void *dev)
{
	// Disable the irq until we processed it.
	disable_irq(irq);

	// Schedule work based on which irq was triggered
	switch (irq) {
		case INT_TCHSCRN1:
			schedule_work(&msm_ts_work_irq1);
			break;
		case INT_TCHSCRN2:
			schedule_work(&msm_ts_work_irq2);
			break;
	}

	// Reset the timeout. When we don't receive irq's for a little while, we assume the pen no longer touches the surface. As this
	// is not always notified by an irq, we have a fallback in which we timeout.
	cancel_delayed_work(&msm_ts_work_timeout);
	schedule_delayed_work(&msm_ts_work_timeout, HZ / 5);

	return IRQ_HANDLED;
}

static int __init msm_ts_init(void)
{
	int err;
#if defined CONFIG_TOUCHSCREEN_TSSC_MANAGER 
	if(!machine_is_htckovsky()) {
		printk("\"Old\" touchscreen driver called for a non-blackstone device\n");
		printk("If you really want that, edit the msm_ts.c file and delete this check\n");
		return 0;
	}
#endif
	printk(KERN_INFO "msm_ts: initing\n");

	/* We depend on a framebuffer for painting calibration dots */
	if (num_registered_fb == 0 && !rawts) {
		printk(KERN_INFO "msm_ts: no framebuffer registered, cannot paint calibration dots\n");
	}

	/* Remove the 0x800 flag, in case they were still set, which would prevent us from getting interrupts */
        writel(readl(MSM_TS_BASE + TSSC_CTL) & ~0x800, MSM_TS_BASE + TSSC_CTL);

	/* Configure input device */
	msm_ts_dev = input_allocate_device();
	if (!msm_ts_dev)
		return -ENOMEM;

	msm_ts_dev->evbit[0] = BIT_MASK(EV_ABS) | BIT_MASK(EV_KEY);
	msm_ts_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	if(!rawts) {
		input_set_abs_params(msm_ts_dev, ABS_X, MSM_TS_ABS_X_MIN, MSM_TS_ABS_X_MAX, 0, 0);
		input_set_abs_params(msm_ts_dev, ABS_Y, MSM_TS_ABS_Y_MIN, MSM_TS_ABS_Y_MAX, 0, 0);
	} else {
		input_set_abs_params(msm_ts_dev, ABS_X, 0, 0xffff, 0, 0);
		input_set_abs_params(msm_ts_dev, ABS_Y, 0, 0xffff, 0, 0);
	}
	input_set_abs_params(msm_ts_dev, ABS_PRESSURE, MSM_TS_ABS_PRESSURE_MIN, MSM_TS_ABS_PRESSURE_MAX, 0, 0);
	input_set_abs_params(msm_ts_dev, ABS_TOOL_WIDTH, MSM_TS_ABS_SIZE_MIN, MSM_TS_ABS_SIZE_MAX, 0, 0);

	msm_ts_dev->name = "MSM touchscreen";
	msm_ts_dev->phys = "msm_ts/input0";

	/* Register the interrupt handler. This IRQ is fired when a touch is detected */
	if (request_irq(INT_TCHSCRN1, msm_ts_interrupt, IRQF_TRIGGER_FALLING, "msm_ts", 0) < 0) {
		printk(KERN_ERR "msm_ts: can't allocate irq %d\n", INT_TCHSCRN1);
		err = -EBUSY;
		goto fail1;
	}

	/* This IRQ is fired when a release is detected. It is not 100% reliable. */
	if (request_irq(INT_TCHSCRN2, msm_ts_interrupt, IRQF_TRIGGER_FALLING, "msm_ts", 0) < 0) {
		printk(KERN_ERR "msm_ts: can't allocate irq %d\n", INT_TCHSCRN2);
		err = -EBUSY;
		goto fail2;
	}

	/* Register the input device */
	err = input_register_device(msm_ts_dev);
	if (err)
		goto fail3;

	/* Register the msmfb pre-dma callback */
	if(!rawts) {
		if (msmfb_predma_register_callback(msm_ts_predma_callback) != 0) {
			printk(KERN_WARNING "msm_ts: unable to register pre-dma callback, calibration dots cannot be drawn\n");
		}
	}

	/* Done */
	printk(KERN_WARNING "msm_ts: init successful\n");

	return 0;

 fail3:
	free_irq(INT_TCHSCRN2, NULL);
 fail2:
	free_irq(INT_TCHSCRN1, NULL);
	cancel_delayed_work(&msm_ts_work_timeout);
	flush_scheduled_work();
 fail1:	
	input_free_device(msm_ts_dev);
	return err;
}

static void __exit msm_ts_exit(void)
{
	msmfb_predma_unregister_callback(msm_ts_predma_callback);
	free_irq(INT_TCHSCRN1, NULL);
	cancel_delayed_work(&msm_ts_work_timeout);
	flush_scheduled_work();
	input_unregister_device(msm_ts_dev);
}

module_init(msm_ts_init);
module_exit(msm_ts_exit);

MODULE_AUTHOR("Octavian Voicu, octavian.voicu@gmail.com");
MODULE_DESCRIPTION("MSM touchscreen driver");
MODULE_LICENSE("GPL");
