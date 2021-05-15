#include <linux/iopoll.h>
#include <media/cec.h>

#include "hdmi.h"

#define HDMI_CEC_INT_MASK ( \
	HDMI_CEC_INT_TX_DONE_MASK | \
	HDMI_CEC_INT_TX_ERROR_MASK | \
	HDMI_CEC_INT_RX_DONE_MASK)

struct hdmi_cec_ctrl {
	struct hdmi *hdmi;
	struct work_struct work;
	spinlock_t lock;
	u32 irq_status;
	u32 tx_status;
	u32 tx_retransmits;
};

static int msm_hdmi_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct hdmi_cec_ctrl *cec_ctrl = adap->priv;
	struct hdmi *hdmi = cec_ctrl->hdmi;

	if (enable) {
		/* timer frequency, 19.2Mhz * 0.05ms / 1000ms = 960 */
		hdmi_write(hdmi, REG_HDMI_CEC_REFTIMER,
			   HDMI_CEC_REFTIMER_REFTIMER(960) |
			   HDMI_CEC_REFTIMER_ENABLE);

		/* read and write timings */
		hdmi_write(hdmi, REG_HDMI_CEC_RD_RANGE, 0x30AB9888);
		hdmi_write(hdmi, REG_HDMI_CEC_WR_RANGE, 0x888AA888);
		hdmi_write(hdmi, REG_HDMI_CEC_RD_START_RANGE, 0x88888888);
		hdmi_write(hdmi, REG_HDMI_CEC_RD_TOTAL_RANGE, 0x99);

		/* start bit low pulse duration, 3.7ms */
		hdmi_write(hdmi, REG_HDMI_CEC_RD_ERR_RESP_LO, 74);

		/* signal free time, 7 * 2.4ms */
		hdmi_write(hdmi, REG_HDMI_CEC_TIME,
			   HDMI_CEC_TIME_SIGNAL_FREE_TIME(7 * 48) |
			   HDMI_CEC_TIME_ENABLE);

		hdmi_write(hdmi, REG_HDMI_CEC_COMPL_CTL, 0xF);
		hdmi_write(hdmi, REG_HDMI_CEC_WR_CHECK_CONFIG, 0x4);
		hdmi_write(hdmi, REG_HDMI_CEC_RD_FILTER, BIT(0) | (0x7FF << 4));

		hdmi_write(hdmi, REG_HDMI_CEC_INT, HDMI_CEC_INT_MASK);
		hdmi_write(hdmi, REG_HDMI_CEC_CTRL, HDMI_CEC_CTRL_ENABLE);
	} else {
		hdmi_write(hdmi, REG_HDMI_CEC_INT, 0);
		hdmi_write(hdmi, REG_HDMI_CEC_CTRL, 0);
		cancel_work_sync(&cec_ctrl->work);
	}

	return 0;
}

static int msm_hdmi_cec_adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct hdmi_cec_ctrl *cec_ctrl = adap->priv;
	struct hdmi *hdmi = cec_ctrl->hdmi;

	hdmi_write(hdmi, REG_HDMI_CEC_ADDR, logical_addr & 0xF);

	return 0;
}

static int msm_hdmi_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				      u32 signal_free_time, struct cec_msg *msg)
{
	struct hdmi_cec_ctrl *cec_ctrl = adap->priv;
	struct hdmi *hdmi = cec_ctrl->hdmi;
	u8 retransmits;
	u32 broadcast;
	u32 status;
	int i;

	/* toggle cec in order to flush out bad hw state, if any */
	hdmi_write(hdmi, REG_HDMI_CEC_CTRL, 0);
	hdmi_write(hdmi, REG_HDMI_CEC_CTRL, HDMI_CEC_CTRL_ENABLE);

	/* flush register writes */
	wmb();

	retransmits = attempts ? (attempts - 1) : 0;
	hdmi_write(hdmi, REG_HDMI_CEC_RETRANSMIT,
		   HDMI_CEC_RETRANSMIT_ENABLE |
		   HDMI_CEC_RETRANSMIT_COUNT(retransmits));

	broadcast = cec_msg_is_broadcast(msg) ? HDMI_CEC_WR_DATA_BROADCAST : 0;
	for (i = 0; i < msg->len; i++) {
		hdmi_write(hdmi, REG_HDMI_CEC_WR_DATA,
			   HDMI_CEC_WR_DATA_DATA(msg->msg[i]) | broadcast);
	}

	/* check line status */
	if (read_poll_timeout(hdmi_read, status, !(status & HDMI_CEC_STATUS_BUSY),
			      5, 1000, false, hdmi, REG_HDMI_CEC_STATUS)) {
		pr_err("CEC line is busy. Retry failed\n");
		return -EBUSY;
	}

	cec_ctrl->tx_retransmits = retransmits;

	/* start transmission */
	hdmi_write(hdmi, REG_HDMI_CEC_CTRL,
		   HDMI_CEC_CTRL_ENABLE |
		   HDMI_CEC_CTRL_SEND_TRIGGER |
		   HDMI_CEC_CTRL_FRAME_SIZE(msg->len) |
		   HDMI_CEC_CTRL_LINE_OE);

	return 0;
}

static void msm_hdmi_cec_adap_free(struct cec_adapter *adap)
{
	struct hdmi_cec_ctrl *cec_ctrl = adap->priv;

	cec_ctrl->hdmi->cec_adap = NULL;
	kfree(cec_ctrl);
}

static const struct cec_adap_ops msm_hdmi_cec_adap_ops = {
	.adap_enable = msm_hdmi_cec_adap_enable,
	.adap_log_addr = msm_hdmi_cec_adap_log_addr,
	.adap_transmit = msm_hdmi_cec_adap_transmit,
	.adap_free = msm_hdmi_cec_adap_free,
};

