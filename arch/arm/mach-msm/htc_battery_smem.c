/* arch/arm/mach-msm/htc_battery_smem.c
 * Based on: htc_battery.c by HTC and Google
 *
 * Copyright (C) 2008 HTC Corporation.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/wakelock.h>
#include <linux/io.h>
#include <asm/gpio.h>
#include <mach/board.h>
#include <asm/mach-types.h>
#include <linux/io.h>

#include "proc_comm_wince.h"

#include <mach/msm_iomap.h>
#include <mach/htc_battery.h>
#include <mach/htc_battery_smem.h>

static struct wake_lock vbus_wake_lock;
static struct work_struct bat_work;
static int bat_suspended = 0;
static int prev_result = 100;

#define TRACE_BATT 1

#if TRACE_BATT
 #define BATT(x...) printk(KERN_INFO "[BATT] " x)
#else
 #define BATT(x...) do {} while (0)
#endif

#define MODULE_NAME "htc_battery"

/* module debugger */
#define HTC_BATTERY_DEBUG		1
#define BATTERY_PREVENTION		1

/* Enable this will shut down if no battery */
#define ENABLE_BATTERY_DETECTION	0

typedef enum {
	DISABLE = 0,
	ENABLE_SLOW_CHG,
	ENABLE_FAST_CHG
} batt_ctl_t;

/* This order is the same as htc_power_supplies[]
 * And it's also the same as htc_cable_status_update()
 */
typedef enum {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC
} charger_type_t;

struct battery_info_reply {
	u32 batt_id;		/* Battery ID from ADC */
	u32 batt_vol;		/* Battery voltage from ADC */
	u32 batt_temp;		/* Battery Temperature (C) from formula and ADC */
	s32 batt_current;	/* Battery current from ADC */
	u32 level;		/* formula */
	u32 charging_source;	/* 0: no cable, 1:usb, 2:AC */
	u32 charging_enabled;	/* 0: Disable, 1: Enable */
	u32 full_bat;		/* Full capacity of battery (mAh) */
};

struct htc_battery_info {
	int present;
	unsigned long update_time;

	/* lock to protect the battery info */
	struct mutex lock;

	struct battery_info_reply rep;
	smem_batt_t *resources;
};

static struct htc_battery_info htc_batt_info;

static unsigned int cache_time = 1000;

static int htc_battery_initial = 0;

// simple maf filter stuff
#define BATT_MAF_SIZE 8
static short volt_maf_buffer[BATT_MAF_SIZE];
static short volt_maf_size = 0;
static short volt_maf_last = 0;

static void maf_add_value( short volt )
{
	// check if we need to correct the index
	if ( volt_maf_last == BATT_MAF_SIZE-1 )
		volt_maf_last = 0;

	// add value to filter buffer
	volt_maf_buffer[volt_maf_last] = volt;
	volt_maf_last++;

	if ( volt_maf_size != BATT_MAF_SIZE-1 )
		volt_maf_size++;	
}

//calculated on the fly.... no caching
static short maf_get_avarage()
{
	int i;
	int maf_temp;

	// make sure we only do it when we have data
	if ( volt_maf_size == 0 )
		return 0;

	// no need todo the avaraging
	if ( volt_maf_size == 1 )
		return volt_maf_buffer[0];

	// our start value is the first sample
	maf_temp = volt_maf_buffer[0];

	for (i=1; i < volt_maf_size; i++) {
		maf_temp = ( maf_temp + volt_maf_buffer[i] ) / 2;		
	}

	return maf_temp;
}

static void maf_clear()
{
	int i;
	for ( i = 0; i < BATT_MAF_SIZE;i++ )
		volt_maf_buffer[i] = 0;

	volt_maf_size = 0;
	volt_maf_last = 0;
}

/* ADC linear correction numbers.
 */
static u32 htc_adc_a = 0;					// Account for Divide Resistors
static u32 htc_adc_b = 0;
static u32 htc_adc_range = 0x1000;	// 12 bit adc range correction.
static u32 batt_vendor = 0;

// todo: use readl
#define GET_BATT_ID 		*(int *)( MSM_SHARED_RAM_BASE + 0xFC0DC )
#define GET_ADC_VREF 		*(int *)( MSM_SHARED_RAM_BASE + 0xFC0E0 )
#define GET_ADC_0_5_VREF 	*(int *)( MSM_SHARED_RAM_BASE + 0xFC0E4 )

static int get_battery_id_detection( struct battery_info_reply *buffer );
static int htc_get_batt_info( struct battery_info_reply *buffer );

static int init_battery_settings( struct battery_info_reply *buffer ) {
	u32 batt_vref = 0;
	u32 batt_vref_half = 0;

	if ( buffer == NULL )
		return -EINVAL;

	if ( htc_get_batt_info( buffer ) < 0 )
		return -EINVAL;

	mutex_lock( &htc_batt_info.lock );

	batt_vref = GET_ADC_VREF;
	batt_vref_half = GET_ADC_0_5_VREF;

	printk( "init_battery_settings, VREF=%d, 0.5 VREF=%d\n", batt_vref, batt_vref_half );

	if ( batt_vref - batt_vref_half >= 500 ) {
		// set global correction var
		htc_adc_a = 625000 / ( batt_vref - batt_vref_half );
		htc_adc_b = 1250000 - ( batt_vref * htc_adc_a );
	}

	printk( "init_battery_settings, ADC_A=%d, ADC_B=%d\n", htc_adc_a, htc_adc_b );

	// calculate the current adc range correction.
	htc_adc_range = ( batt_vref * 0x1000 ) / 1250;

	if ( get_battery_id_detection( buffer ) < 0 ) {
		mutex_unlock(&htc_batt_info.lock);
		return -EINVAL;
	}

	mutex_unlock(&htc_batt_info.lock);

	return 0;
}

