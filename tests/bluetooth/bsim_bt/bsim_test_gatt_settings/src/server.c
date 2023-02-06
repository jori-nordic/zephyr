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

void set_public_addr(void)
{
	bt_addr_le_t addr = {BT_ADDR_LE_RANDOM,
			     {{0x0A, 0x89, 0x67, 0x45, 0x23, 0xC1}}};
	bt_id_create(&addr, NULL);
}

void backchannel_init(void);
void server_procedure(void)
{
	struct bt_conn *conn;
	uint8_t round = get_test_round();

	backchannel_init();
	wait_for_round_start();

	printk("Start test round: %d\n", get_test_round());

	/* Use the same public address for all instances of the central. If we
	 * don't do that, encryption (using the bond stored in NVS) will
	 * fail.
	 */
	set_public_addr();

	bt_enable(NULL);
	gatt_register_service(false);
	if (round >= 2) {
		gatt_register_service(true);
	}

	/* TODO: remove. Wait for GATT hash to complete */
	k_sleep(K_SECONDS(10));

	/* FIXME: triggering a settings load after the hash has been calculated fails:
	 * - the first calc will actually store the hash in NVS, OVERWRITING the old one
	 * - then the settings are loaded, and the hash matches (ofc), peer isn't marked CU
	 *
	 * - when the settings are loaded, a hash calc is rescheduled, but it will erroneously match
	 * - need a flag to indicate if the settings have been loaded or not
	 */
	settings_load();
	k_sleep(K_SECONDS(10));

	conn = connect_as_central();
	printk("connected: conn %p\n", conn);

	if (get_test_round() == 0) {
		printk("bonding\n");
		bond(conn);
	} else {
		printk("encrypting\n");
		set_security(conn, BT_SECURITY_L2);
	}

	wait_disconnected();

	signal_next_test_round();
}
