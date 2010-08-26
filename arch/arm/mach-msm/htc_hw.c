/* arch/arm/mach-msm/htc_hw.c
 * Author: Joe Hansche <madcoder@gmail.com>
 * Based on vogue-hw.c by Martin Johnson <M.J.Jonson@massey.ac.nz>
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

#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/io.h>
#include "proc_comm_wince.h"
#include <asm/mach-types.h>
#include <mach/msm_iomap.h>

#include "htc_hw.h"
#include "AudioPara.c"
#include <linux/msm_audio.h>
#include <asm/gpio.h>

#if 1
 #define DHTC(fmt, arg...) printk(KERN_DEBUG "[HTC] %s: " fmt "\n", __FUNCTION__, ## arg)
#else
 #define DHTC(fmt, arg...) do {} while (0)
#endif

static htc_hw_pdata_t *htc_hw_pdata;
static int force_cdma=0;
module_param(force_cdma, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int call_vol=5;
module_param(call_vol, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int handsfree=1;
module_param(handsfree, int, S_IRUGO | S_IWUSR | S_IWGRP);

extern void micropklt_lcd_ctrl(int);

static ssize_t test_store(struct class *class, const char *buf, size_t count)
{
	int v;
	sscanf(buf, "%d", &v);
	micropklt_lcd_ctrl(v);
	return 1;
}

static ssize_t flash_store(struct class *class, const char *buf, size_t count)
{
	int v;
	sscanf(buf, "%d", &v);
	gpio_set_value(0x3a, !!v);
	return 1;
}

static ssize_t vibrate_store(struct class *class, const char *buf, size_t count)
{
	uint32_t vibrate;
	if (sscanf(buf, "%d", &vibrate) != 1 || vibrate < 0 || vibrate > 0xb22)
		return -EINVAL;
	if (!htc_hw_pdata->set_vibrate)
		return -ENOTSUPP;
	if (vibrate == 0) {
		htc_hw_pdata->set_vibrate(0);
	} else if (vibrate == 1) {
		htc_hw_pdata->set_vibrate(1);
	} else if (vibrate <= 0xb22) {
		htc_hw_pdata->set_vibrate(vibrate);
	}
	return count;
}

int is_cdma(void)
{
	char amss_dump[20];
	char *dot1;

	*(unsigned int *) (amss_dump + 0x0) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0x0);
	*(unsigned int *) (amss_dump + 0x4) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0x4);
	*(unsigned int *) (amss_dump + 0x8) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0x8);
	*(unsigned int *) (amss_dump + 0xc) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0xc);
	*(unsigned int *) (amss_dump + 0x10) = readl(MSM_SHARED_RAM_BASE + 0xfc030 + 0x10);
	amss_dump[19] = '\0';

	dot1 = strchr(amss_dump, '.');
	if(dot1 == NULL) {	// CDMA
		return 1;
	}
	return 0;
}

static ssize_t radio_show(struct class *class, char *buf)
{
	char *radio_type = (machine_is_htcraphael_cdma() || machine_is_htcraphael_cdma500() || 
	                    machine_is_htcdiamond_cdma() || force_cdma || is_cdma()) ? "CDMA" : "GSM";
	return sprintf(buf, "%s\n", radio_type);
}

static ssize_t machtype_show(struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", machine_arch_type);
}

extern unsigned int __amss_version; // amss_para.c
static ssize_t amss_show(struct class *class, char *buf)
{
	return sprintf(buf, "%d\n", __amss_version);
}

static ssize_t battery_show(struct class *class, char *buf)
{
	int *values_32;
	short *values_16;
	void *smem_ptr;
	int x;
	struct msm_dex_command dex;

	dex.cmd = PCOM_GET_BATTERY_DATA;
	msm_proc_comm_wince(&dex, 0);

	smem_ptr = (void *)(MSM_SHARED_RAM_BASE + htc_hw_pdata->battery_smem_offset);

	if (htc_hw_pdata->battery_smem_field_size == 4) {
		values_32 = (int *)(smem_ptr);
		x = readl(MSM_SHARED_RAM_BASE + 0xfc0e0);
		return sprintf(buf, "+0xfc0e0: %08x\n"
			            "%p: %08x %08x %08x %08x %08x\n",
			x, smem_ptr, values_32[0], values_32[1], values_32[2],
			values_32[3], values_32[4] );
	} else {
		values_16 = (short *)(smem_ptr);
		return sprintf(buf, "%p: %04x %04x %04x %04x %04x\n", smem_ptr,
			values_16[0], values_16[1], values_16[2], values_16[3], values_16[4]);
	}
}

static struct class_attribute htc_hw_class_attrs[] = {
	__ATTR_RO(battery),
	__ATTR_RO(radio),
	__ATTR_RO(machtype),
	__ATTR_RO(amss),
	__ATTR(vibrate, 0222, NULL, vibrate_store),
	__ATTR(flash, 0222, NULL, flash_store),
	__ATTR(test,0222, NULL, test_store),
	__ATTR_NULL,
};

static struct class htc_hw_class = {
	.name = "htc_hw",
	.class_attrs = htc_hw_class_attrs,
};

static ssize_t gsmphone_show(struct class *class, char *buf) {
	return sprintf(buf, "%d\n", machine_arch_type);
}

static void set_audio_parameters(char *name) {
	int i;
	for(i=0;i<ARRAY_SIZE(audioparams);i++)
		if(!strcmp(name,audioparams[i].name))
			break;
	if(i==ARRAY_SIZE(audioparams)) {
		printk("Unknown audio parameter: %s\n",name);
		return;
	}
	memcpy((void *)(MSM_SHARED_RAM_BASE+0xfc300),audioparams[i].data,0x140);
}
#ifdef CONFIG_MSM_ADSP
void snd_set_device(int device,int ear_mute, int mic_mute);
int snd_ini();
#else
void snd_set_device(int device, int ear_mute, int mic_mute) {};
int snd_ini() {}
#endif
//htc_hw.c
int turn_mic_bias_on(int on);

void msm_audio_path(int i) {
	char* sparameter= "PHONE_HANDSFREE_VOL5";
	if(!handsfree)
		strcpy(sparameter, "PHONE_EARCUPLE_VOL5");
	if(call_vol>=0 && call_vol<=5)
		sparameter[strlen(sparameter)-1]=call_vol+'0';
	
	struct msm_dex_command dex;
	dex.cmd=PCOM_UPDATE_AUDIO;
	dex.has_data=1;

	switch (i) {
		case 2: // Phone Audio Start
		  printk(KERN_ERR "PARAMETER: %s\n", sparameter);
			set_audio_parameters(sparameter);
			dex.data=0x01;
			msm_proc_comm_wince(&dex,0);

			turn_mic_bias_on(1);
			snd_ini();
			snd_set_device(0,SND_MUTE_UNMUTED,SND_MUTE_UNMUTED); /* "HANDSET" */
			break;
		case 5: // Phone Audio End
                        set_audio_parameters("CE_PLAYBACK_HANDSFREE");
			dex.data=0x01;
			msm_proc_comm_wince(&dex,0);
			//Really turn mic off?
			//Some soft apps might want that too.
			turn_mic_bias_on(0);

			snd_ini();
			snd_set_device(1,SND_MUTE_MUTED,SND_MUTE_MUTED); /* "SPEAKER" */
                        break;
	}
}

