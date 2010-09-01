/* drivers/usb/function/msm_hsusb.c
 *
 * Driver for HighSpeed USB Client Controller in MSM7K
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
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
#include <linux/list.h>

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/workqueue.h>
#include <linux/clk.h>

#include <linux/usb/ch9.h>
#include <linux/usb/cdc.h>
#include <linux/io.h>

#include <asm/mach-types.h>

#include <mach/board.h>
#include <mach/msm_hsusb.h>
#include <mach/perflock.h>


#define MSM_USB_BASE ((unsigned) ui->addr)

#include <mach/msm_hsusb_hw.h>
#include <mach/board_htc.h>

#include "usb_function.h"

#include "diag.h"

#define EPT_FLAG_IN        0x0001

#define SETUP_BUF_SIZE      4096

/* IDs for string descriptors */
#define STRING_LANGUAGE_ID      0
#define STRING_SERIAL           1
#define STRING_PRODUCT          2
#define STRING_MANUFACTURER     3

static char *strings [] = {
	"Mass Storage",
	"ADB",
	"HTC Ethernet Sharing",
	"HTC DIAG",
	"HTC USB Serial Function",
	"HTC PROJECTOR",
	"HTC FSYNC",
	"HTC MTP",
};

static struct perf_lock usb_perf_lock;

#define LANGUAGE_ID             0x0409 /* en-US */

/* current state of VBUS */
static int vbus;

struct usb_function_info {
	struct list_head list;
	unsigned endpoints; /* nonzero if bound */
	unsigned enabled;

	struct usb_function *func;
	struct usb_interface_descriptor ifc[2];
	struct usb_fi_ept ept[0];
};

struct msm_request {
	struct usb_request req;

	struct usb_info *ui;
	struct msm_request *next;

	unsigned busy:1;
	unsigned live:1;
	unsigned alloced:1;
	unsigned dead:1;

	dma_addr_t dma;

	struct ept_queue_item *item;
	dma_addr_t item_dma;
};

#define to_msm_request(r) container_of(r, struct msm_request, req)

struct usb_endpoint {
	struct usb_info *ui;
	struct msm_request *req; /* head of pending requests */
	struct msm_request *last;
	unsigned flags;

	/* bit number (0-31) in various status registers
	** as well as the index into the usb_info's array
	** of all endpoints
	*/
	unsigned char bit;
	unsigned char num;

	unsigned char type;

	unsigned short max_pkt;

	/* pointers to DMA transfer list area */
	/* these are allocated from the usb_info dma space */
	struct ept_queue_head *head;

	struct usb_function_info *owner;
};

static void usb_do_work(struct work_struct *w);


#define USB_STATE_IDLE    0
#define USB_STATE_ONLINE  1
#define USB_STATE_OFFLINE 2

#define USB_FLAG_START          0x0001
#define USB_FLAG_VBUS_ONLINE    0x0002
#define USB_FLAG_VBUS_OFFLINE   0x0004
#define USB_FLAG_RESET          0x0008

struct usb_info {
	/* lock for register/queue/device state changes */
	spinlock_t lock;

	/* single request used for handling setup transactions */
	struct usb_request *setup_req;

	struct platform_device *pdev;
	int irq;
	void *addr;

	struct hrtimer vbus_timer;

	unsigned state;
	unsigned flags;

	unsigned online;
	unsigned running;
	unsigned bound;

	enum usb_device_speed speed;

	struct dma_pool *pool;

	/* dma page to back the queue heads and items */
	unsigned char *buf;
	dma_addr_t dma;

	struct ept_queue_head *head;

	/* used for allocation */
	unsigned next_item;
	unsigned next_ifc_num;

	/* endpoints are ordered based on their status bits,
	** so they are OUT0, OUT1, ... OUT15, IN0, IN1, ... IN15
	*/
	struct usb_endpoint ept[32];

	int *phy_init_seq;
	void (*phy_reset)(void);
	void (*phy_shutdown)(void);

	struct work_struct work;
	unsigned phy_status;
	unsigned phy_fail_count;

	struct usb_function_info **func;
	unsigned num_funcs;
#define ep0out ept[0]
#define ep0in  ept[16]

	struct clk *clk;
	struct clk *pclk;
};

struct usb_device_descriptor desc_device = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,

	.bcdUSB = 0x0200,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	/* the following fields are filled in by usb_probe */
	.idVendor = 0,
	.idProduct = 0,
	.bcdDevice = 0,
	.iManufacturer = 0,
	.iProduct = 0,
	.iSerialNumber = 0,
	.bNumConfigurations = 1,
};

static uint32_t enabled_functions = 0;

static struct usb_info *the_usb_info;

static void flush_endpoint(struct usb_endpoint *ept);
static struct usb_function_info *usb_find_function(const char *name);


