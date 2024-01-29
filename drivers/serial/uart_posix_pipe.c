/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_posix_pipe_uart

#include <stdbool.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "cmdline.h" /* native_posix command line options header */
#include "posix_native_task.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(uart_pipe, LOG_LEVEL_INF);

/*
 * UART driver for POSIX ARCH based boards.
 *
 * It communicates using UNIX named pipes (ie. FIFOs).
 */

struct nu_async_ep {
	uint8_t *buf;
	size_t len;
	struct k_timer expiry;
};

struct nu_state {
	const struct device *dev;	/* pointer to parent */
	char *tx_fifo_path;
	char *rx_fifo_path;
	int tx_fd;
	int rx_fd;
#ifdef CONFIG_UART_ASYNC_API
	struct nu_async_ep tx;
	struct nu_async_ep rx;
	struct k_timer timer;
	uart_callback_t cb;
	void *ud;
#endif
};

static int open_fifo(char *path, int flags)
{
	/* Wait for other side to connect */
	int fd = open(path, flags);

	if (fd < 0) {
		LOG_ERR("Failed to open pipe %s: err %d",
			path, errno);
		k_oops();
	} else {
		/* Now that we have opened, we can set the non-blocking flags */
		flags = fcntl(fd, F_GETFL);
		flags |= O_NONBLOCK;
		fcntl(fd, F_SETFL, flags);

		LOG_DBG("Opened FIFO %s", path);
	}

	return fd;
}

static int nu_init(const struct device *dev)
{
	struct nu_state *s = (struct nu_state *)dev->data;

	s->rx_fd = open_fifo(s->rx_fifo_path, O_RDONLY);
	s->tx_fd = open_fifo(s->tx_fifo_path, O_WRONLY);

	if (s->tx_fd < 0 || s->rx_fd < 0) {
		return -1;
	}

	return 0;
}

/* FIXME: panic if fifo peer has disconnected */
static void nu_poll_out(const struct device *dev, unsigned char c)
{
	struct nu_state *s = (struct nu_state *)dev->data;
	int ret = -1;

	while (ret < 0) {
		ret = write(s->tx_fd, (uint8_t *)&c, sizeof(c));
		if (ret < 0) {
			/* printk("write: %d, waiting...\n", ret); */
			k_msleep(1);
		}
	}
}

static int nu_poll_in(const struct device *dev, unsigned char *c)
{
	struct nu_state *s = (struct nu_state *)dev->data;
	int ret = -1;

	ret = read(s->rx_fd, (uint8_t *)c, sizeof(*c));
	if (ret < 0) {
		return -errno;
	}

	return 0;
}

#ifdef CONFIG_UART_ASYNC_API

#define RETRY_DELAY K_MSEC(1)

static void handle_rx_done(struct nu_state *s, bool complete)
{
	struct uart_event evt;
	uint8_t *buf = s->rx.buf;
	size_t len = s->rx.len;

	/* allow rxing more from callback */
	s->rx.buf = NULL;
	s->rx.len = 0;
	k_timer_stop(&s->rx.expiry);

	memset(&evt, 0, sizeof(evt));

	evt.type = UART_RX_RDY;
	evt.data.rx.buf = buf;
	evt.data.rx.offset = 0;
	if (complete) {
		evt.data.rx.len = len;
	} else {
		evt.data.rx.len = 0;
	}
	s->cb(s->dev, &evt, s->ud);

	memset(&evt, 0, sizeof(evt));

	evt.type = UART_RX_BUF_RELEASED;
	evt.data.rx_buf.buf = buf;
	s->cb(s->dev, &evt, s->ud);

	memset(&evt, 0, sizeof(evt));

	evt.type = UART_RX_DISABLED;
	s->cb(s->dev, &evt, s->ud);
}

static void handle_tx_done(struct nu_state *s, bool complete)
{
	struct uart_event evt;

	memset(&evt, 0, sizeof(evt));

	if (complete) {
		evt.type = UART_TX_DONE;
		evt.data.tx.buf = s->tx.buf;
		evt.data.tx.len = s->tx.len;
	} else {
		evt.type = UART_TX_ABORTED;
		evt.data.tx.buf = s->tx.buf;
		evt.data.tx.len = 0;
	}

	/* allow txing more from callback */
	s->tx.buf = NULL;
	s->tx.len = 0;
	k_timer_stop(&s->tx.expiry);

	s->cb(s->dev, &evt, s->ud);
}

static void nu_expiry_work(struct k_timer *timer)
{
	struct nu_state *s = (struct nu_state *)k_timer_user_data_get(timer);

	__ASSERT_NO_MSG(s);

	/* Figure out if we timed out on RX or TX */
	if (timer == &s->rx.expiry) {
		handle_rx_done(s, false);
	} else if (timer == &s->tx.expiry) {
		handle_tx_done(s, false);
	} else {
		__ASSERT(0, "Spanish inquisition timer");
	}
}

static void nu_timer_work(struct k_timer *timer)
{
	struct nu_state *s = (struct nu_state *)k_timer_user_data_get(timer);
	int ret;

	__ASSERT_NO_MSG(s);

	/* TODO: handle disconnect from pipe */

	/* Try to RX first */
	if (s->rx.buf) {
		ret = read(s->rx_fd, s->rx.buf, s->rx.len);
		if (ret > 0) {
			__ASSERT(ret == s->rx.len, "get a better os");
			handle_rx_done(s, true);
		} else {
			k_timer_start(timer, RETRY_DELAY, K_FOREVER);
		}
	}

	/* Then try to TX */
	if (s->tx.buf) {
		ret = write(s->tx_fd, s->tx.buf, s->tx.len);

		if (ret > 0) {
			__ASSERT(ret == s->tx.len, "get a better os");
			handle_tx_done(s, true);
		} else {
			k_timer_start(timer, RETRY_DELAY, K_FOREVER);
		}
	}
}

