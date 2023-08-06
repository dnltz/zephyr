/*
 * Copyright (c) 2023 Daniel Schultz <d.schultz@phytec.de>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_sci_intr

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/irq_nextlevel.h>
#include <zephyr/sw_isr_table.h>

struct sci_intr_t {
	uint32_t pid;
	uint32_t mux;
};

struct sci_intr_config {
	uintptr_t                 gpio_base_addr;
};

struct sci_intr_data {
	uint32_t enabled;
};

/* Helper Macros for INTR */
#define DEV_GPIO_CFG(dev)						\
	((const struct sci_intr_config * const)(dev)->config)
#define DEV_GPIO(dev)							\
	((volatile struct sci_intr_t *)(DEV_GPIO_CFG(dev))->gpio_base_addr)
#define DEV_GPIO_DATA(dev)				\
	((struct sci_intr_data *)(dev)->data)


static void sci_intr_irq_enable(const struct device *dev, uint32_t irq)
{
	volatile struct sci_intr_t *intc = DEV_GPIO(dev);
	struct sci_intr_data *data = DEV_GPIO_DATA(dev);

	intc->mux = 0x1 << 16 | (irq & 0x1F);
	data->enabled |= 0x1 << irq;
}

static void sci_intr_irq_disable(const struct device *dev, uint32_t irq)
{
	volatile struct sci_intr_t *intc = DEV_GPIO(dev);
	struct sci_intr_data *data = DEV_GPIO_DATA(dev);

	intc->mux = irq & 0x1F;
	data->enabled &= ~(0x1 << irq);
}

static uint32_t sci_intr_get_state(const struct device *dev)
{
	struct sci_intr_data *data = DEV_GPIO_DATA(dev);

	return !!data->enabled;
}

static int sci_intr_get_line_state(const struct device *dev,
					unsigned int irq)
{
	struct sci_intr_data *data = DEV_GPIO_DATA(dev);

	return (data->enabled >> irq) & 0x1;
}

static const struct irq_next_level_api sci_intr_apis = {
	.intr_enable = sci_intr_irq_enable,
	.intr_disable = sci_intr_irq_disable,
	.intr_get_state = sci_intr_get_state,
	.intr_get_line_state = sci_intr_get_line_state,
};


static int sci_intr_init(const struct device *dev)
{
	return 0;
}


static const struct sci_intr_config sci_intr_config0 = {
	.gpio_base_addr = DT_INST_REG_ADDR(0),
};

static struct sci_intr_data sci_intr_data0;

DEVICE_DT_INST_DEFINE(0,
		      sci_intr_init,
		      NULL,
		      &sci_intr_data0, &sci_intr_config0,
		      PRE_KERNEL_1, CONFIG_INTC_INIT_PRIORITY,
		      &sci_intr_apis);
