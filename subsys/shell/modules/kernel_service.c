/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/printk.h>
#include <shell/shell.h>
#include <init.h>
#include <sys/reboot.h>
#include <debug/stack.h>
#include <string.h>
#include <device.h>
#include <drivers/timer/system_timer.h>
#include <kernel.h>
#include <kernel_internal.h>
#include <stdlib.h>
#if defined(CONFIG_LOG_RUNTIME_FILTERING)
#include <zephyr/logging/log_ctrl.h>
#endif

static int cmd_kernel_version(const struct shell *shell,
			      size_t argc, char **argv)
{
	uint32_t version = sys_kernel_version_get();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Zephyr version %d.%d.%d",
		      SYS_KERNEL_VER_MAJOR(version),
		      SYS_KERNEL_VER_MINOR(version),
		      SYS_KERNEL_VER_PATCHLEVEL(version));
	return 0;
}

static int cmd_kernel_uptime(const struct shell *shell,
			     size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Uptime: %u ms", k_uptime_get_32());
	return 0;
}

static int cmd_kernel_cycles(const struct shell *shell,
			      size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "cycles: %u hw cycles", k_cycle_get_32());
	return 0;
}

#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO) && \
	defined(CONFIG_THREAD_MONITOR)
static void shell_tdata_dump(const struct k_thread *cthread, void *user_data)
{
	struct k_thread *thread = (struct k_thread *)cthread;
	const struct shell *shell = (const struct shell *)user_data;
	unsigned int pcnt;
	size_t unused;
	size_t size = thread->stack_info.size;
	const char *tname;
	int ret;

#ifdef CONFIG_THREAD_RUNTIME_STATS
	k_thread_runtime_stats_t rt_stats_thread;
	k_thread_runtime_stats_t rt_stats_all;
#endif

	tname = k_thread_name_get(thread);

	shell_print(shell, "%s%p %-10s",
		      (thread == k_current_get()) ? "*" : " ",
		      thread,
		      tname ? tname : "NA");
	/* Cannot use lld as it's less portable. */
	shell_print(shell, "\toptions: 0x%x, priority: %d timeout: %" PRId64,
		      thread->base.user_options,
		      thread->base.prio,
		      (int64_t)thread->base.timeout.dticks);
	shell_print(shell, "\tstate: %s, entry: %p", k_thread_state_str(thread),
		    thread->entry.pEntry);

#ifdef CONFIG_THREAD_RUNTIME_STATS
	ret = 0;

	if (k_thread_runtime_stats_get(thread, &rt_stats_thread) != 0) {
		ret++;
	}

	if (k_thread_runtime_stats_all_get(&rt_stats_all) != 0) {
		ret++;
	}

	if (ret == 0) {
		pcnt = (rt_stats_thread.execution_cycles * 100U) /
		       rt_stats_all.execution_cycles;

		/*
		 * z_prf() does not support %llu by default unless
		 * CONFIG_MINIMAL_LIBC_LL_PRINTF=y. So do conditional
		 * compilation to avoid blindly enabling this kconfig
		 * so it won't increase RAM/ROM usage too much on 32-bit
		 * targets.
		 */
		shell_print(shell, "\tTotal execution cycles: %u (%u %%)",
			    (uint32_t)rt_stats_thread.execution_cycles,
			    pcnt);
#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
		shell_print(shell, "\tCurrent execution cycles: %u",
			    (uint32_t)rt_stats_thread.current_cycles);
		shell_print(shell, "\tPeak execution cycles: %u",
			    (uint32_t)rt_stats_thread.peak_cycles);
		shell_print(shell, "\tAverage execution cycles: %u",
			    (uint32_t)rt_stats_thread.average_cycles);
#endif
	} else {
		shell_print(shell, "\tTotal execution cycles: ? (? %%)");
#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
		shell_print(shell, "\tCurrent execution cycles: ?");
		shell_print(shell, "\tPeak execution cycles: ?");
		shell_print(shell, "\tAverage execution cycles: ?");
#endif
	}
#endif

	ret = k_thread_stack_space_get(thread, &unused);
	if (ret) {
		shell_print(shell,
			    "Unable to determine unused stack size (%d)\n",
			    ret);
	} else {
		/* Calculate the real size reserved for the stack */
		pcnt = ((size - unused) * 100U) / size;

		shell_print(shell,
			    "\tstack size %zu, unused %zu, usage %zu / %zu (%u %%)\n",
			    size, unused, size - unused, size, pcnt);
	}

}

static int cmd_kernel_threads(const struct shell *shell,
			      size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(shell, "Scheduler: %u since last call", sys_clock_elapsed());
	shell_print(shell, "Threads:");
	k_thread_foreach(shell_tdata_dump, (void *)shell);
	return 0;
}

