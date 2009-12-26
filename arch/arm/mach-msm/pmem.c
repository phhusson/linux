/* arch/arm/mach-msm/board-trout.c
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
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/android_pmem.h>
#include <mach/board_htc.h>
#include <asm/mach-types.h>

#define MODULE_NAME "pmem"
#define MSM_SMI_BASE	0x00000000
#define MSM_SMI2_BASE	0x02000000
#define MSM_EBI_BASE	0x10000000
#define MSM_EBIN_BASE	0x20000000

/* Copy paste from devices_htc.c
 */

static struct android_pmem_platform_data pmem_pdata = {
	.name = "pmem",
	.no_allocator = 1,
	.cached = 1,
};

static struct android_pmem_platform_data pmem_adsp_pdata = {
	.name = "pmem_adsp",
	.no_allocator = 0,
	.cached = 0,
};

static struct android_pmem_platform_data pmem_camera_pdata = {
	.name = "pmem_camera",
	.no_allocator = 0,
	.cached = 0,
};

static struct android_pmem_platform_data pmem_gpu0_pdata = {
	.name = "pmem_gpu0",
	.no_allocator = 1,
	.cached = 0,
	.buffered = 1,
};

static struct android_pmem_platform_data pmem_gpu1_pdata = {
	.name = "pmem_gpu1",
	.no_allocator = 1,
	.cached = 0,
	.buffered = 1,
};

static struct platform_device pmem_device = {
	.name = "android_pmem",
	.id = 0,
	.dev = { .platform_data = &pmem_pdata },
};

static struct platform_device pmem_adsp_device = {
	.name = "android_pmem",
	.id = 1,
	.dev = { .platform_data = &pmem_adsp_pdata },
};

static struct platform_device pmem_gpu0_device = {
	.name = "android_pmem",
	.id = 2,
	.dev = { .platform_data = &pmem_gpu0_pdata },
};

static struct platform_device pmem_gpu1_device = {
	.name = "android_pmem",
	.id = 3,
	.dev = { .platform_data = &pmem_gpu1_pdata },
};

static struct platform_device pmem_camera_device = {
	.name = "android_pmem",
	.id = 4,
	.dev = { .platform_data = &pmem_camera_pdata },
};

