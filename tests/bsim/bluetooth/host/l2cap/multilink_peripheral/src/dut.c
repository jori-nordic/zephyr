/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include "testlib/conn.h"
#include "testlib/scan.h"

#include "babblekit/flags.h"
#include "babblekit/testcase.h"

/* local includes */
#include "data.h"

LOG_MODULE_REGISTER(dut, CONFIG_APP_LOG_LEVEL);

void entrypoint_dut(void)
{
	/* Test purpose:
	 *
	 * For a peripheral device (DUT) that has multiple ACL connections to
	 * central devices: Verify that the data streams on one connection are
	 * not affected by one of the centrals going out of range or not
	 * responding.
	 *
	 * Three devices:
	 * - `dut`: sends that to p0, p1 and p2
	 * - `p0`: maintains an L2CAP credit-based channel link
	 * - `p1`: connects and disconnects randomly
	 * - `p2`: connects and disconnects randomly
	 *
	 * Verdict:
	 * - peer receives notifications #1 and #2
	 */
	int err;
	bt_addr_le_t peer = {};
	struct bt_conn *conn = NULL;

	/* Mark test as in progress. */
	TEST_START("dut");

	/* Initialize Bluetooth */
	err = bt_enable(NULL);
	TEST_ASSERT(err == 0, "Can't enable Bluetooth (err %d)", err);

	LOG_DBG("Bluetooth initialized");

	while (!data_transferred) {
		err = bt_testlib_scan_find_name(&peer, CONFIG_BT_DEVICE_NAME);
		TEST_ASSERT(!err, "Failed to start scan (err %d)", err);

		/* Create a connection using that address */
		err = bt_testlib_connect(&peer, &conn);
		TEST_ASSERT(!err, "Failed to initiate connection (err %d)", err);

		LOG_DBG("Connected");

		/* TODO: establish L2 channel and resume TX */
	}

	TEST_PASS_AND_EXIT("dut");
}
