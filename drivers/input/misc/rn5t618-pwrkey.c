#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/mfd/rn5t618.h>
#include <linux/regmap.h>

struct rn5t618_pwrkey_info {
    struct rn5t618 *rn5t618;
    struct input_dev *input;
    int irq;
};

static irqreturn_t powerbutton_irq(int irq, void *data)
{
    struct rn5t618_pwrkey_info *info = (struct rn5t618_pwrkey_info*) data;

    unsigned int irq_reg;
    unsigned int val;

    regmap_read(info->rn5t618->regmap, RN5T618_PWRIRQ, &irq_reg);

    if (! (irq_reg & 1)) return IRQ_NONE;

    regmap_read(info->rn5t618->regmap, RN5T618_PWRMON, &val);

    regmap_update_bits(info->rn5t618->regmap, RN5T618_PWRIRQ,
                  BIT(0), 0);

    input_report_key(info->input, KEY_POWER, val & 1);
    input_sync(info->input);

	return IRQ_HANDLED;
}

static int rn5t618_pwrkey_probe(struct platform_device *pdev)
{
    struct rn5t618_pwrkey_info *info;
	int err;

    info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
    if (!info)
        return -ENOMEM;

    info->rn5t618 = dev_get_drvdata(pdev->dev.parent);
    info->irq = -1;

	info->input = devm_input_allocate_device(&pdev->dev);
	if (!info->input) {
		dev_err(&pdev->dev, "Can't allocate power button\n");
		return -ENOMEM;
	}

	input_set_capability(info->input, EV_KEY, KEY_POWER);
	info->input->name = "rn5t618_powerkey";
	info->input->phys = "rn5t618_powerkey/input0";

    if (!info->rn5t618->irq_data)
        return -EINVAL;

    info->irq = regmap_irq_get_virq(info->rn5t618->irq_data, RN5T618_IRQ_SYS);
    if (info->irq < 0) {
        dev_err(&pdev->dev, "Failed finding rn5t618 IRQ\n");
        return -EINVAL; 
    }

	err = devm_request_threaded_irq(&pdev->dev, info->irq, NULL, powerbutton_irq,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
			IRQF_ONESHOT,
			"rn5t618_pwrkey", info);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't get IRQ for pwrkey: %d\n", err);
		return err;
	}

	err = input_register_device(info->input);
	if (err) {
		dev_err(&pdev->dev, "Can't register power button: %d\n", err);
		return err;
	}

    // Trigger IRQ on falling and rising edge of power button
    regmap_set_bits(info->rn5t618->regmap, RN5T618_PWRIRSEL, 1);
    // Enable IRQ
    regmap_set_bits(info->rn5t618->regmap, RN5T618_PWRIREN, 1);

	return 0;
}

static struct platform_driver rn5t618_pwrkey_driver = {
	.probe		= rn5t618_pwrkey_probe,
	.driver		= {
		.name	= "rn5t618-powerkey",
	},
};
module_platform_driver(rn5t618_pwrkey_driver);

MODULE_ALIAS("platform:rn5t618-powerkey");
MODULE_DESCRIPTION("RN5T619 Power Button");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pierre-Hugues Husson <phh@phh.me>");

