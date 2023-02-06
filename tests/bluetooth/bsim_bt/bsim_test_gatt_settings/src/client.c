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

	printk("start round 0...........\n");

	conn = connect_as_peripheral();
	printk("connected: conn %p\n", conn);
	wait_secured();

	gatt_discover();
	activate_robust_caching();
	read_test_char(false);

	disconnect(conn);
}

void client_round_1(void)
{
	struct bt_conn *conn;

	printk("start round 1...........\n");

	conn = connect_as_peripheral();
	printk("connected: conn %p\n", conn);
	wait_secured();

	read_test_char(false);

	disconnect(conn);
}

void client_round_2(void)
{
	struct bt_conn *conn;

	printk("start round 2...........\n");

	conn = connect_as_peripheral();
	printk("connected: conn %p\n", conn);
	wait_secured();

	/* GATT DB has changed */
	read_test_char(false);

	disconnect(conn);
}

void client_procedure(void)
{
	bt_enable(NULL);
	settings_load();

	k_msleep(1000);

	client_round_0();
	client_round_1();
	client_round_2();

	PASS("PASS\n");
}
/* Procedures:
 *
 * 1.
 *   - boot
 *   - register service
 *   - connect
 *   - bond
 *   - mark robust caching
 *   - gatt read
 *   - powercycle server
 *
 * 2.
 *   - boot
 *   - register service
 *   - connect
 *   - encrypt
 *   - gatt read (ok)
 *   - disconnect
 *   - powercycle
 *
 * 3.
 *   - boot
 *   - connect
 *   - encrypt
 *   - gatt read (fail)
 *   - CU -> CA (hash read?)
 *   - gatt read (ok)
 *   - powercycle
 *
 * 4.
 *   - boot
 *   - connect
 *   - encrypt
 *   - gatt read (ok)
 *
 */
