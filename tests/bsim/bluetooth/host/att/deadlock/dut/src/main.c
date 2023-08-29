/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>

#include "utils.h"
#include "bstests.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dut, LOG_LEVEL_DBG);

DEFINE_FLAG(is_connected);
DEFINE_FLAG(is_secured);
DEFINE_FLAG(flag_read_complete);

static struct bt_conn *dconn;

static uint8_t gatt_read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
			    const void *data, uint16_t length)
{
	if (err != BT_ATT_ERR_SUCCESS) {
		FAIL("Read failed: 0x%02X\n", err);
	}

	LOG_HEXDUMP_DBG(data, length, "Read data:");

	SET_FLAG(flag_read_complete);

	// bt_conn_unref(dconn);

	return 0;
}

static void gatt_read(void)
{
	static struct bt_gatt_read_params read_params;
	int err;

	printk("Reading chrc\n");

	read_params.func = gatt_read_cb;
	read_params.by_uuid.start_handle = 0x0001;
	read_params.by_uuid.end_handle = 0xffff;
	read_params.by_uuid.uuid = test_characteristic_uuid;

	UNSET_FLAG(flag_read_complete);

	err = bt_gatt_read(dconn, &read_params);
	if (err != 0) {
		FAIL("bt_gatt_read failed: %d\n", err);
	}

	// WAIT_FOR_FLAG(flag_read_complete);
	printk("success\n");
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		FAIL("Failed to connect to %s (%u)", addr, conn_err);
		return;
	}

	LOG_DBG("%s", addr);

	dconn = bt_conn_ref(conn);
	SET_FLAG(is_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("%p %s (reason 0x%02x)", conn, addr, reason);

	bt_conn_unref(dconn);
	UNSET_FLAG(is_connected);
}

void secured(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	LOG_DBG("%p level %d err %d", conn, level, err);

	__ASSERT_NO_MSG(err == 0);
	SET_FLAG(is_secured);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = secured,
};

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	struct bt_le_conn_param *param;
	struct bt_conn *conn;
	int err;

	err = bt_le_scan_stop();
	if (err) {
		FAIL("Stop LE scan failed (err %d)", err);
		return;
	}

	char str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, str, sizeof(str));

	LOG_DBG("Connecting to %s", str);

	param = BT_LE_CONN_PARAM_DEFAULT;
	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &conn);
	if (err) {
		FAIL("Create conn failed (err %d)", err);
		return;
	}
}

static void connect(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	UNSET_FLAG(is_secured);
	UNSET_FLAG(is_connected);

	int err = bt_le_scan_start(&scan_param, device_found);

	ASSERT(!err, "Scanning failed to start (err %d)\n", err);

	LOG_DBG("Central initiating connection...");
	WAIT_FOR_FLAG(is_connected);

	k_msleep(1000);
	err = bt_conn_set_security(dconn, BT_SECURITY_L2);
	__ASSERT_NO_MSG(err == 0);

	WAIT_FOR_FLAG(is_secured);

	LOG_DBG("Central EATTing...");

	while (bt_eatt_count(dconn) < 15) {
		k_msleep(100);
	}
	LOG_ERR("Central EATT-ed...");
}

/* static void disconnect_device(struct bt_conn *conn, void *data) */
/* { */
/* 	int err; */

/* 	SET_FLAG(is_connected); */

/* 	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN); */
/* 	ASSERT(!err, "Failed to initate disconnect (err %d)", err); */

/* 	LOG_DBG("Waiting for disconnection..."); */
/* 	WAIT_FOR_FLAG_UNSET(is_connected); */
/* } */

void test_procedure_0(void)
{
	LOG_DBG("Deadlock DUT/central started*");
	int err;

	err = bt_enable(NULL);
	ASSERT(err == 0, "Can't enable Bluetooth (err %d)\n", err);
	LOG_DBG("Central Bluetooth initialized.");

	for (int i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		connect();
	}

	LOG_INF("===== ALL CONNECTED =====");

	k_msleep(5000);

	for (int i = 0; i < CONFIG_BT_EATT_MAX; i++) {
		gatt_read();
	}

	LOG_INF("===== ALL READ SEND =====");

	k_sleep(K_SECONDS(30));

	// bt_conn_foreach(BT_CONN_TYPE_LE, disconnect_device, NULL);

	PASS("DUT done\n");
}

void test_tick(bs_time_t HW_device_time)
{
	bs_trace_debug_time(0, "Simulation ends now.\n");
	if (bst_result != Passed) {
		bst_result = Failed;
		bs_trace_error("Test did not pass before simulation ended.\n");
	}
}

void test_init(void)
{
	bst_ticker_set_next_tick_absolute(TEST_TIMEOUT_SIMULATED);
	bst_result = In_progress;
}

static const struct bst_test_instance test_to_add[] = {
	{
		.test_id = "dut",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_procedure_0,
	},
	BSTEST_END_MARKER,
};

static struct bst_test_list *install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_to_add);
};

bst_test_install_t test_installers[] = {install, NULL};

int main(void)
{
	bst_main();

	return 0;
}
