/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/ipm.h>

#include <ipc/rpmsg_service.h>
#include <nrf.h>
#include <nrfx.h>

#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_HCI_DRIVER)
#define LOG_MODULE_NAME bt_hci_driver_nrf53
#include "common/log.h"

void bt_rpmsg_rx(uint8_t *data, size_t len);

static K_SEM_DEFINE(ready_sem, 0, 1);
static K_SEM_DEFINE(rx_sem, 0, 1);

BUILD_ASSERT(CONFIG_HEAP_MEM_POOL_SIZE >= 1024,
	"Not enough heap memory for RPMsg queue allocation");

static int endpoint_id;

static int endpoint_cb(struct rpmsg_endpoint *ept, void *data, size_t len,
	uint32_t src, void *priv)
{
	BT_DBG("Received message of %u bytes.", len);
	BT_HEXDUMP_DBG((uint8_t *)data, len, "Data:");

NRF_P1_S->OUTSET = 1<<4;
	bt_rpmsg_rx(data, len);
NRF_P1_S->OUTCLR = 1<<4;

	return RPMSG_SUCCESS;
}

int bt_rpmsg_platform_init(void)
{
	int err;

	printk("##### hello from RPMSG driver\n");
	err = rpmsg_service_register_endpoint("nrf_bt_hci", endpoint_cb);

	if (err < 0) {
		LOG_ERR("Registering endpoint failed with %d", err);
		return RPMSG_ERR_INIT;
	}

	endpoint_id = err;

	return RPMSG_SUCCESS;
}

int bt_rpmsg_platform_send(struct net_buf *buf)
{
NRF_P1_S->OUTSET = 1<<8;
	int err = rpmsg_service_send(endpoint_id, buf->data, buf->len);
NRF_P1_S->OUTCLR = 1<<8;
	return err;
}

int bt_rpmsg_platform_endpoint_is_bound(void)
{
	return rpmsg_service_endpoint_is_bound(endpoint_id);
}
