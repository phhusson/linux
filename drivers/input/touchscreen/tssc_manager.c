/* drivers/input/touchscreen/tssc_manager.c * * Copyright (C) 2008 HTC, Inc.
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

#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/string.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <mach/msm_iomap.h>
#include <mach/msm_tssc.h>
#include <linux/earlysuspend.h>
#include <linux/rtc.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
static void tssc_manager_early_suspend(struct early_suspend *h);
static void tssc_manager_late_resume(struct early_suspend *h);
#endif

typedef int msm_ts_handler_t(int, int, int);
msm_ts_handler_t *msm_ts_handler_pad; // blackstone handler

/// For calibration, display the reference points.
static ssize_t tssc_calibration_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return calibration_show(buf);
}

/// For calibration, store the reference points.
static ssize_t tssc_calibration_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	calibration_store(buf);

	return count;
}

static ssize_t tssc_calibration_points_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return calibration_points_show(buf);
}

static ssize_t tssc_calibration_points_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	calibration_points_store(buf);

	return count;
}

static ssize_t tssc_calibration_screen_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return calibration_screen_show(buf);
}

static ssize_t tssc_calibration_screen_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	calibration_screen_store(buf);

	return count;
}

static ssize_t tssc_calibration_mfg_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return calibration_mfg_show(buf);
}
static ssize_t tssc_calibration_mfg_store(struct device *dev,
				    struct device_attribute *attr, const char *buf, size_t count)
{
	calibration_mfg_store(buf);
	return count;
}
/// sys/class/input/input1/calibration
static DEVICE_ATTR(calibration, 0666, tssc_calibration_show, tssc_calibration_store);

/// sys/class/input/input1/calibration_points
static DEVICE_ATTR(calibration_points, 0666, tssc_calibration_points_show, tssc_calibration_points_store);

/// sys/class/input/input1/calibration_screen
static DEVICE_ATTR(calibration_screen, 0666, tssc_calibration_screen_show, tssc_calibration_screen_store);
/// sys/class/input/input1/calibration_mfg
static DEVICE_ATTR(calibration_mfg, 0666, tssc_calibration_mfg_show, tssc_calibration_mfg_store);

#define TSSC_MANAGER_NAME "tssc-manager"

#define INT_TCHSCRN1         30
#define INT_TCHSCRN2         31
static unsigned int msm7225_irq_down = INT_TCHSCRN1;

struct tssc_manager_data {
	struct input_dev *input_dev;
	int use_irq;
	struct hrtimer timer;
	struct work_struct work;
	struct work_struct polling_work;
	int x;
	int x16;
	int y;
	int y16;
	int z1;
	int z2;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};


//----------------------------------------------
#define ENABLE_TSSC_AVERAGE

#define TOUCH_POLLING_NSEC 8400000//9000000//
#define TOUCH_QUEUE_NUMBER 2//3

static long touch_sample_count = 0;
static long touch_report_count = 0;
static int touch_queue_index = 0;
static bool first_touch = true;
//static int disable_msm7225_irq_down = 0;

int touch_queue_x[TOUCH_QUEUE_NUMBER];
int touch_queue_y[TOUCH_QUEUE_NUMBER];
int touch_queue_p[TOUCH_QUEUE_NUMBER];
int touch_deviation_x[TOUCH_QUEUE_NUMBER];
int touch_deviation_y[TOUCH_QUEUE_NUMBER];
int touch_average_x;
int touch_average_y;
int touch_average_p;
long total_x = 0;
long total_y = 0;

#define TOUCH_MOVE_VALUE 30
#define XY_temp 7//10//5//
int touch_noise_index = TOUCH_QUEUE_NUMBER;

struct msm_tssc_ssbi *tssc_codec = (struct msm_tssc_ssbi *)MSM_SSBI_BASE;
struct msm_tssc_software_register *tssc_reg = (struct msm_tssc_software_register *)(MSM_TSSC_BASE + 0x100);

enum {
	DEBUG_TP_OFF = 0,
	DEBUG_TP_ON = 1,
};
static int debug_tp;
module_param_named(debug, debug_tp, int, S_IRUGO | S_IWUSR | S_IWGRP);

//----------------------------------------------

/* The Touchscreen of Blackstone tends to track
 * wrong data. We have to check the input to prevent 
 * jumps across the screen */
