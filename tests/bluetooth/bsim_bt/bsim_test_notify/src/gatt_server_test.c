/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"
#include "zephyr/bluetooth/bluetooth.h"
#include "zephyr/bluetooth/addr.h"
#include "zephyr/toolchain/gcc.h"
#include <stdint.h>
#include <string.h>

extern enum bst_result_t bst_result;

CREATE_FLAG(flag_is_connected);

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
}

static struct bt_conn_auth_info_cb bt_conn_auth_info_cb = {
	.pairing_failed = pairing_failed,
	.pairing_complete = pairing_complete,
};

static void format_id_addr(int id, char addr_str[BT_ADDR_LE_STR_LEN])
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = CONFIG_BT_ID_MAX;

	bt_id_get(addrs, &count);
	ASSERT(count > id, "");

	bt_addr_le_to_str(&addrs[id], addr_str, BT_ADDR_LE_STR_LEN);
}

static void adv_on_id(int id, struct bt_le_adv_param *param)
{
	memset(param, 0, sizeof(*param));
	param->id = id;

	param->interval_min = 0x0020;
	param->interval_max = 0x4000;
	param->options |= BT_LE_ADV_OPT_USE_NAME;
	param->options |= BT_LE_ADV_OPT_ONE_TIME;
	param->options |= BT_LE_ADV_OPT_CONNECTABLE;
	int err = bt_le_adv_start(param, NULL, 0, NULL, 0);
	ASSERT(err == 0, "Advertising failed to start (err %d)\n", err);

	char addr_str[BT_ADDR_LE_STR_LEN];
	format_id_addr(id, addr_str);
	printk("Advertising started on ID %d - %s\n", id, addr_str);
}

int bt_encrypt_le(const uint8_t key[16], const uint8_t plaintext[16],
		  uint8_t enc_data[16]);
static void setup(void)
{
	int err;
	bt_addr_le_t central;
	bt_addr_le_t addr;
	int id_a;
	int id_b;
	struct bt_le_adv_param adv_param1;
	struct bt_le_adv_param adv_param2;
	struct bt_le_adv_param adv_param3;

	err = bt_enable(NULL);
	if (err != 0) {
		FAIL("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_conn_auth_info_cb_register(&bt_conn_auth_info_cb);

	BUILD_ASSERT(CONFIG_BT_MAX_PAIRED >= 2, "CONFIG_BT_MAX_PAIRED is too small.");
	BUILD_ASSERT(CONFIG_BT_ID_MAX >= 3, "CONFIG_BT_ID_MAX is too small.");

	/* Create and bond id a  */
	printk("sync 1: Bonding id a\n");
	addr = peripheral_id_a();
	id_a = bt_id_create(&addr, NULL);
	ASSERT(id_a >= 0, "bt_id_create id_a failed (err %d)\n", id_a);
	printk("bt_id_create id_a: %d\n", id_a);

	adv_on_id(id_a, &adv_param1);
	WAIT_FOR_FLAG(flag_is_connected);
	/* Central should bond here, and trigger a disconnect. */
	WAIT_FOR_FLAG_UNSET(flag_is_connected);

	/* Create and bond id b  */
	printk("sync 2: Bonding id b\n");
	addr = peripheral_id_b();
	id_b = bt_id_create(&addr, NULL);
	ASSERT(id_b >= 0, "bt_id_create id_b failed (err %d)\n", id_b);
	printk("bt_id_create: %d\n", id_b);

	adv_on_id(id_b, &adv_param2);
	WAIT_FOR_FLAG(flag_is_connected);
	central = *bt_conn_get_dst(g_conn);
	/* Central should bond here. */
	WAIT_FOR_FLAG_UNSET(flag_is_connected);

	/* bt_id_delete(id_a); */

	/* Directed advertisement connect  */
	adv_param3 = (struct bt_le_adv_param){};
	adv_param3.interval_min = 0x0020;
	adv_param3.interval_max = 0x4000;
	adv_param3.options |= BT_LE_ADV_OPT_USE_NAME;
	adv_param3.options |= BT_LE_ADV_OPT_ONE_TIME;
	adv_param3.options |= BT_LE_ADV_OPT_CONNECTABLE;
	adv_param3.id = id_b;
	adv_param3.options |= BT_LE_ADV_OPT_DIR_ADDR_RPA;
	adv_param3.peer = &central;
	err = bt_le_adv_start(&adv_param3, NULL, 0, NULL, 0);
	if (err != 0) {
		FAIL("Advertising failed to start (err %d)\n", err);
		return;
	}

	WAIT_FOR_FLAG(flag_is_connected);
	/* Central should verify that its bond with id_b works as expected. */
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

static const struct bst_test_instance test_gatt_server[] = {
	{
		.test_id = "gatt_server_none",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main_none,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_gatt_server_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_gatt_server);
}
