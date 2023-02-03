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

void server_procedure(void)
{
	struct bt_conn *conn;

	bt_enable(NULL);
	gatt_register_service();
	settings_load();

	/* TODO: remove. Wait for GATT hash to complete */
	k_sleep(K_SECONDS(10));

	conn = connect_as_central();
	printk("connected: conn %p\n", conn);

	PASS("PASS\n");
}
