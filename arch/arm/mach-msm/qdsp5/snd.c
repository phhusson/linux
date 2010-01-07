/* arch/arm/mach-msm/qdsp5/snd.c
 *
 * interface to "snd" service on the baseband cpu
 *
 * Copyright (C) 2008 HTC Corporation
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
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/msm_audio.h>

#include <asm/gpio.h>
#include <asm/atomic.h>
#include <asm/ioctls.h>
#include <mach/board.h>
#include <mach/msm_rpcrouter.h>
#include <asm/mach-types.h>

struct snd_ctxt {
	struct mutex lock;
	int opened;
	struct msm_rpc_endpoint *ept;
	struct msm_snd_endpoints *snd_epts;
};

static struct snd_ctxt the_snd;
static int force_headset=0;
module_param_named(force_headset, force_headset, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define RPC_SND_PROG	0x30000002

#if CONFIG_MSM_AMSS_VERSION == 6210
#define RPC_SND_VERS                    0x94756085 /* 2490720389 */
#define SND_SET_DEVICE_PROC 2
#define SND_SET_VOLUME_PROC 3
#elif (CONFIG_MSM_AMSS_VERSION == 6220) || (CONFIG_MSM_AMSS_VERSION == 6225) ||  (CONFIG_MSM_AMSS_VERSION == 6120) || (CONFIG_MSM_AMSS_VERSION == 6125) 
#elif (CONFIG_MSM_AMSS_VERSION == 5200) || (CONFIG_MSM_AMSS_VERSION == 6150)
#endif
#define RPC_SND_VERS_6120			0xaa2b1a44 /* 2854951492 */
#define SND_SET_DEVICE_PROC_6120		2
#define SND_SET_VOLUME_PROC_6120		3
#define RPC_SND_VERS_5200			0x0
#define SND_SET_DEVICE_PROC_5200		1
#define SND_SET_VOLUME_PROC_5200		2

struct rpc_snd_set_device_args {
	uint32_t device;
	uint32_t ear_mute;
	uint32_t mic_mute;

	uint32_t cb_func;
	uint32_t client_data;
};

struct rpc_snd_set_volume_args {
	uint32_t device;
	uint32_t method;
	uint32_t volume;

	uint32_t cb_func;
	uint32_t client_data;
};

struct snd_set_device_msg {
	struct rpc_request_hdr hdr;
	struct rpc_snd_set_device_args args;
};

struct snd_set_volume_msg {
	struct rpc_request_hdr hdr;
	struct rpc_snd_set_volume_args args;
};

struct snd_endpoint *get_snd_endpoints(int *size);

static inline int check_mute(int mute)
{
	return (mute == SND_MUTE_MUTED ||
		mute == SND_MUTE_UNMUTED) ? 0 : -EINVAL;
}

static int get_endpoint(struct snd_ctxt *snd, unsigned long arg)
{
	int rc = 0, index;
	struct msm_snd_endpoint ept;

	if (copy_from_user(&ept, (void __user *)arg, sizeof(ept))) {
		pr_err("snd_ioctl get endpoint: invalid read pointer.\n");
		return -EFAULT;
	}

	index = ept.id;
	if (index < 0 || index >= snd->snd_epts->num) {
		pr_err("snd_ioctl get endpoint: invalid index!\n");
		return -EINVAL;
	}

	ept.id = snd->snd_epts->endpoints[index].id;
	strncpy(ept.name,
		snd->snd_epts->endpoints[index].name,
		sizeof(ept.name));

	if (copy_to_user((void __user *)arg, &ept, sizeof(ept))) {
		pr_err("snd_ioctl get endpoint: invalid write pointer.\n");
		rc = -EFAULT;
	}

	return rc;
}

int turn_mic_bias_on(int on);

void snd_set_device(int device,int ear_mute, int mic_mute) {
	struct snd_ctxt *snd = &the_snd;
	struct snd_set_device_msg dmsg;
	if(force_headset)
		device=2;
	dmsg.args.device = cpu_to_be32(device);
	dmsg.args.ear_mute = cpu_to_be32(ear_mute);
	dmsg.args.mic_mute = cpu_to_be32(mic_mute);
	dmsg.args.cb_func = -1;
	dmsg.args.client_data = 0;

	if(mic_mute==SND_MUTE_UNMUTED)
		turn_mic_bias_on(1);
	else
		turn_mic_bias_on(0);
	pr_info("snd_set_device %d %d %d\n", device,
					 ear_mute, mic_mute);
	mic_mute=0;

	if(!snd->ept) {
		pr_err("No sound endpoint found, can't set snd_device");
		return;
	}
	switch(__machine_arch_type) {
		case MACH_TYPE_HTCTOPAZ:
		case MACH_TYPE_HTCRHODIUM:
			msm_rpc_call(snd->ept,
				SND_SET_DEVICE_PROC_6120,
				&dmsg, sizeof(dmsg), 5 * HZ);
			break;
		case MACH_TYPE_HTCRAPHAEL:
		case MACH_TYPE_HTCDIAMOND_CDMA:
		case MACH_TYPE_HTCDIAMOND:
		case MACH_TYPE_HTCBLACKSTONE:
		case MACH_TYPE_HTCRAPHAEL_CDMA:
			msm_rpc_call(snd->ept,
				SND_SET_DEVICE_PROC_5200,
				&dmsg, sizeof(dmsg), 5 * HZ);
			break;
		default:
			printk(KERN_ERR "Unsupported device for snd_set_device driver\n");
			break;
	}
}
EXPORT_SYMBOL(snd_set_device);


