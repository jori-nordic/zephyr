/*
 * Copyright (c) 2023 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unistd.h>

#include <zephyr/bluetooth/buf.h>

void bt_btsnoop_write(enum bt_buf_type buf_type, const void *data, size_t len);