static void shell_stack_dump(const struct k_thread *thread, void *user_data)
{
	const struct shell *shell = (const struct shell *)user_data;
	unsigned int pcnt;
	size_t unused;
	size_t size = thread->stack_info.size;
	const char *tname;
	int ret;

	ret = k_thread_stack_space_get(thread, &unused);
	if (ret) {
		shell_print(shell,
			    "Unable to determine unused stack size (%d)\n",
			    ret);
		return;
	}

	tname = k_thread_name_get((struct k_thread *)thread);

	/* Calculate the real size reserved for the stack */
	pcnt = ((size - unused) * 100U) / size;

	shell_print((const struct shell *)user_data,
		"%p %-10s (real size %zu):\tunused %zu\tusage %zu / %zu (%u %%)",
		      thread,
		      tname ? tname : "NA",
		      size, unused, size - unused, size, pcnt);
}

extern K_KERNEL_STACK_ARRAY_DEFINE(z_interrupt_stacks, CONFIG_MP_NUM_CPUS,
				   CONFIG_ISR_STACK_SIZE);

static int cmd_kernel_stacks(const struct shell *shell,
			     size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	k_thread_foreach(shell_stack_dump, (void *)shell);

	/* Placeholder logic for interrupt stack until we have better
	 * kernel support, including dumping arch-specific exception-related
	 * stack buffers.
	 */
	for (int i = 0; i < CONFIG_MP_NUM_CPUS; i++) {
		size_t unused;
		const uint8_t *buf = Z_KERNEL_STACK_BUFFER(z_interrupt_stacks[i]);
		size_t size = K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[i]);
		int err = z_stack_space_get(buf, size, &unused);

		(void)err;
		__ASSERT_NO_MSG(err == 0);

		shell_print(shell,
			"%p IRQ %02d     (real size %zu):\tunused %zu\tusage %zu / %zu (%zu %%)",
			      &z_interrupt_stacks[i], i, size, unused,
			      size - unused, size,
			      ((size - unused) * 100U) / size);
	}

	return 0;
}
#endif

static int cmd_kernel_sleep(const struct shell *shell,
			    size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	k_msleep(strtoul(argv[1], NULL, 10));

	return 0;
}

#if defined(CONFIG_LOG_RUNTIME_FILTERING)
static int16_t log_source_id_get(const char *name)
{
	int16_t count = (int16_t)log_src_cnt_get(CONFIG_LOG_DOMAIN_ID);

	for (int16_t i = 0; i < count; i++) {
		if (strcmp(log_source_name_get(CONFIG_LOG_DOMAIN_ID, i), name)
		    == 0) {
			return i;
		}
	}
	return -1;
}

static int cmd_kernel_log_level_set(const struct shell *shell,
				    size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint8_t severity = strtoul(argv[2], NULL, 10);
	if (severity > 5) {
		shell_error(shell, "Invalid log level: %d", severity);
		shell_help(shell);
		return SHELL_CMD_HELP_PRINTED;
	}

	int16_t source_id = log_source_id_get(argv[1]);
	if (source_id == -1) {
		shell_error(shell, "Unable to find log source: %s", argv[1]);
	}

	log_filter_set(NULL, 0, source_id, severity);

	return 0;
}
#endif

#if defined(CONFIG_REBOOT)
static int cmd_kernel_reboot_warm(const struct shell *shell,
				  size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
#if (CONFIG_KERNEL_SHELL_REBOOT_DELAY > 0)
	k_sleep(K_MSEC(CONFIG_KERNEL_SHELL_REBOOT_DELAY));
#endif
	sys_reboot(SYS_REBOOT_WARM);
	return 0;
}

static int cmd_kernel_reboot_cold(const struct shell *shell,
				  size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
#if (CONFIG_KERNEL_SHELL_REBOOT_DELAY > 0)
	k_sleep(K_MSEC(CONFIG_KERNEL_SHELL_REBOOT_DELAY));
#endif
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_kernel_reboot,
	SHELL_CMD(cold, NULL, "Cold reboot.", cmd_kernel_reboot_cold),
	SHELL_CMD(warm, NULL, "Warm reboot.", cmd_kernel_reboot_warm),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(sub_kernel,
	SHELL_CMD(cycles, NULL, "Kernel cycles.", cmd_kernel_cycles),
#if defined(CONFIG_REBOOT)
	SHELL_CMD(reboot, &sub_kernel_reboot, "Reboot.", NULL),
#endif
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO) && \
		defined(CONFIG_THREAD_MONITOR)
	SHELL_CMD(stacks, NULL, "List threads stack usage.", cmd_kernel_stacks),
	SHELL_CMD(threads, NULL, "List kernel threads.", cmd_kernel_threads),
#endif
	SHELL_CMD(uptime, NULL, "Kernel uptime.", cmd_kernel_uptime),
	SHELL_CMD(version, NULL, "Kernel version.", cmd_kernel_version),
	SHELL_CMD_ARG(sleep, NULL, "ms", cmd_kernel_sleep, 2, 0),
#if defined(CONFIG_LOG_RUNTIME_FILTERING)
	SHELL_CMD_ARG(log-level, NULL, "<module name> <severity (0-4)>", cmd_kernel_log_level_set, 3, 0),
#endif
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(kernel, &sub_kernel, "Kernel commands", NULL);