static ssize_t audio_store(struct class *class,
                                   const char *buf,
                                   size_t count)
{
        uint32_t audio;
        if (sscanf(buf, "%d", &audio) != 1)
                return -EINVAL;
        printk("Sound: %d\n",audio);
        msm_audio_path(audio);
        return count;
}


// these are for compatability with the vogue ril
static struct class_attribute vogue_hw_class_attrs[] = {
	__ATTR(audio, 0222, NULL, audio_store),
	__ATTR_RO(gsmphone),
	__ATTR_NULL,
};

static struct class vogue_hw_class = {
	.name = "vogue_hw",
	.class_attrs = vogue_hw_class_attrs,
};

static int __init htc_hw_probe(struct platform_device *pdev)
{
	int ret;
	htc_hw_pdata = (htc_hw_pdata_t *)pdev->dev.platform_data;
	ret = class_register(&htc_hw_class);
	ret = class_register(&vogue_hw_class);
	if (ret)
		printk(KERN_ERR "%s: class init failed: %d\n", __func__, ret);
	DHTC("done");
	return ret;
}

static struct platform_driver htc_hw_driver = {
	.probe = htc_hw_probe,
	.driver = {
		.name = "htc_hw",
		.owner = THIS_MODULE,
	},
};

static int __init htc_hw_init(void)
{
	DHTC("Initializing HTC hardware platform driver");
	return platform_driver_register(&htc_hw_driver);
}

module_init(htc_hw_init);
MODULE_DESCRIPTION("HTC hardware platform driver");
MODULE_AUTHOR("Joe Hansche <madcoder@gmail.com>");
MODULE_LICENSE("GPL");
