/*
 * Copyright (c) 2019 Phytec Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file GPIO driver for the VexRiscv
 */

#include <errno.h>
#include <kernel.h>
#include <device.h>
#include <soc.h>
#include <gpio.h>
#include <misc/util.h>

struct gpio_vexriscv_config {
	u32_t regs;
};

struct gpio_vexriscv_regs {
	/* First 4 regs are reserved for future IP-Core information */
	unsigned int reserved[4];
	unsigned int read;
	unsigned int write;
	unsigned int direction;
};

#define DEV_GPIO_CFG(dev)						     \
	((struct gpio_vexriscv_config *)(dev)->config->config_info)
#define DEV_GPIO(dev)							     \
	((struct gpio_vexriscv_regs *)(DEV_GPIO_CFG(dev))->regs)

static int gpio_vexriscv_config(struct device *dev,
			        int access_op,
			        u32_t pin,
			        int flags)
{
	volatile struct gpio_vexriscv_regs *gpio = DEV_GPIO(dev);

	if (access_op != GPIO_ACCESS_BY_PIN)
		return -ENOTSUP;

	/* Configure gpio direction */
	if (flags & GPIO_DIR_OUT)
		gpio->direction |= BIT(pin);
	else
		gpio->direction &= ~BIT(pin);

	return 0;
}

/**
 * @brief Set the pin
 *
 * @param dev Device struct
 * @param access_op Access operation
 * @param pin The pin number
 * @param value Value to set (0 or 1)
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_vexriscv_write(struct device *dev,
			       int access_op,
			       u32_t pin,
			       u32_t value)
{
	volatile struct gpio_vexriscv_regs *gpio = DEV_GPIO(dev);

	if (access_op != GPIO_ACCESS_BY_PIN)
		return -ENOTSUP;

	/* If pin is configured as input return with error */
	if (!(gpio->direction & BIT(pin)))
		return -EINVAL;

	if (value)
		gpio->write |= BIT(pin);
	else
		gpio->write &= ~BIT(pin);

	return 0;
}

/**
 * @brief Read the pin
 *
 * @param dev Device struct
 * @param access_op Access operation
 * @param pin The pin number
 * @param value Value of input pin(s)
 *
 * @return 0 if successful, failed otherwise
 */
static int gpio_vexriscv_read(struct device *dev,
			      int access_op,
			      u32_t pin,
			      u32_t *value)
{
	volatile struct gpio_vexriscv_regs *gpio = DEV_GPIO(dev);

	if (access_op != GPIO_ACCESS_BY_PIN)
		return -ENOTSUP;

	/*
	 * If gpio is configured as output,
	 * read gpio value from out_val register,
	 * otherwise read gpio value from in_val register
	 */
	if (gpio->direction & BIT(pin))
		*value = !!(gpio->write & BIT(pin));
	else
		*value = !!(gpio->read & BIT(pin));

	return 0;
}

/**
 * @brief Initialize a GPIO controller
 *
 * Perform basic initialization of a GPIO controller
 *
 * @param dev GPIO device struct
 *
 * @return 0
 */
static int gpio_vexriscv_init(struct device *dev)
{
	volatile struct gpio_vexriscv_config *cfg = DEV_GPIO_CFG(dev);
	volatile struct gpio_vexriscv_regs *gpio = DEV_GPIO(dev);

	gpio->write = 0;
	gpio->direction = 0;

	return 0;
}

static const struct gpio_driver_api gpio_vexriscv_driver_api = {
	.config              = gpio_vexriscv_config,
	.write               = gpio_vexriscv_write,
	.read                = gpio_vexriscv_read,
};

static const struct gpio_vexriscv_config gpio_vexriscv_dev_cfg_0 = {
	.regs = GPIO_0_BASE_ADDRESS,
};

DEVICE_AND_API_INIT(gpio_vexriscv_0, CONFIG_GPIO_VEXRISCV_DEV_NAME,
		    gpio_vexriscv_init,
		    GPIO_0_BASE_ADDRESS, &gpio_vexriscv_dev_cfg_0,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    (void *)&gpio_vexriscv_driver_api);

#ifdef GPIO_1_BASE_ADDRESS
static const struct gpio_vexriscv_config gpio_vexriscv_dev_cfg_1 = {
	.regs = GPIO_1_BASE_ADDRESS,
};

DEVICE_AND_API_INIT(gpio_vexriscv_1, "gpio1",
		    gpio_vexriscv_init,
		    GPIO_1_BASE_ADDRESS, &gpio_vexriscv_dev_cfg_1,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    (void *)&gpio_vexriscv_driver_api);
#endif