static int get_battery_id_detection( struct battery_info_reply *buffer ) {
	// dono incorparate these in the battery info.
	u32 batt_id;
	u32 batt_capacity;
	struct msm_dex_command dex;

	dex.cmd = PCOM_GET_BATTERY_ID;
	msm_proc_comm_wince( &dex, 0 );

	batt_id = GET_BATT_ID;
	batt_id = ( batt_id * 0xA28 ) / htc_adc_range;					// apply the adc range correction.
	//batt_id = ( ( htc_adc_a * batt_id ) + htc_adc_b )  / 1000;
	//buffer->batt_id = batt_id;

	printk( "get_battery_id_detection, HTC_SMEM_BATTERY_ID = %d\n", GET_BATT_ID );
	printk( "get_battery_id_detection, batt_id_kohm = %d\n", batt_id );

	if ( batt_id > 800 ) {
		batt_vendor = 0xFF;
		buffer->full_bat = 100; // hack
	}

	// battery capacity part is a hack..
	if ( batt_id < 280 ) {
		batt_vendor = 1;
		batt_capacity = 1800;
		buffer->full_bat = 1800000;
	} else {
		batt_vendor = 2;
		batt_capacity = 1340;
		buffer->full_bat = 1340000;
	}

	printk( "get_battery_id_detection, vendor = %d, capacity: %d\n", batt_vendor,  batt_capacity);
	return 0;
}

static enum power_supply_property htc_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
};

static enum power_supply_property htc_power_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static char *supply_list[] = {
	"battery",
};

/* HTC dedicated attributes */
static ssize_t htc_battery_show_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf);

static int htc_power_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val);

static int htc_battery_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val);

static struct power_supply htc_power_supplies[] = {
	{
		.name = "battery",
		.type = POWER_SUPPLY_TYPE_BATTERY,
		.properties = htc_battery_properties,
		.num_properties = ARRAY_SIZE(htc_battery_properties),
		.get_property = htc_battery_get_property,
	},
	{
		.name = "usb",
		.type = POWER_SUPPLY_TYPE_USB,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = htc_power_properties,
		.num_properties = ARRAY_SIZE(htc_power_properties),
		.get_property = htc_power_get_property,
	},
	{
		.name = "ac",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.supplied_to = supply_list,
		.num_supplicants = ARRAY_SIZE(supply_list),
		.properties = htc_power_properties,
		.num_properties = ARRAY_SIZE(htc_power_properties),
		.get_property = htc_power_get_property,
	},
};

static int fake_charger=0;
module_param_named(fake, fake_charger, int, S_IRUGO | S_IWUSR | S_IWGRP);

enum {
	DEBUG_BATT	= 1<<0,
	DEBUG_CABLE	= 1<<1,
};

static int debug_mask=0;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

/* -------------------------------------------------------------------------- */

#if defined(CONFIG_DEBUG_FS)
int htc_battery_set_charging(batt_ctl_t ctl);
static int batt_debug_set(void *data, u64 val)
{
	return htc_battery_set_charging((batt_ctl_t) val);
}

static int batt_debug_get(void *data, u64 *val)
{
	return -ENOSYS;
}

DEFINE_SIMPLE_ATTRIBUTE(batt_debug_fops, batt_debug_get, batt_debug_set, "%llu\n");
static int __init batt_debug_init(void)
{
	struct dentry *dent;

	dent = debugfs_create_dir("htc_battery", 0);
	if (IS_ERR(dent))
		return PTR_ERR(dent);

	debugfs_create_file("charger_state", 0644, dent, NULL, &batt_debug_fops);

	return 0;
}

device_initcall(batt_debug_init);
#endif

static int init_batt_gpio(void)
{
	if (gpio_request(htc_batt_info.resources->gpio_battery_detect, "batt_detect") < 0)
		goto gpio_failed;
	if (gpio_request(htc_batt_info.resources->gpio_charger_enable, "charger_en") < 0)
		goto gpio_failed;
	if (gpio_request(htc_batt_info.resources->gpio_charger_current_select, "charge_current") < 0)
		goto gpio_failed;
	if (machine_is_htckovsky())
		if (gpio_request(htc_batt_info.resources->gpio_ac_detect, "ac_detect") < 0)
			goto gpio_failed;

	return 0;

gpio_failed:
	return -EINVAL;
}

/*
 *	battery_charging_ctrl - battery charing control.
 * 	@ctl:			battery control command
 *
 */
static int battery_charging_ctrl(batt_ctl_t ctl)
{
	int result = 0;

	switch (ctl) {
	case DISABLE:
		if(debug_mask&DEBUG_CABLE)
			BATT("charger OFF\n");
		/* 0 for enable; 1 disable */
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_enable, 1);
		break;
	case ENABLE_SLOW_CHG:
		if(debug_mask&DEBUG_CABLE)
			BATT("charger ON (SLOW)\n");
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_current_select, 0);
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_enable, 0);
		break;
	case ENABLE_FAST_CHG:
		if(debug_mask&DEBUG_CABLE)
			BATT("charger ON (FAST)\n");
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_current_select, 1);
		result = gpio_direction_output(htc_batt_info.resources->gpio_charger_enable, 0);
		break;
	default:
		printk(KERN_ERR "Not supported battery ctr called.!\n");
		result = -EINVAL;
		break;
	}

	return result;
}

