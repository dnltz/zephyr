/*
 * Copyright (c) 2024 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_davinci_watchdog

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define LOG_LEVEL CONFIG_WDT_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wdt_davinci);

/* Helper Macros for WATCHDOG */
#define DEV_CFG(dev) ((const struct wdt_davinci_cfg *)((dev)->config))
#define DEV_DATA(dev) ((struct wdt_davinci_data *)(dev)->data)
#define DEV_WDT_CFG_BASE(dev) \
	((struct wdt_davinci_regs *)DEVICE_MMIO_NAMED_GET(dev, base))

#define DWD_ENABLE 0xA98559DA
#define DWD_DISABLE 0x5312ACED
#define WDKEY_FIRST_WRITE 0xE51A
#define WDKEY_SECOND_WRITE 0xA35C

#define RTI_DWWDPRLD_MULTIPLIER_SHIFT 13

struct wdt_davinci_regs {
	uint32_t reserved[36];
	uint32_t dwd_ctrl;
	uint32_t dwd_preload;
	uint32_t wd_status;
	uint32_t wd_key;
};


struct wdt_davinci_cfg {
	DEVICE_MMIO_NAMED_ROM(base);
};

struct wdt_davinci_data {
	DEVICE_MMIO_NAMED_RAM(base);
	bool timeout_active;
	bool wdt_started;
};

static int wdt_davinci_setup(const struct device *dev, uint8_t options)
{
	volatile struct wdt_davinci_regs *regs = DEV_WDT_CFG_BASE(dev);
	volatile struct wdt_davinci_data *data = DEV_DATA(dev);

	if (!data->timeout_active)
		return -EINVAL;

	if (data->wdt_started)
		return -EBUSY;

	regs->dwd_ctrl = DWD_ENABLE;
	data->wdt_started = true;

	return 0;
}

static int wdt_davinci_disable(const struct device *dev)
{
	volatile struct wdt_davinci_regs *regs = DEV_WDT_CFG_BASE(dev);
	volatile struct wdt_davinci_data *data = DEV_DATA(dev);

	if (!data->timeout_active)
		return -EINVAL;

	if (!data->wdt_started)
		return -EBUSY;

	regs->dwd_ctrl = DWD_DISABLE;
	data->wdt_started = false;

	return 0;
}

static int wdt_davinci_install_timeout(const struct device *dev,
				       const struct wdt_timeout_cfg *cfg)
{
	volatile struct wdt_davinci_regs *regs = DEV_WDT_CFG_BASE(dev);
	volatile struct wdt_davinci_data *data = DEV_DATA(dev);

	printk("window min %i\n", cfg->window.min);
	printk("window max %i\n", cfg->window.max);

	if (data->timeout_active)
		return -ENOMEM;

	if (!(cfg->flags & WDT_FLAG_RESET_SOC))
		return -ENOTSUP;


	data->timeout_active = true;

	return 0;
}

static int wdt_davinci_feed(const struct device *dev, int channel_id)
{
	volatile struct wdt_davinci_regs *regs = DEV_WDT_CFG_BASE(dev);

	regs->wd_key = WDKEY_FIRST_WRITE;
	regs->wd_key = WDKEY_SECOND_WRITE;

	return 0;
}

static const struct wdt_driver_api wdt_davinci_api = {
	.setup = wdt_davinci_setup,
	.disable = wdt_davinci_disable,
	.install_timeout = wdt_davinci_install_timeout,
	.feed = wdt_davinci_feed,
};

static int wdt_davinci_init(const struct device *dev)
{
	DEVICE_MMIO_NAMED_MAP(dev, base, K_MEM_CACHE_NONE);

	return 0;
}

#define DAVINCI_WDT_INIT(index)						         \
	static struct wdt_davinci_cfg wdt_davinci_cfg_##index = {		 \
		DEVICE_MMIO_NAMED_ROM_INIT(base, DT_DRV_INST(index)),		 \
	};									 \
	static struct wdt_davinci_data wdt_davinci_data_##index;		 \
	DEVICE_DT_INST_DEFINE(index, &wdt_davinci_init, NULL,			 \
			      &wdt_davinci_data_##index,			 \
			      &wdt_davinci_cfg_##index,				 \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,	 \
			      &wdt_davinci_api);

DT_INST_FOREACH_STATUS_OKAY(DAVINCI_WDT_INIT)
