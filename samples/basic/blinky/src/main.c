/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>
#include <nrf.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define INST NRF_SPIM3
void do_stuff(void)
{
	printk("config gpio\n");
	/* config_gpio(); */

	printk("send over SPI\n");

	INST->PSEL.SCK =   3UL; /* P1.10 */
	INST->PSEL.MOSI =  4UL; /* P1.11 */
	INST->PSEL.MISO =  0x80000000; /* not connected */
	INST->PSEL.CSN =   0x80000000; /* not connected */

	INST->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M8; /* 4 MHz */
	INST->CONFIG = 0;

	INST->ENABLE = 7UL;
	INST->TASKS_STOP = 1;
	INST->RXD.MAXCNT = 0;

	static char myarray[100];

	for(int i=0; i<10; i++) {
		int bytes = sprintf(myarray, "hello spi logger! i = %d", i);
		INST->TXD.MAXCNT = (uint32_t)bytes & 0xFFFF;
		INST->TXD.PTR = (uint32_t)myarray;

		INST->TASKS_START = 1;
		INST->EVENTS_END = 0;

		while(INST->EVENTS_END != 1) {
			__NOP();
		}
		INST->EVENTS_END = 0;
	}
	printk("send ok\n");
}

void main(void)
{
	int ret;

	if (!device_is_ready(led.port)) {
		return;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}

	do_stuff();

	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return;
		}
		k_msleep(SLEEP_TIME_MS);
	}
}