int htc_battery_set_charging(batt_ctl_t ctl)
{
	int rc;

	if ((rc = battery_charging_ctrl(ctl)) < 0)
		goto result;

	if (!htc_battery_initial) {
		htc_batt_info.rep.charging_enabled = ctl & 0x3;
	} else {
		mutex_lock(&htc_batt_info.lock);
		htc_batt_info.rep.charging_enabled = ctl & 0x3;
		mutex_unlock(&htc_batt_info.lock);
	}
result:
	return rc;
}

int htc_battery_status_update(u32 curr_level)
{
	int notify;

	if (!htc_battery_initial)
		return 0;

	mutex_lock(&htc_batt_info.lock);
	notify = (htc_batt_info.rep.level != curr_level);
	htc_batt_info.rep.level = curr_level;
	mutex_unlock(&htc_batt_info.lock);

	if (notify)
		power_supply_changed(&htc_power_supplies[CHARGER_BATTERY]);
	return 0;
}

int htc_cable_status_update(int status)
{
	int rc = 0;
	unsigned source;
	unsigned last_source;

	if (!htc_battery_initial)
		return 0;

	mutex_lock(&htc_batt_info.lock);
	if(readl(MSM_SHARED_RAM_BASE+0xfc00c))
		status=CHARGER_AC;	/* vbus present, says AC to have full speed charging. (perhaps breaks kovsky ?) */
	else
		status=CHARGER_BATTERY;	/* no vbus present */

	if(fake_charger)
		status=CHARGER_USB;

	last_source = htc_batt_info.rep.charging_source;

	switch(status) {
	case CHARGER_BATTERY:
		if(debug_mask&DEBUG_CABLE)
			BATT("cable NOT PRESENT\n");
		htc_batt_info.rep.charging_source = CHARGER_BATTERY;
		break;
	case CHARGER_USB:
		if(debug_mask&DEBUG_CABLE)
			BATT("cable USB\n");
		htc_batt_info.rep.charging_source = CHARGER_USB;
		break;
	case CHARGER_AC:
		if(debug_mask&DEBUG_CABLE)
			BATT("cable AC\n");
		htc_batt_info.rep.charging_source = CHARGER_AC;
		break;
	default:
		printk(KERN_ERR "%s: Not supported cable status received!\n",
				__FUNCTION__);
		rc = -EINVAL;
	}
	source = htc_batt_info.rep.charging_source;
	mutex_unlock(&htc_batt_info.lock);

	htc_battery_set_charging(status);
	//msm_hsusb_set_vbus_state((source==CHARGER_USB) || (source==CHARGER_AC));

	if (  source == CHARGER_USB || source==CHARGER_AC ) {
		wake_lock(&vbus_wake_lock);
	} else if(last_source != source) {
		/* give userspace some time to see the uevent and update
		 * LED state or whatnot...
		 */
		wake_lock_timeout(&vbus_wake_lock, HZ / 2);
	} else {
		wake_unlock(&vbus_wake_lock);
	}

	/* make sure that we only change the powersupply state if we really have to */
	if (source == CHARGER_BATTERY || last_source == CHARGER_BATTERY)
		power_supply_changed(&htc_power_supplies[CHARGER_BATTERY]);
	if (source == CHARGER_USB || last_source == CHARGER_USB)
		power_supply_changed(&htc_power_supplies[CHARGER_USB]);
	if (source == CHARGER_AC || last_source == CHARGER_AC)
		power_supply_changed(&htc_power_supplies[CHARGER_AC]);

	return rc;
}

static void htc_bat_work(struct work_struct *work) {
	int rc = 0;
	int ac_detect = 1;

	if (htc_batt_info.resources->gpio_ac_detect == 0)
		return;

	ac_detect = gpio_get_value(htc_batt_info.resources->gpio_ac_detect);
	BATT("ac_detect=%d\n", ac_detect);
	if (ac_detect == 0) {
		rc = battery_charging_ctrl(ENABLE_SLOW_CHG);
		BATT("charging enable rc=%d\n", rc);
	} else {
		rc = battery_charging_ctrl(DISABLE);
		BATT("charging disable rc=%d\n", rc);
	}
	msm_hsusb_set_vbus_state(!!readl(MSM_SHARED_RAM_BASE+ 0xfc00c));
}

static short battery_table_4[] = {
	0,      0,
	0xe11,	0,
	0xe1e,	5,
	0xe3c,	10,
	0xe7e,	20,
	0xe97,	30,
	0xeab,	40,
	0xec1,	50,
	0xed5,	60,
	0xf0c,	70,
	0xf43,	80,
	0xf93,	90,
	0xfcb,  100,
	0x1000, 100,
	0,	0
};

/* 2 byte alligned */
struct htc_batt_info_u16 {
	volatile u16 batt_id;
	volatile u16 batt_temp;
	volatile u16 batt_vol;
	volatile s16 batt_charge;
	volatile u16 batt_discharge;
};