#if 0
static unsigned ulpi_read(struct usb_info *ui, unsigned reg)
{
	unsigned timeout = 100000;

	/* initiate read operation */
	writel(ULPI_RUN | ULPI_READ | ULPI_ADDR(reg),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while ((readl(USB_ULPI_VIEWPORT) & ULPI_RUN) && (--timeout)) ;

	if (timeout == 0) {
		printk(KERN_ERR "ulpi_read: timeout %08x\n", readl(USB_ULPI_VIEWPORT));
		return 0xffffffff;
	}
	return ULPI_DATA_READ(readl(USB_ULPI_VIEWPORT));
}
#endif

static void ulpi_write(struct usb_info *ui, unsigned val, unsigned reg)
{
	unsigned timeout = 10000;

	/* initiate write operation */
	writel(ULPI_RUN | ULPI_WRITE |
	       ULPI_ADDR(reg) | ULPI_DATA(val),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while ((readl(USB_ULPI_VIEWPORT) & ULPI_RUN) && (--timeout)) ;

	if (timeout == 0)
		printk(KERN_WARNING "%s: timeout: reg: 0x%X, var: 0x%X\n",
		__func__, reg, val);
}

static void ulpi_init(struct usb_info *ui)
{
	int *seq = ui->phy_init_seq;

	if (!seq)
		return;

	while (seq[0] >= 0) {
		ulpi_write(ui, seq[0], seq[1]);
		seq += 2;
	}
}

static void init_endpoints(struct usb_info *ui)
{
	unsigned n;
	for (n = 0; n < 32; n++) {
		struct usb_endpoint *ept = ui->ept + n;

		ept->ui = ui;
		ept->bit = n;
		ept->num = n & 15;

		if (ept->bit > 15) {
			/* IN endpoint */
			ept->head = ui->head + (ept->num << 1) + 1;
			ept->flags = EPT_FLAG_IN;
		} else {
			/* OUT endpoint */
			ept->head = ui->head + (ept->num << 1);
			ept->flags = 0;
		}

	}
}

static void configure_endpoints(struct usb_info *ui)
{
	unsigned n;
	unsigned cfg;

	for (n = 0; n < 32; n++) {
		struct usb_endpoint *ept = ui->ept + n;

		cfg = CONFIG_MAX_PKT(ept->max_pkt) | CONFIG_ZLT;

		if (ept->bit == 0)
			/* ep0 out needs interrupt-on-setup */
			cfg |= CONFIG_IOS;

		ept->head->config = cfg;
		ept->head->next = TERMINATE;
	}
}

static struct usb_endpoint *alloc_endpoint(struct usb_info *ui,
					   struct usb_function_info *owner,
					   unsigned kind)
{
	unsigned n;
	for (n = 0; n < 32; n++) {
		struct usb_endpoint *ept = ui->ept + n;
		if (ept->num == 0)
			continue;
		if (ept->owner)
			continue;

		if (ept->flags & EPT_FLAG_IN) {
			if (kind == EPT_BULK_IN) {
				ept->max_pkt = 512;
				ept->owner = owner;
				return ept;
			} else if (kind == EPT_INT_IN) {
				ept->max_pkt = 64;
				ept->owner = owner;
				return ept;
			}
		} else {
			if (kind == EPT_BULK_OUT) {
				ept->max_pkt = 512;
				ept->owner = owner;
				return ept;
			} else if (kind == EPT_INT_OUT) {
				ept->max_pkt = 64;
				ept->owner = owner;
				return ept;
			}
		}
	}

	return 0;
}

static void free_endpoints(struct usb_info *ui, struct usb_function_info *owner)
{
	unsigned n;
	for (n = 0; n < 32; n++) {
		struct usb_endpoint *ept = ui->ept + n;
		if (ept->owner == owner) {
			ept->owner = 0;
			ept->max_pkt = 0;
		}
	}
}

struct usb_request *usb_ept_alloc_req(struct usb_endpoint *ept,
					unsigned bufsize)
{
	struct usb_info *ui = ept->ui;
	struct msm_request *req;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		goto fail1;

	req->item = dma_pool_alloc(ui->pool, GFP_KERNEL, &req->item_dma);
	if (!req->item)
		goto fail2;

	if (bufsize) {
		req->req.buf = kmalloc(bufsize, GFP_KERNEL);
		if (!req->req.buf)
			goto fail3;
		req->alloced = 1;
	}

	return &req->req;

fail3:
	dma_pool_free(ui->pool, req->item, req->item_dma);
fail2:
	kfree(req);
fail1:
	return 0;
}

static void do_free_req(struct usb_info *ui, struct msm_request *req)
{
	if (req->alloced)
		kfree(req->req.buf);

	dma_pool_free(ui->pool, req->item, req->item_dma);
	kfree(req);
}

void usb_ept_free_req(struct usb_endpoint *ept, struct usb_request *_req)
{
	struct msm_request *req = to_msm_request(_req);
	struct usb_info *ui = ept->ui;
	unsigned long flags;
	int dead = 0;

	spin_lock_irqsave(&ui->lock, flags);
	/* defer freeing resources if request is still busy */
	if (req->busy)
		dead = req->dead = 1;
	spin_unlock_irqrestore(&ui->lock, flags);

	/* if req->dead, then we will clean up when the request finishes */
	if (!dead)
		do_free_req(ui, req);
}

static void usb_ept_enable(struct usb_endpoint *ept, int yes)
{
	struct usb_info *ui = ept->ui;
	int in = ept->flags & EPT_FLAG_IN;
	unsigned char type = ept->type;
	unsigned n;

	if (yes) {
		if (ui->speed == USB_SPEED_HIGH)
			ept->max_pkt = 512;
		else
			ept->max_pkt = 64;
	}

	n = readl(USB_ENDPTCTRL(ept->num));

	if (in) {
		if (type == EPT_BULK_IN)
			n = (n & (~CTRL_TXT_MASK)) | CTRL_TXT_BULK;
		else if (type == EPT_INT_IN)
			n = (n & (~CTRL_TXT_MASK)) | CTRL_TXT_INT;
		if (yes)
			n |= CTRL_TXE | CTRL_TXR;
		else
			n &= (~CTRL_TXE);
	} else {
		if (type == EPT_BULK_OUT)
			n = (n & (~CTRL_RXT_MASK)) | CTRL_RXT_BULK;
		else if (type == EPT_INT_OUT)
			n = (n & (~CTRL_RXT_MASK)) | CTRL_RXT_INT;
		if (yes)
			n |= CTRL_RXE | CTRL_RXR;
		else
			n &= ~(CTRL_RXE);
	}
	writel(n, USB_ENDPTCTRL(ept->num));

#if 0
	printk(KERN_DEBUG "ept %d %s %s\n",
	       ept->num, in ? "in" : "out", yes ? "enabled" : "disabled");
#endif
}

static void usb_ept_start(struct usb_endpoint *ept)
{
	struct usb_info *ui = ept->ui;
	struct msm_request *req = ept->req;

	BUG_ON(req->live);

	/* link the hw queue head to the request's transaction item */
	ept->head->next = req->item_dma;
	ept->head->info = 0;

	/* start the endpoint */
	writel(1 << ept->bit, USB_ENDPTPRIME);

	/* mark this chain of requests as live */
	while (req) {
		req->live = 1;
		req = req->next;
	}
}

int usb_ept_queue_xfer(struct usb_endpoint *ept, struct usb_request *_req)
{
	unsigned long flags;
	struct msm_request *req = to_msm_request(_req);
	struct msm_request *last;
	struct usb_info *ui = ept->ui;
	struct ept_queue_item *item = req->item;
	unsigned length = req->req.length;

	if (length > 0x4000)
		return -EMSGSIZE;

	spin_lock_irqsave(&ui->lock, flags);

	if (req->busy) {
		req->req.status = -EBUSY;
		spin_unlock_irqrestore(&ui->lock, flags);
		printk(KERN_WARNING
		       "usb_ept_queue_xfer() tried to queue busy request\n");
		return -EBUSY;
	}

	if (!ui->online && (ept->num != 0)) {
		req->req.status = -ENODEV;
		spin_unlock_irqrestore(&ui->lock, flags);
		printk(KERN_WARNING "usb_ept_queue_xfer() tried to queue request while offline\n");
		return -ENODEV;
	}

	req->busy = 1;
	req->live = 0;
	req->next = 0;
	req->req.status = -EBUSY;

	req->dma = dma_map_single(NULL, req->req.buf, length,
				  (ept->flags & EPT_FLAG_IN) ?
				  DMA_TO_DEVICE : DMA_FROM_DEVICE);

	/* prepare the transaction descriptor item for the hardware */
	item->next = TERMINATE;
	item->info = INFO_BYTES(length) | INFO_IOC | INFO_ACTIVE;
	item->page0 = req->dma;
	item->page1 = (req->dma + 0x1000) & 0xfffff000;
	item->page2 = (req->dma + 0x2000) & 0xfffff000;
	item->page3 = (req->dma + 0x3000) & 0xfffff000;

	/* Add the new request to the end of the queue */
	last = ept->last;
	if (last) {
		/* Already requests in the queue. add us to the
		 * end, but let the completion interrupt actually
		 * start things going, to avoid hw issues
		 */
		last->next = req;

		/* only modify the hw transaction next pointer if
		 * that request is not live
		 */
		if (!last->live)
			last->item->next = req->item_dma;
	} else {
		/* queue was empty -- kick the hardware */
		ept->req = req;
		usb_ept_start(ept);
	}
	ept->last = req;

	spin_unlock_irqrestore(&ui->lock, flags);
	return 0;
}

int usb_ept_flush(struct usb_endpoint *ept)
{
	printk(KERN_INFO "%s \n", __func__);
	flush_endpoint(ept);
	return 0;
}

int usb_ept_get_max_packet(struct usb_endpoint *ept)
{
	return ept->max_pkt;
}

/* --- endpoint 0 handling --- */

static void set_configuration(struct usb_info *ui)
{
	unsigned int i, n, online;

	online = ui->online;
	for (i = 0; i < ui->num_funcs; i++) {
		struct usb_function_info *fi = ui->func[i];

		if (fi->endpoints == 0)
			continue;

		for (n = 0; n < fi->endpoints; n++)
			usb_ept_enable(fi->ept[n].ept, online);

		fi->func->configure(online, fi->func->context);
	}
}

static void ep0out_complete(struct usb_endpoint *ept, struct usb_request *req)
{
	req->complete = 0;
}

static void ep0in_complete(struct usb_endpoint *ept, struct usb_request *req)
{
	/* queue up the receive of the ACK response from the host */
	if (req->status == 0) {
		struct usb_info *ui = ept->ui;
		req->length = 0;
		req->complete = ep0out_complete;
		usb_ept_queue_xfer(&ui->ep0out, req);
	}
}

static void ep0in_complete_sendzero(struct usb_endpoint *ept,
				struct usb_request *req)
{
	if (req->status == 0) {
		struct usb_info *ui = ept->ui;
		req->length = 0;
		req->complete = ep0in_complete;
		usb_ept_queue_xfer(&ui->ep0in, req);
	}
}

static void ep0_setup_ack(struct usb_info *ui)
{
	struct usb_request *req = ui->setup_req;
	req->length = 0;
	req->complete = 0;
	usb_ept_queue_xfer(&ui->ep0in, req);
}

static void ep0_setup_stall(struct usb_info *ui)
{
	writel((1<<16) | (1<<0), USB_ENDPTCTRL(0));
}

static void ep0_setup_send(struct usb_info *ui, unsigned wlen)
{
	struct usb_request *req = ui->setup_req;
	struct usb_endpoint *ept = &ui->ep0in;

	/* never send more data than the host requested */
	if (req->length > wlen)
		req->length = wlen;

	/* if we are sending a short response that ends on
	 * a packet boundary, we'll need to send a zero length
	 * packet as well.
	 */
	if ((req->length != wlen) && ((req->length & 63) == 0))
		req->complete = ep0in_complete_sendzero;
	else
		req->complete = ep0in_complete;

	usb_ept_queue_xfer(ept, req);
}


static int usb_find_descriptor(struct usb_info *ui,
				unsigned id, struct usb_request *req);

static void handle_setup(struct usb_info *ui)
{
	struct usb_ctrlrequest ctl;

	memcpy(&ctl, ui->ep0out.head->setup_data, sizeof(ctl));
	writel(EPT_RX(0), USB_ENDPTSETUPSTAT);

	/* any pending ep0 transactions must be canceled */
	flush_endpoint(&ui->ep0out);
	flush_endpoint(&ui->ep0in);

#if 0
	printk(KERN_DEBUG
	       "setup: type=%02x req=%02x val=%04x idx=%04x len=%04x\n",
	       ctl.bRequestType, ctl.bRequest, ctl.wValue,
	       ctl.wIndex, ctl.wLength);
#endif

	if (ctl.bRequestType == (USB_DIR_IN | USB_TYPE_STANDARD)) {
		if (ctl.bRequest == USB_REQ_GET_STATUS) {
			if (ctl.wLength == 2) {
				struct usb_request *req = ui->setup_req;
				req->length = 2;
				memset(req->buf, 0, 2);
				ep0_setup_send(ui, 2);
				return;
			}
		}
		if (ctl.bRequest == USB_REQ_GET_DESCRIPTOR) {
			struct usb_request *req = ui->setup_req;

			if (!usb_find_descriptor(ui, ctl.wValue, req)) {
				ep0_setup_send(ui, ctl.wLength);
				return;
			}
		}
	}
	if (ctl.bRequestType ==
		(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT)) {
		if (ctl.bRequest == USB_REQ_CLEAR_FEATURE) {
			if ((ctl.wValue == 0) && (ctl.wLength == 0)) {
				unsigned num = ctl.wIndex & 0x0f;

				if (num != 0) {
					if (ctl.wIndex & 0x80)
						num += 16;

					usb_ept_enable(ui->ept + num, 1);
					ep0_setup_ack(ui);
					return;
				}
			}
		}
	}
	if (ctl.bRequestType ==
		(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE)) {
		if (ctl.bRequest == USB_REQ_SET_INTERFACE) {
			if ((ctl.wValue == 0) && (ctl.wIndex == 0)
				&& (ctl.wLength == 0)) {
				/* XXX accept for non-0 interfaces */
				ep0_setup_ack(ui);
				return;
			}
		}
	}
	if (ctl.bRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD)) {
		if (ctl.bRequest == USB_REQ_SET_CONFIGURATION) {
			ui->online = !!ctl.wValue;
			set_configuration(ui);
			configure_endpoints(ui);
			goto ack;
		}
		if (ctl.bRequest == USB_REQ_SET_ADDRESS) {
			/* write address delayed (will take effect
			** after the next IN txn)
			*/
			writel((ctl.wValue << 25) | (1 << 24), USB_DEVICEADDR);
			goto ack;
		}
	}

	if ((ctl.bRequestType & USB_TYPE_MASK) != USB_TYPE_STANDARD) {
		/* let functions handle vendor and class requests */

		int i;
		for (i = 0; i < ui->num_funcs; i++) {
			struct usb_function_info *fi = ui->func[i];

			if (fi->func->setup && ctl.wIndex ==
				fi->ifc[0].bInterfaceNumber) {
				if (ctl.bRequestType & USB_DIR_IN) {
					/* IN request */
					struct usb_request *req = ui->setup_req;

					int ret = fi->func->setup(&ctl,
						req->buf,
						SETUP_BUF_SIZE,
						fi->func->context);
					if (ret >= 0) {
						req->length = ret;
						ep0_setup_send(ui, ctl.wLength);
						return;
					}
				} else {
					/* OUT request */
					/* FIXME - support reading setup
					 * data from host.
					 */
					int ret = fi->func->setup(&ctl, NULL, 0,
							fi->func->context);
					/*
					if (ret > 0) {
						return;
					}
					else if(ret == 0) goto ack;
					*/
					if (ret >= 0)
						goto ack;
				}
			}
		}
	}

	ep0_setup_stall(ui);
	return;

ack:
	ep0_setup_ack(ui);
}

static void handle_endpoint(struct usb_info *ui, unsigned bit)
{
	struct usb_endpoint *ept = ui->ept + bit;
	struct msm_request *req;
	unsigned long flags;
	unsigned info;

#if 0
	printk(KERN_DEBUG "handle_endpoint() %d %s req=%p(%08x)\n",
	       ept->num, (ept->flags & EPT_FLAG_IN) ? "in" : "out",
	       ept->req, ept->req ? ept->req->item_dma : 0);
#endif

	/* expire all requests that are no longer active */
	spin_lock_irqsave(&ui->lock, flags);
	while ((req = ept->req)) {
		info = req->item->info;

		/* if we've processed all live requests, time to
		 * restart the hardware on the next non-live request
		 */
		if (!req->live) {
			usb_ept_start(ept);
			break;
		}

		/* if the transaction is still in-flight, stop here */
		if (info & INFO_ACTIVE)
			break;

		/* advance ept queue to the next request */
		ept->req = req->next;
		if (ept->req == 0)
			ept->last = 0;

		dma_unmap_single(NULL, req->dma, req->req.length,
				 (ept->flags & EPT_FLAG_IN) ?
				 DMA_TO_DEVICE : DMA_FROM_DEVICE);

		if (info & (INFO_HALTED | INFO_BUFFER_ERROR | INFO_TXN_ERROR)) {
			/* XXX pass on more specific error code */
			req->req.status = -EIO;
			req->req.actual = 0;
			printk(KERN_INFO "hsusb: ept %d %s error. info=%08x\n",
			       ept->num,
			       (ept->flags & EPT_FLAG_IN) ? "in" : "out",
			       info);
		} else {
			req->req.status = 0;
			req->req.actual = req->req.length -
				((info >> 16) & 0x7FFF);
		}
		req->busy = 0;
		req->live = 0;
		if (req->dead)
			do_free_req(ui, req);

		if (req->req.complete) {
			spin_unlock_irqrestore(&ui->lock, flags);
			req->req.complete(ept, &req->req);
			spin_lock_irqsave(&ui->lock, flags);
		}
	}
	spin_unlock_irqrestore(&ui->lock, flags);
}

static void flush_endpoint_hw(struct usb_info *ui, unsigned bits)
{
	/* flush endpoint, canceling transactions
	** - this can take a "large amount of time" (per databook)
	** - the flush can fail in some cases, thus we check STAT
	**   and repeat if we're still operating
	**   (does the fact that this doesn't use the tripwire matter?!)
	*/
	do {
		writel(bits, USB_ENDPTFLUSH);
		while (readl(USB_ENDPTFLUSH) & bits)
			udelay(100);
	} while (readl(USB_ENDPTSTAT) & bits);
}

static void flush_endpoint_sw(struct usb_endpoint *ept)
{
	struct usb_info *ui = ept->ui;
	struct msm_request *req, *next;
	unsigned long flags;

	/* inactive endpoints have nothing to do here */
	if (ept->max_pkt == 0)
		return;

	/* put the queue head in a sane state */
	ept->head->info = 0;
	ept->head->next = TERMINATE;

	/* cancel any pending requests */
	spin_lock_irqsave(&ui->lock, flags);
	req = ept->req;
	ept->req = 0;
	ept->last = 0;
	while (req != 0) {
		next = req->next;

		req->busy = 0;
		req->live = 0;
		req->req.status = -ENODEV;
		req->req.actual = 0;
		if (req->req.complete) {
			spin_unlock_irqrestore(&ui->lock, flags);
			req->req.complete(ept, &req->req);
			spin_lock_irqsave(&ui->lock, flags);
		}
		if (req->dead)
			do_free_req(ui, req);
		req = req->next;
	}
	spin_unlock_irqrestore(&ui->lock, flags);

}

static void flush_endpoint(struct usb_endpoint *ept)
{
	flush_endpoint_hw(ept->ui, (1 << ept->bit));
	flush_endpoint_sw(ept);
}

static void flush_all_endpoints(struct usb_info *ui)
{
	unsigned n;

	flush_endpoint_hw(ui, 0xffffffff);

	for (n = 0; n < 32; n++)
		flush_endpoint_sw(ui->ept + n);
}

/* add usb_connected notify behavior ===== */
static DEFINE_MUTEX(notify_sem);
static atomic_t atomic_usb_connected = ATOMIC_INIT(0);
static struct work_struct usb_connect_notifier_wq;

static void send_usb_connect_notify(struct work_struct *send_usb_wq)
{
	static struct t_usb_status_notifier *notifier = NULL;
	mutex_lock(&notify_sem);
	list_for_each_entry(notifier, &g_lh_usb_notifier_list, notifier_link)	{
		if (notifier->func != NULL)	{
			/* Notify other drivers about USB online. */
			notifier->func(atomic_read(&atomic_usb_connected));
		}
	}
	mutex_unlock(&notify_sem);
}

int usb_register_notifier(struct t_usb_status_notifier *notifier)
{
	if (!notifier || !notifier->name || !notifier->func)
		return -EINVAL;

	mutex_lock(&notify_sem);
	list_add(&notifier->notifier_link,
		&g_lh_usb_notifier_list);
	mutex_unlock(&notify_sem);
	return 0;
}

#ifdef MSM_HSUSB_SHOW_USB_NOTIFIER_MESSAGE
static void usb_notify_connected(int connected)
{
	printk(KERN_DEBUG "\n\n%s() RUN(%d)...............\n\n\n",
		__func__, connected);
}

static struct t_usb_status_notifier usb_notifier = {
	.name = "usb_connected",
	.func = usb_notify_connected,
};
#endif /* MSM_HSUSB_SHOW_USB_NOTIFIER_MESSAGE */
/* END: add usb_connected notify behavior ===== */

static irqreturn_t usb_interrupt(int irq, void *data)
{
	struct usb_info *ui = data;
	unsigned n;

	n = readl(USB_USBSTS);
	writel(n, USB_USBSTS);

	/* somehow we got an IRQ while in the reset sequence: ignore it */
	if (ui->running == 0)
		return IRQ_HANDLED;

	if (n & STS_PCI) {
		switch (readl(USB_PORTSC) & PORTSC_PSPD_MASK) {
		case PORTSC_PSPD_FS:
			printk(KERN_INFO "usb: portchange USB_SPEED_FULL\n");
			ui->speed = USB_SPEED_FULL;
			break;
		case PORTSC_PSPD_LS:
			printk(KERN_INFO "usb: portchange USB_SPEED_LOW\n");
			ui->speed = USB_SPEED_LOW;
			break;
		case PORTSC_PSPD_HS:
			printk(KERN_INFO "usb: portchange USB_SPEED_HIGH\n");
			ui->speed = USB_SPEED_HIGH;
			break;
		}
	}

	if (n & STS_URI) {
		printk(KERN_INFO "usb: reset\n");

		writel(readl(USB_ENDPTSETUPSTAT), USB_ENDPTSETUPSTAT);
		writel(readl(USB_ENDPTCOMPLETE), USB_ENDPTCOMPLETE);
		writel(0xffffffff, USB_ENDPTFLUSH);
		writel(0, USB_ENDPTCTRL(1));

		if (ui->online != 0) {
			/* marking us offline will cause ept queue attempts to fail */
			ui->online = 0;

			flush_all_endpoints(ui);

			/* XXX: we can't seem to detect going offline, so deconfigure
			 * XXX: on reset for the time being
			 */
			set_configuration(ui);
		}
		if (atomic_read(&atomic_usb_connected) == 0)	{
			atomic_set(&atomic_usb_connected, 1);
			schedule_work(&usb_connect_notifier_wq);
		}
	}

	if (n & STS_SLI)
		printk(KERN_INFO "usb: suspend\n");

	if (n & STS_UI) {
		n = readl(USB_ENDPTSETUPSTAT);
		if (n & EPT_RX(0))
			handle_setup(ui);

		n = readl(USB_ENDPTCOMPLETE);
		writel(n, USB_ENDPTCOMPLETE);
		while (n) {
			unsigned bit = __ffs(n);
			handle_endpoint(ui, bit);
			n = n & (~(1 << bit));
		}
	}
	return IRQ_HANDLED;
}

static void msm_hsusb_request_reset(struct usb_info *reset_ui)
{
	unsigned long flags;
	spin_lock_irqsave(&reset_ui->lock, flags);
	reset_ui->flags |= USB_FLAG_RESET;
	schedule_work(&reset_ui->work);
	spin_unlock_irqrestore(&reset_ui->lock, flags);
}

static ssize_t show_usb_cable_connect(struct device *dev,
				struct device_attribute *attr,
		char *buf)
{
	unsigned length;
	length = sprintf(buf, "%d", atomic_read(&atomic_usb_connected));
	return length;
}

static DEVICE_ATTR(usb_cable_connect, 0444, show_usb_cable_connect, NULL);

static int usb_setting_serial_number_mfg = -1;
static char mfg_df_serialno[16];
static const char dummy_str[] = "000000000000";
/* Assign serial number 0 under DIAG mode */
static char g_normal_serialno[16];


static ssize_t show_usb_serial_number(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	unsigned length;
	struct msm_hsusb_platform_data *pdata = dev->platform_data;

	length = sprintf(buf, "%s", pdata->serial_number);
	return length;
}

static ssize_t store_usb_serial_number(struct device *dev,
					struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct usb_info *ui = the_usb_info;
	struct msm_hsusb_platform_data *pdata = dev->platform_data;

	if (buf[0] == '0' || buf[0] == '1')	{
		memset(mfg_df_serialno, 0x0, sizeof(mfg_df_serialno));
		if (buf[0] == '0')       {
			strncpy(mfg_df_serialno, dummy_str, strlen(dummy_str));
			usb_setting_serial_number_mfg = 1;
		}	else	{
			strncpy(mfg_df_serialno, pdata->serial_number,
				strlen(pdata->serial_number));
			usb_setting_serial_number_mfg = 0;
		}
		/* reset_device */
		if (ui)
			msm_hsusb_request_reset(ui);
	}

	return count;
}

static DEVICE_ATTR(usb_serial_number, 0644,
			show_usb_serial_number, store_usb_serial_number);

static ssize_t show_dummy_usb_serial_number(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	unsigned length;
	struct msm_hsusb_platform_data *pdata = dev->platform_data;

	if (usb_setting_serial_number_mfg)
		length = sprintf(buf, "%s", mfg_df_serialno); /* dummy */
	else
		length = sprintf(buf, "%s", pdata->serial_number); /* Real */
	return length;
}

static ssize_t store_dummy_usb_serial_number(struct device *dev,
struct device_attribute *attr, const char *buf, size_t count)
{
	struct usb_info *ui = the_usb_info;
	int data_buff_size = (sizeof(mfg_df_serialno) > strlen(buf))?
		strlen(buf):sizeof(mfg_df_serialno);
	int loop_i;

	/* avoid overflow, mfg_df_serialno[16] always is 0x0 */
	if (data_buff_size == 16)
		data_buff_size--;

	for (loop_i = 0; loop_i < data_buff_size; loop_i++)	{
		if (buf[loop_i] >= 0x30 && buf[loop_i] <= 0x39) /* 0-9 */
			continue;
		else
		if (buf[loop_i] >= 0x41 && buf[loop_i] <= 0x5A) /* A-Z */
			continue;
		if (buf[loop_i] == 0x0A) /* Line Feed */
			continue;
		else {
			printk(KERN_WARNING "%s(): get invaild char (0x%2.2X)\n",
			 __func__, buf[loop_i]);
			return -EINVAL;
		}
	}

	if (!usb_setting_serial_number_mfg)
		usb_setting_serial_number_mfg = 1;
	memset(mfg_df_serialno, 0x0, sizeof(mfg_df_serialno));
	strncpy(mfg_df_serialno, buf, data_buff_size);
	/*device_reset */
	if (ui)
		msm_hsusb_request_reset(ui);

	return count;
}

static DEVICE_ATTR(dummy_usb_serial_number, 0644,
	show_dummy_usb_serial_number, store_dummy_usb_serial_number);

static ssize_t show_usb_function_switch(struct device *dev,
				struct device_attribute *attr,
		char *buf)
{
	unsigned length = 0, loop_i;
	struct msm_hsusb_platform_data *pdata;
	struct usb_function_info *fi;
	struct usb_info *ui = the_usb_info;
	pdata = ui->pdev->dev.platform_data;
	for (loop_i = 0; loop_i < pdata->num_functions; loop_i++) {
		fi = usb_find_function(pdata->functions[loop_i]);
		if (!fi)	{
			printk(KERN_ERR "%s(): usb_find_function(%s) fail\n", __func__, pdata->functions[loop_i]);
			return 0;
		}
		length += sprintf(buf + length, "%s:%s\n",
				fi->func->name,
				(fi->enabled)?"enable":"disable");
	}
	return length;
}

int usb_function_switch(unsigned func_switch)
{
	unsigned loop_i;
	struct usb_info *ui = the_usb_info;
	struct msm_hsusb_platform_data *pdata;
	struct usb_function_info *fi;
	unsigned u;

	pdata = ui->pdev->dev.platform_data;
	u = func_switch;
	if (u < 0 || u > 255)
		return -1;

	for (loop_i = 0; loop_i < pdata->num_functions; loop_i++) {
		fi = usb_find_function(pdata->functions[loop_i]);
		if (!fi) {
			printk(KERN_ERR "%s(): usb_find_function fail\n",
				__func__);
			return -1;
		}
		if (u & (1 << fi->func->position_bit))
			fi->enabled = 1;
		else
			fi->enabled = 0;
	}

	if (ui->state == USB_STATE_ONLINE)
		msm_hsusb_request_reset(ui);
	return 0;
}

static ssize_t store_usb_function_switch(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned u;
	ssize_t  ret;

	u = simple_strtoul(buf, NULL, 10);
	ret = usb_function_switch(u);

	if (ret == 0)
		return count;
	else
		return ret;
}

static DEVICE_ATTR(usb_function_switch, 0666,
	show_usb_function_switch, store_usb_function_switch);

static void usb_prepare(struct usb_info *ui)
{
	int ret = -1;

	spin_lock_init(&ui->lock);

	memset(ui->buf, 0, 4096);
	ui->head = (void *) (ui->buf + 0);
	ui->state=0;

	/* only important for reset/reinit */
	memset(ui->ept, 0, sizeof(ui->ept));
	ui->next_item = 0;
	ui->next_ifc_num = 0;

	init_endpoints(ui);

	ui->ep0in.max_pkt = 64;
	ui->ep0out.max_pkt = 64;

	ui->setup_req = usb_ept_alloc_req(&ui->ep0in, SETUP_BUF_SIZE);

	INIT_WORK(&ui->work, usb_do_work);

#ifdef MSM_HSUSB_SHOW_USB_NOTIFIER_MESSAGE
	usb_register_notifier(&usb_notifier);
#endif
	INIT_WORK(&usb_connect_notifier_wq, send_usb_connect_notify);

	ret = device_create_file(&ui->pdev->dev,
		&dev_attr_usb_cable_connect);
	if (ret != 0)
		printk(KERN_WARNING "dev_attr_usb_cable_connect failed\n");

	ret = device_create_file(&ui->pdev->dev,
		&dev_attr_usb_serial_number);
	if (ret != 0)
		printk(KERN_WARNING "dev_attr_usb_serial_number failed\n");

	ret = device_create_file(&ui->pdev->dev,
		&dev_attr_dummy_usb_serial_number);
	if (ret != 0)
		printk(KERN_WARNING "dev_attr_dummy_usb_serial_number failed\n");

	ret = device_create_file(&ui->pdev->dev,
		&dev_attr_usb_function_switch);
	if (ret != 0)
		printk(KERN_WARNING "dev_attr_usb_function_switch failed\n");
}

static void usb_suspend_phy(struct usb_info *ui)
{
	/* disable interface protect circuit to drop current consumption */
	ulpi_write(ui, (1 << 7), 0x08);
	/* clear the SuspendM bit -> suspend the PHY */
	ulpi_write(ui, 1 << 6, 0x06);
}

static void usb_bind_driver(struct usb_info *ui, struct usb_function_info *fi)
{
	struct usb_endpoint *ept;
	struct usb_endpoint_descriptor *ed;
	struct usb_endpoint *elist[10];
	struct usb_function *func = fi->func;
	unsigned n, count;

	if (func->ifc_num > 2)
		return;

	count = func->ifc_ept_count;

	if (count > 8)
		return;

	fi->ifc[0].bLength = USB_DT_INTERFACE_SIZE;
	fi->ifc[0].bDescriptorType = USB_DT_INTERFACE;
	fi->ifc[0].bAlternateSetting = 0;
	fi->ifc[0].bNumEndpoints = count != 2 ? 1 : count;
	fi->ifc[0].bInterfaceClass = func->ifc_class;
	fi->ifc[0].bInterfaceSubClass = func->ifc_subclass;
	fi->ifc[0].bInterfaceProtocol = func->ifc_protocol;
	fi->ifc[0].iInterface = func->ifc_index;

	if (func->cdc_desc) {
		fi->ifc[1].bLength = USB_DT_INTERFACE_SIZE;
		fi->ifc[1].bDescriptorType = USB_DT_INTERFACE;
		fi->ifc[1].bAlternateSetting = 0;
		fi->ifc[1].bNumEndpoints = 2;
		fi->ifc[1].bInterfaceClass = 0x0A;
		fi->ifc[1].bInterfaceSubClass = 0;
		fi->ifc[1].bInterfaceProtocol = 0;
		fi->ifc[1].iInterface = func->ifc_index;
	}
	for (n = 0; n < count; n++) {
		ept = alloc_endpoint(ui, fi, func->ifc_ept_type[n]);
		if (!ept) {
			printk(KERN_WARNING
			       "failed to allocated endpoint %d\n", n);
			free_endpoints(ui, fi);
			return;
		}
		ed = &(fi->ept[n].desc);

		ed->bLength = USB_DT_ENDPOINT_SIZE;
		ed->bDescriptorType = USB_DT_ENDPOINT;
		ed->bEndpointAddress =
			ept->num | ((ept->flags & EPT_FLAG_IN) ? 0x80 : 0);
		ed->bmAttributes =
			(func->ifc_ept_type[n] ==
				EPT_INT_IN || func->ifc_ept_type[n] ==
				EPT_INT_OUT) ? 0x03 : 0x02;
		ed->bInterval = 0;

		elist[n] = ept;
		ept->type = func->ifc_ept_type[n];
		fi->ept[n].ept = ept;
	}

	elist[count] = &ui->ep0in;
	elist[count + 1] = &ui->ep0out;

	fi->ifc[0].bInterfaceNumber = ui->next_ifc_num++;

	/* Workaround:  RNDIS have to be interfaceNumber = 0*/
	if (func->cdc_desc) {
		fi->ifc[0].bInterfaceNumber = 0;
		fi->ifc[1].bInterfaceNumber = 1;
		ui->next_ifc_num++;
	}
	fi->endpoints = count;

	func->bind(elist, func->context);
	printk(KERN_DEBUG "%s: %s\n", __func__, func->name);
}

static void usb_reset(struct usb_info *ui)
{
	unsigned long flags;
	printk(KERN_INFO "hsusb: reset controller\n");

	spin_lock_irqsave(&ui->lock, flags);
	ui->running = 0;
	spin_unlock_irqrestore(&ui->lock, flags);

#if 0
	/* we should flush and shutdown cleanly if already running */
	writel(0xffffffff, USB_ENDPTFLUSH);
	msleep(2);
#endif
	/* disable usb interrupts */
	writel(0, USB_USBINTR);

	/* wait for a while after enable usb clk*/
	msleep(5);

	/* RESET */
	writel(2, USB_USBCMD);
	msleep(10);

	if (ui->phy_reset)
		ui->phy_reset();

	/* INCR8 BURST mode */
	writel(0x02, USB_SBUSCFG);	/*boost performance to fix CRC error.*/

	/* select DEVICE mode */
	writel(0x12, USB_USBMODE);
	msleep(1);

	/* select ULPI phy */
	writel(0x80000000, USB_PORTSC);

	ulpi_init(ui);

	writel(ui->dma, USB_ENDPOINTLISTADDR);

	configure_endpoints(ui);

	/* marking us offline will cause ept queue attempts to fail */
	ui->online = 0;

	/* terminate any pending transactions */
	flush_all_endpoints(ui);

	printk(KERN_DEBUG "usb: notify offline\n");
	set_configuration(ui);

	/* enable interrupts */
	writel(STS_URI | STS_SLI | STS_UI | STS_PCI, USB_USBINTR);

	/* go to RUN mode (D+ pullup enable) */
	writel(0x00080001, USB_USBCMD);

	spin_lock_irqsave(&ui->lock, flags);
	ui->running = 1;
	spin_unlock_irqrestore(&ui->lock, flags);
}

static void usb_start(struct usb_info *ui)
{
	unsigned long flags;
	unsigned count = 0;
	int i;

	for (i = 0; i < ui->num_funcs; i++) {
		struct usb_function_info *fi = ui->func[i];
		usb_bind_driver(ui, fi);
		if (fi->endpoints)
			count++;
	}

	if (count == 0) {
		printk(KERN_DEBUG
		       "usb_start: no functions bound. not starting\n");
		return;
	}

	spin_lock_irqsave(&ui->lock, flags);
	ui->flags |= USB_FLAG_START;
	schedule_work(&ui->work);
	spin_unlock_irqrestore(&ui->lock, flags);
}

static LIST_HEAD(usb_function_list);
static DEFINE_MUTEX(usb_function_list_lock);

static struct usb_info *the_usb_info;

static struct usb_function_info *usb_find_function(const char *name)
{
	struct list_head *entry;
	list_for_each(entry, &usb_function_list) {
		struct usb_function_info *fi =
			list_entry(entry, struct usb_function_info, list);

		if (!strcmp(name, fi->func->name))
			return fi;
	}

	return NULL;
}

struct usb_fi_ept *get_ept_info(const char *function)
{
	struct usb_function_info *fi;
	fi = usb_find_function(function);
	if (!fi)
		return NULL;
	return &fi->ept[0];
}

struct usb_interface_descriptor *get_ifc_desc(const char *function)
{
	struct usb_function_info *fi;
	fi = usb_find_function(function);
	if (!fi)
		return NULL;
	return &fi->ifc[0];
}
static void usb_try_to_bind(void)
{
	struct usb_info *ui = the_usb_info;
	struct msm_hsusb_platform_data *pdata;
	struct usb_function_info *fi;
	int i;

	if (!ui || ui->bound || !ui->pdev)
		return;

	pdata = ui->pdev->dev.platform_data;

	for (i = 0; i < pdata->num_functions; i++) {
		if (ui->func[i])
			continue;
		fi = usb_find_function(pdata->functions[i]);
		if (!fi)
			return;
		ui->func[i] = fi;
		ui->num_funcs++;
	}

	/* we have found all the needed functions */
	ui->bound = 1;
	printk(KERN_DEBUG "%s: functions bound. starting.\n", __func__);
	usb_start(ui);
}

int usb_function_register(struct usb_function *driver)
{
	struct usb_function_info *fi;
	unsigned n;
	int ret = 0;

	mutex_lock(&usb_function_list_lock);

	n = driver->ifc_ept_count;
	fi = kzalloc(sizeof(*fi) + n * sizeof(struct usb_fi_ept), GFP_KERNEL);
	if (!fi) {
		ret = -ENOMEM;
		goto fail;
	}
	fi->func = driver;
	fi->enabled = !driver->disabled;
	list_add(&fi->list, &usb_function_list);

	usb_try_to_bind();
	printk(KERN_DEBUG "%s: %s\n", __func__, driver->name);
fail:
	mutex_unlock(&usb_function_list_lock);
	return ret;
}

unsigned return_usb_function_enabled(const char *function)
{
	struct usb_function_info *fi = usb_find_function(function);
	return fi->enabled;
}

int usb_function_enable(const char *function, int enable)
{
	struct usb_function_info *fi = usb_find_function(function);
	struct usb_info *ui = the_usb_info;

	if (fi && fi->enabled != enable) {
		fi->enabled = enable;

		if (ui)
			msm_hsusb_request_reset(ui);
	}
	return fi->enabled;
}

static int usb_free(struct usb_info *ui, int ret)
{
	if (ui->irq)
		free_irq(ui->irq, 0);
	if (ui->pool)
		dma_pool_destroy(ui->pool);
	if (ui->dma)
		dma_free_coherent(&ui->pdev->dev, 4096, ui->buf, ui->dma);
	if (ui->addr)
		iounmap(ui->addr);
	if (ui->clk)
		clk_put(ui->clk);
	if (ui->pclk)
		clk_put(ui->pclk);
	kfree(ui);
	return ret;
}

static void usb_do_work_check_vbus(struct usb_info *ui)
{
	unsigned long iflags;

	spin_lock_irqsave(&ui->lock, iflags);
	if (vbus)
		ui->flags |= USB_FLAG_VBUS_ONLINE;
	else
		ui->flags |= USB_FLAG_VBUS_OFFLINE;
	spin_unlock_irqrestore(&ui->lock, iflags);
}

static void usb_do_work(struct work_struct *w)
{
	struct usb_info *ui = container_of(w, struct usb_info, work);
	unsigned long iflags;
	unsigned flags, _vbus;

	for (;;) {
		spin_lock_irqsave(&ui->lock, iflags);
		flags = ui->flags;
		ui->flags = 0;
		_vbus = vbus;
		spin_unlock_irqrestore(&ui->lock, iflags);

		/* give up if we have nothing to do */
		if (flags == 0)
			break;

		switch (ui->state) {
		case USB_STATE_IDLE:
			if (flags & USB_FLAG_START) {
				pr_info("hsusb: IDLE -> ONLINE\n");
				clk_enable(ui->clk);
				clk_enable(ui->pclk);
				usb_reset(ui);

				ui->state = USB_STATE_ONLINE;
				usb_do_work_check_vbus(ui);
			}
			break;
		case USB_STATE_ONLINE:
			/* If at any point when we were online, we received
			 * the signal to go offline, we must honor it
			 */
			if (flags & USB_FLAG_VBUS_OFFLINE) {
				pr_info("hsusb: ONLINE -> OFFLINE\n");

				/* prevent irq context stuff from doing anything */
				spin_lock_irqsave(&ui->lock, iflags);

				if (atomic_read(&atomic_usb_connected) == 1)
					atomic_set(&atomic_usb_connected, 0);

				ui->running = 0;
				ui->online = 0;
				spin_unlock_irqrestore(&ui->lock, iflags);

				/* terminate any transactions, etc */
				flush_all_endpoints(ui);
				set_configuration(ui);

				/* power down phy, clock down usb */
				spin_lock_irqsave(&ui->lock, iflags);
				usb_suspend_phy(ui);
				clk_disable(ui->pclk);
				clk_disable(ui->clk);
				spin_unlock_irqrestore(&ui->lock, iflags);

				ui->state = USB_STATE_OFFLINE;
				usb_do_work_check_vbus(ui);
				if (is_perf_lock_active(&usb_perf_lock))
					perf_unlock(&usb_perf_lock);

				/*pull low D+*/
				writel(0x00080000, USB_USBCMD);

				break;
			}
			if (flags & USB_FLAG_RESET) {
				pr_info("hsusb: ONLINE -> RESET\n");
				usb_reset(ui);
				pr_info("hsusb: RESET -> ONLINE\n");
				break;
			}
			break;
		case USB_STATE_OFFLINE:
		default:
			/* If we were signaled to go online and vbus is still
			 * present when we received the signal, go online.
			 */
			if ((flags & USB_FLAG_VBUS_ONLINE) && _vbus) {
				pr_info("hsusb: OFFLINE -> ONLINE\n");
				clk_enable(ui->clk);
				clk_enable(ui->pclk);
				usb_reset(ui);

				ui->state = USB_STATE_ONLINE;
				usb_do_work_check_vbus(ui);
				if (!is_perf_lock_active(&usb_perf_lock))
					perf_lock(&usb_perf_lock);
			}
			break;
		}
	}
}


void msm_hsusb_set_vbus_state(int online)
{
	unsigned long flags = 0;
	struct usb_info *ui = the_usb_info;

	if (ui)
	{
		spin_lock_irqsave(&ui->lock, flags);
		hrtimer_cancel(&ui->vbus_timer);
	}
	if (vbus != online) {
		vbus = online;
		if (ui) {
			if (online)
			{
				ui->flags|=USB_FLAG_VBUS_ONLINE;
				schedule_work(&ui->work);
				/*ui->flags |= USB_FLAG_VBUS_ONLINE;
				delay 1.5s
				hrtimer_start(&ui->vbus_timer, ktime_set(1, 500 *1000 *1000), HRTIMER_MODE_REL);
				*/
				/*delay 1s*/
				//hrtimer_start(&ui->vbus_timer, ktime_set(1, 0), HRTIMER_MODE_REL);
			}
			else
			{
				ui->flags |= USB_FLAG_VBUS_OFFLINE;
				schedule_work(&ui->work);
			}
		}
	}
	if (ui)
		spin_unlock_irqrestore(&ui->lock, flags);
}

static enum hrtimer_restart vbus_polling_timer_func(struct hrtimer *timer)
{
	unsigned long flags = 0;
	struct usb_info *ui= container_of(timer, struct usb_info, vbus_timer);

	if (ui)
	{
		spin_lock_irqsave(&ui->lock, flags);
		ui->flags |= USB_FLAG_VBUS_ONLINE;
		schedule_work(&ui->work);
		spin_unlock_irqrestore(&ui->lock, flags);
	}
	return HRTIMER_NORESTART;
}


void usb_function_reenumerate(void)
{
	struct usb_info *ui = the_usb_info;

	/* disable and re-enable the D+ pullup */
	printk(KERN_DEBUG "%s(): runing...\n", __func__);
	writel(0x00080000, USB_USBCMD);
	msleep(10);
	writel(0x00080001, USB_USBCMD);
}

#if defined(CONFIG_DEBUG_FS)
static char debug_buffer[PAGE_SIZE];

static ssize_t debug_read_status(struct file *file, char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	struct usb_info *ui = file->private_data;
	char *buf = debug_buffer;
	unsigned long flags;
	struct usb_endpoint *ept;
	struct msm_request *req;
	int n;
	int i = 0;

	spin_lock_irqsave(&ui->lock, flags);

	i += scnprintf(buf + i, PAGE_SIZE - i,
		       "regs: setup=%08x prime=%08x stat=%08x done=%08x\n",
		       readl(USB_ENDPTSETUPSTAT),
		       readl(USB_ENDPTPRIME),
		       readl(USB_ENDPTSTAT),
		       readl(USB_ENDPTCOMPLETE));
	i += scnprintf(buf + i, PAGE_SIZE - i,
		       "regs:   cmd=%08x   sts=%08x intr=%08x port=%08x\n\n",
		       readl(USB_USBCMD),
		       readl(USB_USBSTS),
		       readl(USB_USBINTR),
		       readl(USB_PORTSC));


	for (n = 0; n < 32; n++) {
		ept = ui->ept + n;
		if (ept->max_pkt == 0)
			continue;

		i += scnprintf(buf + i, PAGE_SIZE - i,
			       "ept%d %s cfg=%08x active=%08x next=%08x info=%08x\n",
			       ept->num, (ept->flags & EPT_FLAG_IN) ? "in " : "out",
			       ept->head->config, ept->head->active,
			       ept->head->next, ept->head->info);

		for (req = ept->req; req; req = req->next)
			i += scnprintf(buf + i, PAGE_SIZE - i,
				       "  req @%08x next=%08x info=%08x page0=%08x %c %c\n",
				       req->item_dma, req->item->next,
				       req->item->info, req->item->page0,
				       req->busy ? 'B' : ' ',
				       req->live ? 'L' : ' '
				);
	}

	i += scnprintf(buf + i, PAGE_SIZE - i,
		       "phy failure count: %d\n", ui->phy_fail_count);

	spin_unlock_irqrestore(&ui->lock, flags);

	return simple_read_from_buffer(ubuf, count, ppos, buf, i);
}

static ssize_t debug_write_reset(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct usb_info *ui = file->private_data;
	unsigned long flags;

	spin_lock_irqsave(&ui->lock, flags);
	ui->flags |= USB_FLAG_RESET;
	schedule_work(&ui->work);
	spin_unlock_irqrestore(&ui->lock, flags);

	return count;
}

static ssize_t debug_write_cycle(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	usb_function_reenumerate();
	return count;
}

static int debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

const struct file_operations debug_stat_ops = {
	.open = debug_open,
	.read = debug_read_status,
};

const struct file_operations debug_reset_ops = {
	.open = debug_open,
	.write = debug_write_reset,
};

const struct file_operations debug_cycle_ops = {
	.open = debug_open,
	.write = debug_write_cycle,
};

static void usb_debugfs_init(struct usb_info *ui)
{
	struct dentry *dent;
	dent = debugfs_create_dir("usb", 0);
	if (IS_ERR(dent))
		return;

	debugfs_create_file("status", 0444, dent, ui, &debug_stat_ops);
	debugfs_create_file("reset", 0222, dent, ui, &debug_reset_ops);
	debugfs_create_file("cycle", 0222, dent, ui, &debug_cycle_ops);
}
#else
static void usb_debugfs_init(struct usb_info *ui) {}
#endif

static int usb_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct usb_info *ui;
	int irq;
	int ret;

	ui = kzalloc(sizeof(struct usb_info), GFP_KERNEL);
	if (!ui)
		return -ENOMEM;

	spin_lock_init(&ui->lock);
	ui->pdev = pdev;

	if (pdev->dev.platform_data) {
		struct msm_hsusb_platform_data *pdata = pdev->dev.platform_data;
		ui->phy_reset = pdata->phy_reset;
		ui->phy_shutdown = pdata->phy_shutdown;
		ui->phy_init_seq = pdata->phy_init_seq;

		/* USB device descriptor fields */
		desc_device.idVendor = pdata->vendor_id;
		desc_device.idProduct = pdata->product_id;
		desc_device.bcdDevice = pdata->version;
		if (pdata->serial_number)
			desc_device.iSerialNumber = STRING_SERIAL;
		if (pdata->product_name)
			desc_device.iProduct = STRING_PRODUCT;
		if (pdata->manufacturer_name)
			desc_device.iManufacturer = STRING_MANUFACTURER;

		ui->func = kzalloc(sizeof(struct usb_function *) *
				   pdata->num_functions, GFP_KERNEL);

		pdata = ui->pdev->dev.platform_data;
		strncpy(g_normal_serialno,
			pdata->serial_number, strlen(pdata->serial_number));

		if (!ui->func) {
			kfree(ui);
			return -ENOMEM;
		}
	}

	irq = platform_get_irq(pdev, 0);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res || (irq < 0))
		return usb_free(ui, -ENODEV);

	ui->addr = ioremap(res->start, 4096);
	if (!ui->addr)
		return usb_free(ui, -ENOMEM);

	ui->buf = dma_alloc_coherent(&pdev->dev, 4096, &ui->dma, GFP_KERNEL);
	if (!ui->buf)
		return usb_free(ui, -ENOMEM);

	ui->pool = dma_pool_create("hsusb", NULL, 32, 32, 0);
	if (!ui->pool)
		return usb_free(ui, -ENOMEM);

	printk(KERN_INFO "usb_probe() io=%p, irq=%d, dma=%p(%x)\n",
	       ui->addr, irq, ui->buf, ui->dma);

	ui->clk = clk_get(&pdev->dev, "usb_hs_clk");
	if (IS_ERR(ui->clk))
		return usb_free(ui, PTR_ERR(ui->clk));

	ui->pclk = clk_get(&pdev->dev, "usb_hs_pclk");
	if (IS_ERR(ui->pclk))
		return usb_free(ui, PTR_ERR(ui->pclk));

	printk(KERN_INFO "usb_probe() io=%p, irq=%d, dma=%p(%x)\n",
	       ui->addr, irq, ui->buf, ui->dma);

	ret = request_irq(irq, usb_interrupt, 0, pdev->name, ui);
	if (ret)
		return usb_free(ui, ret);
	enable_irq_wake(irq);
	ui->irq = irq;

	the_usb_info = ui;

	usb_debugfs_init(ui);

	usb_prepare(ui);

	 perf_lock_init(&usb_perf_lock, PERF_LOCK_HIGHEST, "usb");

	/* initialize mfg serial number */
	usb_setting_serial_number_mfg = 0;
	strncpy(mfg_df_serialno, dummy_str, strlen(dummy_str));

	hrtimer_init(&ui->vbus_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ui->vbus_timer.function = vbus_polling_timer_func ;


		pr_info("hsusb: IDLE -> ONLINE\n");
		clk_enable(ui->clk);
		clk_enable(ui->pclk);
		usb_reset(ui);

		ui->state = USB_STATE_ONLINE;
		usb_do_work_check_vbus(ui);

	return 0;
}

