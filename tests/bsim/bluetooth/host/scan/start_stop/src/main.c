/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/atomic_builtin.h>
#include <babblekit/testcase.h>
#include <babblekit/sync.h>
#include <babblekit/flags.h>

LOG_MODULE_REGISTER(bt_bsim_scan_start_stop, LOG_LEVEL_DBG);

#define WAIT_TIME_S 60
#define WAIT_TIME   (WAIT_TIME_S * 1e6)

static bt_addr_le_t adv_addr;

static void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		TEST_FAIL("Test failed (not passed after %d seconds)\n", WAIT_TIME_S);
	}
}

static void test_init(void)
{
	bst_ticker_set_next_tick_absolute(WAIT_TIME);
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];

	k_msleep(500); 		/* https://thedailywtf.com/articles/The-Speedup-Loop */
	memcpy(&adv_addr, addr, sizeof(adv_addr));

	bt_addr_le_to_str(&adv_addr, addr_str, sizeof(addr_str));
	LOG_DBG("Device found: %s (RSSI %d), type %u, AD data len %u",
	       addr_str, rssi, type, ad->len);
}

#define BT_LE_SCAN_ACTIVE_CONTINUOUS_BUT_NOT_DUPLICATE BT_LE_SCAN_PARAM(BT_LE_SCAN_TYPE_ACTIVE, \
									0, \
									BT_GAP_SCAN_FAST_INTERVAL_MIN, \
									BT_GAP_SCAN_FAST_WINDOW)

void run_dut(void)
{
	LOG_DBG("start");
	int err;

	LOG_DBG("Starting DUT");

	err = bt_enable(NULL);
	TEST_ASSERT(!err, "Bluetooth init failed (err %d)\n", err);

	LOG_DBG("Bluetooth initialised");

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CONTINUOUS_BUT_NOT_DUPLICATE, device_found);
	TEST_ASSERT(!err, "Scanner setup failed (err %d)\n", err);
	LOG_DBG("Explicit scanner started");

	k_sleep(K_SECONDS(40));

	TEST_PASS("Test passed (DUT)\n");
}

void run_tester(void)
{
	LOG_DBG("start");

	int err;

	LOG_DBG("Starting DUT");

	err = bt_enable(NULL);
	TEST_ASSERT(!err, "Bluetooth init failed (err %d)\n", err);

	LOG_DBG("Bluetooth initialised");

	struct bt_le_ext_adv *per_adv;

	struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_EXT_ADV,
								BT_GAP_ADV_FAST_INT_MIN_1,
								BT_GAP_ADV_FAST_INT_MAX_1,
								NULL);

	err = bt_le_ext_adv_create(&adv_param, NULL, &per_adv);
	TEST_ASSERT(!err, "Failed to create advertising set: %d", err);
	LOG_DBG("Created extended advertising set.");

	err = bt_le_ext_adv_start(per_adv, BT_LE_EXT_ADV_START_DEFAULT);
	TEST_ASSERT(!err, "Failed to start extended advertising: %d", err);
	LOG_DBG("Started extended advertising.");

	TEST_PASS("Tester done");
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "scanner",
		.test_descr = "SCANNER",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = run_dut,
	},
	{
		.test_id = "periodic_adv",
		.test_descr = "PER_ADV",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = run_tester,
	},
	BSTEST_END_MARKER
};

struct bst_test_list *test_scan_start_stop_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}

bst_test_install_t test_installers[] = {test_scan_start_stop_install, NULL};

int main(void)
{
	bst_main();
	return 0;
}
