/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "utils.h"
#include "zephyr/bluetooth/addr.h"
#include "zephyr/bluetooth/bluetooth.h"
#include "zephyr/bluetooth/conn.h"
#include "zephyr/toolchain/gcc.h"

#include <stdint.h>
#include <string.h>

void server_procedure(void)
{
	int err;
	struct bt_conn *conn;

	err = bt_enable(NULL);

	conn = connect_as_central();
	printk("connected: conn %p\n", conn);
	disconnect(conn);

	PASS("PASS\n");
}
