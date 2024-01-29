/*
 * Copyright (c) 2022 Libre Solar Technologies GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include <string.h>

/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_posix_uart)

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/*
 * Print a null-terminated string character by character to the UART interface
 */
static void print_string(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

#define MSG_SIZE 200

static void rx_line(char *buf)
{
	int ret = -1;

	for (int i = 0; i < MSG_SIZE; i++) {
		while (ret) {
			ret = uart_poll_in(uart_dev, &buf[i]);
		}
		if (buf[i] == '\n') return;
	}
}

int main(void)
{
	char tx_buf[MSG_SIZE];

	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
		return 0;
	}

	print_string("Hello! I'm your echo bot.\r\n");
	print_string("Tell me something and press enter:\r\n");

	for (;;) {
		rx_line(tx_buf);

		print_string("Echo: ");
		print_string(tx_buf);
		print_string("\r\n");

		memset(tx_buf, 0, MSG_SIZE);
	}
	return 0;
}
