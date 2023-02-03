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
#include <zephyr/settings/settings.h>

#include <stdint.h>
#include <string.h>

void backchannel_init(void);
void server_procedure(void)
{
	struct bt_conn *conn;

	backchannel_init();
	wait_for_round_start();

	printk("Start test round: %d\n", get_test_round());

	bt_enable(NULL);
	gatt_register_service();
	settings_load();

	for (int i=0; i<2; i++) {
	/* TODO: remove. Wait for GATT hash to complete */
	k_sleep(K_SECONDS(10));

	conn = connect_as_central();
	printk("connected: conn %p\n", conn);

	if (i==0) {
		printk("bonding\n");
		bond(conn);
	} else {
		printk("encrypting\n");
		set_security(conn, BT_SECURITY_L2);
	}
	printk("encrypted\n");

	k_sleep(K_SECONDS(10));
	}

	signal_next_test_round();
}
