/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include "testlib/conn.h"
#include "testlib/scan.h"
#include "testlib/log_utils.h"

#include "babblekit/flags.h"
#include "babblekit/testcase.h"

/* local includes */
#include "data.h"

LOG_MODULE_REGISTER(dut, LOG_LEVEL_DBG);

static DEFINE_FLAG(is_subscribed);
static DEFINE_FLAG(sent_all_frags);
static DEFINE_FLAG(mtu_has_been_exchanged);
static K_SEM_DEFINE(first_frag, 0, 1);

extern unsigned long runtime_log_level;

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	/* assume we only get it for the `test_gatt_service` */
	if (value != 0) {
		SET_FLAG(is_subscribed);
	} else {
		UNSET_FLAG(is_subscribed);
	}
}

BT_GATT_SERVICE_DEFINE(test_gatt_service, BT_GATT_PRIMARY_SERVICE(test_service_uuid),
		       BT_GATT_CHARACTERISTIC(test_characteristic_uuid,
					      (BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
					       BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_INDICATE),
					      BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL, NULL,
					      NULL),
		       BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),);

static void _mtu_exchanged(struct bt_conn *conn, uint8_t err,
			   struct bt_gatt_exchange_params *params)
{
	LOG_ERR("MTU exchanged");
	SET_FLAG(mtu_has_been_exchanged);
}

static void exchange_mtu(struct bt_conn *conn)
{
	int err;
	struct bt_gatt_exchange_params params = {
		.func = _mtu_exchanged,
	};

	UNSET_FLAG(mtu_has_been_exchanged);

	err = bt_gatt_exchange_mtu(conn, &params);
	TEST_ASSERT(!err, "Failed MTU exchange (err %d)", err);

	WAIT_FOR_FLAG(mtu_has_been_exchanged);
}

int __real_bt_send(struct net_buf *buf);

#define NUM_FRAGS_PER_NOTIFICATION (GATT_PAYLOAD_SIZE + 1 + 4 + (4 * ))

static size_t expect_len;
static size_t curr_len;

/* TODO: maybe a cleaner way of doing DPI? */
static void set_flags(size_t length)
{
	/* Any action attempted by the main thread will have to wait until the
	 * TX processor is done. That means that even if technically the
	 * controller hasn't gotten the current frag yet, in practice we can
	 * consider it has, as the TX processor runs from a cooperative
	 * execution priority.
	 */
	curr_len += length;

	if (curr_len == length) {
		LOG_ERR("first");
		k_sem_give(&first_frag);
	}

	if (curr_len == expect_len) {
		LOG_ERR("last frag");
		SET_FLAG(sent_all_frags);
	}
}

int __wrap_bt_send(struct net_buf *buf)
{
	if (bt_buf_get_type(buf) == BT_BUF_ACL_OUT) {
		struct bt_hci_acl_hdr *acl;
		uint16_t handle;
		uint16_t len;

		acl = (void*)buf->data;
		len = sys_le16_to_cpu(acl->len);
		handle = sys_le16_to_cpu(acl->handle);

		set_flags(len);
	}

	return __real_bt_send(buf);
}

const uint8_t notification_data[GATT_PAYLOAD_SIZE];

void bt_conn_suspend_tx(bool suspend);

static void test_iteration(bt_addr_le_t *peer)
{
	int err;
	struct bt_conn *conn = NULL;
	const struct bt_gatt_attr *attr;

	/* Create a connection using that address */
	err = bt_testlib_connect(peer, &conn);
	TEST_ASSERT(!err, "Failed to initiate connection (err %d)", err);

	LOG_DBG("Connected");

	LOG_INF("Wait until peer subscribes");
	UNSET_FLAG(is_subscribed);
	WAIT_FOR_FLAG(is_subscribed);

	/* Prepare data for notifications
	 * attrs[0] is our service declaration
	 * attrs[1] is our characteristic declaration
	 * attrs[2] is our characteristic value
	 *
	 * We store a pointer for the characteristic value as that is the
	 * value we want to notify later.
	 *
	 * We could alternatively use `bt_gatt_notify_uuid()`.
	 */
	attr = &test_gatt_service.attrs[2];

	exchange_mtu(conn);

	LOG_INF("Send notification #1");
	LOG_HEXDUMP_DBG(notification_data, sizeof(notification_data), "Notification payload");

	curr_len = 0;
	expect_len = GATT_PAYLOAD_SIZE + 1 + 2 + 4;

	err = bt_gatt_notify(conn, attr, notification_data, sizeof(notification_data));
	TEST_ASSERT(!err, "Failed to send notification: err %d", err);

	k_sem_take(&first_frag, K_FOREVER);

	UNSET_FLAG(sent_all_frags);
	WAIT_FOR_FLAG(sent_all_frags);

	curr_len = 0;
	expect_len = GATT_PAYLOAD_SIZE + 1 + 2 + 4;

	err = bt_gatt_notify(conn, attr, notification_data, sizeof(notification_data));
	TEST_ASSERT(!err, "Failed to send notification: err %d", err);

	/* Use a semaphore instead of a flag to be sure we get scheduled right away. */
	k_sem_take(&first_frag, K_FOREVER);
	bt_conn_suspend_tx(true);

	/* Disconnect and destroy connection object */
	LOG_INF("Disconnect");
	err = bt_testlib_disconnect(&conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	TEST_ASSERT(!err, "Failed to disconnect (err %d)", err);

	bt_conn_suspend_tx(false);
}

void entrypoint_dut(void)
{
	/* Test purpose:
	 *
	 * Verifies that we don't leak resources or mess up host state when a
	 * disconnection happens whilst the host is transmitting ACL fragments.
	 *
	 * To achieve that, we use hook into L2CAP to allow us to disconnect
	 * before we have sent all the ACL fragments to the controller.
	 *
	 * Two devices:
	 * - `dut`: the device whose host we are testing
	 * - `peer`: anime side-character. not important.
	 *
	 * Procedure (for n iterations):
	 * - [dut] establish connection to `peer`
	 * - [peer] discover GATT and subscribe to the test characteristic
	 * - [dut] send long notification
	 * - [peer] wait for notification
	 * - [dut] send another long notification
	 * - [dut] disconnect
	 *
	 * [verdict]
	 * - All test cycles complete
	 */
	int err;
	bt_addr_le_t peer = {};

	/* Mark test as in progress. */
	TEST_START("dut");

	/* Set the log level given by the `log_level` CLI argument */
	bt_testlib_log_level_set("dut", runtime_log_level);

	/* Initialize Bluetooth */
	err = bt_enable(NULL);
	TEST_ASSERT(err == 0, "Can't enable Bluetooth (err %d)", err);

	LOG_DBG("Bluetooth initialized");

	/* Find the address of the peer. In our case, both devices are actually
	 * the same executable (with the same config) but executed with
	 * different arguments. We can then just use CONFIG_BT_DEVICE_NAME which
	 * contains our device name in string form.
	 */
	err = bt_testlib_scan_find_name(&peer, CONFIG_BT_DEVICE_NAME);
	TEST_ASSERT(!err, "Failed to start scan (err %d)", err);

	for (int i = 0; i < TEST_ITERATIONS; i++) {
		LOG_INF("## Iteration %d", i);
		test_iteration(&peer);
	}

	TEST_PASS("dut");
}