static struct resource ram_console_resource[] = {
	{
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device ram_console_device = {
	.name = "ram_console",
	.id = -1,
	.num_resources  = ARRAY_SIZE(ram_console_resource),
	.resource       = ram_console_resource,
};

void __init msm_add_mem_devices(struct msm_pmem_setting *setting)
{
	if (setting->pmem_size) {
		pmem_pdata.start = setting->pmem_start;
		pmem_pdata.size = setting->pmem_size;
		platform_device_register(&pmem_device);
	}

	if (setting->pmem_adsp_size) {
		pmem_adsp_pdata.start = setting->pmem_adsp_start;
		pmem_adsp_pdata.size = setting->pmem_adsp_size;
		platform_device_register(&pmem_adsp_device);
	}

	if (setting->pmem_gpu0_size) {
		pmem_gpu0_pdata.start = setting->pmem_gpu0_start;
		pmem_gpu0_pdata.size = setting->pmem_gpu0_size;
		platform_device_register(&pmem_gpu0_device);
	}

	if (setting->pmem_gpu1_size) {
		pmem_gpu1_pdata.start = setting->pmem_gpu1_start;
		pmem_gpu1_pdata.size = setting->pmem_gpu1_size;
		platform_device_register(&pmem_gpu1_device);
	}

	if (setting->pmem_camera_size) {
		pmem_camera_pdata.start = setting->pmem_camera_start;
		pmem_camera_pdata.size = setting->pmem_camera_size;
		platform_device_register(&pmem_camera_device);
	}

	if (setting->ram_console_size) {
		ram_console_resource[0].start = setting->ram_console_start;
		ram_console_resource[0].end = setting->ram_console_start
			+ setting->ram_console_size - 1;
		platform_device_register(&ram_console_device);
	}
}
/*
 * End of paste*/
static struct msm_pmem_setting pmem_setting = {
	/*
	 * For reference only
	.pmem_start = MSM_PMEM_MDP_BASE,
	.pmem_size = MSM_PMEM_MDP_SIZE,
	.pmem_adsp_start = MSM_PMEM_ADSP_BASE,
	.pmem_adsp_size = MSM_PMEM_ADSP_SIZE,
	.pmem_gpu0_start = MSM_PMEM_GPU0_BASE,
	.pmem_gpu0_size = MSM_PMEM_GPU0_SIZE,
	.pmem_gpu1_start = MSM_PMEM_GPU1_BASE,
	.pmem_gpu1_size = MSM_PMEM_GPU1_SIZE,
	.pmem_camera_start = MSM_PMEM_CAMERA_BASE,
	.pmem_camera_size = MSM_PMEM_CAMERA_SIZE,
	.ram_console_start = MSM_RAM_CONSOLE_BASE,
	.ram_console_size = MSM_RAM_CONSOLE_SIZE,
	*/
};
struct resource resources_msm_fb[]={
	{
	},
};
#define CALC_PMEM(name, prev, size) \
	pmem_setting.name## _start = pmem_setting.prev## _start+\
		pmem_setting.prev## _size;\
	pmem_setting.name## _size = size;


static void __init msm_pmem_init() {
	switch(__machine_arch_type) {
		case MACH_TYPE_HTCDIAMOND:
			//SMI 64 + EBI 128
			pmem_setting.pmem_start=MSM_SMI2_BASE+0x100000;//First 1MB is wince SPL
			pmem_setting.pmem_size=0x1000000;//16MB
			CALC_PMEM(pmem_adsp, pmem, 0x800000);//8MB
			CALC_PMEM(fb, pmem_adsp, 0x200000);//2MB
			CALC_PMEM(pmem_camera, fb, 0x100000);//1MB

			//GPU1 must be in EBI bank 1
			pmem_setting.pmem_gpu1_start=MSM_EBI_BASE+107*1024*1024;
			pmem_setting.pmem_gpu1_size=0x800000;

			//Put ramconsole somewhere ...
			pmem_setting.ram_console_start=0x00800000;
			pmem_setting.ram_console_size=0x00100000;
			break;
		case MACH_TYPE_HTCRAPHAEL:
		case MACH_TYPE_HTCRAPHAEL_CDMA500:
		case MACH_TYPE_HTCRAPHAEL_CDMA:
		case MACH_TYPE_HTCDIAMOND_CDMA:
		case MACH_TYPE_HTCBLACKSTONE:
		case MACH_TYPE_HTCTOPAZ:
		case MACH_TYPE_HTCRHODIUM:
			//SMI 32 + EBI 2*128
			pmem_setting.pmem_start=MSM_EBIN_BASE;
			pmem_setting.pmem_size=0x1000000;//16MB
			CALC_PMEM(pmem_adsp, pmem, 0x800000);//8MB
			CALC_PMEM(fb, pmem_adsp, 0x200000);//2MB
			CALC_PMEM(pmem_camera, fb, 0x100000);//1MB
			
			//GPU1 must be in EBI bank 1
			pmem_setting.pmem_gpu1_start=MSM_EBI_BASE+107*1024*1024;
			pmem_setting.pmem_gpu1_size=0x800000;

			pmem_setting.ram_console_start=0x8e0000;
			pmem_setting.ram_console_size=0x20000;
			break;
		default:
			//SMI 32 + EBI 128
			//So much things for so few memory

			pmem_setting.pmem_start=MSM_EBI_BASE+89*1024*1024;
			pmem_setting.pmem_size=0x800000;//8MB
			CALC_PMEM(pmem_adsp, pmem, 0x800000);//8MB
			CALC_PMEM(fb, pmem_adsp, 0x200000);//2MB
			CALC_PMEM(pmem_camera, fb, 0x100000);//1MB
			
			//GPU1 must be in EBI bank 1
			pmem_setting.pmem_gpu1_start=MSM_EBI_BASE+107*1024*1024;
			pmem_setting.pmem_gpu1_size=0x800000;
			break;

	}
	//GPU0 must be in SMI1
	pmem_setting.pmem_gpu0_start=MSM_SMI_BASE+0x100000;//1MB for wince SPL
	pmem_setting.pmem_gpu0_size=0x800000;
	resources_msm_fb[0].start=pmem_setting.fb_start;
	resources_msm_fb[0].end=pmem_setting.fb_start+pmem_setting.fb_size;
	resources_msm_fb[0].flags=IORESOURCE_MEM;
	msm_add_mem_devices(&pmem_setting);
}
module_init(msm_pmem_init);

MODULE_DESCRIPTION("PMEM settings");
MODULE_AUTHOR("HUSSON Pierre-Hugues <phhusson@free.fr>");
MODULE_LICENSE("GPL");