static int blackstone_noise_check(int x, int y, int p)
{
	#define MAX_JUMP 160
	#define FIRST_JUMP 30
	static int last_x = 0;
	static int last_y = 0;
	static int last_p = 0;

	/* The first input often is far off. 
	 * We ignore it*/
	if (!touch_sample_count && first_touch) {
		first_touch = false;
		return 1;
	}
	
	
	/* the second input can also be off
	 * this only happend when pressure is low
	 * we replace the first input with the second one*/
	if (touch_sample_count==1 && p==5) {
		if (abs(x-touch_queue_x[0])>FIRST_JUMP || abs(y-touch_queue_y[0])>FIRST_JUMP) {
			touch_queue_x[0] = x;
			touch_queue_y[0] = y;
			return 1;
		}
	}

	/* even more jumps... */
	if (touch_sample_count>1) {
	/* jumps can be caused by significant changes in pressure */
		if (p<100 || abs(last_p-p)>50) {
			if (abs(x-last_x)>50 || abs(y-last_y)>50) {
				return 1;
			}
		}
		/* nobody can slide his finger faster than 160 units
		 * per input. It has to be a jump */
		if (abs(x-last_x)>MAX_JUMP || abs(y-last_y)>MAX_JUMP) {
			return 1;
		}
	}
	
	last_x = x;
	last_y = y;
	last_p = p;
	return 0;
}