#define CEC_IRQ_FRAME_WR_DONE 0x01
#define CEC_IRQ_FRAME_RD_DONE 0x02

static void msm_hdmi_cec_handle_rx_done(struct hdmi_cec_ctrl *cec_ctrl)
{
	struct hdmi *hdmi = cec_ctrl->hdmi;
	struct cec_msg msg = {};
	u32 data;
	int i;

	data = hdmi_read(hdmi, REG_HDMI_CEC_RD_DATA);
	msg.len = (data & 0x1f00) >> 8;
	if (msg.len < 1 || msg.len > CEC_MAX_MSG_SIZE)
		return;

	msg.msg[0] = data & 0xff;
	for (i = 1; i < msg.len; i++)
		msg.msg[i] = hdmi_read(hdmi, REG_HDMI_CEC_RD_DATA) & 0xff;

	cec_received_msg(hdmi->cec_adap, &msg);
}

static void msm_hdmi_cec_handle_tx_done(struct hdmi_cec_ctrl *cec_ctrl)
{
	struct hdmi *hdmi = cec_ctrl->hdmi;
	u32 tx_status;

	tx_status = (cec_ctrl->tx_status & HDMI_CEC_STATUS_TX_STATUS__MASK) >>
		HDMI_CEC_STATUS_TX_STATUS__SHIFT;

	switch (tx_status) {
	case 0:
		cec_transmit_done(hdmi->cec_adap,
				  CEC_TX_STATUS_OK, 0, 0, 0, 0);
		break;
	case 1:
		cec_transmit_done(hdmi->cec_adap,
				  CEC_TX_STATUS_NACK, 0, 1, 0, 0);
		break;
	case 2:
		cec_transmit_done(hdmi->cec_adap,
				  CEC_TX_STATUS_ARB_LOST, 1, 0, 0, 0);
		break;
	case 3:
		cec_transmit_done(hdmi->cec_adap,
				  CEC_TX_STATUS_MAX_RETRIES |
				  CEC_TX_STATUS_NACK,
				  0, cec_ctrl->tx_retransmits + 1, 0, 0);
		break;
	default:
		cec_transmit_done(hdmi->cec_adap,
				  CEC_TX_STATUS_ERROR, 0, 0, 0, 1);
		break;
	}
}

static void msm_hdmi_cec_work(struct work_struct *work)
{
	struct hdmi_cec_ctrl *cec_ctrl =
		container_of(work, struct hdmi_cec_ctrl, work);
	unsigned long flags;

	spin_lock_irqsave(&cec_ctrl->lock, flags);

	if (cec_ctrl->irq_status & CEC_IRQ_FRAME_WR_DONE)
		msm_hdmi_cec_handle_tx_done(cec_ctrl);

	if (cec_ctrl->irq_status & CEC_IRQ_FRAME_RD_DONE)
		msm_hdmi_cec_handle_rx_done(cec_ctrl);

	cec_ctrl->irq_status = 0;
	cec_ctrl->tx_status = 0;

	spin_unlock_irqrestore(&cec_ctrl->lock, flags);
}

void msm_hdmi_cec_irq(struct hdmi *hdmi)
{
	struct hdmi_cec_ctrl *cec_ctrl;
	unsigned long flags;
	u32 int_status;

	if (!hdmi->cec_adap)
		return;

	cec_ctrl = hdmi->cec_adap->priv;

	int_status = hdmi_read(hdmi, REG_HDMI_CEC_INT);
	if (!(int_status & HDMI_CEC_INT_MASK))
		return;

	spin_lock_irqsave(&cec_ctrl->lock, flags);

	if (int_status & (HDMI_CEC_INT_TX_DONE | HDMI_CEC_INT_TX_ERROR)) {
		cec_ctrl->tx_status = hdmi_read(hdmi, REG_HDMI_CEC_STATUS);
		cec_ctrl->irq_status |= CEC_IRQ_FRAME_WR_DONE;
	}

	if (int_status & HDMI_CEC_INT_RX_DONE)
		cec_ctrl->irq_status |= CEC_IRQ_FRAME_RD_DONE;

	spin_unlock_irqrestore(&cec_ctrl->lock, flags);

	hdmi_write(hdmi, REG_HDMI_CEC_INT, int_status);
	queue_work(hdmi->workq, &cec_ctrl->work);
}

int msm_hdmi_cec_init(struct hdmi *hdmi)
{
	struct platform_device *pdev = hdmi->pdev;
	struct hdmi_cec_ctrl *cec_ctrl;
	struct cec_adapter *cec_adap;
	int ret;

	cec_ctrl = kzalloc(sizeof (*cec_ctrl), GFP_KERNEL);
	if (!cec_ctrl)
		return -ENOMEM;

	cec_ctrl->hdmi = hdmi;
	INIT_WORK(&cec_ctrl->work, msm_hdmi_cec_work);

	cec_adap = cec_allocate_adapter(&msm_hdmi_cec_adap_ops,
					cec_ctrl, "msm",
					CEC_CAP_DEFAULTS |
					CEC_CAP_CONNECTOR_INFO, 1);
	ret = PTR_ERR_OR_ZERO(cec_adap);
	if (ret < 0) {
		kfree(cec_ctrl);
		return ret;
	}

	/* Set the logical address to Unregistered */
	hdmi_write(hdmi, REG_HDMI_CEC_ADDR, 0xf);

	ret = cec_register_adapter(cec_adap, &pdev->dev);
	if (ret < 0) {
		cec_delete_adapter(cec_adap);
		return ret;
	}

	hdmi->cec_adap = cec_adap;

	return 0;
}

void msm_hdmi_cec_exit(struct hdmi *hdmi)
{
	cec_unregister_adapter(hdmi->cec_adap);
}
