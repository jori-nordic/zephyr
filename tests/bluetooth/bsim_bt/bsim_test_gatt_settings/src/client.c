/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "utils.h"
#include "zephyr/bluetooth/addr.h"
#include "zephyr/bluetooth/bluetooth.h"
#include "zephyr/bluetooth/conn.h"
#include <zephyr/settings/settings.h>

#include <stdint.h>
#include <string.h>

void client_procedure(void)
{
	struct bt_conn *conn;

	bt_enable(NULL);
	settings_load();

	conn = connect_as_peripheral();
	printk("connected: conn %p\n", conn);
	gatt_discover();
	activate_robust_caching();
	read_test_char(false);

	disconnect(conn);

	PASS("PASS\n");
}
