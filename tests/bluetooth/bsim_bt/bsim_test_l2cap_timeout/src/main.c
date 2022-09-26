/* main_l2cap_timeout.c - Application main entry point */

/*
 * Copyright (c) 2022 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "utils.h"

#define LOG_MODULE_NAME main_l2cap_timeout
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_DBG);

extern enum bst_result_t bst_result;

static struct bt_conn *default_conn;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

CREATE_FLAG(is_connected);
CREATE_FLAG(l2cap_disconnected);
CREATE_FLAG(l2cap_rejected);

static int chan_recv_cb(struct bt_l2cap_chan *l2cap_chan, struct net_buf *buf)
{
	LOG_DBG("chan %p");
	return 0;
}

static void chan_sent_cb(struct bt_l2cap_chan *l2cap_chan)
{
	LOG_DBG("chan %p");
	return;
}

static void chan_connected_cb(struct bt_l2cap_chan *l2cap_chan)
{
	LOG_DBG("chan %p");
}

static void chan_disconnected_cb(struct bt_l2cap_chan *l2cap_chan)
{
	LOG_DBG("chan %p");
	SET_FLAG(l2cap_disconnected);
}

static void chan_status_cb(struct bt_l2cap_chan *l2cap_chan, atomic_t *status)
{
	LOG_DBG("chan %p, status: %ld", l2cap_chan, *status);
}

static void chan_released_cb(struct bt_l2cap_chan *l2cap_chan)
{
	LOG_DBG("chan %p");
}

static void chan_reconfigured_cb(struct bt_l2cap_chan *l2cap_chan)
{
	LOG_DBG("chan %p");
}

static const struct bt_l2cap_chan_ops l2cap_ops = {
	.recv = chan_recv_cb,
	.sent = chan_sent_cb,
	.connected = chan_connected_cb,
	.disconnected = chan_disconnected_cb,
	.status = chan_status_cb,
	.released = chan_released_cb,
	.reconfigured = chan_reconfigured_cb,
};

static int accept(struct bt_conn *conn, struct bt_l2cap_chan **l2cap_chan)
{
	LOG_ERR("stall");
	/* Stall until other side times out */
	k_sleep(K_SECONDS(50));
	/* Supervision timeout kicks in here */

	LOG_ERR("rejected");
	SET_FLAG(l2cap_rejected);
	return -ENOMEM;
}

struct bt_l2cap_server l2cap_server;

static void register_l2cap_server(void)
{
	l2cap_server.accept = accept;
	l2cap_server.psm = 0;

	if (bt_l2cap_server_register(&l2cap_server) < 0) {
		FAIL("Failed to register server");
		return;
	}

	LOG_DBG("L2CAP server registered, PSM: 0x%X", l2cap_server.psm);
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		FAIL("Failed to connect to %s (%u)", addr, conn_err);
		bt_conn_unref(default_conn);
		default_conn = NULL;
		return;
	}

	default_conn = bt_conn_ref(conn);
	LOG_DBG("%s", addr);

	SET_FLAG(is_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("%s (reason 0x%02x)", addr, reason);

	if (default_conn != conn) {
		FAIL("Conn mismatch disconnect %s %s)", default_conn, conn);
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;
	UNSET_FLAG(is_connected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void test_peripheral_main(void)
{
	LOG_DBG("L2CAP Timeout: Peripheral started");
	int err;

	err = bt_enable(NULL);
	if (err) {
		FAIL("Can't enable Bluetooth (err %d)", err);
		return;
	}

	register_l2cap_server();

	LOG_DBG("Peripheral Bluetooth initialized.");
	LOG_DBG("Connectable advertising...");
	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		FAIL("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_DBG("Advertising started.");
	LOG_DBG("Peripheral waiting for connection...");
	WAIT_FOR_FLAG_SET(is_connected);
	LOG_DBG("Peripheral Connected.");

	/* Wait until l2cap has rejected the channel open */
	LOG_DBG("Wait until l2cap connection request is rejected");
	WAIT_FOR_FLAG_SET(l2cap_rejected);

	WAIT_FOR_FLAG_UNSET(is_connected);
	PASS("L2CAP Timeout: Peripheral passed\n");
	bs_trace_silent_exit(0);
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	struct bt_le_conn_param *param;
	int err;

	err = bt_le_scan_stop();
	if (err) {
		FAIL("Stop LE scan failed (err %d)", err);
		return;
	}

	param = BT_LE_CONN_PARAM_DEFAULT;
	/* Supervision timeout still kicks in at 20s in */
	param->interval_min = 3000;
	param->interval_max = 3200;
	param->timeout = 3200;

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &default_conn);
	if (err) {
		FAIL("Create conn failed (err %d)", err);
		return;
	}
}

static void test_central_main(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};


	LOG_DBG("L2CAP Timeout: Central started");
	int err;

	err = bt_enable(NULL);
	if (err) {
		FAIL("Can't enable Bluetooth (err %d)\n", err);
		return;
	}
	LOG_DBG("Central Bluetooth initialized.\n");

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		FAIL("Scanning failed to start (err %d)\n", err);
		return;
	}

	LOG_DBG("Scanning successfully started\n");

	LOG_DBG("Central waiting for connection...\n");
	WAIT_FOR_FLAG_SET(is_connected);
	LOG_DBG("Central Connected.\n");

	static struct bt_l2cap_le_chan le_chan = {0};
	le_chan.chan.ops = &l2cap_ops;
	le_chan.required_sec_level = BT_SECURITY_L1;

	err = bt_l2cap_chan_connect(default_conn, &le_chan.chan, 0x80);
	LOG_ERR("l2cap connect err %d", err);
	WAIT_FOR_FLAG_SET(l2cap_disconnected);
	LOG_ERR("l2cap disconencted");

	/* Disconnect */
	LOG_DBG("Central Disconnecting....");
	err = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	bt_conn_unref(default_conn);
	LOG_DBG("Central tried to disconnect");

	if (err) {
		FAIL("Disconnection failed (err %d)", err);
		return;
	}

	LOG_DBG("Central Disconnected.");

	PASS("L2CAP Timeout: Central passed\n");
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "peripheral",
		.test_descr = "Peripheral L2CAP Timeout",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_peripheral_main
	},
	{
		.test_id = "central",
		.test_descr = "Central L2CAP Timeout",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_central_main
	},
	BSTEST_END_MARKER
};

struct bst_test_list *test_main_l2cap_timeout_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}

void test_init(void)
{
	bst_result = In_progress;
	bst_ticker_set_next_tick_absolute(WAIT_TIME);
}

void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("test failed (not passed after %i us)\n", WAIT_TIME);
	}
}

bst_test_install_t test_installers[] = {
	test_main_l2cap_timeout_install,
	NULL
};

void main(void)
{
	bst_main();
}
