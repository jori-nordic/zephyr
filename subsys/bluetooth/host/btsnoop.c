/*
 * Copyright (c) 2023 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/init.h>

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/buf.h>

#include "btsnoop.h"

#define BTSNOOP_INIT_PRIORITY 60

#define BTSNOOP_FILENAME_LEN 18

struct btsnoop_hdr {
        char id_pattern[8];
        uint32_t version;
        uint32_t datalink_type;
} __packed;

struct btsnoop_pkt_record {
        uint32_t original_len;
        uint32_t included_len;
        uint32_t flags;
        uint32_t drops;
        int64_t ts;
} __packed;

static int btsnoop_file_fd;

static void btsnoop_write_hdr(void)
{
        struct btsnoop_hdr hdr;

        memcpy(hdr.id_pattern, "btsnoop", 8);
        hdr.version = 1;
        hdr.datalink_type = 1002;

        if (write(btsnoop_file_fd, &hdr, sizeof(hdr)) == -1) {
                close(btsnoop_file_fd);
                return;
        }
}

static void btsnoop_init(void)
{
        pid_t pid = getpid();
	char filename[BTSNOOP_FILENAME_LEN]; // log.PID.btsnoop

	snprintk(filename, BTSNOOP_FILENAME_LEN, "log.%d.btsnoop", pid);
	printk("filename: %s\n", filename);

	int btsnoop_file_fd = open(filename, O_WRONLY | O_APPEND | O_CREAT, (mode_t)0666);
	if (btsnoop_file_fd == -1) {
		k_oops();
		return;
	}

        btsnoop_write_hdr();
}

static uint64_t btsnoop_ts_get(void)
{
        uint64_t cycles;
        const uint64_t cycles_per_s = sys_clock_hw_cycles_per_sec();
        const uint64_t us_per_s = 1000000llu;

        if (CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER) {
                cycles = k_cycle_get_64();
        } else {
                cycles = (uint64_t)k_cycle_get_32();
        }

        // s = msps * c / cps;

        return us_per_s * cycles / cycles_per_s; /* 1000x precision */
        // return cycles / (cycles_per_s / us_per_s); /* 1000x range */
}

static uint32_t btsnoop_get_pkt_flags(enum bt_buf_type buf_type)
{
        switch (buf_type) {
        case BT_BUF_CMD:
                return 0x02;
        case BT_BUF_EVT:
                return 0x03;
        case BT_BUF_ACL_IN:
                return 0x00;
        case BT_BUF_ACL_OUT:
		return 0x01;
	case BT_BUF_H4:
	case BT_BUF_ISO_IN:
	case BT_BUF_ISO_OUT:
                return 0xff;
        }
}

void bt_btsnoop_write(enum bt_buf_type buf_type, const void *data, size_t len)
{
        struct btsnoop_pkt_record pkt;

        pkt.original_len = len;
        pkt.included_len = len;
        pkt.flags = btsnoop_get_pkt_flags(buf_type);
        pkt.drops = 0;
        pkt.ts = btsnoop_ts_get();

        if (write(btsnoop_file_fd, &pkt, sizeof(pkt)) == -1) {
                close(btsnoop_file_fd);
                return;
        }

	if (write(btsnoop_file_fd, data, len) == -1) {
		close(btsnoop_file_fd);
		return;
	}
}

static int bt_btsnoop_init()
{
        IF_ENABLED(CONFIG_BT_BTSNOOP, ({
                btsnoop_init();
        }))

	return 0;
}

SYS_INIT(bt_btsnoop_init, PRE_KERNEL_1, BTSNOOP_INIT_PRIORITY);
