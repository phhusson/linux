#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/irq.h>

struct aw9523b_gpio_priv {
	struct gpio_chip gpio_chip;
	struct irq_chip		irq_chip;
	struct regmap *regmap;
	struct mutex		lock;
    struct i2c_client *client;
	unsigned		irq_enabled;
    unsigned status;
};

#define REG_INPUT_PORT0     0x00
#define REG_INPUT_PORT1     0x01
#define REG_OUTPUT_PORT0    0x02
#define REG_OUTPUT_PORT1    0x03
#define REG_CONFIG_PORT0    0x04
#define REG_CONFIG_PORT1    0x05
#define REG_INT_PORT0       0x06
#define REG_INT_PORT1       0x07
#define REG_ID              0x10 
#define REG_CTL             0x11 

static int aw9523b_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int gpio)
{
	struct aw9523b_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int addr = REG_CONFIG_PORT0;
    unsigned reg;

    int offset = gpio;
    if(gpio >= 8) {
        addr = REG_CONFIG_PORT1;
        offset -= 8;
    }

	regmap_read(priv->regmap, addr, &reg);
    if( reg & (1 << offset) )
        return GPIO_LINE_DIRECTION_IN;
    return GPIO_LINE_DIRECTION_OUT;
}

static int aw9523b_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int gpio)
{
	struct aw9523b_gpio_priv *priv = gpiochip_get_data(chip);

	unsigned int addr = REG_CONFIG_PORT0;

    int offset = gpio;
    if(gpio >= 8) {
        addr = REG_CONFIG_PORT1;
        offset -= 8;
    }
    // 0-bit mean output, 1-bit mean input
    regmap_update_bits(priv->regmap, addr, 1<<offset, 1<<offset);
    return 0;
}

static int aw9523b_gpio_direction_output(struct gpio_chip *chip,
					unsigned int gpio, int value)
{
	struct aw9523b_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int addr = REG_CONFIG_PORT0;

    int offset = gpio;
    if(gpio >= 8) {
        addr = REG_CONFIG_PORT1;
        offset -= 8;
    }

    regmap_update_bits(priv->regmap, addr, 1<<offset, 0);
    //aw9523b_gpio_set(chip, gpio, value);
    return 0;
}

static int aw9523b_gpio_get(struct gpio_chip *chip, unsigned int gpio)
{
	struct aw9523b_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int addr = REG_INPUT_PORT0;
    unsigned reg;

    int offset = gpio;
    if(gpio >= 8) {
        addr = REG_INPUT_PORT1;
        offset -= 8;
    }
	regmap_read(priv->regmap, addr, &reg);

	return !!(reg & (1 << offset));
}

static void aw9523b_gpio_set(struct gpio_chip *chip, unsigned int gpio,
			    int value)
{
	struct aw9523b_gpio_priv *priv = gpiochip_get_data(chip);
	unsigned int addr = REG_OUTPUT_PORT0;

    int offset = gpio;
    if(gpio >= 8) {
        addr = REG_OUTPUT_PORT1;
        offset -= 8;
    }
    regmap_update_bits(priv->regmap, addr, 1<<offset, value ? (1<<offset) : 0);
}

static void aw9523b_irq_enable(struct irq_data *data)
{
	struct aw9523b_gpio_priv *priv = irq_data_get_irq_chip_data(data);
	unsigned int addr = REG_INT_PORT0;

    int offset = data->hwirq;
    if(offset >= 8) {
        addr = REG_INT_PORT1;
        offset -= 8;
    }
    regmap_update_bits(priv->regmap, addr, 1<<offset, 0);
	priv->irq_enabled |= (1 << data->hwirq);
}

static void aw9523b_irq_disable(struct irq_data *data)
{
	struct aw9523b_gpio_priv *priv = irq_data_get_irq_chip_data(data);
	unsigned int addr = REG_INT_PORT0;

    int offset = data->hwirq;
    if(offset >= 8) {
        addr = REG_INT_PORT1;
        offset -= 8;
    }
    regmap_update_bits(priv->regmap, addr, 1<<offset, 1<<offset);
	priv->irq_enabled &= ~(1 << data->hwirq);
}

