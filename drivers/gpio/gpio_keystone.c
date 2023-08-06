/*
 * Copyright (c) 2023 Daniel Schultz <d.schultz@phytec.de>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_keystone_gpio

/**
 * @file GPIO driver for the TI Keystone MPUs
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>

#define MAX_GPIO_PER_BANK 32

typedef void (*keystone_cfg_func_t)(void);

struct gpio_keystone_t {
	uint32_t dir;
	uint32_t out_data;
	uint32_t set_data;
	uint32_t clr_data;
	uint32_t in_data;
	uint32_t set_rising;
	uint32_t clr_rising;
	uint32_t set_falling;
	uint32_t clr_falling;
	uint32_t intstat;
};

struct gpio_keystone_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	uintptr_t                 gpio_base_addr;
	uint32_t 		  ngpios;
	uint32_t                  gpio_irq_base;
	keystone_cfg_func_t         gpio_cfg_func;
};

struct gpio_keystone_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	/* list of callbacks */
	sys_slist_t cb;
};

/* Helper Macros for GPIO */
#define DEV_GPIO_CFG(dev)						\
	((const struct gpio_keystone_config * const)(dev)->config)
#define DEV_GPIO(dev)							\
	((volatile struct gpio_keystone_t *)(DEV_GPIO_CFG(dev))->gpio_base_addr)
#define DEV_GPIO_DATA(dev)				\
	((struct gpio_keystone_data *)(dev)->data)


static void gpio_keystone_irq_handler(const struct device *dev)
{
	struct gpio_keystone_data *data = DEV_GPIO_DATA(dev);
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);
	const struct gpio_keystone_config *cfg = DEV_GPIO_CFG(dev);

	uint32_t pins = gpio->intstat;

	for (int i = 0; i < cfg->ngpios; i++) {
		if ((pins >> i) & 0x1) {
			gpio->intstat &= ~BIT(i);
			pins &= ~BIT(i);
			gpio_fire_callbacks(&data->cb, dev, BIT(i));
		}
		if (!pins) {
			return;
		}
	}
}

/* Given gpio_irq_base and the pin number, return the IRQ number for the pin */
static inline unsigned int gpio_keystone_pin_irq(unsigned int base_irq, int pin)
{
	return base_irq + pin;
}

/**
 * @brief Configure pin
 *
 * @param dev Device structure
 * @param pin The pin number
 * @param flags Flags of pin or port
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_keystone_config(const struct device *dev,
			        gpio_pin_t pin,
			        gpio_flags_t flags)
{
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);
	const struct gpio_keystone_config *cfg = DEV_GPIO_CFG(dev);

	if (pin >= cfg->ngpios) {
		return -EINVAL;
	}

	/* No open-drain support */
	if ((flags & GPIO_SINGLE_ENDED) != 0) {
		return -ENOTSUP;
	}

	/* We only support pull-up/pull-down */
	if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0) {
		return -ENOTSUP;
	}

	/* Set the initial output value before enabling output to avoid
	 * glitches
	 */
	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
		gpio->set_data |= BIT(pin);
	}
	if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
		gpio->set_data &= ~BIT(pin);
	}

	if ((flags & GPIO_OUTPUT) != 0) {
		gpio->dir &= ~BIT(pin);
	} else {
		gpio->dir |= BIT(pin);
	}

	return 0;
}

static int gpio_keystone_port_get_raw(const struct device *dev,
				    gpio_port_value_t *value)
{
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);

	*value = gpio->in_data;

	return 0;
}

static int gpio_keystone_port_set_masked_raw(const struct device *dev,
					   gpio_port_pins_t mask,
					   gpio_port_value_t value)
{
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);

	gpio->set_data = (gpio->out_data & ~mask) | (value & mask);

	return 0;
}

static int gpio_keystone_port_set_bits_raw(const struct device *dev,
					 gpio_port_pins_t mask)
{
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);

	gpio->set_data |= mask;

	return 0;
}

static int gpio_keystone_port_clear_bits_raw(const struct device *dev,
					   gpio_port_pins_t mask)
{
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);

	gpio->clr_data &= ~mask;

	return 0;
}

static int gpio_keystone_port_toggle_bits(const struct device *dev,
					gpio_port_pins_t mask)
{
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);

	gpio->set_data ^= mask;

	return 0;
}

static int gpio_keystone_pin_interrupt_configure(const struct device *dev,
					       gpio_pin_t pin,
					       enum gpio_int_mode mode,
					       enum gpio_int_trig trig)
{
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);
	const struct gpio_keystone_config *cfg = DEV_GPIO_CFG(dev);

	switch (mode) {
	case GPIO_INT_MODE_DISABLED:
		irq_disable(gpio_keystone_pin_irq(cfg->gpio_irq_base, pin));
		break;
	case GPIO_INT_MODE_LEVEL:
		__ASSERT(false, "Invalid MODE %d passed to driver", mode);
		return -ENOTSUP;
	case GPIO_INT_MODE_EDGE:
		if ((trig & GPIO_INT_HIGH_1) != 0) {
			gpio->set_rising |= BIT(pin);
		} else {
			gpio->clr_rising |= BIT(pin);
		}
		if ((trig & GPIO_INT_LOW_0) != 0) {
			gpio->set_falling |= BIT(pin);
		} else {
			gpio->clr_falling |= BIT(pin);
		}
		irq_enable(gpio_keystone_pin_irq(cfg->gpio_irq_base, pin));
		break;
	default:
		__ASSERT(false, "Invalid MODE %d passed to driver", mode);
		return -ENOTSUP;
	}

	return 0;
}

