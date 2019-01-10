/*
 * Copyright (c) 2019 Phytec Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <arch/cpu.h>
#include <uart.h>

struct uart_vexriscv_config {
	u32_t regs;
	u32_t sys_clk_freq;
	u32_t current_speed;
};

struct uart_vexriscv_regs {
	/* First 4 regs are reserved for future IP-Core information */
	unsigned int reserved[4];
	unsigned int read_write;
	unsigned int status;
	unsigned int clock_div;
	unsigned int frame_cfg;
};

#define DEV_UART_CFG(dev)						     \
	((struct uart_vexriscv_config *)(dev)->config->config_info)
#define DEV_UART(dev)							     \
	((struct uart_vexriscv_regs *)(DEV_UART_CFG(dev))->regs)

static void uart_vexriscv_poll_out(struct device *dev, unsigned char c)
{
	volatile struct uart_vexriscv_regs *uart = DEV_UART(dev);

	while ((uart->status & 0x00FF0000) == 0);
	uart->read_write = c;
}

static int uart_vexriscv_poll_in(struct device *dev, unsigned char *c)
{
	volatile struct uart_vexriscv_regs *uart = DEV_UART(dev);
	int val;
	
	val = uart->read_write;
	if (val & 0x10000) {
		*c = val & 0xFF;
		return 0;
	}

	return -1;
}

static int uart_vexriscv_init(struct device *dev)
{
	volatile struct uart_vexriscv_config *cfg = DEV_UART_CFG(dev);
	volatile struct uart_vexriscv_regs *uart = DEV_UART(dev);

	uart->clock_div = cfg->sys_clk_freq / cfg->current_speed / 8;
	uart->frame_cfg = 7;

	return 0;
}


static const struct uart_driver_api uart_vexriscv_driver_api = {
	.poll_in = uart_vexriscv_poll_in,
	.poll_out = uart_vexriscv_poll_out,
};

static const struct uart_vexriscv_config uart_vexriscv_dev_cfg_0 = {
	.regs = UART_0_BASE_ADDRESS,
	.sys_clk_freq = UART_0_CLOCK_FREQUENCY,
	.current_speed = UART_0_CURRENT_SPEED,
};

DEVICE_AND_API_INIT(uart_vexriscv_0, UART_0_LABEL,
		    uart_vexriscv_init,
		    UART_0_BASE_ADDRESS, &uart_vexriscv_dev_cfg_0,
		    PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    (void *)&uart_vexriscv_driver_api);