/* 4 byte alligned */
struct htc_batt_info_u32 {
	volatile u32 batt_id;
	volatile u32 batt_temp;
	volatile u32 batt_vol;
	volatile s32 batt_charge;
	volatile u32 batt_discharge;
};

/* todo convert to more readable code */
 static int batt_current_temp_correction(s64 R0/* temp */, s64 R1, s64 R2)
 {
		s64 R3;
		s64 R4;

		if ( R0 > 250 ) {
			R0 = 0;
		} else {
			R1 = abs((int)R1);
			R3 = 250 - R0;
			R1 = R3 * R1;
			R0 = R1 * R2;
			R4 = R0 * 8;

			R1 = (R4 * 0x32C88B7BLL) >> 32;
			R3 = (R0 * 8) - R1;
			R3 = R1 + ( R3 / 2 );
			R0 = R3 >> 20;
		}

		R1 = R2 * 8;
		R3 = (R1 * 0xCCCCCCCD) >> 32; // part of div 10
		R3 = R3 >> 3;

		if (R0 < R3)
			R0 = R3;

		return R0;
}

static int GetBatteryDischargeLevel( int volt, int charge, int temp, int vendor )
{
	int volt_discharge_resistor;
	int voltage_0;
	int voltage_1;
	int voltage_2;
	int voltage_max = 3808;		// top most value of the graph
	int temp_correction;
	int temp_correction_const;
	int result;
	int temp_correct_volt;
	int corrected_volt;
	int corr_return;

   if ( vendor != 1 ) {
	   voltage_0 = 0xdb0 | 0x0E;		// 3518 mV
	   voltage_1 = 0xe50 | 0x04;		// 3668 mV
	   voltage_2 = 0xe90 | 0x0F;		// 3743 mV
	   voltage_max |= 0x0A;				// 3818 mV
	   temp_correction_const = 23;	// 23
	   temp_correction = 150;			// 150
	} else {
	   voltage_0 = 0xd80 | 0x07;		// 3463 mV
	   voltage_1 = 0xe40 | 0x0F;		// 3663 mV
	   voltage_2 = 0xea0 | 0x03;		// 3747 mV
	   voltage_max |= 0x05;				// 3813 mV
	   temp_correction_const = 11;	// 11
	   temp_correction = 200;			// 200
	}

	/* if we are charging we don't have to calculate the voltage over the discharge current resistor */
	if ( charge > 0 ) {
		volt_discharge_resistor = 0;
	} else {
		if ( vendor == 1 ) {
			volt_discharge_resistor = ( abs( charge ) * 22 ) / 100;
		} else {
			volt_discharge_resistor = ( abs( charge ) * 27 ) / 100;
		}

		/* wierd code I know... :S something related to overflow shit :S */
		if ( volt_discharge_resistor < 0 )															//volt_discharge_resistor += volt_discharge_resistor >> 31;
			volt_discharge_resistor = volt_discharge_resistor + 1;
	}

	if ( temp > 250 )
		temp_correct_volt = 0;
	else
		temp_correct_volt = ( temp_correction_const * ( ( 250 - temp ) * abs( charge ) ) ) / 10000;

	corrected_volt = volt + volt_discharge_resistor;

	/* calculate the voltage offset because of the temperature and current use */
	corr_return = batt_current_temp_correction( temp, charge, temp_correction );

	if ( voltage_max - temp_correct_volt > corrected_volt ) {
		if ( voltage_2 - temp_correct_volt > corrected_volt ) {
			if ( voltage_1 - temp_correct_volt > corrected_volt ) {
				if ( ( corr_return - temp_correct_volt ) + voltage_0 > corrected_volt ) {
					result = 0;
				} else {
					result = ( ( ( ( corrected_volt - corr_return ) - voltage_0 ) + temp_correct_volt ) * 3 ) / ( ( voltage_1 - corr_return ) - voltage_1 );
				}
			} else {
				result = ( ( ( ( corrected_volt - voltage_1 ) + temp_correct_volt ) * 16 ) / ( voltage_2 - voltage_1 ) ) + 3;
			}
		} else {
			result = ( ( ( ( corrected_volt - voltage_2 ) + temp_correct_volt ) * 31 ) / ( voltage_max - voltage_2 ) ) + 19;
		}
	} else {
		result = ( ( ( ( corrected_volt - voltage_max ) + temp_correct_volt ) * 50 ) / ( ( ( temp_correct_volt - voltage_max ) + 0x1000 ) + 29 ) ) + 50;
	}

	/* would be stupid if we report more than 100% */
	if ( result > 100 )
		result = 100;
		
	// Test Stabilization
	
	if ( result > ( prev_result + 1 ) ) {
		if ( result < ( prev_result + 10 ) ) {
			result = prev_result;
		}
	}

	if ( result < ( prev_result - 1 ) ) {
		if ( result > ( prev_result - 10 ) ) {
			result = prev_result;
		}
	}

	return result;
 }
 
  static int GetBatteryDischargeLevelCharging( int volt, int charge, int temp, int vendor )
{
	int volt_discharge_resistor;
	int voltage_0;
	int voltage_1;
	int voltage_2;
	int voltage_max = 3808;		// top most value of the graph
	int temp_correction;
	int temp_correction_const;
	int result;
	int temp_correct_volt;
	int corrected_volt;
	int corr_return;

   if ( vendor != 1 ) {
	   voltage_0 = 0xdb0 | 0x0E;		// 3518 mV
	   voltage_1 = 0xe50 | 0x04;		// 3668 mV
	   voltage_2 = 0xe90 | 0x0F;		// 3743 mV
	   voltage_max |= 0x0A;				// 3818 mV
	   temp_correction_const = 23;	// 23
	   temp_correction = 150;			// 150
	} else {
	   voltage_0 = 0xd80 | 0x07;		// 3463 mV
	   voltage_1 = 0xe40 | 0x0F;		// 3663 mV
	   voltage_2 = 0xea0 | 0x03;		// 3747 mV
	   voltage_max |= 0x05;				// 3813 mV
	   temp_correction_const = 11;	// 11
	   temp_correction = 200;			// 200
	}

	/* if we are charging we don't have to calculate the voltage over the discharge current resistor */
	if ( charge > 0 ) {
		volt_discharge_resistor = 0;
	} else {
		if ( vendor == 1 ) {
			volt_discharge_resistor = ( abs( charge ) * 22 ) / 100;
		} else {
			volt_discharge_resistor = ( abs( charge ) * 27 ) / 100;
		}

		/* wierd code I know... :S something related to overflow shit :S */
		if ( volt_discharge_resistor < 0 )															//volt_discharge_resistor += volt_discharge_resistor >> 31;
			volt_discharge_resistor = volt_discharge_resistor + 1;
	}

	if ( temp > 250 )
		temp_correct_volt = 0;
	else
		temp_correct_volt = ( temp_correction_const * ( ( 250 - temp ) * abs( charge ) ) ) / 10000;

	corrected_volt = volt + volt_discharge_resistor;

	/* calculate the voltage offset because of the temperature and current use */
	corr_return = batt_current_temp_correction( temp, charge, temp_correction );

	if ( voltage_max - temp_correct_volt > corrected_volt ) {
		if ( voltage_2 - temp_correct_volt > corrected_volt ) {
			if ( voltage_1 - temp_correct_volt > corrected_volt ) {
				if ( ( corr_return - temp_correct_volt ) + voltage_0 > corrected_volt ) {
					result = 0;
				} else {
					result = ( ( ( ( corrected_volt - corr_return ) - voltage_0 ) + temp_correct_volt ) * 3 ) / ( ( voltage_1 - corr_return ) - voltage_1 );
				}
			} else {
				result = ( ( ( ( corrected_volt - voltage_1 ) + temp_correct_volt ) * 9 ) / ( voltage_2 - voltage_1 ) ) + 3;
			}
		} else {
			result = ( ( ( ( corrected_volt - voltage_2 ) + temp_correct_volt ) * 17 ) / ( voltage_max - voltage_2 ) ) + 19;
		}
	} else {
		result = ( ( ( ( corrected_volt - voltage_max ) + temp_correct_volt ) * 30 ) / ( ( ( temp_correct_volt - voltage_max ) + 0x1000 ) + 29 ) ) + 50;
	}

	/* would be stupid if we report more than 100% */
	if ( result > 100 )
		result = 100;
		
	// Test Stabilization
	
	if ( result < prev_result ) {
		result = prev_result;
	}
	
	if ( result > ( prev_result + 1 ) ) {
		if ( result < ( prev_result + 10 ) ) {
			result = prev_result;
		}
	}

	/*if ( result < ( prev_result - 1 ) ) {
		if ( result > ( prev_result - 10 ) ) {
			result = prev_result;
		}
	}*/

	return result;
 }