static int touch_check_noise(int x, int x16, int y, int y16, int p)
{
    struct timespec ts;
	struct rtc_time tm;
	
	int X_temp=abs(x-x16);
	int Y_temp=abs(y-y16);

	if(machine_is_htcblackstone()) 	
		if (blackstone_noise_check(x,y,p))
			return 1;
		

	if (x16>0 && y16>0) {
		if ((X_temp>=XY_temp)||(Y_temp>=XY_temp)) {
        		if(debug_tp & DEBUG_TP_ON) printk(KERN_DEBUG "touch_check_noise(): This is a noise point.\t");
        		if(debug_tp & DEBUG_TP_ON) printk(KERN_DEBUG "x=%d y=%d x16=%d y16=%d\t", x, y, x16, y16);
        
	    		getnstimeofday(&ts);
	    		rtc_time_to_tm(ts.tv_sec, &tm);
        		if(debug_tp & DEBUG_TP_ON) pr_info("(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n",
        		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
                                
        		return 1;
		} else {
        		return 0;
    		}
	} else {
        	return 0;
    	}
}

/*
 * Add touch point data into the queue.
 */
static int touch_add_queue(int x, int x16, int y, int y16,int p)
{
    	if(touch_check_noise(x, x16, y, y16, p))
    	{
    	    return 0;
    	}

	touch_queue_x[touch_queue_index] = x;
	touch_queue_y[touch_queue_index] = y;
	touch_queue_p[touch_queue_index] = p;

	touch_queue_index++;

	if (touch_queue_index >= TOUCH_QUEUE_NUMBER)
		touch_queue_index = 0;

	//touch_sample_count++;
	return ++touch_sample_count;
}



/*
 * Report touch up input event.
 */
static void touch_input_up(struct input_dev *dev)
{
	if (msm_ts_handler_pad) 
		(*msm_ts_handler_pad)(0, 0, 0);
	
	if (touch_report_count > 0) {
		input_report_abs(dev, ABS_PRESSURE, 0);
		input_report_abs(dev, ABS_TOOL_WIDTH, 0);
		input_report_key(dev, BTN_TOUCH, 0);
		input_report_key(dev, BTN_2, 0);
		input_sync(dev);
		touch_report_count = 0; /* reset report count */
	}
}

static void touch_get_average(void)
{
        int i;
        long total_x = 0;
        long total_y = 0;
        long total_p = 0;

        for (i = 0; i < TOUCH_QUEUE_NUMBER; i++) {
                total_x += touch_queue_x[i];
                total_y += touch_queue_y[i];
                total_p += touch_queue_p[i];
        }

        touch_average_x = total_x / TOUCH_QUEUE_NUMBER;
        touch_average_y = total_y / TOUCH_QUEUE_NUMBER;
        touch_average_p = total_p / TOUCH_QUEUE_NUMBER;
}

/*
 * Process the current touch point in the queue.
 */
static void touch_process_queue(struct input_dev *dev)
{
	int x = 0;
	int y = 0;
	int bspad = 0;
	
    	struct timespec ts;
    	struct rtc_time tm;

	if (touch_sample_count <= 1) {
		if (touch_sample_count == 1) {
    			if(debug_tp & DEBUG_TP_ON) printk(KERN_DEBUG "touch_process_queue(): This is point #%ld.\t", touch_sample_count);

        		getnstimeofday(&ts);
        		rtc_time_to_tm(ts.tv_sec, &tm);
        		if(debug_tp & DEBUG_TP_ON) pr_info("(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n",
        		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
    		}
		return;
    	}

	if (touch_sample_count >= TOUCH_QUEUE_NUMBER)
		touch_get_average();
	calibration_translate(touch_average_x, touch_average_y, &x, &y);

	if (msm_ts_handler_pad) 
		bspad = (*msm_ts_handler_pad)(x, y, 1);
	
	if (!bspad && x >= 0 && y >= 0) {
		if(debug_tp & DEBUG_TP_ON) printk(KERN_DEBUG "touch_process_queue(): x=%d\t", x);
	        input_report_abs(dev, ABS_X, x);
	        if(debug_tp & DEBUG_TP_ON) printk(KERN_DEBUG "y=%d\t", y);
	        input_report_abs(dev, ABS_Y, y);
	        if(debug_tp & DEBUG_TP_ON) printk(KERN_DEBUG "p=%d\n", touch_average_p);
	        input_report_abs(dev, ABS_PRESSURE, touch_average_p);
	        input_report_abs(dev, ABS_TOOL_WIDTH, 1);
	        input_report_key(dev, BTN_TOUCH, 1);
	        input_report_key(dev, BTN_2, 0);
	        input_sync(dev);

	        touch_report_count++;
	}
}

/*
 * Process the final touch points in the queue.
 */
static void touch_finish_queue(struct input_dev *dev)
{
	touch_sample_count = 0;
	touch_queue_index = 0;


	touch_input_up(dev);
}

static void tssc_manager_work_func(struct work_struct *work)
{
	struct tssc_manager_data *ts;
	
	ts = container_of(work, struct tssc_manager_data, work);
    
	
	hrtimer_start(&ts->timer, ktime_set(0, 0), HRTIMER_MODE_REL);
}

static int tssc_manager_polling_func(struct tssc_manager_data *ts)
{

	//struct tssc_manager_data *ts;

	int pressure;

	//ts = container_of(polling_work, struct tssc_manager_data, polling_work);

 
	if (tssc_reg->tssc_ctl.data_flag == 0) { /* Check if TSSC data ready */
		//tssc_reg->tssc_ctl.command_wr = 1; /* Trigger next operation */

		if (tssc_reg->tssc_ctl.data_flag == 0) { /* Try again */
			ts->x = 0;
			ts->y = 0;
			ts->z1 = 0;
			ts->z2 = 0;

			/* When data is not available, we need to differentiate
			 * whether this is a queue finish event, or a missing
			 * data glitch. Otherwise, false "double taps" get reported.
			 * -bzo
			 */
			if ((tssc_reg->tssc_status.penirq_status != 1) || (tssc_reg->tssc_status.busy != 0)) {
				tssc_reg->tssc_ctl.intr_flag1 = 0;
				return 0;
			}
		}
	}

	if (tssc_reg->tssc_ctl.data_flag) { /* TSSC data is valid */
#ifdef ENABLE_TSSC_AVERAGE
		ts->x = tssc_reg->tssc_avg_12.samples_avg_1;
		ts->y = tssc_reg->tssc_avg_12.samples_avg_2;
		ts->z1 = tssc_reg->tssc_avg_34.samples_avg_3;
		ts->z2 = tssc_reg->tssc_avg_34.samples_avg_4;
#else
		ts->x = tssc_reg->tssc_sample_1_1.raw_sample_1;
		ts->x16 = tssc_reg->tssc_sample_1_8.raw_sample_16;
		ts->y = tssc_reg->tssc_sample_2_1.raw_sample_1;
		ts->y16 = tssc_reg->tssc_sample_2_8.raw_sample_16;
		ts->z1 = tssc_reg->tssc_sample_3_1.raw_sample_1;
		ts->z2 = tssc_reg->tssc_sample_4_1.raw_sample_1;
#endif
	}

	tssc_reg->tssc_ctl.intr_flag1 = 0; /* Clear INTR_FLAG1 for next point */
	tssc_reg->tssc_ctl.data_flag = 0; /* Clear DATA_FLAG for next point */
	
	if(debug_tp & DEBUG_TP_ON) printk(KERN_DEBUG "tssc_manager_polling_func(): ts->z1=%d ts->z2=%d\n", ts->z1, ts->z2);
	if (ts->z1) {
		pressure = (ts->x * ((ts->z2 * 100 / ts->z1) - 100)) / 900;
		//pressure = 350 - pressure;
		pressure = 270 - pressure;
		
		if (pressure <= 5)
		    pressure = 5; /* Report all touch points */
		else if (pressure > 255)
		    pressure = 255;
	} else {
		pressure = 0;
		//printk(KERN_DEBUG "tssc_manager_polling_func(): ts->z1==0\n");
    	}

	if (ts->x!=0 && ts->y!=0 && ts->z1!=0 && ts->z2!=0 && pressure) {
		if (touch_add_queue(ts->x, ts->x16, ts->y, ts->y16, pressure)) {
			touch_process_queue(ts->input_dev);
	    	}
	} else {
        //printk(KERN_DEBUG "tssc_manager_polling_func(): !(ts->x!=0 && ts->y!=0 && ts->z1!=0 && ts->z2!=0)\n");
        //printk(KERN_DEBUG "tssc_manager_polling_func(): pressure==0)\n");
    	}

	if ((tssc_reg->tssc_status.penirq_status == 1) && (tssc_reg->tssc_status.busy == 0)) {
		touch_finish_queue(ts->input_dev);
	    	//hrtimer_cancel(&ts->timer);
        	enable_irq(msm7225_irq_down);
			first_touch=true;
        	return 1;
	}
    	return 0;
}

static enum hrtimer_restart tssc_polling_timer_func (struct hrtimer *timer)
{
	struct tssc_manager_data *ts = container_of(timer, struct tssc_manager_data, timer);

	//schedule_work(&ts->polling_work);
	if(!tssc_manager_polling_func(ts))
	{
		hrtimer_start(&ts->timer, ktime_set(0, TOUCH_POLLING_NSEC), HRTIMER_MODE_REL);
    	}		
	return HRTIMER_NORESTART;
}

static irqreturn_t tssc_manager_irq_down_handler(int irq, void *dev_id)
{
	struct tssc_manager_data *ts = dev_id;
	disable_irq(msm7225_irq_down);
	schedule_work(&ts->work);

	return IRQ_HANDLED;
}

static void tssc_power_on(void)
{
	// Set highest priority of SSBI port to TSSC.
	tssc_codec->priorities.priority0 = 0x2;
	tssc_codec->priorities.priority1 = 0x1;
	tssc_codec->priorities.priority2 = 0x0;


	// Enable TSSC.
	tssc_reg->tssc_ctl.enable = 0x1;


	// Reset TSSC.
	tssc_reg->tssc_ctl.tssc_sw_reset = 0x1;


	// Enable TSSC.
	tssc_reg->tssc_ctl.enable = 0x1;


	// Master mode.
	tssc_reg->tssc_ctl.mode = 0x3;


	// Enable the averaging function.
#ifdef ENABLE_TSSC_AVERAGE
	tssc_reg->tssc_ctl.en_average = 0x1;
#else
	tssc_reg->tssc_ctl.en_average = 0x0;
#endif


	// Enable the debounce logic inside TSSC.
	tssc_reg->tssc_test_1.gate_debounce_en = 0x1;
	tssc_reg->tssc_ctl.debounce_en = 0x1;


	// Debounce time = 400us.
	tssc_reg->tssc_ctl.debounce_time = 0x0;//0x7;//

	// Clear data flag to ready for subsequent sample.
	tssc_reg->tssc_ctl.data_flag = 0x0;

	// 10-bit resolution.
	tssc_reg->tssc_opn.resolution1 = 0x1;
	tssc_reg->tssc_opn.resolution2 = 0x1;
	tssc_reg->tssc_opn.resolution3 = 0x1;
	tssc_reg->tssc_opn.resolution4 = 0x1;


	// Number of samples.
#ifdef ENABLE_TSSC_AVERAGE
	tssc_reg->tssc_opn.num_sample1 = 0x3;	// 16 samples
	tssc_reg->tssc_opn.num_sample2 = 0x3;	// 16 samples
	tssc_reg->tssc_opn.num_sample3 = 0x1;	// 4 samples
	tssc_reg->tssc_opn.num_sample4 = 0x1;	// 4 samples
#else
	tssc_reg->tssc_opn.num_sample1 = 0x0;	// 1 sample
	tssc_reg->tssc_opn.num_sample2 = 0x0;	// 1 sample
	tssc_reg->tssc_opn.num_sample3 = 0x0;	// 1 sample
	tssc_reg->tssc_opn.num_sample4 = 0x0;	// 1 sample
#endif


	// Place holder for the operations in the master mode.
	// Reference: Touchscreen Operation for MSM7200 and MSM7500, Table 4-1.
	tssc_reg->tssc_opn.operation1 = 0x01; // X, 4-wire
	tssc_reg->tssc_opn.operation2 = 0x02; // Y, 4-wire
	tssc_reg->tssc_opn.operation3 = 0x03; // Z1, 4-wire
	tssc_reg->tssc_opn.operation4 = 0x04; // Z2, 4-wire


	// Specifies the sampling interval in milliseconds units: 1ms.
	tssc_reg->tssc_sampling_int.sampling_int = 0x1;//0x2;//0x3;//

	touch_sample_count = 0;
	touch_report_count = 0;
	touch_queue_index = 0;
}

static void tssc_power_off(void)
{
	tssc_reg->tssc_ctl.enable = 0x0;
}

static int tssc_manager_input_init(void)
{
	struct tssc_manager_data *ts;
	int ret = 0;
	int ret_down = 0;

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	INIT_WORK(&ts->work, tssc_manager_work_func);
	//INIT_WORK(&ts->polling_work, tssc_manager_polling_work_func);

	tssc_power_on();

	// Allocate input device.
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		printk(KERN_ERR "tssc_manager_input_init: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

	ts->input_dev->name = TSSC_MANAGER_NAME;
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(BTN_2, ts->input_dev->keybit);
	set_bit(EV_ABS, ts->input_dev->evbit);

	// Set input parameters boundary.
	input_set_abs_params(ts->input_dev, ABS_X, TP_X_MIN, TP_X_MAX, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, TP_Y_MIN, TP_Y_MAX, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_HAT0X, TP_X_MIN, TP_X_MAX, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_HAT0Y, TP_Y_MIN, TP_Y_MAX, 0, 0);

	ret = input_register_device(ts->input_dev);
	if (ret) {
		printk(KERN_ERR "tssc_manager_input_init: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	// Create device files.
	ret = device_create_file(&ts->input_dev->dev, &dev_attr_calibration);
	if (ret) {
		printk(KERN_ERR "tssc_manager_input_init: Error to create calibration attribute\n");
		goto err_input_register_device_failed;
	}
	ret = device_create_file(&ts->input_dev->dev, &dev_attr_calibration_points);
	if (ret) {
		printk(KERN_ERR "tssc_manager_input_init: Error to create calibration_points attribute\n");
		goto err_input_register_device_failed;
	}
	ret = device_create_file(&ts->input_dev->dev, &dev_attr_calibration_screen);
	if (ret) {
		printk(KERN_ERR "tssc_manager_input_init: Error to create calibration_screen attribute\n");
		goto err_input_register_device_failed;
	}
	ret = device_create_file(&ts->input_dev->dev, &dev_attr_calibration_mfg);
	if (ret) {
		printk(KERN_ERR "tssc_manager_input_init: Error to create calibration_mfg attribute\n");
		goto err_input_register_device_failed;
	}
	if (msm7225_irq_down) {
		ret_down = request_irq(msm7225_irq_down, tssc_manager_irq_down_handler,
			IRQF_TRIGGER_RISING, TSSC_MANAGER_NAME, ts);
	}

	if (ret_down == 0) {
		ts->use_irq = 1;
	} else {
		ts->use_irq = 0;
	}

	hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ts->timer.function = tssc_polling_timer_func ;
	//hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = tssc_manager_early_suspend;
	ts->early_suspend.resume = tssc_manager_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	printk(KERN_INFO "tssc_manager_input_init: Start touchscreen %s in %s mode\n",
			ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");
	calibration_init();

	return 0;

err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	kfree(ts);
err_alloc_data_failed:
	return ret;
}

static int tssc_manager_probe(struct platform_device *platform_dev)
{
	printk(KERN_INFO "tssc_manager_probe\n");

	return tssc_manager_input_init();
}

static int tssc_manager_remove(struct platform_device *platform_dev)
{
	struct tssc_manager_data *ts = dev_get_drvdata(&platform_dev->dev);

	printk(KERN_INFO "tssc_manager_remove\n");

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif

	if (ts->use_irq) {
		free_irq(msm7225_irq_down, ts);
	}

	hrtimer_cancel(&ts->timer);

	input_unregister_device(ts->input_dev);

	// Remove device files.
	device_remove_file(&ts->input_dev->dev, &dev_attr_calibration);
	device_remove_file(&ts->input_dev->dev, &dev_attr_calibration_points);
	device_remove_file(&ts->input_dev->dev, &dev_attr_calibration_screen);
	device_remove_file(&ts->input_dev->dev, &dev_attr_calibration_mfg);

	kfree(ts);
	return 0;
}

static int tssc_manager_suspend(struct tssc_manager_data *ts, pm_message_t mesg)
{

	if (ts->use_irq) {
		disable_irq(msm7225_irq_down);
	}

	//hrtimer_cancel(&ts->timer);
	//cancel_work_sync(&ts->work);
	//cancel_work_sync(&ts->polling_work);

	tssc_power_off();

	return 0;
}

static int tssc_manager_resume(struct tssc_manager_data *ts)
{

    printk(KERN_DEBUG "tssc_manager_resume(): ts->use_irq=%d\n", ts->use_irq);

	tssc_power_on();

	if (ts->use_irq) {
		enable_irq(msm7225_irq_down);
	}

	//hrtimer_start(&ts->timer, ktime_set(0, TOUCH_POLLING_NSEC), HRTIMER_MODE_REL);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void tssc_manager_early_suspend(struct early_suspend *h)
{
	struct tssc_manager_data *ts;
	ts = container_of(h, struct tssc_manager_data, early_suspend);
	tssc_manager_suspend(ts, PMSG_SUSPEND);
}

static void tssc_manager_late_resume(struct early_suspend *h)
{
	struct tssc_manager_data *ts;
	ts = container_of(h, struct tssc_manager_data, early_suspend);
	tssc_manager_resume(ts);
}
#endif

static struct platform_driver tssc_manager_driver = {
	.probe		= tssc_manager_probe,
	.remove		= tssc_manager_remove,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend	= tssc_manager_suspend,
	.resume		= tssc_manager_resume,
#endif
	.driver = {
		.name	= TSSC_MANAGER_NAME,
	},
};

static int __devinit tssc_manager_init(void)
{
	int result = 0;

	printk(KERN_INFO "tssc_manager_init\n");
	result = platform_driver_register(&tssc_manager_driver);
	if (result < 0)
		printk(KERN_ERR "tssc_manager_init: platform_driver_register failed\n");

	return result;
}

static void __exit tssc_manager_exit(void)
{
	printk(KERN_ERR "tssc_manager_exit\n");
}

module_init(tssc_manager_init);
module_exit(tssc_manager_exit);

MODULE_DESCRIPTION("TSSC Manager Driver");
MODULE_LICENSE("GPL");
