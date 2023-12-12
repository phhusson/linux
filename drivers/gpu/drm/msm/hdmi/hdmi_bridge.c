// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_edid.h>
#include <media/cec.h>
#include <drm/display/drm_scdc_helper.h>

#include "msm_kms.h"
#include "hdmi.h"

static void msm_hdmi_power_on(struct drm_bridge *bridge)
{
	struct drm_device *dev = bridge->dev;
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	int i, ret;

	pm_runtime_get_sync(&hdmi->pdev->dev);

	ret = regulator_bulk_enable(config->pwr_reg_cnt, hdmi->pwr_regs);
	if (ret)
		DRM_DEV_ERROR(dev->dev, "failed to enable pwr regulator: %d\n", ret);

	if (config->pwr_clk_cnt > 0) {
		DBG("pixclock: %lu", hdmi->pixclock);
		ret = clk_set_rate(hdmi->pwr_clks[0], hdmi->pixclock);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to set pixel clk: %s (%d)\n",
					config->pwr_clk_names[0], ret);
		}
	}

	for (i = 0; i < config->pwr_clk_cnt; i++) {
		ret = clk_prepare_enable(hdmi->pwr_clks[i]);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to enable pwr clk: %s (%d)\n",
					config->pwr_clk_names[i], ret);
		}
	}
}

static void power_off(struct drm_bridge *bridge)
{
	struct drm_device *dev = bridge->dev;
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	int i, ret;

	/* TODO do we need to wait for final vblank somewhere before
	 * cutting the clocks?
	 */
	mdelay(16 + 4);

	for (i = 0; i < config->pwr_clk_cnt; i++)
		clk_disable_unprepare(hdmi->pwr_clks[i]);

	ret = regulator_bulk_disable(config->pwr_reg_cnt, hdmi->pwr_regs);
	if (ret)
		DRM_DEV_ERROR(dev->dev, "failed to disable pwr regulator: %d\n", ret);

	pm_runtime_put(&hdmi->pdev->dev);
}

/* TX major version that supports scrambling */
#define HDMI_TX_SCRAMBLER_MIN_TX_VERSION 0x04
#define HDMI_TX_SCRAMBLER_THRESHOLD_RATE_KHZ 340000000
#define HDMI_TX_SCRAMBLER_TIMEOUT_MSEC 200

enum hdmi_tx_ddc_timer_type {
	HDMI_TX_DDC_TIMER_HDCP2P2_RD_MSG,
	HDMI_TX_DDC_TIMER_SCRAMBLER_STATUS,
	HDMI_TX_DDC_TIMER_UPDATE_FLAGS,
	HDMI_TX_DDC_TIMER_STATUS_FLAGS,
	HDMI_TX_DDC_TIMER_CED,
	HDMI_TX_DDC_TIMER_MAX,
};

#define HDMI_DEFAULT_TIMEOUT_HSYNC 28571

static void msm_hdmi_scrambler_ddc_reset(struct hdmi *hdmi)
{
	u32 reg_val;

	/* clear ack and disable interrupts */
	reg_val = BIT(14) | BIT(9) | BIT(5) | BIT(1);
	hdmi_write(hdmi, REG_HDMI_DDC_INT_CTRL2, reg_val);

	/* Reset DDC timers */
	reg_val = BIT(0) | hdmi_read(hdmi, REG_HDMI_SCRAMBLER_STATUS_DDC_CTRL);
	hdmi_write(hdmi, REG_HDMI_SCRAMBLER_STATUS_DDC_CTRL, reg_val);

	reg_val = hdmi_read(hdmi, REG_HDMI_SCRAMBLER_STATUS_DDC_CTRL);
	reg_val &= ~BIT(0);
	hdmi_write(hdmi, REG_HDMI_SCRAMBLER_STATUS_DDC_CTRL, reg_val);
}

static void msm_hdmi_scrambler_ddc_disable(struct hdmi *hdmi)
{
	u32 reg_val;

	msm_hdmi_scrambler_ddc_reset(hdmi);

	/* Disable HW DDC access to RxStatus register */
	reg_val = hdmi_read(hdmi, REG_HDMI_HW_DDC_CTRL);
	reg_val &= ~(BIT(8) | BIT(9));
	hdmi_write(hdmi, REG_HDMI_HW_DDC_CTRL, reg_val);
}