static int gpio_keystone_manage_callback(const struct device *dev,
				       struct gpio_callback *callback,
				       bool set)
{
	struct gpio_keystone_data *data = DEV_GPIO_DATA(dev);

	return gpio_manage_callback(&data->cb, callback, set);
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int gpio_keystone_port_get_dir(const struct device *dev, gpio_port_pins_t map,
				    gpio_port_pins_t *inputs, gpio_port_pins_t *outputs)
{
	const struct gpio_keystone_config *cfg = DEV_GPIO_CFG(dev);
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);

	map &= cfg->common.port_pin_mask;

	if (inputs != NULL) {
		*inputs = map & gpio->in_data;
	}

	if (outputs != NULL) {
		*outputs = map & gpio->out_data;
	}

	return 0;
}
#endif /* CONFIG_GPIO_GET_DIRECTION */


static const struct gpio_driver_api gpio_keystone_driver = {
	.pin_configure           = gpio_keystone_config,
	.port_get_raw            = gpio_keystone_port_get_raw,
	.port_set_masked_raw     = gpio_keystone_port_set_masked_raw,
	.port_set_bits_raw       = gpio_keystone_port_set_bits_raw,
	.port_clear_bits_raw     = gpio_keystone_port_clear_bits_raw,
	.port_toggle_bits        = gpio_keystone_port_toggle_bits,
	.pin_interrupt_configure = gpio_keystone_pin_interrupt_configure,
	.manage_callback         = gpio_keystone_manage_callback,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction      = gpio_keystone_port_get_dir,
#endif /* CONFIG_GPIO_GET_DIRECTION */
};

/**
 * @brief Initialize a GPIO controller
 *
 * Perform basic initialization of a GPIO controller
 *
 * @param dev GPIO device struct
 *
 * @return 0
 */
static int gpio_keystone_init(const struct device *dev)
{
	volatile struct gpio_keystone_t *gpio = DEV_GPIO(dev);
	const struct gpio_keystone_config *cfg = DEV_GPIO_CFG(dev);

	if (cfg->ngpios >= MAX_GPIO_PER_BANK) {
		return -EINVAL;
	}

	/* Set default values */
	gpio->clr_data = ~0 & cfg->common.port_pin_mask;
	gpio->dir &= ~cfg->common.port_pin_mask;

	/* Setup IRQ handler for each gpio pin */
	cfg->gpio_cfg_func();

	return 0;
}

static void gpio_keystone_cfg_0(void);

static const struct gpio_keystone_config gpio_keystone_config0 = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(0),
	},
	.gpio_base_addr = DT_INST_REG_ADDR(0),
	.ngpios = DT_INST_PROP(0, ngpios),
	.gpio_irq_base  = DT_INST_IRQN(0),
	.gpio_cfg_func  = gpio_keystone_cfg_0,
};

static struct gpio_keystone_data gpio_keystone_data0;

DEVICE_DT_INST_DEFINE(0,
		    gpio_keystone_init,
		    NULL,
		    &gpio_keystone_data0, &gpio_keystone_config0,
		    PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,
		    &gpio_keystone_driver);

#define		IRQ_INIT(n)					\
IRQ_CONNECT(DT_INST_IRQ_BY_IDX(0, n, irq),			\
		DT_INST_IRQ_BY_IDX(0, n, priority),		\
		gpio_keystone_irq_handler,			\
		DEVICE_DT_INST_GET(0),				\
		0);

static void gpio_keystone_cfg_0(void)
{
#if DT_INST_IRQ_HAS_IDX(0, 0)
	IRQ_INIT(0);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 1)
	IRQ_INIT(1);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 2)
	IRQ_INIT(2);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 3)
	IRQ_INIT(3);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 4)
	IRQ_INIT(4);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 5)
	IRQ_INIT(5);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 6)
	IRQ_INIT(6);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 7)
	IRQ_INIT(7);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 8)
	IRQ_INIT(8);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 9)
	IRQ_INIT(9);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 10)
	IRQ_INIT(10);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 11)
	IRQ_INIT(11);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 12)
	IRQ_INIT(12);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 13)
	IRQ_INIT(13);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 14)
	IRQ_INIT(14);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 15)
	IRQ_INIT(15);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 16)
	IRQ_INIT(16);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 17)
	IRQ_INIT(17);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 18)
	IRQ_INIT(18);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 19)
	IRQ_INIT(19);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 20)
	IRQ_INIT(20);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 21)
	IRQ_INIT(21);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 22)
	IRQ_INIT(22);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 23)
	IRQ_INIT(23);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 24)
	IRQ_INIT(24);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 25)
	IRQ_INIT(25);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 26)
	IRQ_INIT(26);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 27)
	IRQ_INIT(27);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 28)
	IRQ_INIT(28);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 29)
	IRQ_INIT(29);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 30)
	IRQ_INIT(30);
#endif
#if DT_INST_IRQ_HAS_IDX(0, 31)
	IRQ_INIT(31);
#endif
}
