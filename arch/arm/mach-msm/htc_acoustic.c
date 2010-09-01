/* arch/arm/mach-msm/htc_acoustic.c
 *
 * Copyright (C) 2007-2008 HTC Corporation
 * Author: Laurence Chen <Laurence_Chen@htc.com>
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
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mutex.h>

#include <mach/msm_smd.h>
#include <mach/msm_rpcrouter.h>
#include <mach/msm_iomap.h>
#include <asm/mach-types.h>
#include "proc_comm_wince.h"

#include "smd_private.h"

#define ACOUSTIC_IOCTL_MAGIC 'p'
#define ACOUSTIC_ARM11_DONE	_IOW(ACOUSTIC_IOCTL_MAGIC, 22, unsigned int)

#define HTCRPOG 0x30100002
#define HTCVERS 0
#define ONCRPC_SET_MIC_BIAS_PROC       (1)
#define ONCRPC_ACOUSTIC_INIT_PROC      (5)
#define ONCRPC_ALLOC_ACOUSTIC_MEM_PROC (6)

#define HTC_ACOUSTIC_TABLE_SIZE        (0x10000)

#define D(fmt, args...) printk(KERN_INFO "htc-acoustic: "fmt, ##args)
#define E(fmt, args...) printk(KERN_ERR "htc-acoustic: "fmt, ##args)

struct set_smem_req {
	struct rpc_request_hdr hdr;
	uint32_t size;
};

struct set_smem_rep {
	struct rpc_reply_hdr hdr;
	int n;
};

struct set_acoustic_req {
	struct rpc_request_hdr hdr;
};

struct set_acoustic_rep {
	struct rpc_reply_hdr hdr;
	int n;
};

static uint32_t htc_acoustic_vir_addr;
static struct msm_rpc_endpoint *endpoint = NULL;
static struct mutex api_lock;
static struct mutex rpc_connect_mutex;
static unsigned int mic_offset;

int turn_mic_bias_on(int on)
{
	struct msm_dex_command dex;
	unsigned int i;
	printk("Turnin mic bias on %d\n", on);

	printk("acoustic dump: you know you want it\n");
	printk("unsigned int audparms[] = {\n");

	for(i=0; i < 1024; i++)
	{
		if(i%5==0)
			printk("\t");
		printk("0x%8.8X,",readl(MSM_SHARED_RAM_BASE + 0xfc200 + i*4));
		if(i%5==4)
			printk("\n");
	}

	unsigned int audparms[] = {0x301000E1,0x00000001,0x00000001,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000001,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000001,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x40000005,
				0x00004000,0x00004000,0x7F650000,0x07800000,0x1B0CFF9A,
				0x01ECF333,0x200AFFEE,0x00007F65,0x0000ED00,0x7F650000,
				0x07800000,0x1B0CFF9A,0x01ECF333,0x200AFFEE,0x7FFF7F65,
				0x7FFF0800,0x0000149F,0x08000014,0x20002000,0x004600FA,
				0x02FF0001,0x00200040,0x00404650,0x080041A0,0x4E200063,
				0x00014E20,0x17704A38,0x01000000,0x04000100,0x04000200,
				0x02580300,0x1CA80190,0x2EE001C2,0x00000FA0,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00020000,0x00020002,0x00000002,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00020000,0x00020002,
				0xFFFF0002,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000,
				0x00000000,0x00000000,0x00000000,0x00000000,0x00000000};
				   
	for(i=0; i < 175; i++)
	{
		writel(audparms[i],MSM_SHARED_RAM_BASE + 0xfc200 + i*4);
	}

	
	printk("DBGTABLE 0x%X, 0x%X, 0x%X, 0x%X\n", audparms[0x138>>2], audparms[0x188>>2], audparms[0x228>>2], audparms[0x230>>2]);
	dex.cmd=PCOM_UPDATE_AUDIO;
	dex.has_data=1;

	/*  enable handset mic */
	*(unsigned *)(MSM_SHARED_RAM_BASE+mic_offset)=0xffff0080 | (on?0x100:0);
	//*(unsigned *)(MSM_SHARED_RAM_BASE+mic_offset)=0x93009307;
	//*(unsigned *)(MSM_SHARED_RAM_BASE+mic_offset+0x4)=0x93019307;
	//*(unsigned *)(MSM_SHARED_RAM_BASE+mic_offset+0x8)=0xFFFF0000;

	dex.data=0x10;
	msm_proc_comm_wince(&dex,0);

	/* some devices needs pm_mic_en */
	if (machine_is_htcdiamond_cdma() || machine_is_htcraphael_cdma() || machine_is_htcraphael_cdma500() || machine_is_htckovsky())
	{
		int ret;
		struct {
			struct rpc_request_hdr hdr;
			uint32_t data;
		} req;

		if (!endpoint)
			endpoint = msm_rpc_connect(0x30000061, 0x0, 0);
		if (!endpoint) {
			printk("Couldn't open rpc endpoint\n");
			return -EIO;
		}
		req.data=cpu_to_be32(0x1);
		ret = msm_rpc_call(endpoint, 0x1c, &req, sizeof(req), 5 * HZ);
	}

	return 0;
}