void msm_hdmi_ddc_scrambling_irq(struct hdmi *hdmi)
{
	bool scrambler_timer_off = false;
	u32 intr2, intr5;

	intr2 = hdmi_read(hdmi, REG_HDMI_DDC_INT_CTRL2);
	intr5 = hdmi_read(hdmi, REG_HDMI_DDC_INT_CTRL5);

	DRM_DEBUG("intr2: 0x%x, intr5: 0x%x\n", intr2, intr5);

	if (intr2 & BIT(12)) {
		DRM_ERROR("SCRAMBLER_STATUS_NOT\n");
		intr2 |= BIT(14);
		scrambler_timer_off = true;
	}

	if (intr2 & BIT(8)) {
		DRM_ERROR("SCRAMBLER_STATUS_DDC_FAILED\n");
		intr2 |= BIT(9);
		scrambler_timer_off = true;
	}

	hdmi_write(hdmi, REG_HDMI_DDC_INT_CTRL2, intr2);

	if (intr5 & BIT(8)) {
		DRM_ERROR("SCRAMBLER_STATUS_DDC_REQ_TIMEOUT\n");
		intr5 |= BIT(9);
		intr5 &= ~BIT(10);
		scrambler_timer_off = true;
	}

	hdmi_write(hdmi, REG_HDMI_DDC_INT_CTRL5, intr5);

	if (scrambler_timer_off)
		msm_hdmi_scrambler_ddc_disable(hdmi);
}

static int msm_hdmi_bridge_ddc_clear_irq(struct hdmi *hdmi, char *what)
{
	u32 ddc_int_ctrl, ddc_status, in_use, timeout;
	u32 sw_done_mask = BIT(2);
	u32 sw_done_ack  = BIT(1);
	u32 in_use_by_sw = BIT(0);
	u32 in_use_by_hw = BIT(1);

	/* clear and enable interrutps */
	ddc_int_ctrl = sw_done_mask | sw_done_ack;

	hdmi_write(hdmi, REG_HDMI_DDC_INT_CTRL, ddc_int_ctrl);

	/* wait until DDC HW is free */
	timeout = 100;
	do {
		ddc_status = hdmi_read(hdmi, REG_HDMI_DDC_HW_STATUS);
		in_use = ddc_status & (in_use_by_sw | in_use_by_hw);
		if (in_use) {
			DBG("ddc is in use by %s, timeout(%d)\n",
			ddc_status & in_use_by_sw ? "sw" : "hw",
			timeout);
			udelay(100);
		}
	} while (in_use && --timeout);

	if (!timeout) {
		DRM_ERROR("%s: timedout\n", what);
		return -ETIMEDOUT;
	}

	return 0;
}

static int msm_hdmi_bridge_scrambler_ddc_check_status(struct hdmi *hdmi)
{
	int rc = 0;
	u32 reg_val;

	/* check for errors and clear status */
	reg_val = hdmi_read(hdmi, REG_HDMI_SCRAMBLER_STATUS_DDC_STATUS);
	if (reg_val & BIT(4)) {
		DRM_ERROR("ddc aborted\n");
		reg_val |= BIT(5);
		rc = -ECONNABORTED;
	}

	if (reg_val & BIT(8)) {
		DRM_ERROR("timed out\n");
		reg_val |= BIT(9);
		rc = -ETIMEDOUT;
	}

	if (reg_val & BIT(12)) {
		DRM_ERROR("NACK0\n");
		reg_val |= BIT(13);
		rc = -EIO;
	}
	if (reg_val & BIT(14)) {
		DRM_ERROR("NACK1\n");
		reg_val |= BIT(15);
		rc = -EIO;
	}
	hdmi_write(hdmi, REG_HDMI_SCRAMBLER_STATUS_DDC_STATUS, reg_val);

	return rc;
}