/* battery data device correction
 * apply the variouse corrections over the raw battery data
 */
 static int htc_raph_batt_corr( struct battery_info_reply *buffer )
 {
	int av_index;
	/* initial corrected voltage reporting
	 * These value's are read with a 12 bit adc.
	 * The best thing todo is to make sure that the work area in the measurement
	 * are evenly divided over the adc. This means that after we have read the value
	 * we need todo some corrections to the measured value before we get usuable
	 * readings.
	 * note: this is WIP so don't trust it.
	 * note: the linear correction value can be different on every model.
	 * todo: add ADC_REF and ADC_REF 1/2 corrections.
	 */
	//buffer->batt_vol = ( buffer->batt_vol * 0x1450 ) / htc_adc_range; 								// apply a linear correction.
	buffer->batt_vol = ( buffer->batt_vol * 0x1450 ) / 0x1000; 								// apply a linear correction.
	//buffer->batt_vol = ( ( htc_adc_a * buffer->batt_vol ) + htc_adc_b ) / 1000;

	/* Despite its very plauseble that the next piece if code is correct, there is no way
	 * to be sure of it. Android doesn't support "current" info in its battery reports. So
	 * there is no way to be sure.
	 */
	//buffer->batt_current = ( buffer->batt_current * 2600 ) / htc_adc_range;					// apply a linear correction.
	buffer->batt_current = ( buffer->batt_current * 2600 ) / 0x1000;					// apply a linear correction.
	//buffer->batt_current = ( ( htc_adc_a * buffer->batt_current ) + htc_adc_b )  / 1000;

	// temperature adc correction
	//buffer->batt_temp = ( buffer->batt_temp * 2600 ) / htc_adc_range;
	buffer->batt_temp = ( buffer->batt_temp * 2600 ) / 0x1000;
	//buffer->batt_temp = ( ( htc_adc_a * buffer->batt_temp ) + htc_adc_b ) / 1000;

	if (machine_is_htctopaz())
		av_index = ( buffer->batt_temp * 200 ) / ( 2600 - buffer->batt_temp );
	else
		av_index = ( buffer->batt_temp * 18 ) / ( 2600 - buffer->batt_temp );

	// everything below 8 is HOT
	if ( av_index < 8 )
		av_index = 8;

	// max size of the table, everything higher than 1347 would
	// cause the battery to freeze in a instance.
	if ( av_index > 1347 )
		av_index = 1347;

	buffer->batt_temp = temp_table[ av_index - 8 ];
	if(buffer->batt_temp > 330)
		//>60°C ? You're already burnt, you don't care about actual temperature.
		buffer->batt_temp=280;//7°C

	/* this should make sure that if we are discharging the current is negative */
	if ( buffer->charging_enabled == 0 ) {
		if ( buffer->batt_current > 0 )
			buffer->batt_current= 0 - buffer->batt_current;
	}

	if ( buffer->charging_enabled == 0 ) { 
		buffer->level = GetBatteryDischargeLevel( buffer->batt_vol, buffer->batt_current, buffer->batt_temp, batt_vendor );
	} else {
		buffer->level = GetBatteryDischargeLevelCharging( buffer->batt_vol, buffer->batt_current, buffer->batt_temp, batt_vendor );
	}
	return 0;
}

