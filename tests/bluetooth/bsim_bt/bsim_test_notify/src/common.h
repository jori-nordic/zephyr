/**
 * Common functions and helpers for BSIM GATT tests
 *
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "bs_types.h"
#include "bs_tracing.h"
#include "time_machine.h"
#include "bstests.h"
#include "zephyr/sys/__assert.h"

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <stdint.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

extern enum bst_result_t bst_result;

#define WAIT_TIME (60 * 1e6) /*seconds*/

#define CREATE_FLAG(flag) static atomic_t flag = (atomic_t)false
#define SET_FLAG(flag) (void)atomic_set(&flag, (atomic_t)true)
#define UNSET_FLAG(flag) (void)atomic_set(&flag, (atomic_t)false)
#define WAIT_FOR_FLAG(flag)                                                                        \
	printk("wait set " #flag "\n"); \
	while (!(bool)atomic_get(&flag)) {                                                         \
		(void)k_sleep(K_MSEC(1));                                                          \
	}\
	printk("end wait " #flag "\n");
#define WAIT_FOR_FLAG_UNSET(flag)	  \
	printk("wait unset " #flag "\n"); \
	while ((bool)atomic_get(&flag)) { \
		(void)k_sleep(K_MSEC(1)); \
	}\
	printk("end wait " #flag "\n");

#define FAIL(...)                                                                                  \
	do {                                                                                       \
		bst_result = Failed;                                                               \
		bs_trace_error_time_line(__VA_ARGS__);                                             \
	} while (0)

#define PASS(...)                                                                                  \
	do {                                                                                       \
		bst_result = Passed;                                                               \
		bs_trace_info_time(1, __VA_ARGS__);                                                \
	} while (0)

void test_tick(bs_time_t HW_device_time);
void test_init(void);

static inline bt_addr_le_t peripheral_id_a() {
	bt_addr_le_t addr;
	int err = bt_addr_le_from_str("FC:AF:B0:2F:D2:A8", "random", &addr);
	__ASSERT_NO_MSG(!err);
	return addr;
}

static inline bt_addr_le_t peripheral_id_b() {
	bt_addr_le_t addr;
	int err = bt_addr_le_from_str("FC:75:B3:90:E6:D9", "random", &addr);
	__ASSERT_NO_MSG(!err);
	return addr;
}

static inline bt_addr_le_t central_id() {
	bt_addr_le_t addr;
	int err = bt_addr_le_from_str("FF:98:69:6B:E4:D5", "random", &addr);
	__ASSERT_NO_MSG(!err);
	return addr;
}