static int msm_hdmi_bridge_scrambler_status_timer_setup(struct hdmi *hdmi,
							u32 timeout_hsync)
{
	u32 reg_val;
	int rc;

	msm_hdmi_bridge_ddc_clear_irq(hdmi, "scrambler");

	hdmi_write(hdmi, REG_HDMI_SCRAMBLER_STATUS_DDC_TIMER_CTRL,
			   timeout_hsync);
	hdmi_write(hdmi, REG_HDMI_SCRAMBLER_STATUS_DDC_TIMER_CTRL2,
			   timeout_hsync);
	reg_val = hdmi_read(hdmi, REG_HDMI_DDC_INT_CTRL5);
	reg_val |= BIT(10);
	hdmi_write(hdmi, REG_HDMI_DDC_INT_CTRL5, reg_val);

	reg_val = hdmi_read(hdmi, REG_HDMI_DDC_INT_CTRL2);
	/* Trigger interrupt if scrambler status is 0 or DDC failure */
	reg_val |= BIT(10);
	reg_val &= ~(BIT(15) | BIT(16));
	reg_val |= BIT(16);
	hdmi_write(hdmi, REG_HDMI_DDC_INT_CTRL2, reg_val);

	/* Enable DDC access */
	reg_val = hdmi_read(hdmi, REG_HDMI_HW_DDC_CTRL);

	reg_val &= ~(BIT(8) | BIT(9));
	reg_val |= BIT(8);
	hdmi_write(hdmi, REG_HDMI_HW_DDC_CTRL, reg_val);

	/* WAIT for 200ms as per HDMI 2.0 standard for sink to respond */
	msleep(200);

	/* clear the scrambler status */
	rc = msm_hdmi_bridge_scrambler_ddc_check_status(hdmi);
	if (rc)
		DRM_ERROR("scrambling ddc error %d\n", rc);

	msm_hdmi_scrambler_ddc_disable(hdmi);

	return rc;
}

static int msm_hdmi_bridge_setup_ddc_timers(struct hdmi *hdmi,
					    u32 type, u32 to_in_num_lines)
{
	if (type >= HDMI_TX_DDC_TIMER_MAX) {
		DRM_ERROR("Invalid timer type %d\n", type);
		return -EINVAL;
	}

	switch (type) {
	case HDMI_TX_DDC_TIMER_SCRAMBLER_STATUS:
		msm_hdmi_bridge_scrambler_status_timer_setup(hdmi,
							     to_in_num_lines);
		break;
	default:
		DRM_ERROR("%d type not supported\n", type);
		return -EINVAL;
	}

	return 0;
}

static int msm_hdmi_get_timeout_in_hysnc(const struct drm_display_mode *mode,
					 u32 timeout_ms)
{
	/*
	 * pixel clock  = h_total * v_total * fps
	 * 1 sec = pixel clock number of pixels are transmitted.
	 * time taken by one line (h_total) = 1s / (v_total * fps).
	 * lines for give time = (time_ms * 1000) / (1000000 / (v_total * fps))
	 *                     = (time_ms * clock) / h_total
	 */
	return (timeout_ms * mode->clock / mode->htotal);
}