/* todo modify for rhodium */
 static int htc_rhod_batt_corr( struct battery_info_reply *buffer )
 {
	// int v;
	// int i;
	int av_index;
	// int capacity;
	
	buffer->batt_vol = ( buffer->batt_vol * 0x11BC ) / 0x1000;
	
	buffer->batt_current = ( buffer->batt_current * 2600 ) / 0x1000;
	
	// temperature adc correction
	buffer->batt_temp = ( buffer->batt_temp * 2600 ) / htc_adc_range;
	//buffer->batt_temp = ( ( htc_adc_a * buffer->batt_temp ) + htc_adc_b ) / 1000;

	av_index = ( buffer->batt_temp * 100 ) / ( 2600 - buffer->batt_temp );

	// everything below 8 is HOT
	if ( av_index < 8 )
		av_index = 8;

	// max size of the table, everything higher than 1347 would
	// cause the battery to freeze in a instance.
	if ( av_index > 1347 )
		av_index = 1347;

	buffer->batt_temp = temp_table[ av_index - 8 ];

/*
	v = buffer->batt_vol;
	v = (v < 0) ? 0 : (v > 0xfff) ? 0xfff : v;
	capacity = 100;
	for (i=2; battery_table_4[i]; i+=2) {
		if (v<battery_table_4[i]) {
			capacity = battery_table_4[i-1] + ((v - battery_table_4[i-2]) * (battery_table_4[i+1] - battery_table_4[i-1])) / (battery_table_4[i]-battery_table_4[i-2]);
			break;
		}
	}

	if ( capacity < 5 )
		capacity = 5;

	buffer->level = capacity;
*/

	/* this should make sure that if we are discharging the current is negative */
	if ( buffer->charging_enabled == 0 ) {
		if ( buffer->batt_current > 0 )
			buffer->batt_current= 0 - buffer->batt_current;
	}

	if ( buffer->charging_enabled == 0 ) {
		buffer->level = GetBatteryDischargeLevel( buffer->batt_vol, buffer->batt_current, buffer->batt_temp, batt_vendor );
	} else {
		buffer->level = GetBatteryDischargeLevelCharging( buffer->batt_vol, buffer->batt_current, buffer->batt_temp, batt_vendor );
	}
	return 0;
}

