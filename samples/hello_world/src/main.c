/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <gpio.h>
#include <misc/printk.h>
#include <misc/__assert.h>
#include <string.h>

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

#define LED_OK		0
#define LED_WARN	1
#define LED_ERROR	2
#define LED_HEART	3

void heartbeat(void)
{
	struct device *led_dev = device_get_binding("gpio1");
	int val = 0;

	gpio_pin_configure(led_dev, LED_HEART, GPIO_DIR_OUT);
	while (1) {
		gpio_pin_write(led_dev, LED_HEART, val);
		val = !val;
		k_sleep(1000);
	}

}

K_THREAD_DEFINE(heartbeat_id, STACKSIZE, heartbeat, NULL, NULL, NULL,
		PRIORITY, 0, K_NO_WAIT);
