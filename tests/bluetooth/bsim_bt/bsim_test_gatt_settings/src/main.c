/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "utils.h"
#include "bstests.h"

void server_procedure(void);
void client_procedure(void);

#define BS_SECONDS(dur_sec)    ((bs_time_t)dur_sec * USEC_PER_SEC)
#define TEST_TIMEOUT_SIMULATED BS_SECONDS(60)

static int test_round;
static int total_rounds;
static char * settings_file;

int get_test_round(void)
{
	return test_round;
}

bool is_final_round(void)
{
	return test_round == total_rounds - 1;
}

char *get_settings_file(void)
{
	return settings_file;
}

static void test_args(int argc, char **argv)
{
	ASSERT(argc == 3, "Please specify only 3 test arguments\n");

	test_round = atol(argv[0]);
	total_rounds = atol(argv[1]);
	settings_file = argv[2];

	bs_trace_raw(0, "Test round %u\n", test_round);
	bs_trace_raw(0, "Total rounds %u\n", total_rounds);
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
		.test_id = "server",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = server_procedure,
		.test_args_f = test_args,
	},
	{
		.test_id = "client",
		.test_pre_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = client_procedure,
		.test_args_f = test_args,
	},
	BSTEST_END_MARKER,
};

static struct bst_test_list *install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_to_add);
};

bst_test_install_t test_installers[] = { install, NULL };

void main(void)
{
	bst_main();
}
