/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include "testlib/adv.h"
#include "testlib/conn.h"

#include "babblekit/flags.h"
#include "babblekit/sync.h"
#include "babblekit/testcase.h"

/* local includes */
#include "data.h"

LOG_MODULE_REGISTER(peer, CONFIG_APP_LOG_LEVEL);

/* Read the comments on `entrypoint_dut()` first. */
void entrypoint_peer(void)
{
	int err;
	struct bt_conn *conn;
	uint16_t handle;

	/* Mark test as in progress. */
	TEST_START("peer");

	/* Initialize Bluetooth */
	err = bt_enable(NULL);
	TEST_ASSERT(err == 0, "Can't enable Bluetooth (err %d)", err);

	LOG_DBG("Bluetooth initialized");

	err = bt_testlib_adv_conn(&conn, BT_ID_DEFAULT, bt_get_name());
	TEST_ASSERT(!err, "Failed to start connectable advertising (err %d)", err);

	/* Disconnect and destroy connection object */
	LOG_DBG("Disconnect");
	err = bt_testlib_disconnect(&conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	TEST_ASSERT(!err, "Failed to disconnect (err %d)", err);

	TEST_PASS("peer");
}
