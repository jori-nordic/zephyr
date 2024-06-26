/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/logging/log.h>

#include "testlib/scan.h"
#include "testlib/conn.h"

#include "babblekit/flags.h"
#include "babblekit/sync.h"
#include "babblekit/testcase.h"

/* local includes */
#include "data.h"

LOG_MODULE_REGISTER(central, CONFIG_APP_LOG_LEVEL);

static int total_rx;
static struct bt_l2cap_le_chan le_chan;

void sent_cb(struct bt_l2cap_chan *chan)
{
	TEST_FAIL("Tester should not send data");
}

int recv_cb(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	/* TODO: verify data is in sequence */
	LOG_DBG("received %d bytes", buf->len);
	total_rx++;

	return 0;
}

void l2cap_chan_connected_cb(struct bt_l2cap_chan *chan)
{
	LOG_DBG("%p", chan);
}

void l2cap_chan_disconnected_cb(struct bt_l2cap_chan *chan)
{
	LOG_DBG("%p", chan);
}

static struct bt_l2cap_chan_ops ops = {
	.connected = l2cap_chan_connected_cb,
	.disconnected = l2cap_chan_disconnected_cb,
	.recv = recv_cb,
	.sent = sent_cb,
};

int server_accept_cb(struct bt_conn *conn,
		     struct bt_l2cap_server *server,
		     struct bt_l2cap_chan **chan)
{
	memset(&le_chan, 0, sizeof(le_chan));
	le_chan.chan.ops = &ops;
	*chan = &le_chan.chan;

	return 0;
}

static struct bt_l2cap_server test_l2cap_server = {
	.accept = server_accept_cb
};

static int l2cap_server_register(bt_security_t sec_level)
{
	test_l2cap_server.psm = 0;
	test_l2cap_server.sec_level = sec_level;

	int err = bt_l2cap_server_register(&test_l2cap_server);

	TEST_ASSERT(err == 0, "Failed to register l2cap server (err %d)", err);

	return test_l2cap_server.psm;
}

static void acl_connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		LOG_ERR("Failed to connect to %s (0x%02x)", addr, err);
		return;
	}

	LOG_DBG("Connected to %s", addr);
}

static void acl_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("Disconnected from %s (reason 0x%02x)", addr, reason);
}

static struct bt_conn_cb central_cb = {
	.connected = acl_connected,
	.disconnected = acl_disconnected,
};

/* Read the comments on `entrypoint_dut()` first. */
void entrypoint_central(void)
{
	int err;
	struct bt_conn *conn;
	bt_addr_le_t dut;

	/* Mark test as in progress. */
	TEST_START("central");

	/* Initialize Bluetooth */
	err = bt_conn_cb_register(&central_cb);
	TEST_ASSERT(err == 0, "Can't register callbacks (err %d)", err);

	err = bt_enable(NULL);
	TEST_ASSERT(err == 0, "Can't enable Bluetooth (err %d)", err);

	LOG_DBG("Bluetooth initialized");

	while (total_rx < SDU_NUM) {
		err = bt_testlib_scan_find_name(&dut, DUT_NAME);
		TEST_ASSERT(!err, "Failed to start scan (err %d)", err);

		/* Create a connection using that address */
		err = bt_testlib_connect(&dut, &conn);
		TEST_ASSERT(!err, "Failed to initiate connection (err %d)", err);

		LOG_DBG("Connected");

		int psm = l2cap_server_register(BT_SECURITY_L1);

		LOG_DBG("Registered server PSM %x", psm);

		/* Receive in the background */
		k_sleep(K_MSEC(1000));

		/* Disconnect and destroy connection object */
		/* TODO: more chaos */
		err = bt_testlib_disconnect(&conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		TEST_ASSERT(!err, "Failed to disconnect (err %d)", err);

		LOG_DBG("Disconnected");

		k_sleep(K_MSEC(100));
	}

	TEST_PASS("central");
}