static int msm_hdmi_bridge_setup_scrambler(struct hdmi *hdmi)
{
	struct drm_crtc *crtc = hdmi->encoder->crtc;
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct drm_scdc *scdc;
	bool scrambler_on;
	u32 tmds_clock_ratio;
	u32 reg_val;
	int timeout_hsync;
	int rc = 0;

	scdc = &hdmi->connector->display_info.hdmi.scdc;
	if (!scdc->supported)
		return 0;

	/* use actual clock instead of mode clock */
	tmds_clock_ratio =
		hdmi->pixclock > HDMI_TX_SCRAMBLER_THRESHOLD_RATE_KHZ;

	if (!drm_scdc_set_high_tmds_clock_ratio(hdmi->connector,
						tmds_clock_ratio)) {
		DRM_ERROR("TMDS CLK RATIO ERR\n");
		return -EIO;
	}

	/* Read HDMI version */
	reg_val = hdmi_read(hdmi, REG_HDMI_VERSION);
	reg_val = (reg_val & 0xF0000000) >> 28;

	/* Scrambling is supported from HDMI TX 4.0 */
	if (reg_val < HDMI_TX_SCRAMBLER_MIN_TX_VERSION) {
		DRM_INFO("scrambling not supported by tx\n");
		return 0;
	}

	scrambler_on = scdc->scrambling.supported &&
		(scdc->scrambling.low_rates || tmds_clock_ratio);

	DRM_INFO("scrambler %s\n", scrambler_on ? "on" : "off");

	if (scrambler_on) {
		reg_val = hdmi_read(hdmi, REG_HDMI_CTRL);
		reg_val |= HDMI_CTRL_SCRAMBLER_EN;
		hdmi_write(hdmi, REG_HDMI_CTRL, reg_val);

		if (!drm_scdc_set_scrambling(hdmi->connector, true)) {
			DRM_ERROR("failed to enable scrambling\n");
			return -EIO;
		}

		/*
		 * Setup hardware to periodically check for scrambler
		 * status bit on the sink. Sink should set this bit
		 * with in 200ms after scrambler is enabled.
		 */
		timeout_hsync = msm_hdmi_get_timeout_in_hysnc(mode,
						HDMI_TX_SCRAMBLER_TIMEOUT_MSEC);

		if (timeout_hsync <= 0) {
			DRM_ERROR("err in timeout hsync calc\n");
			timeout_hsync = HDMI_DEFAULT_TIMEOUT_HSYNC;
		}
		DBG("timeout for scrambling en: %d hsyncs\n",
				  timeout_hsync);

		rc = msm_hdmi_bridge_setup_ddc_timers(hdmi,
			HDMI_TX_DDC_TIMER_SCRAMBLER_STATUS, timeout_hsync);
	} else {
		drm_scdc_set_scrambling(hdmi->connector, false);
		reg_val = hdmi_read(hdmi, REG_HDMI_CTRL);
		reg_val &= ~HDMI_CTRL_SCRAMBLER_EN;
		hdmi_write(hdmi, REG_HDMI_CTRL, reg_val);
	}

	return rc;
}

#define AVI_IFRAME_LINE_NUMBER 1

static void msm_hdmi_config_avi_infoframe(struct hdmi *hdmi)
{
	struct drm_crtc *crtc = hdmi->encoder->crtc;
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	union hdmi_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)];
	u32 val;
	int len;

	drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						 hdmi->connector, mode);

	len = hdmi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (len < 0) {
		DRM_DEV_ERROR(&hdmi->pdev->dev,
			"failed to configure avi infoframe\n");
		return;
	}

	/*
	 * the AVI_INFOx registers don't map exactly to how the AVI infoframes
	 * are packed according to the spec. The checksum from the header is
	 * written to the LSB byte of AVI_INFO0 and the version is written to
	 * the third byte from the LSB of AVI_INFO3
	 */
	hdmi_write(hdmi, REG_HDMI_AVI_INFO(0),
		   buffer[3] |
		   buffer[4] << 8 |
		   buffer[5] << 16 |
		   buffer[6] << 24);

	hdmi_write(hdmi, REG_HDMI_AVI_INFO(1),
		   buffer[7] |
		   buffer[8] << 8 |
		   buffer[9] << 16 |
		   buffer[10] << 24);

	hdmi_write(hdmi, REG_HDMI_AVI_INFO(2),
		   buffer[11] |
		   buffer[12] << 8 |
		   buffer[13] << 16 |
		   buffer[14] << 24);

	hdmi_write(hdmi, REG_HDMI_AVI_INFO(3),
		   buffer[15] |
		   buffer[16] << 8 |
		   buffer[1] << 24);

	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL0,
		   HDMI_INFOFRAME_CTRL0_AVI_SEND |
		   HDMI_INFOFRAME_CTRL0_AVI_CONT);

	val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL1);
	val &= ~HDMI_INFOFRAME_CTRL1_AVI_INFO_LINE__MASK;
	val |= HDMI_INFOFRAME_CTRL1_AVI_INFO_LINE(AVI_IFRAME_LINE_NUMBER);
	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL1, val);
}

#define VENDOR_IFRAME_LINE_NUMBER 3