static int htc_get_batt_info(struct battery_info_reply *buffer)
{
	int i, capacity, v;
	short av_volt = 0, av_curr = 0, av_temp = 0, av_index = 0;

	volatile struct htc_batt_info_u32 *batt_32 = NULL;
	volatile struct htc_batt_info_u16 *batt_16 = NULL;
	struct msm_dex_command dex;

	if ( buffer == NULL )
		return -EINVAL;

	if ( !htc_batt_info.resources || !htc_batt_info.resources->smem_offset ) {
		printk( KERN_ERR MODULE_NAME ": smem_offset not set\n" );
		return -EINVAL;
	}

	dex.cmd = PCOM_GET_BATTERY_DATA;
	msm_proc_comm_wince(&dex, 0);

	mutex_lock(&htc_batt_info.lock);

	if (htc_batt_info.resources->smem_field_size == 4) {
		batt_32 = (void *)(MSM_SHARED_RAM_BASE + htc_batt_info.resources->smem_offset);

		buffer->batt_vol = batt_32->batt_vol;
		buffer->batt_current = batt_32->batt_charge;
		buffer->batt_temp = batt_32->batt_temp;
		buffer->batt_id = batt_32->batt_id;

	} else if (htc_batt_info.resources->smem_field_size == 2) {
		batt_16 = (void *)(MSM_SHARED_RAM_BASE + htc_batt_info.resources->smem_offset);

		buffer->batt_vol = batt_16->batt_vol;
		buffer->batt_current = batt_16->batt_charge;
		buffer->batt_temp = batt_16->batt_temp;
		buffer->batt_id = batt_16->batt_id;

	} else {
		printk(KERN_WARNING MODULE_NAME ": unsupported smem_field_size\n");
		mutex_unlock(&htc_batt_info.lock);
		return -ENOTSUPP;
	}

	// add the corrected value to the maf filter
	maf_add_value( buffer->batt_vol );

	// calculate the avarage
	buffer->batt_vol = maf_get_avarage();

	/* platform spec battery correction */
	if ( machine_is_htcraphael() || machine_is_htcraphael_cdma() || machine_is_htcraphael_cdma500() ||
	    machine_is_htcdiamond() || machine_is_htcdiamond_cdma() ||
	    machine_is_htctopaz() ) {
		htc_raph_batt_corr( buffer );
	} else if ( machine_is_htcrhodium() ) {
		htc_rhod_batt_corr( buffer );
	} else {
		/* fallback for not supported devices */
		htc_raph_batt_corr( buffer );
	}

	if(debug_mask&DEBUG_BATT) {
		if (htc_batt_info.resources->smem_field_size == 4) {
			printk("Raw batt Info:\n"
				"batt_id: %d\n"
				"batt_temp: %d\n"
				"batt_vol: %d\n"
				"batt_charge: %d\n"
				"batt_discharge: %d\n",
				batt_32->batt_id,
				batt_32->batt_temp,
				batt_32->batt_vol,
				batt_32->batt_charge,
				batt_32->batt_discharge
				);
		} else {
			printk("Raw batt Info:\n"
				"batt_id:              %d\n"
				"batt_temp:         %d\n"
				"batt_vol:             %d\n"
				"batt_charge:      %d\n"
				"batt_discharge: %d\n",
				batt_16->batt_id,
				batt_16->batt_temp,
				batt_16->batt_vol,
				batt_16->batt_charge,
				batt_16->batt_discharge
				);

			printk("Corrected batt Info:\n"
				"batt_id:               %d\n"
				"batt_temp:         %d\n"
				"batt_vol:             %d\n"
				"batt_charge:      %d\n",
				buffer->batt_id,
				buffer->batt_temp,
				buffer->batt_vol,
				buffer->batt_current
				);

			printk( "batt: current battery level: %u\n" , buffer->level );
		}
	}
	if(buffer->level<5)
		buffer->level=5;

	if (gpio_get_value(htc_batt_info.resources->gpio_charger_enable) == 0) {
		buffer->charging_enabled = 1;
		if (gpio_get_value(htc_batt_info.resources->gpio_charger_current_select)) {
			buffer->charging_source = CHARGER_AC;	// 900mA
		} else {
			buffer->charging_source = CHARGER_USB;	// 500mA
		}
	} else {
		buffer->charging_enabled = 0;
		buffer->charging_source = CHARGER_BATTERY;
	}

	if(!machine_is_htckovsky() && htc_batt_info.resources->smem_field_size==2) {
		// these were taken out in a kovsky commit, so assume it doesn't need them.
		buffer->charging_enabled = ( batt_16->batt_charge > 0x700 );
		buffer->charging_source =  ( batt_16->batt_charge < 0x200 ) ? CHARGER_BATTERY : CHARGER_USB;
	}

	mutex_unlock(&htc_batt_info.lock);

	htc_cable_status_update(buffer->charging_source);

	return 0;
}

/* -------------------------------------------------------------------------- */
static int htc_power_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	charger_type_t charger;

	mutex_lock(&htc_batt_info.lock);
	charger = htc_batt_info.rep.charging_source;
	mutex_unlock(&htc_batt_info.lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
			val->intval = (charger ==  CHARGER_AC ? 1 : 0);
		else if (psy->type == POWER_SUPPLY_TYPE_USB)
			val->intval = (charger ==  CHARGER_USB ? 1 : 0);
		else
			val->intval = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int htc_battery_get_charging_status(void)
{
	u32 level;
	charger_type_t charger;
	int ret;

	mutex_lock(&htc_batt_info.lock);
	charger = htc_batt_info.rep.charging_source;

	switch (charger) {
	case CHARGER_BATTERY:
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case CHARGER_USB:
	case CHARGER_AC:
		level = htc_batt_info.rep.level;
		if (level == 100)
			ret = POWER_SUPPLY_STATUS_FULL;
		else
			ret = POWER_SUPPLY_STATUS_CHARGING;
		break;
	default:
		ret = POWER_SUPPLY_STATUS_UNKNOWN;
	}
	mutex_unlock(&htc_batt_info.lock);
	return ret;
}

static int htc_battery_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = htc_battery_get_charging_status();
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = htc_batt_info.present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		mutex_lock(&htc_batt_info.lock);
		val->intval = htc_batt_info.rep.level;
		mutex_unlock(&htc_batt_info.lock);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void htc_battery_external_power_changed(struct power_supply *psy) {
	BATT("external power changed\n");
	maf_clear();
	schedule_work(&bat_work);
	return;
}

static irqreturn_t htc_bat_gpio_isr(int irq, void *data) {
	BATT("IRQ %d for GPIO \n", irq);
	schedule_work(&bat_work);
	return IRQ_HANDLED;
}

#define HTC_BATTERY_ATTR(_name)							\
{										\
	.attr = { .name = #_name, .mode = S_IRUGO, .owner = THIS_MODULE },	\
	.show = htc_battery_show_property,					\
	.store = NULL,								\
}

static struct device_attribute htc_battery_attrs[] = {
	HTC_BATTERY_ATTR(batt_id),
	HTC_BATTERY_ATTR(batt_vol),
	HTC_BATTERY_ATTR(batt_temp),
	HTC_BATTERY_ATTR(batt_current),
	HTC_BATTERY_ATTR(charging_source),
	HTC_BATTERY_ATTR(charging_enabled),
	HTC_BATTERY_ATTR(full_bat),
};

enum {
	BATT_ID = 0,
	BATT_VOL,
	BATT_TEMP,
	BATT_CURRENT,
	CHARGING_SOURCE,
	CHARGING_ENABLED,
	FULL_BAT,
};

static int htc_battery_create_attrs(struct device *dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(htc_battery_attrs); i++) {
		rc = device_create_file(dev, &htc_battery_attrs[i]);
		if (rc)
			goto htc_attrs_failed;
	}

	goto succeed;

htc_attrs_failed:
	while (i--)
		device_remove_file(dev, &htc_battery_attrs[i]);
succeed:
	return rc;
}

static ssize_t htc_battery_show_property(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i = 0;
	const ptrdiff_t off = attr - htc_battery_attrs;

	/* check cache time to decide if we need to update */
	if (htc_batt_info.update_time &&
            time_before(jiffies, htc_batt_info.update_time +
                                msecs_to_jiffies(cache_time)))
                goto dont_need_update;

	if (htc_get_batt_info(&htc_batt_info.rep) < 0) {
		printk(KERN_ERR "%s: get_batt_info failed!!!\n", __FUNCTION__);
	} else {
		htc_batt_info.update_time = jiffies;
	}
dont_need_update:

	mutex_lock(&htc_batt_info.lock);
	switch (off) {
	case BATT_ID:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.batt_id);
		break;
	case BATT_VOL:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.batt_vol);
		break;
	case BATT_TEMP:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.batt_temp);
		break;
	case BATT_CURRENT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.batt_current);
		break;
	case CHARGING_SOURCE:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.charging_source);
		break;
	case CHARGING_ENABLED:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.charging_enabled);
		break;
	case FULL_BAT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			       htc_batt_info.rep.full_bat);
		break;
	default:
		i = -EINVAL;
	}
	mutex_unlock(&htc_batt_info.lock);

	return i;
}

