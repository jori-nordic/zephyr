/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"

extern enum bst_result_t bst_result;

CREATE_FLAG(flag_is_connected);
CREATE_FLAG(flag_long_subscribe);
CREATE_FLAG(flag_mtu_exchanged);

static struct bt_conn *g_conn;

#define ARRAY_ITEM(i, _) i
const uint8_t chrc_data[] = { LISTIFY(CHRC_SIZE, ARRAY_ITEM, (,)) }; /* 1, 2, 3 ... */
const uint8_t long_chrc_data[] = { LISTIFY(LONG_CHRC_SIZE, ARRAY_ITEM, (,)) }; /* 1, 2, 3 ... */

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		FAIL("Failed to connect to %s (%u)\n", addr, err);
		return;
	}

	printk("Connected to %s\n", addr);

	g_conn = bt_conn_ref(conn);
	SET_FLAG(flag_is_connected);
	UNSET_FLAG(flag_mtu_exchanged);
	UNSET_FLAG(flag_long_subscribe);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != g_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(g_conn);

	g_conn = NULL;
	UNSET_FLAG(flag_is_connected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static ssize_t read_test_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	printk("Read short\n");
	return bt_gatt_attr_read(conn, attr, buf, len, offset, (void *)chrc_data,
				 sizeof(chrc_data));
}

static ssize_t read_long_test_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				   uint16_t len, uint16_t offset)
{
	printk("Read long\n");
	return bt_gatt_attr_read(conn, attr, buf, len, offset, (void *)long_chrc_data,
				 sizeof(long_chrc_data));
}

static void short_subscribe(const struct bt_gatt_attr *attr, uint16_t value)
{
	const bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	printk("Short notifications %s\n", notif_enabled ? "enabled" : "disabled");
}

static void long_subscribe(const struct bt_gatt_attr *attr, uint16_t value)
{
	const bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	if (notif_enabled) {
		SET_FLAG(flag_long_subscribe);
	}

	printk("Long notifications %s\n", notif_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(test_svc, BT_GATT_PRIMARY_SERVICE(TEST_SERVICE_UUID),
		       BT_GATT_CHARACTERISTIC(TEST_CHRC_UUID,
					      BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_test_chrc, NULL, NULL),
		       BT_GATT_CCC(short_subscribe, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
		       BT_GATT_CHARACTERISTIC(TEST_LONG_CHRC_UUID,
					      BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_long_test_chrc, NULL, NULL),
		       BT_GATT_CCC(long_subscribe, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static volatile size_t num_notifications_sent;

static void notification_sent(struct bt_conn *conn, void *user_data)
{
	size_t *length = user_data;

	printk("Sent notification #%u with length %d\n", num_notifications_sent++, *length);
}

static inline void long_notify(void)
{
	static size_t length = LONG_CHRC_SIZE;
	static struct bt_gatt_notify_params params = {
		.attr = &attr_test_svc[4],
		.data = long_chrc_data,
		.len = LONG_CHRC_SIZE,
		.func = notification_sent,
		.user_data = &length,
		.uuid = NULL,
	};
	int err;

	do {
		err = bt_gatt_notify_cb(g_conn, &params);

		if (err == -ENOMEM) {
			printk("ENOMEM, sleeping\n");
			k_sleep(K_MSEC(10));
		} else if (err) {
			printk("Long notify failed (err %d)\n", err);
			return;
		}
	} while (err);
}

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	printk("MTU exchanged: [TX] %d [RX] %d\n", tx, rx);
	SET_FLAG(flag_mtu_exchanged);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = att_mtu_updated,
};

extern bool g_corrupt_radio;
static void test_main(void)
{
	int err;
	const struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	};

	err = bt_enable(NULL);

	if (err != 0) {
		FAIL("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_gatt_cb_register(&gatt_callbacks);

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err != 0) {
		FAIL("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");

	for (int i=0; i<5; i++) {
	printk("#############################\n");
	printk("Wait for connection\n");
	WAIT_FOR_FLAG(flag_is_connected);

	WAIT_FOR_FLAG(flag_long_subscribe);
	WAIT_FOR_FLAG(flag_mtu_exchanged);

	printk("Notifying\n");
	for (int i = 0; i < NOTIFICATION_COUNT / 2; i++) {
		printk("Queue notification #%d\n", i);
		long_notify();
	}
	g_corrupt_radio = true;

	printk("Wait for flag");
	WAIT_FOR_FLAG_UNSET(flag_is_connected);
	g_corrupt_radio = false;

	printk("..................................................");
	printk("disconnected ok\n");
	num_notifications_sent = 0;

	printk("GATT server cycle %d ok\n", i);
	}
	PASS("GATT server passed\n");
}

static const struct bst_test_instance test_gatt_server[] = {
	{
		.test_id = "gatt_server",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_gatt_server_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_gatt_server);
}