EXPORT_SYMBOL(turn_mic_bias_on);

static int acoustic_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long pgoff, delta;
	int rc = -EINVAL;
	size_t size;

	D("mmap\n");

	mutex_lock(&api_lock);

	size = vma->vm_end - vma->vm_start;

	if (vma->vm_pgoff != 0) {
		E("mmap failed: page offset %lx\n", vma->vm_pgoff);
		goto done;
	}

	if (!htc_acoustic_vir_addr) {
		E("mmap failed: smem region not allocated\n");
		rc = -EIO;
		goto done;
	}

	pgoff = MSM_SHARED_RAM_PHYS +
		(htc_acoustic_vir_addr - (uint32_t)MSM_SHARED_RAM_BASE);
	delta = PAGE_ALIGN(pgoff) - pgoff;

	if (size + delta > HTC_ACOUSTIC_TABLE_SIZE) {
		E("mmap failed: size %d\n", size);
		goto done;
	}

	pgoff += delta;
	vma->vm_flags |= VM_IO | VM_RESERVED;

	rc = io_remap_pfn_range(vma, vma->vm_start, pgoff >> PAGE_SHIFT,
		      size, vma->vm_page_prot);

	if (rc < 0)
		E("mmap failed: remap error %d\n", rc);

done:	mutex_unlock(&api_lock);
	return rc;
}

static int acoustic_open(struct inode *inode, struct file *file)
{
	int rc = -EIO;
	struct set_smem_req req_smem;
	struct set_smem_rep rep_smem;

	D("open\n");

	mutex_lock(&api_lock);

	BUG_ON(!htc_acoustic_vir_addr);

	rc = 0;
done:
	mutex_unlock(&api_lock);
	return rc;
}

static int acoustic_release(struct inode *inode, struct file *file)
{
	D("release\n");
	return 0;
}

static long acoustic_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	int rc, reply_value;
	struct set_acoustic_req req;
	struct set_acoustic_rep rep;

	D("ioctl\n");

	mutex_lock(&api_lock);

	switch (cmd) {
	case ACOUSTIC_ARM11_DONE:
		D("ioctl: ACOUSTIC_ARM11_DONE called %d.\n", current->pid);
		struct msm_dex_command dex;
		dex.cmd=PCOM_UPDATE_AUDIO;
		dex.has_data=1;
		dex.data=0x10;
		msm_proc_comm_wince(&dex,0);
		break;
	default:
		E("ioctl: invalid command\n");
		rc = -EINVAL;
	}

	mutex_unlock(&api_lock);
	return 0;
}


static struct file_operations acoustic_fops = {
	.owner = THIS_MODULE,
	.open = acoustic_open,
	.release = acoustic_release,
	.mmap = acoustic_mmap,
	.unlocked_ioctl = acoustic_ioctl,
};

static struct miscdevice acoustic_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "htc-acoustic",
	.fops = &acoustic_fops,
};

static int __init acoustic_init(void)
{
	switch(__machine_arch_type) {
		case MACH_TYPE_HTCTOPAZ:
		case MACH_TYPE_HTCRHODIUM:
			htc_acoustic_vir_addr=(void *)(MSM_SHARED_RAM_BASE+0xfc300);
			mic_offset = 0xfb9c0;
			break;
		case MACH_TYPE_HTCRAPHAEL:
		case MACH_TYPE_HTCDIAMOND_CDMA:
		case MACH_TYPE_HTCDIAMOND:
		case MACH_TYPE_HTCBLACKSTONE:
		case MACH_TYPE_HTCRAPHAEL_CDMA:
		case MACH_TYPE_HTCRAPHAEL_CDMA500:
		case MACH_TYPE_HTCKOVSKY:
			mic_offset = 0xfed00;
			htc_acoustic_vir_addr=(void *)(MSM_SHARED_RAM_BASE+0xfc300);
			break;
		default:
			printk(KERN_ERR "Unsupported device for htc_acoustic driver\n");
			return -1;
			break;
	}
	mutex_init(&api_lock);
	mutex_init(&rpc_connect_mutex);
	//No seriously, you don't want htc_acoustic yet.
	//Most likely needs a rewritten libhtc_acoustic
	//return misc_register(&acoustic_misc);
	return 0;
}

static void __exit acoustic_exit(void)
{
	misc_deregister(&acoustic_misc);
}

module_init(acoustic_init);
module_exit(acoustic_exit);

MODULE_AUTHOR("Laurence Chen <Laurence_Chen@htc.com>");
MODULE_DESCRIPTION("HTC acoustic driver");
MODULE_LICENSE("GPL");


