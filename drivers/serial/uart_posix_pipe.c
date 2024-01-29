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

struct nu_state {
	char *tx_fifo_path;
	char *rx_fifo_path;
	int tx_fd;
	int rx_fd;
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

static struct uart_driver_api nu_api = {
	.poll_out = nu_poll_out,
	.poll_in = nu_poll_in,
	/* TODO: add ASYNC api methods */
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
