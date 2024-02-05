/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>


void backchannel_init(void);
void backchannel_sync_send(void);
void backchannel_sync_wait(void);
bool backchannel_sync_received(void);