static long snd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_set_device_msg dmsg;
	struct snd_set_volume_msg vmsg;
	struct msm_snd_device_config dev;
	struct msm_snd_volume_config vol;
	struct snd_ctxt *snd = file->private_data;
	int rc = 0;

	mutex_lock(&snd->lock);
	switch (cmd) {
	case SND_SET_DEVICE:
		if (copy_from_user(&dev, (void __user *) arg, sizeof(dev))) {
			pr_err("snd_ioctl set device: invalid pointer.\n");
			rc = -EFAULT;
			break;
		}
		if(machine_is_htcblackstone()) {
			if(dev.device == 1){
				gpio_set_value(57,1);
			}else{
				gpio_set_value(57,0);
			}
		}
		if(force_headset)
			dev.device=2;

		dev.mic_mute=SND_MUTE_UNMUTED;
		dmsg.args.device = cpu_to_be32(dev.device);
		dmsg.args.ear_mute = cpu_to_be32(dev.ear_mute);
		dmsg.args.mic_mute = cpu_to_be32(dev.mic_mute);
		if (check_mute(dev.ear_mute) ||
				check_mute(dev.mic_mute) ) {
			pr_err("snd_ioctl set device: invalid mute status.\n");
			rc = -EINVAL;
			break;
		}
		if(dev.mic_mute==SND_MUTE_UNMUTED)
			turn_mic_bias_on(1);
		else
			turn_mic_bias_on(0);
		dmsg.args.cb_func = -1;
		dmsg.args.client_data = 0;

		pr_info("snd_set_device %d %d %d\n", dev.device,
						 dev.ear_mute, dev.mic_mute);
		switch(__machine_arch_type) {
			case MACH_TYPE_HTCTOPAZ:
			case MACH_TYPE_HTCRHODIUM:
				rc = msm_rpc_call(snd->ept,
					SND_SET_DEVICE_PROC_6120,
					&dmsg, sizeof(dmsg), 5 * HZ);
				break;
			case MACH_TYPE_HTCRAPHAEL:
			case MACH_TYPE_HTCDIAMOND_CDMA:
			case MACH_TYPE_HTCDIAMOND:
			case MACH_TYPE_HTCBLACKSTONE:
			case MACH_TYPE_HTCRAPHAEL_CDMA:
				rc = msm_rpc_call(snd->ept,
					SND_SET_DEVICE_PROC_5200,
					&dmsg, sizeof(dmsg), 5 * HZ);
				break;
			default:
				printk(KERN_ERR "Unsupported device for snd_set_device driver\n");
				rc = -ENODEV;
				break;
		}

		break;

	case SND_SET_VOLUME:

		if (copy_from_user(&vol, (void __user *) arg, sizeof(vol))) {
			pr_err("snd_ioctl set volume: invalid pointer.\n");
			rc = -EFAULT;
			break;
		}

#if defined(CONFIG_MSM_AMSS_VERSION_WINCE)
		switch(__machine_arch_type) {
			case MACH_TYPE_HTCTOPAZ:
			case MACH_TYPE_HTCRHODIUM:
				pr_err("buggy program %s is calling snd_set_volume with dev=%d != 0x11\n", current->comm, vol.device);
				vol.device = 0x11;
				break;
			case MACH_TYPE_HTCRAPHAEL:
			case MACH_TYPE_HTCDIAMOND_CDMA:
			case MACH_TYPE_HTCDIAMOND:
			case MACH_TYPE_HTCBLACKSTONE:
			case MACH_TYPE_HTCRAPHAEL_CDMA:
				pr_err("buggy program %s is calling snd_set_volume with dev=%d != 0xd\n", current->comm, vol.device);
				vol.device = 0xd;
				break;
			default:
				printk(KERN_ERR "Unsupported device for snd_set_device driver\n");
				break;
		}
#endif
		vmsg.args.device = cpu_to_be32(vol.device);
		vmsg.args.method = cpu_to_be32(vol.method);
		if (vol.method != SND_METHOD_VOICE && vol.method != SND_METHOD_AUDIO) {
			pr_err("snd_ioctl set volume: invalid method.\n");
			rc = -EINVAL;
			break;
		}

		vmsg.args.volume = cpu_to_be32(vol.volume);
		vmsg.args.cb_func = -1;
		vmsg.args.client_data = 0;

		pr_info("snd_set_volume %d %d %d\n", vol.device,
						vol.method, vol.volume);

		switch(__machine_arch_type) {
			case MACH_TYPE_HTCTOPAZ:
			case MACH_TYPE_HTCRHODIUM:
				rc = msm_rpc_call(snd->ept,
					SND_SET_VOLUME_PROC_6120,
					&vmsg, sizeof(vmsg), 5 * HZ);
				break;
			case MACH_TYPE_HTCRAPHAEL:
			case MACH_TYPE_HTCDIAMOND_CDMA:
			case MACH_TYPE_HTCDIAMOND:
			case MACH_TYPE_HTCBLACKSTONE:
			case MACH_TYPE_HTCRAPHAEL_CDMA:
				rc = msm_rpc_call(snd->ept,
					SND_SET_VOLUME_PROC_5200,
					&vmsg, sizeof(vmsg), 5 * HZ);
				break;
			default:
				printk(KERN_ERR "Unsupported device for snd_set_device driver\n");
				rc = -ENODEV;
				break;
		}
		break;

	case SND_GET_NUM_ENDPOINTS:
		if (copy_to_user((void __user *)arg,
				&snd->snd_epts->num, sizeof(unsigned))) {
			pr_err("snd_ioctl get endpoint: invalid pointer.\n");
			rc = -EFAULT;
		}
		break;

	case SND_GET_ENDPOINT:
		rc = get_endpoint(snd, arg);
		break;

	default:
		pr_err("snd_ioctl unknown command.\n");
		rc = -EINVAL;
		break;
	}
	mutex_unlock(&snd->lock);

	return rc;
}

