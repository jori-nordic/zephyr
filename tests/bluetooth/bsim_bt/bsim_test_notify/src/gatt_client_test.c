/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>

#include "common.h"
#include "zephyr/bluetooth/addr.h"
#include "zephyr/bluetooth/conn.h"
#include <stdint.h>

CREATE_FLAG(flag_is_connected);
CREATE_FLAG(flag_is_bonded);

static struct bt_conn *g_conn;

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		FAIL("Failed to connect to %s (%u)\n", addr, err);
		return;
	}

	printk("connected, conn %d, dst %s\n", bt_conn_index(conn), addr);

	if (g_conn == NULL) {
		g_conn = bt_conn_ref(conn);
	}

	if (conn == g_conn) {
		SET_FLAG(flag_is_connected);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("disconnected, conn %d, dst %s, reason 0x%02x\n", bt_conn_index(conn), addr, reason);

	if (conn != g_conn) {
		return;
	}

	bt_conn_unref(g_conn);
	g_conn = NULL;

	UNSET_FLAG(flag_is_connected);
	UNSET_FLAG(flag_is_bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	FAIL("pairing_failed, conn %d, bt_security_err %d)\n", bt_conn_index(conn), reason);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(g_conn), addr_str, sizeof(addr_str));
	printk("pairing_complete, conn %d, dst %s, bonded %d\n", bt_conn_index(conn), addr_str, bonded);

	SET_FLAG(flag_is_bonded);
}

static struct bt_conn_auth_info_cb bt_conn_auth_info_cb = {
	.pairing_failed = pairing_failed,
	.pairing_complete = pairing_complete,
};

static bt_addr_le_t expected_addr;

void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	int err;

	if (g_conn != NULL) {
		return;
	}

	/* We're only interested in connectable events */
	if (type != BT_HCI_ADV_IND && type != BT_HCI_ADV_DIRECT_IND) {
		FAIL("Unexpected advertisement type.");
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("device_found, dst %s, RSSI %d\n", addr_str, rssi);

	if (bt_addr_le_cmp(&expected_addr, BT_ADDR_LE_ANY) != 0) {
		if (bt_addr_le_cmp(addr, &expected_addr) != 0) {
			char expected_addr_str[BT_ADDR_LE_STR_LEN];
			bt_addr_le_to_str(&expected_addr, expected_addr_str, sizeof(expected_addr_str));
			printk("Ignoring. Looking for %s.\n", expected_addr_str);
			return;
		}
	}

	err = bt_le_scan_stop();
	if (err != 0) {
		FAIL("Could not stop scan: %d");
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &g_conn);
	if (err != 0) {
		FAIL("Could not connect to peer: %d", err);
	}
}

static void setup(void)
{
	int id;
	bt_addr_le_t addr;

	addr = central_id();
	uint8_t irk[18];
	memset(irk, 123, sizeof(irk));
	id = bt_id_create(&addr, irk);
	if (id < 0) {
		FAIL("bt_id_create id_b failed (err %d)\n", id);
		return;
	}
	printk("bt_id_create: %d\n", id);

	int err;
	err = bt_enable(NULL);
	if (err != 0) {
		FAIL("Bluetooth discover failed (err %d)\n", err);
	}

	bt_conn_auth_info_cb_register(&bt_conn_auth_info_cb);

	BUILD_ASSERT(CONFIG_BT_MAX_PAIRED >= 2, "CONFIG_BT_MAX_PAIRED is too small.");

	/* Connect and bond with remote id a {{{ */
	printk("sync 1: Bonding id a\n");
	expected_addr = BT_ADDR_LE_ANY[0];
	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err != 0) {
		FAIL("Scanning failed to start (err %d)\n", err);
	}
	printk("bt_le_scan_start ok\n");

	WAIT_FOR_FLAG(flag_is_connected);

	err = bt_conn_set_security(g_conn, BT_SECURITY_L2);
	if (err) {
		FAIL("Starting encryption procedure failed (%d)\n", err);
	}

	WAIT_FOR_FLAG(flag_is_bonded);

	err = bt_conn_disconnect(g_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err) {
		FAIL("Disconnect failed (%d)\n", err);
	}
	printk("bt_conn_disconnect ok\n");

	WAIT_FOR_FLAG_UNSET(flag_is_connected);
	/* }}} */

	/* Connect and bond with remote id b {{{ */
	printk("sync 2: Bonding id b\n");
	expected_addr = BT_ADDR_LE_ANY[0];
	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err != 0) {
		FAIL("Scanning failed to start (err %d)\n", err);
	}
	printk("bt_le_scan_start ok\n");

	WAIT_FOR_FLAG(flag_is_connected);

	err = bt_conn_set_security(g_conn, BT_SECURITY_L2);
	if (err) {
		FAIL("Starting encryption procedure failed (%d)\n", err);
	}
	printk("bt_conn_set_security ok\n");

	WAIT_FOR_FLAG(flag_is_bonded);

	err = bt_conn_disconnect(g_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err) {
		FAIL("Disconnect failed (%d)\n", err);
	}
	printk("bt_conn_disconnect ok\n");

	WAIT_FOR_FLAG_UNSET(flag_is_connected);
	/* }}} */

	/* Connect to directed advertisement {{{ */
	expected_addr = peripheral_id_b();
	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err != 0) {
		FAIL("Scanning failed to start (err %d)\n", err);
	}

	printk("Scanning successfully started\n");
	WAIT_FOR_FLAG(flag_is_connected);

	/* }}} */
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static void test_main_none(void)
{
	bt_conn_cb_register(&conn_callbacks);
	setup();
	PASS("");
}

static const struct bst_test_instance test_vcs[] = {
	{
		.test_id = "gatt_client_none",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main_none,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_gatt_client_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_vcs);
}
