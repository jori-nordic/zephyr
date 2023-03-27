/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* An SDU should fit in 3 PDUs */
#define L2CAP_MTU 150
#define L2CAP_MPS 30
#define L2CAP_PSM 0x0080
/* use the first dynamic channel ID */
#define L2CAP_CID 0x0040