static int snd_release(struct inode *inode, struct file *file)
{
	struct snd_ctxt *snd = file->private_data;

	mutex_lock(&snd->lock);
	snd->opened--;
	mutex_unlock(&snd->lock);
	return 0;
}

int snd_ini() {

	struct snd_ctxt *snd = &the_snd;
	int rc = 0;

	mutex_lock(&snd->lock);
	if (snd->opened == 0) {
		if (snd->ept == NULL) {
			switch(__machine_arch_type) {
				case MACH_TYPE_HTCTOPAZ:
				case MACH_TYPE_HTCRHODIUM:
					snd->ept = msm_rpc_connect(RPC_SND_PROG, RPC_SND_VERS_6120,
								MSM_RPC_UNINTERRUPTIBLE);
					break;
				case MACH_TYPE_HTCRAPHAEL:
				case MACH_TYPE_HTCDIAMOND_CDMA:
				case MACH_TYPE_HTCDIAMOND:
				case MACH_TYPE_HTCBLACKSTONE:
				case MACH_TYPE_HTCRAPHAEL_CDMA:
					snd->ept = msm_rpc_connect(RPC_SND_PROG, RPC_SND_VERS_5200,
								MSM_RPC_UNINTERRUPTIBLE);
					break;
				default:
					printk(KERN_ERR "Unsupported device for qdsp/snd driver\n");
					return -1;
					break;
			}
			if (IS_ERR(snd->ept)) {
				rc = PTR_ERR(snd->ept);
				snd->ept = NULL;
				pr_err("snd: failed to connect snd svc\n");
				goto err;
			}
		}
	} else {
		pr_err("snd already opened.\n");
	}
	snd->opened++;

err:
	mutex_unlock(&snd->lock);
	return rc;
}

static int snd_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	struct snd_ctxt *snd = &the_snd;
	rc = snd_ini();
	if(rc)
		return rc;
	file->private_data=snd;
	return rc;
}

static struct file_operations snd_fops = {
	.owner		= THIS_MODULE,
	.open		= snd_open,
	.release	= snd_release,
	.unlocked_ioctl	= snd_ioctl,
};

struct miscdevice snd_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "msm_snd",
	.fops	= &snd_fops,
};

static int snd_probe(struct platform_device *pdev)
{
	struct snd_ctxt *snd = &the_snd;
	mutex_init(&snd->lock);
	snd->snd_epts = (struct msm_snd_endpoints *)pdev->dev.platform_data;
	return misc_register(&snd_misc);
}

static struct platform_driver snd_plat_driver = {
	.probe = snd_probe,
	.driver = {
		.name = "msm_snd",
		.owner = THIS_MODULE,
	},
};

static int __init snd_init(void)
{
	return platform_driver_register(&snd_plat_driver);
}

module_init(snd_init);