static void msm_hdmi_config_vs_infoframe(struct hdmi *hdmi)
{
	struct drm_crtc *crtc = hdmi->encoder->crtc;
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct hdmi_vendor_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_SIZE(VENDOR)];
	u32 val;
	int len;

	drm_hdmi_vendor_infoframe_from_display_mode(&frame,
						    hdmi->connector, mode);

	len = hdmi_vendor_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (len < 0) {
		DRM_DEV_ERROR(&hdmi->pdev->dev,
			      "failed to configure vendor infoframe\n");
		return;
	}

	val = (buffer[3] << 8) | buffer[7] | buffer[2];
	if (frame.s3d_struct != HDMI_3D_STRUCTURE_INVALID)
		val |= frame.s3d_struct << 24;
	else
		val |= frame.vic << 16;

	hdmi_write(hdmi, REG_HDMI_VENSPEC_INFO(0), val);
	hdmi_write(hdmi, REG_HDMI_VENSPEC_INFO(1), buffer[9] >> 4);
	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL0,
		   HDMI_INFOFRAME_CTRL0_VENSPEC_INFO_SEND |
		   HDMI_INFOFRAME_CTRL0_VENSPEC_INFO_CONT);

	val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL1);
	val &= ~HDMI_INFOFRAME_CTRL1_VENSPEC_INFO_LINE__MASK;
	val |= HDMI_INFOFRAME_CTRL1_VENSPEC_INFO_LINE(VENDOR_IFRAME_LINE_NUMBER);
	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL1, val);
}

static void msm_hdmi_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	struct hdmi_phy *phy = hdmi->phy;

	DBG("power up");

	if (!hdmi->power_on) {
		msm_hdmi_phy_resource_enable(phy);
		msm_hdmi_power_on(bridge);
		hdmi->power_on = true;
		if (hdmi->hdmi_mode) {
			msm_hdmi_config_avi_infoframe(hdmi);
			msm_hdmi_config_vs_infoframe(hdmi);
			msm_hdmi_audio_update(hdmi);
		}
	}

	msm_hdmi_phy_powerup(phy, hdmi->pixclock);
	msm_hdmi_set_mode(hdmi, true);
	msm_hdmi_bridge_setup_scrambler(hdmi);

	if (hdmi->hdcp_ctrl)
		msm_hdmi_hdcp_on(hdmi->hdcp_ctrl);
}

static void msm_hdmi_ctrl_reset(struct hdmi *hdmi)
{
	uint32_t reg_val;

	/* Assert HDMI CTRL SW reset */
	reg_val = hdmi_read(hdmi, REG_HDMI_CTRL_SW_RESET);
	reg_val |= BIT(0);
	hdmi_write(hdmi, REG_HDMI_CTRL_SW_RESET, reg_val);

	/* Disable the audio engine */
	reg_val = hdmi_read(hdmi, REG_HDMI_AUDIO_CFG);
	reg_val &= ~HDMI_AUDIO_CFG_ENGINE_ENABLE;
	hdmi_write(hdmi, REG_HDMI_AUDIO_CFG, reg_val);

	/* Clear audio sample send */
	reg_val = hdmi_read(hdmi, REG_HDMI_AUDIO_PKT_CTRL1);
	reg_val &= ~HDMI_AUDIO_PKT_CTRL1_AUDIO_SAMPLE_SEND;
	hdmi_write(hdmi, REG_HDMI_AUDIO_PKT_CTRL1, reg_val);

	/* Clear sending VBI ctrl packets */
	reg_val = hdmi_read(hdmi, REG_HDMI_VBI_PKT_CTRL);
	reg_val &= ~(HDMI_VBI_PKT_CTRL_GC_ENABLE |
		     HDMI_VBI_PKT_CTRL_ISRC_SEND |
		     HDMI_VBI_PKT_CTRL_ACP_SEND);
	hdmi_write(hdmi, REG_HDMI_VBI_PKT_CTRL, reg_val);

	/* Clear sending infoframe packets */
	reg_val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL0);
	reg_val &= ~(HDMI_INFOFRAME_CTRL0_AVI_SEND |
		     HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SEND |
		     HDMI_INFOFRAME_CTRL0_MPEG_INFO_SEND |
		     HDMI_INFOFRAME_CTRL0_VENSPEC_INFO_SEND |
		     BIT(15) | BIT(19));
	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL0, reg_val);

	/* Clear sending general ctrl packets */
	reg_val = hdmi_read(hdmi, REG_HDMI_GEN_PKT_CTRL);
	reg_val &= ~(HDMI_GEN_PKT_CTRL_GENERIC0_SEND |
		     HDMI_GEN_PKT_CTRL_GENERIC1_SEND);
	hdmi_write(hdmi, REG_HDMI_GEN_PKT_CTRL, reg_val);

	/* De-assert HDMI CTRL SW reset */
	reg_val = hdmi_read(hdmi, REG_HDMI_CTRL_SW_RESET);
	reg_val &= ~BIT(0);
	hdmi_write(hdmi, REG_HDMI_CTRL_SW_RESET, reg_val);
}