static int nu_callback_set(const struct device *dev, uart_callback_t callback, void *user_data)
{
	struct nu_state *s = (struct nu_state *)dev->data;

	s->dev = dev;
	s->cb = callback;
	s->ud = user_data;

	/* TODO: move this to DTS macro */
	memset(&s->tx, 0, sizeof(struct nu_async_ep));
	memset(&s->rx, 0, sizeof(struct nu_async_ep));

	k_timer_init(&s->timer, nu_timer_work, NULL);
	k_timer_user_data_set(&s->timer, s);

	k_timer_init(&s->rx.expiry, nu_expiry_work, NULL);
	k_timer_user_data_set(&s->rx.expiry, s);

	k_timer_init(&s->tx.expiry, nu_expiry_work, NULL);
	k_timer_user_data_set(&s->tx.expiry, s);

	return 0;
}

static int nu_tx(const struct device *dev, const uint8_t *buf, size_t len, int32_t timeout)
{
	struct nu_state *s = (struct nu_state *)dev->data;

	if (s->tx.buf) {
		/* only one transaction supported at a time */
		return -EALREADY;
	}

	s->tx.buf = (uint8_t *)buf;
	s->tx.len = len;

	/* Always TX from ISR context */
	k_timer_start(&s->timer, K_NO_WAIT, K_NO_WAIT);
	if (timeout != SYS_FOREVER_US) {
		k_timer_start(&s->tx.expiry, K_USEC(timeout), K_NO_WAIT);
	}

	return 0;
}

static int nu_tx_abort(const struct device *dev)
{
	/* 'sup */
	return -ENOTSUP;
}

static int nu_rx_enable(const struct device *dev, uint8_t *buf, size_t len, int32_t timeout)
{
	struct nu_state *s = (struct nu_state *)dev->data;

	if (s->rx.buf) {
		/* only one transaction supported at a time */
		return -EALREADY;
	}

	s->rx.buf = buf;
	s->rx.len = len;

	/* Always RX from ISR context */
	k_timer_start(&s->timer, K_NO_WAIT, K_NO_WAIT);
	if (timeout != SYS_FOREVER_US) {
		k_timer_start(&s->rx.expiry, K_USEC(timeout), K_NO_WAIT);
	}

	return 0;
}

static int nu_rx_buf_rsp(const struct device *dev, uint8_t *buf, size_t len)
{
	/* 'sup */
	return -ENOTSUP;
}

static int nu_rx_disable(const struct device *dev)
{
	/* 'sup */
	return -ENOTSUP;
}
#endif	/* CONFIG_UART_ASYNC_API */

static struct uart_driver_api nu_api = {
	.poll_out = nu_poll_out,
	.poll_in = nu_poll_in,
#ifdef CONFIG_UART_ASYNC_API
	.callback_set = nu_callback_set,
	.tx = nu_tx,
	.tx_abort = nu_tx_abort,
	.rx_enable = nu_rx_enable,
	.rx_buf_rsp = nu_rx_buf_rsp,
	.rx_disable = nu_rx_disable,
#endif	/* CONFIG_UART_ASYNC_API */
};

#define UART_NATIVE_CMDLINE_ADD(n)					\
	static void nu_##n##_extra_cmdline_opts(void)			\
	{								\
		static struct args_struct_t nu_##n##_opts[] = {		\
		{							\
		.option = "fifo_" #n "_rx",				\
		.name = "\"path\"",					\
		.type = 's',						\
		.dest = (void *)&nu_##n##_state.rx_fifo_path,		\
		.descript = "Full path to FIFO used for serial driver: host -> device" \
	},								\
		{							\
		.option = "fifo_" #n "_tx",				\
		.name = "\"path\"",					\
		.type = 's',						\
		.dest = (void *)&nu_##n##_state.tx_fifo_path,		\
		.descript = "Full path to FIFO used for serial driver: device -> host" \
	},								\
		ARG_TABLE_ENDMARKER					\
	};								\
									\
		native_add_command_line_opts(nu_##n##_opts);		\
	}								\
									\
	static void nu_##n##_cleanup(void)				\
	{								\
		close(nu_##n##_state.rx_fd);				\
		close(nu_##n##_state.tx_fd);				\
	}								\
									\
	NATIVE_TASK(nu_##n##_extra_cmdline_opts, PRE_BOOT_1, 11);	\
	NATIVE_TASK(nu_##n##_cleanup, ON_EXIT, 99);			\


#define UART_NATIVE_PIPE_DEFINE(n)				\
								\
	static struct nu_state nu_##n##_state;			\
								\
	UART_NATIVE_CMDLINE_ADD(n);				\
								\
	DEVICE_DT_INST_DEFINE(n,				\
			      &nu_init,				\
			      NULL,				\
			      &nu_##n##_state,			\
			      NULL,				\
			      PRE_KERNEL_1,			\
			      CONFIG_SERIAL_INIT_PRIORITY,	\
			      &nu_api);

DT_INST_FOREACH_STATUS_OKAY(UART_NATIVE_PIPE_DEFINE)
