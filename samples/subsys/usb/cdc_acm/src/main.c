/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Sample echo app for CDC ACM class
 *
 * Sample app for USB CDC ACM class driver. The received data is echoed back
 * to the serial port.
 */

#include <stdio.h>
#include <string.h>
#include <device.h>
#include <drivers/uart.h>
#include <zephyr.h>
#include <sys/ring_buffer.h>

#include <usb/usb_device.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(cdc_acm_echo, LOG_LEVEL_INF);

#define UART_DEVICE_NAME "UART_1"


const struct uart_config uart_cfg = { .baudrate = 115200,
				      .parity = UART_CFG_PARITY_NONE,
				      .stop_bits = UART_CFG_STOP_BITS_1,
				      .data_bits = UART_CFG_DATA_BITS_8,
				      .flow_ctrl = UART_CFG_FLOW_CTRL_NONE };

#define RING_BUF_SIZE 1024
uint8_t ring_buffer_tx[RING_BUF_SIZE];
uint8_t ring_buffer_rx[RING_BUF_SIZE];

struct ring_buf ringbuf_tx;
struct ring_buf ringbuf_rx;

const struct device * uart_dev;
const struct device * usb_dev;

static void uart_interrupt_handler(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			int recv_len, rb_len;
			uint8_t buffer[64];
			size_t len = MIN(ring_buf_space_get(&ringbuf_rx),
					 sizeof(buffer));

			recv_len = uart_fifo_read(dev, buffer, len);

			rb_len = ring_buf_put(&ringbuf_tx, buffer, recv_len);
			if (rb_len < recv_len) {
				LOG_ERR("Drop %u bytes", recv_len - rb_len);
			}

			LOG_DBG("tty fifo -> ringbuf_tx %d bytes", rb_len);

			/* Send to USB */
			uart_irq_tx_enable(usb_dev);
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buffer[64];
			int rb_len, send_len;

			rb_len = ring_buf_get(&ringbuf_rx, buffer, sizeof(buffer));
			if (!rb_len) {
				LOG_DBG("Ring buffer empty, disable TX IRQ");
				uart_irq_tx_disable(dev);
				continue;
			}

			/* Relay USB RX to UART TX */
			send_len = uart_fifo_fill(dev, buffer, rb_len);
			if (send_len < rb_len) {
				LOG_ERR("Drop %d bytes", rb_len - send_len);
			}

			LOG_DBG("ringbuf_rx -> tty fifo %d bytes", send_len);
		}
	}
}

static void usb_interrupt_handler(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			int recv_len, rb_len;
			uint8_t buffer[64];
			size_t len = MIN(ring_buf_space_get(&ringbuf_rx),
					 sizeof(buffer));

			recv_len = uart_fifo_read(dev, buffer, len);

			rb_len = ring_buf_put(&ringbuf_rx, buffer, recv_len);
			if (rb_len < recv_len) {
				LOG_ERR("Drop %u bytes", recv_len - rb_len);
			}

			LOG_DBG("tty fifo -> ringbuf_rx %d bytes", rb_len);

			/* Send to UART1 */
			uart_irq_tx_enable(uart_dev);
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t buffer[64];
			int rb_len, send_len;

			rb_len = ring_buf_get(&ringbuf_tx, buffer, sizeof(buffer));
			if (!rb_len) {
				LOG_DBG("Ring buffer empty, disable TX IRQ");
				uart_irq_tx_disable(dev);
				continue;
			}

			/* Relay UART RX to USB TX */
			send_len = uart_fifo_fill(dev, buffer, rb_len);
			if (send_len < rb_len) {
				LOG_ERR("Drop %d bytes", rb_len - send_len);
			}

			LOG_DBG("ringbuf_tx -> tty fifo %d bytes", send_len);
		}
	}
}

void main(void)
{
	const struct device *dev;
	uint32_t baudrate, dtr = 0U;
	int ret;

	/* Configure UART device */
	uart_dev = device_get_binding(UART_DEVICE_NAME);

	if (!uart_dev) {
		printk("Cannot get UART device\n");
	}

	ret = uart_configure(uart_dev, &uart_cfg);

	if (ret) {
		printk("UART config failed\n");
	}

	uart_irq_callback_set(uart_dev, uart_interrupt_handler);

	/* Enable rx interrupts */
	uart_irq_rx_enable(uart_dev);

	/* Configure USB device */
	dev = device_get_binding("CDC_ACM_0");
	usb_dev = dev;
	if (!dev) {
		LOG_ERR("CDC ACM device not found");
		return;
	}

	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return;
	}

	ring_buf_init(&ringbuf_rx, sizeof(ring_buffer_rx), ring_buffer_rx);
	ring_buf_init(&ringbuf_tx, sizeof(ring_buffer_tx), ring_buffer_tx);

	LOG_INF("Wait for DTR");

	while (true) {
		uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			break;
		} else {
			/* Give CPU resources to low priority threads. */
			k_sleep(K_MSEC(100));
		}
	}

	LOG_INF("DTR set");

	/* They are optional, we use them to test the interrupt endpoint */
	ret = uart_line_ctrl_set(dev, UART_LINE_CTRL_DCD, 1);
	if (ret) {
		LOG_WRN("Failed to set DCD, ret code %d", ret);
	}

	ret = uart_line_ctrl_set(dev, UART_LINE_CTRL_DSR, 1);
	if (ret) {
		LOG_WRN("Failed to set DSR, ret code %d", ret);
	}

	/* Wait 1 sec for the host to do all settings */
	k_busy_wait(1000000);

	ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret) {
		LOG_WRN("Failed to get baudrate, ret code %d", ret);
	} else {
		LOG_INF("Baudrate detected: %d", baudrate);
	}

	uart_irq_callback_set(dev, usb_interrupt_handler);

	/* Enable rx interrupts */
	uart_irq_rx_enable(dev);
}