static void msm_hdmi_bridge_post_disable(struct drm_bridge *bridge)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	struct hdmi_phy *phy = hdmi->phy;

	if (hdmi->hdcp_ctrl)
		msm_hdmi_hdcp_off(hdmi->hdcp_ctrl);

	DBG("power down");
	msm_hdmi_set_mode(hdmi, false);

	msm_hdmi_phy_powerdown(phy);

	msm_hdmi_ctrl_reset(hdmi);

	if (hdmi->power_on) {
		power_off(bridge);
		hdmi->power_on = false;
		if (hdmi->hdmi_mode)
			msm_hdmi_audio_update(hdmi);
		msm_hdmi_phy_resource_disable(phy);
	}
}

static void msm_hdmi_bridge_mode_set(struct drm_bridge *bridge,
		 const struct drm_display_mode *mode,
		 const struct drm_display_mode *adjusted_mode)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	int hstart, hend, vstart, vend;
	uint32_t frame_ctrl;

	mode = adjusted_mode;

	hdmi->pixclock = mode->clock * 1000;

	hstart = mode->htotal - mode->hsync_start;
	hend   = mode->htotal - mode->hsync_start + mode->hdisplay;

	vstart = mode->vtotal - mode->vsync_start - 1;
	vend   = mode->vtotal - mode->vsync_start + mode->vdisplay - 1;

	DBG("htotal=%d, vtotal=%d, hstart=%d, hend=%d, vstart=%d, vend=%d",
			mode->htotal, mode->vtotal, hstart, hend, vstart, vend);

	hdmi_write(hdmi, REG_HDMI_TOTAL,
			HDMI_TOTAL_H_TOTAL(mode->htotal - 1) |
			HDMI_TOTAL_V_TOTAL(mode->vtotal - 1));

	hdmi_write(hdmi, REG_HDMI_ACTIVE_HSYNC,
			HDMI_ACTIVE_HSYNC_START(hstart) |
			HDMI_ACTIVE_HSYNC_END(hend));
	hdmi_write(hdmi, REG_HDMI_ACTIVE_VSYNC,
			HDMI_ACTIVE_VSYNC_START(vstart) |
			HDMI_ACTIVE_VSYNC_END(vend));

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		hdmi_write(hdmi, REG_HDMI_VSYNC_TOTAL_F2,
				HDMI_VSYNC_TOTAL_F2_V_TOTAL(mode->vtotal));
		hdmi_write(hdmi, REG_HDMI_VSYNC_ACTIVE_F2,
				HDMI_VSYNC_ACTIVE_F2_START(vstart + 1) |
				HDMI_VSYNC_ACTIVE_F2_END(vend + 1));
	} else {
		hdmi_write(hdmi, REG_HDMI_VSYNC_TOTAL_F2,
				HDMI_VSYNC_TOTAL_F2_V_TOTAL(0));
		hdmi_write(hdmi, REG_HDMI_VSYNC_ACTIVE_F2,
				HDMI_VSYNC_ACTIVE_F2_START(0) |
				HDMI_VSYNC_ACTIVE_F2_END(0));
	}

	frame_ctrl = 0;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		frame_ctrl |= HDMI_FRAME_CTRL_HSYNC_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		frame_ctrl |= HDMI_FRAME_CTRL_VSYNC_LOW;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		frame_ctrl |= HDMI_FRAME_CTRL_INTERLACED_EN;
	DBG("frame_ctrl=%08x", frame_ctrl);
	hdmi_write(hdmi, REG_HDMI_FRAME_CTRL, frame_ctrl);

	if (hdmi->hdmi_mode)
		msm_hdmi_audio_update(hdmi);
}

