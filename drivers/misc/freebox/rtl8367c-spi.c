/*
 * Copyright (C) 2017 Freebox SAS, Arnaud Vrac <avrac@freebox.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/spi/spi.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/delay.h>

#define RTL8367C_MAX_PORTS			4

#define RTL8367C_CHIP_NUMBER_REG		0x1300
#define RTL8367C_CHIP_VER_REG			0x1301

#define RTL8367C_PHY_AD_REG			0x130f
#define   RTL8367C_PHY_AD_PWRDN_SHIFT		5
#define   RTL8367C_PHY_AD_PWRDN_MASK		0x20

#define RTL8367C_MAGIC_ID_REG			0x13c2
#define   RTL8367C_MAGIC_ID_VAL			0x0249

#define RTL8367C_IA_CTRL_REG			0x1f00
#define   RTL8367C_IA_CTRL_RW_SHIFT		1
#define   RTL8367C_IA_CTRL_RW_MASK		0x2
#define   RTL8367C_IA_CTRL_RW_READ		(0 << RTL8367C_IA_CTRL_RW_SHIFT)
#define   RTL8367C_IA_CTRL_RW_WRITE		(1 << RTL8367C_IA_CTRL_RW_SHIFT)
#define   RTL8367C_IA_CTRL_CMD_SHIFT		0
#define   RTL8367C_IA_CTRL_CMD_MASK		0x1

#define RTL8367C_IA_STATUS_REG			0x1f01
#define   RTL8367C_IA_STATUS_SHIFT		2
#define   RTL8367C_IA_STATUS_MASK		0x7
#define   RTL8367C_IA_STATUS_PHY_BUSY		BIT(2)
#define   RTL8367C_IA_STATUS_SDS_BUSY		BIT(1)
#define   RTL8367C_IA_STATUS_MDX_BUSY		BIT(0)

#define RTL8367C_IA_ADDRESS_REG			0x1f02
#define RTL8367C_IA_WRITE_DATA_REG		0x1f03
#define RTL8367C_IA_READ_DATA_REG		0x1f04

#define RTL8367C_PHY_BASE			0x2000
#define RTL8367C_PHY_SHIFT			5
#define RTL8367C_INTERNAL_PHY_REG(_addr, _reg) \
	(RTL8367C_PHY_BASE + ((_addr) << RTL8367C_PHY_SHIFT) + (_reg))

#define RTL8367C_PHY_REG_MAX			31
#define RTL8367C_PHY_ADDR_MAX			7

struct rtl8367c {
	struct gpio_desc *reset_gpio;
	struct spi_device *spi;
	struct device *dev;
};

static inline int
rtl8367c_read_reg(struct rtl8367c *chip, u16 reg)
{
	u8 buf[] = { 0x03, reg >> 8, reg & 0xff };
	__be16 value;
	int ret;

	ret = spi_write_then_read(chip->spi, buf, sizeof (buf), &value, 2);
	if (ret < 0)
		return ret;

	return be16_to_cpu(value);
}

static inline int
rtl8367c_write_reg(struct rtl8367c *chip, u16 reg, u16 value)
{
	u8 buf[] = { 0x02, reg >> 8, reg & 0xff, value >> 8, value & 0xff };

	return spi_write(chip->spi, buf, sizeof (buf));
}

static inline int
rtl8367c_write_reg_mask(struct rtl8367c *chip, u16 reg,
			u16 value, u16 mask)
{
	int status;

	status = rtl8367c_read_reg(chip, reg);
	if (status < 0)
		return status;

	status &= ~mask;
	value &= mask;

	return rtl8367c_write_reg(chip, reg, status | value);
}

static int
rtl8367c_wait_phy(struct rtl8367c *chip)
{
	int ret;
	int timeout = 5;

	while (1) {
		ret = rtl8367c_read_reg(chip, RTL8367C_IA_STATUS_REG);
		if (ret < 0)
			return ret;

		if ((ret & RTL8367C_IA_STATUS_PHY_BUSY) == 0)
			break;

		if (timeout--)
			return -ETIMEDOUT;

		udelay(1);
	}

	return 0;
}

static int
rtl8367c_write_phy_reg(struct rtl8367c *chip, u8 phy_addr,
		       u8 phy_reg, u16 value)
{
	int ret;

	if (phy_addr > RTL8367C_PHY_ADDR_MAX)
		return -EINVAL;

	if (phy_reg > RTL8367C_PHY_REG_MAX)
		return -EINVAL;

	ret = rtl8367c_read_reg(chip, RTL8367C_IA_STATUS_REG);
	if (ret < 0)
		return ret;

	if (ret & RTL8367C_IA_STATUS_PHY_BUSY)
		return -ETIMEDOUT;

	ret = rtl8367c_write_reg(chip, RTL8367C_IA_WRITE_DATA_REG, value);
	if (ret < 0)
		return ret;

	ret = rtl8367c_write_reg(chip, RTL8367C_IA_ADDRESS_REG,
				 RTL8367C_INTERNAL_PHY_REG(phy_addr, phy_reg));
	if (ret < 0)
		return ret;

	ret = rtl8367c_write_reg(chip, RTL8367C_IA_CTRL_REG,
				 RTL8367C_IA_CTRL_CMD_MASK |
				 RTL8367C_IA_CTRL_RW_WRITE);
	if (ret < 0)
		return ret;

	ret = rtl8367c_wait_phy(chip);
	if (ret < 0)
		return ret;

	return 0;
}

static int
rtl8367c_enable_phy(struct rtl8367c *chip, int enable)
{
	int phy_num, ret;

	ret = rtl8367c_write_reg_mask(chip, RTL8367C_PHY_AD_REG,
				      enable << RTL8367C_PHY_AD_PWRDN_SHIFT,
				      RTL8367C_PHY_AD_PWRDN_MASK);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to enable embedded PHYs\n");
		return ret;
	}

	for (phy_num = 0; phy_num < RTL8367C_MAX_PORTS; phy_num++) {
		ret = rtl8367c_write_phy_reg(chip, phy_num, 0, 0x1140);
		if (ret < 0) {
			dev_warn(chip->dev, "Failed to power up PHY %d\n",
				 phy_num);
		}
	}

	return 0;
}

static void
rtl8367c_handle_reset(struct rtl8367c *chip, struct device_node *np)
{
	chip->reset_gpio = devm_gpiod_get_optional(chip->dev, "reset",
						   GPIOD_OUT_HIGH);
	if (!chip->reset_gpio) {
		dev_dbg(chip->dev, "No reset GPIO defined\n");
		return;
	}

	gpiod_set_value_cansleep(chip->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(chip->reset_gpio, 0);
	msleep(80);
}

static int
rtl8367c_probe(struct rtl8367c *chip, struct device_node *np)
{
	u16 chip_num;
	u16 chip_ver;
	int ret;

	rtl8367c_handle_reset(chip, np);

	ret = rtl8367c_write_reg(chip, RTL8367C_MAGIC_ID_REG,
				 RTL8367C_MAGIC_ID_VAL);
	if (ret < 0)
		return ret;

	ret = rtl8367c_read_reg(chip, RTL8367C_CHIP_NUMBER_REG);
	if (ret < 0) {
		dev_err(chip->dev, "failed to read chip number: %d\n", ret);
		return ret;
	}

	chip_num = ret;

	ret = rtl8367c_read_reg(chip, RTL8367C_CHIP_VER_REG);
	if (ret < 0) {
		dev_err(chip->dev, "failed to read chip version: %d\n", ret);
		return ret;
	}

	chip_ver = ret;

	if (chip_num != 0x6367) {
		dev_err(chip->dev, "unsupported chip id %04x\n", chip_num);
		return -ENXIO;
	}

	dev_info(chip->dev, "RTL8367C driver loaded, chip id=%04x ver=%04x\n",
		 chip_num, chip_ver);

	return rtl8367c_enable_phy(chip, 1);
}

static void
rtl8367c_remove(struct rtl8367c *chip)
{
	if (chip->reset_gpio) {
		gpiod_set_value_cansleep(chip->reset_gpio, 1);
		gpiod_unexport(chip->reset_gpio);
	}
}

static int
rtl8367c_spi_probe(struct spi_device *spi)
{
	struct rtl8367c *chip;

	chip = devm_kzalloc(&spi->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	spi_set_drvdata(spi, chip);
	chip->spi = spi;
	chip->dev = &spi->dev;

	return rtl8367c_probe(chip, spi->dev.of_node);
}

static void
rtl8367c_spi_remove(struct spi_device *spi)
{
	struct rtl8367c *chip;

	chip = spi_get_drvdata(spi);
	if (chip)
		rtl8367c_remove(chip);
}

static const struct spi_device_id rtl8367c_spi_id[] = {
	{ "rtl8367c-spi", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, rtl8367c_spi_id);

#ifdef CONFIG_OF
static const struct of_device_id rtl8367c_spi_of_match[] = {
	{ .compatible = "realtek,rtl8367c-spi" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rtl8367c_spi_of_match);
#endif

static struct spi_driver rtl8367c_spi_driver = {
	.driver = {
		.name = "rtl8367c_spi",
		.of_match_table = of_match_ptr(rtl8367c_spi_of_match),
	},
	.probe = rtl8367c_spi_probe,
	.remove = rtl8367c_spi_remove,
	.id_table = rtl8367c_spi_id,
};

module_spi_driver(rtl8367c_spi_driver);

MODULE_AUTHOR("Arnaud Vrac <avrac@freebox.fr>");
MODULE_DESCRIPTION("Driver for Realtek RTL8367C Switch in SPI managed mode");
MODULE_LICENSE("GPL v2");