static struct platform_driver usb_driver = {
	.probe = usb_probe,
	.driver = { .name = "msm_hsusb", },
};

static int __init usb_init(void)
{
	return platform_driver_register(&usb_driver);
}

module_init(usb_init);

static void copy_string_descriptor(char *string, char *buffer)
{
	int length, i;

	if (string) {
		length = strlen(string);
		buffer[0] = 2 * length + 2;
		buffer[1] = USB_DT_STRING;
		for (i = 0; i < length; i++) {
			buffer[2 * i + 2] = string[i];
			buffer[2 * i + 3] = 0;
		}
	}
}

static int usb_find_descriptor(struct usb_info *ui,
			unsigned id, struct usb_request *req)
{
	struct msm_hsusb_platform_data *pdata = ui->pdev->dev.platform_data;
	int i;
	unsigned type = id >> 8;
	id &= 0xff;

	if ((type == USB_DT_DEVICE) && (id == 0)) {
		/* Compute our product ID based on which of our functions
		** are enabled. Also compute and save our list of enabled
		** functions to avoid a race condition between the
		** USB_DT_DEVICE and USB_DT_CONFIG requests.
		** (that is, make sure the product ID we return for
		** USB_DT_DEVICE matches the list of interfaces we
		** return for USB_DT_CONFIG.
		*/
		enabled_functions = 0;
		for (i = 0; i < ui->num_funcs; i++) {
			struct usb_function_info *fi = ui->func[i];
			if (fi->enabled)
				enabled_functions |=
				(1 << fi->func->position_bit);
		}

		if (enabled_functions &
			(1 << USB_FUNCTION_INTERNET_SHARING_NUM))
			desc_device.bDeviceClass = 0x02;
		else
			desc_device.bDeviceClass = 0;

		/* default product ID */
		desc_device.idProduct = pdata->product_id;
		/* set idProduct based on which functions are enabled */
		for (i = 0; i < pdata->num_products; i++) {
			if (pdata->products[i].functions == enabled_functions) {
				struct msm_hsusb_product *product =
					&pdata->products[i];
				if (product->functions == enabled_functions)
					desc_device.idProduct =
						product->product_id;
			}
		}
		/*
		printk(KERN_INFO "GET USB -> VID: 0x%4.4X, PID: 0x%4.4X, (0x%X) ###\n",
			desc_device.idVendor,
			desc_device.idProduct,enabled_functions);
		*/

		req->length = sizeof(desc_device);
		memcpy(req->buf, &desc_device, req->length);
		return 0;
	}

	if ((type == USB_DT_CONFIG) && (id == 0)) {
		struct usb_config_descriptor cfg;
		unsigned ifc_count = 0;
		unsigned n;
		char *ptr, *start;
		int max_packet;

		if (ui->speed == USB_SPEED_HIGH)
			max_packet = 512;
		else
			max_packet = 64;
		start = req->buf;
		ptr = start + USB_DT_CONFIG_SIZE;

		for (i = 0; i < ui->num_funcs; i++) {
			struct usb_function_info *fi = ui->func[i];

		/* check to see if the function was enabled when we
		** received the USB_DT_DEVICE request to make ensure
		** that the interfaces we return here match the
		** product ID we returned in the USB_DT_DEVICE.
		*/
		/* modify it, because occur lost interface/endpoint desctiptor
		 * by Anthony_Chang */
		/* if (enabled_functions & (1 << i)) {*/
			if (fi->enabled)	{
				if (fi->func->cdc_desc) {
					struct usb_descriptor_header **src =
						fi->func->cdc_desc;
					ifc_count += 2;
					for (; NULL != *src; src++) {
						unsigned len = (*src)->bLength;
						memcpy(ptr, *src, len);
						ptr += len;
					}
				} else {
					ifc_count++;
					memcpy(ptr, &fi->ifc[0],
						fi->ifc[0].bLength);
					ptr += fi->ifc[0].bLength;

					for (n = 0; n < fi->endpoints; n++) {
						/* XXX hardcoded bulk */
						fi->ept[n].desc.wMaxPacketSize =
							max_packet;
						memcpy(ptr,
							&(fi->ept[n].desc),
							fi->ept[n].desc.bLength);
						ptr += fi->ept[n].desc.bLength;
					}
				}
			}
		}

		cfg.bLength = USB_DT_CONFIG_SIZE;
		cfg.bDescriptorType = USB_DT_CONFIG;
		cfg.wTotalLength = ptr - start;
		cfg.bNumInterfaces = ifc_count;
		cfg.bConfigurationValue = 1;
		cfg.iConfiguration = 0;
		cfg.bmAttributes = 0x80;
		cfg.bMaxPower = 0x80;

		memcpy(start, &cfg, USB_DT_CONFIG_SIZE);

		req->length = ptr - start;
		return 0;
	}

	if (type == USB_DT_STRING) {
		char *buffer = req->buf;

		buffer[0] = 0;
		switch (id) {
		case STRING_LANGUAGE_ID:
		/* return language ID */
			buffer[0] = 4;
			buffer[1] = USB_DT_STRING;
			buffer[2] = LANGUAGE_ID & 0xFF;
			buffer[3] = LANGUAGE_ID >> 8;
		break;
		case STRING_SERIAL:
			if (!usb_setting_serial_number_mfg) /* real */
				copy_string_descriptor(pdata->serial_number,
						buffer);
			else	/* dummy */
				copy_string_descriptor(mfg_df_serialno,
						buffer);
				strncpy(pdata->serial_number,
					g_normal_serialno,
					strlen(g_normal_serialno));
		break;
		case STRING_PRODUCT:
			copy_string_descriptor(pdata->product_name,
						buffer);
		break;
		case STRING_MANUFACTURER:
			copy_string_descriptor(pdata->manufacturer_name,
						buffer);
		break;
		case STRING_UMS:
		case STRING_ADB:
		case STRING_ES:
		case STRING_FSERIAL:
		case STRING_PROJECTOR:
		case STRING_FSYNC:
		case STRING_DIAG:
		case STRING_MTP:
			copy_string_descriptor(strings[id-4], buffer);
		break;
		default:
			printk(KERN_WARNING "usb:wrong string id = 0x%x\n", id);
		break;
		}

		if (buffer[0]) {
			req->length = buffer[0];
			return 0;
		} else
			return -1;
	}

	return -1;
}