static irqreturn_t aw9523b_irq(int irq, void *data)
{
	struct aw9523b_gpio_priv  *priv = data;
	unsigned long change, i, status;

    unsigned port0, port1;
	regmap_read(priv->regmap, REG_INPUT_PORT0, &port0);
	regmap_read(priv->regmap, REG_INPUT_PORT0, &port1);

    dev_err(&priv->client->dev, "Received irq for aw9523b\n");

	status = (port1 << 8) | port0;

	mutex_lock(&priv->lock);
	change = (priv->status ^ status) & priv->irq_enabled;
	priv->status = status;
	mutex_unlock(&priv->lock);

	for_each_set_bit(i, &change, priv->gpio_chip.ngpio) {
        dev_err(&priv->client->dev, "\tReceived irq for %lu\n", i);
		handle_nested_irq(irq_find_mapping(priv->gpio_chip.irq.domain, i));
    }

	return IRQ_HANDLED;
}

static const struct regmap_config aw9523b_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const struct gpio_chip aw9523b_template_chip = {
	.label			= "aw9523b-gpio",
	.owner			= THIS_MODULE,
	.get_direction		= aw9523b_gpio_get_direction,
	.direction_input	= aw9523b_gpio_direction_input,
	.direction_output	= aw9523b_gpio_direction_output,
	.get			= aw9523b_gpio_get,
	.set			= aw9523b_gpio_set,
	.base			= -1,
    .ngpio          = 16,
	.can_sleep		= true,
};


static void noop(struct irq_data *data) { }
static int aw9523b_gpio_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct aw9523b_gpio_priv *priv;
    struct gpio_desc *gpiod;
	int ret;
    unsigned reg;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
    priv->client = client;
	priv->gpio_chip = aw9523b_template_chip;
	priv->gpio_chip.parent = &client->dev;

	priv->regmap = devm_regmap_init_i2c(client, &aw9523b_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	ret = devm_gpiochip_add_data(&client->dev, &priv->gpio_chip, priv);
	if (ret < 0) {
		dev_err(&client->dev, "Unable to register gpiochip\n");
		return ret;
	}

    gpiod = devm_gpiod_get_optional(&client->dev, "reset", GPIOD_IN);
    if (IS_ERR(gpiod)) {
        ret = PTR_ERR(gpiod);
        if (ret != -EPROBE_DEFER)
            dev_dbg(&client->dev, "Failed to get reset GPIO: %d\n", ret);
        return ret;
    }

    gpiod_direction_output(gpiod, 1);
    msleep(10);
    gpiod_direction_output(gpiod, 0);

	regmap_read(priv->regmap, REG_ID, &reg);

	i2c_set_clientdata(client, priv);

	if (client->irq && true) {
		priv->irq_chip.name = "aw9523b",
		priv->irq_chip.irq_enable = aw9523b_irq_enable,
		priv->irq_chip.irq_disable = aw9523b_irq_disable,
		priv->irq_chip.irq_ack = noop,
		priv->irq_chip.irq_mask = noop,
		priv->irq_chip.irq_unmask = noop,
		//priv->irq_chip.irq_set_wake = pcf857x_irq_set_wake,
		ret = gpiochip_irqchip_add_nested(&priv->gpio_chip,
						     &priv->irq_chip,
						     0, handle_level_irq,
						     IRQ_TYPE_NONE);
		if (ret) {
			dev_err(&client->dev, "cannot add irqchip\n");
            return ret;
		}

		ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, aw9523b_irq, IRQF_ONESHOT |
					IRQF_TRIGGER_FALLING | IRQF_SHARED,
					dev_name(&client->dev), priv);
		if (ret)
			return ret;

		gpiochip_set_nested_irqchip(&priv->gpio_chip, &priv->irq_chip,
					    client->irq);
	}
    dev_err(&client->dev, "Read awinic id %x\n", reg);

	return 0;
}

static const struct of_device_id aw9523b_gpio_of_match_table[] = {
	{ .compatible = "awinic,aw9523b", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, aw9523b_gpio_of_match_table);

static struct i2c_driver aw9523b_gpio_driver = {
	.driver = {
		.name = "aw9523b-gpio",
		.of_match_table = aw9523b_gpio_of_match_table,
	},
	.probe = aw9523b_gpio_probe,
};
module_i2c_driver(aw9523b_gpio_driver);

MODULE_AUTHOR("Pierre-Hugues Husson <phh@phh.me>");
MODULE_DESCRIPTION("GPIO interface for Awinic AW9523b");
MODULE_LICENSE("GPL");