static int htc_battery_thread(void *data)
{
	daemonize("battery");
	allow_signal(SIGKILL);

	while (!signal_pending((struct task_struct *)current)) {
		msleep(10000);
		if (!bat_suspended)
			power_supply_changed(&htc_power_supplies[CHARGER_BATTERY]);
	}
	return 0;
}
static int htc_battery_probe(struct platform_device *pdev)
{
	int i, rc;

	INIT_WORK(&bat_work, htc_bat_work);
	htc_batt_info.resources = (smem_batt_t *)pdev->dev.platform_data;

	if (!htc_batt_info.resources) {
		printk(KERN_ERR "%s: no pdata resources!\n", __FUNCTION__);
		return -EINVAL;
	}

	/* init battery gpio */
	if ((rc = init_batt_gpio()) < 0) {
		printk(KERN_ERR "%s: init battery gpio failed!\n", __FUNCTION__);
		return rc;
	}

	/* init structure data member */
	htc_batt_info.update_time 	= jiffies;
	htc_batt_info.present 		= gpio_get_value(htc_batt_info.resources->gpio_battery_detect);

	/* init power supplier framework */
	for (i = 0; i < ARRAY_SIZE(htc_power_supplies); i++) {
		rc = power_supply_register(&pdev->dev, &htc_power_supplies[i]);
		if (rc)
			printk(KERN_ERR "Failed to register power supply (%d)\n", rc);
	}

	/* create htc detail attributes */
	htc_battery_create_attrs(htc_power_supplies[CHARGER_BATTERY].dev);

	//if ( htc_get_batt_info(&htc_batt_info.rep) < 0 )
		//printk(KERN_ERR "%s: get info failed\n", __FUNCTION__);

	/* init static battery settings */
	if ( init_battery_settings( &htc_batt_info.rep ) < 0)
		printk(KERN_ERR "%s: init battery settings failed\n", __FUNCTION__);

	htc_battery_initial = 1;

	htc_batt_info.update_time = jiffies;
	kernel_thread(htc_battery_thread, NULL, CLONE_KERNEL);
	if (machine_is_htckovsky()) {
		rc = request_irq(gpio_to_irq(htc_batt_info.resources->gpio_ac_detect),
				htc_bat_gpio_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"htc_ac_detect", &htc_batt_info);
        if (rc)
            printk(KERN_ERR "IRQ-request rc=%d\n", rc);
    }

	return 0;
}

#if CONFIG_PM
static int htc_battery_suspend(struct i2c_client *client, pm_message_t mesg)
{
	bat_suspended = 1;
	return 0;
}

static int htc_battery_resume(struct i2c_client *client)
{
	bat_suspended = 0;
	return 0;
}
#else
 #define htc_battery_suspend NULL
 #define htc_battery_resume NULL
#endif

static struct platform_driver htc_battery_driver = {
	.probe	= htc_battery_probe,
	.driver	= {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
	.suspend = htc_battery_suspend,
	.resume = htc_battery_resume,
};

static int __init htc_battery_init(void)
{
	wake_lock_init(&vbus_wake_lock, WAKE_LOCK_SUSPEND, "vbus_present");
	mutex_init(&htc_batt_info.lock);
	platform_driver_register(&htc_battery_driver);
	printk( KERN_INFO "HTC Battery Driver initialized\n" );

	return 0;
}

late_initcall(htc_battery_init);
MODULE_DESCRIPTION("HTC Battery Driver");
MODULE_LICENSE("GPL");
