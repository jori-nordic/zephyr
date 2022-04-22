/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ZEPHYR_H_
#define ZEPHYR_INCLUDE_ZEPHYR_H_

/*
 * Applications can identify whether they are built for Zephyr by
 * macro below. (It may be already defined by a makefile or toolchain.)
 */
#ifndef __ZEPHYR__
#define __ZEPHYR__
#endif

#include <kernel.h>

#define GP0 (1 << 4)		/* rpmsg send */
#define GP1 (1 << 5)		/* address -> disabled */
#define GP2 (1 << 6)		/* hci_driver_receive_process */
#define GP3 (1 << 7)		/* rpmsg receive (APP) */
#define GP4 (1 << 8)
#define GP5 (1 << 9)
#define GPX (GP0 | GP1 | GP2 | GP3 | GP4 | GP5)
#define GPNET (GP0 | GP1 | GP2)
#define GPAPP (GP3)

#endif /* ZEPHYR_INCLUDE_ZEPHYR_H_ */