static struct edid *msm_hdmi_bridge_get_edid(struct drm_bridge *bridge,
		struct drm_connector *connector)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	struct edid *edid;
	uint32_t hdmi_ctrl;

	hdmi_ctrl = hdmi_read(hdmi, REG_HDMI_CTRL);
	hdmi_write(hdmi, REG_HDMI_CTRL, hdmi_ctrl | HDMI_CTRL_ENABLE);

	edid = drm_get_edid(connector, hdmi->i2c);
	cec_s_phys_addr_from_edid(hdmi->cec_adap, edid);

	hdmi_write(hdmi, REG_HDMI_CTRL, hdmi_ctrl);

	hdmi->hdmi_mode = drm_detect_hdmi_monitor(edid);

	return edid;
}

static enum drm_mode_status msm_hdmi_bridge_mode_valid(struct drm_bridge *bridge,
		const struct drm_display_info *info,
		const struct drm_display_mode *mode)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct msm_drm_private *priv = bridge->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	long actual, requested;

	requested = 1000 * mode->clock;
	if (!info->hdmi.scdc.supported && requested > 340000000)
		return MODE_CLOCK_RANGE;
	if (mode->hdisplay > 2560)
		return MODE_CLOCK_RANGE;
	if (drm_mode_vrefresh(mode) > 60)
		return MODE_CLOCK_RANGE;

	/* for mdp5/apq8074, we manage our own pixel clk (as opposed to
	 * mdp4/dtv stuff where pixel clk is assigned to mdp/encoder
	 * instead):
	 */
	if (kms->funcs->round_pixclk)
		actual = kms->funcs->round_pixclk(kms,
			requested, hdmi_bridge->hdmi->encoder);
	else if (config->pwr_clk_cnt > 0)
		actual = clk_round_rate(hdmi->pwr_clks[0], requested);
	else
		actual = requested;

	DBG("requested=%ld, actual=%ld", requested, actual);

	if (actual != requested)
		return MODE_CLOCK_RANGE;

	return 0;
}

static const struct drm_bridge_funcs msm_hdmi_bridge_funcs = {
		.pre_enable = msm_hdmi_bridge_pre_enable,
		.post_disable = msm_hdmi_bridge_post_disable,
		.mode_set = msm_hdmi_bridge_mode_set,
		.mode_valid = msm_hdmi_bridge_mode_valid,
		.get_edid = msm_hdmi_bridge_get_edid,
		.detect = msm_hdmi_bridge_detect,
};

static void
msm_hdmi_hotplug_work(struct work_struct *work)
{
	struct hdmi_bridge *hdmi_bridge =
		container_of(work, struct hdmi_bridge, hpd_work);
	struct drm_bridge *bridge = &hdmi_bridge->base;

	drm_bridge_hpd_notify(bridge, drm_bridge_detect(bridge));
}

/* initialize bridge */
int msm_hdmi_bridge_init(struct hdmi *hdmi)
{
	struct drm_bridge *bridge = NULL;
	struct hdmi_bridge *hdmi_bridge;
	int ret;

	hdmi_bridge = devm_kzalloc(hdmi->dev->dev,
			sizeof(*hdmi_bridge), GFP_KERNEL);
	if (!hdmi_bridge)
		return -ENOMEM;

	hdmi_bridge->hdmi = hdmi;
	INIT_WORK(&hdmi_bridge->hpd_work, msm_hdmi_hotplug_work);

	bridge = &hdmi_bridge->base;
	bridge->funcs = &msm_hdmi_bridge_funcs;
	bridge->ddc = hdmi->i2c;
	bridge->type = DRM_MODE_CONNECTOR_HDMIA;
	bridge->ops = DRM_BRIDGE_OP_HPD |
		DRM_BRIDGE_OP_DETECT |
		DRM_BRIDGE_OP_EDID;

	ret = devm_drm_bridge_add(hdmi->dev->dev, bridge);
	if (ret)
		return ret;

	ret = drm_bridge_attach(hdmi->encoder, bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		return ret;

	hdmi->bridge = bridge;

	return 0;
}
