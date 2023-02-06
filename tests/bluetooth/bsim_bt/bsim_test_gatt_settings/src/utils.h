/**
 * Common functions and helpers for BSIM ADV tests
 *
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bs_tracing.h"
#include "bs_types.h"
#include "bstests.h"
#include "time_machine.h"
#include "zephyr/sys/__assert.h"

#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

extern enum bst_result_t bst_result;

#define DECLARE_FLAG(flag) extern atomic_t flag
#define DEFINE_FLAG(flag)  atomic_t flag = (atomic_t) false
#define SET_FLAG(flag)	   (void)atomic_set(&flag, (atomic_t) true)
#define UNSET_FLAG(flag)   (void)atomic_set(&flag, (atomic_t) false)
#define WAIT_FOR_FLAG(flag)                                                                        \
	while (!(bool)atomic_get(&flag)) {                                                         \
		(void)k_sleep(K_MSEC(1));                                                          \
	}
#define WAIT_FOR_FLAG_UNSET(flag)                                                                  \
	while ((bool)atomic_get(&flag)) {                                                          \
		(void)k_sleep(K_MSEC(1));                                                          \
	}
#define TAKE_FLAG(flag)                                                                            \
	while (!(bool)atomic_cas(&flag, true, false)) {                                            \
		(void)k_sleep(K_MSEC(1));                                                          \
	}

#define ASSERT(expr, ...)                                                                          \
	do {                                                                                       \
		if (!(expr)) {                                                                     \
			FAIL(__VA_ARGS__);                                                         \
		}                                                                                  \
	} while (0)

DECLARE_FLAG(flag_test_end);

#define FAIL(...)					\
	SET_FLAG(flag_test_end);			\
	do {						\
		bst_result = Failed;			\
		bs_trace_error_time_line(__VA_ARGS__);	\
	} while (0)

#define PASS(...)					\
	SET_FLAG(flag_test_end);			\
	do {						\
		bst_result = Passed;			\
		bs_trace_info_time(1, __VA_ARGS__);	\
	} while (0)

void test_tick(bs_time_t HW_device_time);
void test_init(void);
void backchannel_init(void);
void signal_next_test_round(void);
void wait_for_round_start(void);

struct bt_conn* connect_as_central(void);
struct bt_conn* connect_as_peripheral(void);
void disconnect(struct bt_conn *conn);
void wait_disconnected(void);
struct bt_conn* get_conn(void);

void set_security(struct bt_conn *conn, bt_security_t sec);
void wait_secured(void);
void bond(struct bt_conn *conn);
void wait_bonded(void);

void gatt_register_service(bool other);
void gatt_discover(void);
void activate_robust_caching(void);
void read_test_char(bool expect_err);

char *get_settings_file(void);
int get_test_round(void);
void signal_next_test_round(void);
void wait_for_round_start(void);

bool is_final_round(void);
