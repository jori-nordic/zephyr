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

void client_round_0(void)
{
	struct bt_conn *conn;

	printk("start round...........\n");

	conn = connect_as_peripheral();
	printk("connected: conn %p\n", conn);
	wait_secured();
	printk("encrypted\n");

	gatt_discover();
	activate_robust_caching();
	read_test_char(false);

	disconnect(conn);
}

void client_procedure(void)
{
	bt_enable(NULL);
	settings_load();

	client_round_0();
	client_round_0();

	PASS("PASS\n");
}
