/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, 4);

static void start_scan(void);

static struct bt_conn *default_conn;

static const struct bt_uuid_128 my_svc_uuid =
	BT_UUID_INIT_128(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78,
			 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const struct bt_uuid_128 my_char_uuid =
	BT_UUID_INIT_128(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78,
			 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static struct bt_uuid_128 uuid;

static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;

static volatile uint8_t counter = 0;

static uint8_t notify_func(struct bt_conn *conn,
			   struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	if (!data) {
		printk("[UNSUBSCRIBED]\n");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	if (((uint8_t *)data)[0] != counter++) {
		printk("%d != %d\n", ((uint8_t *)data)[0], counter-1);
		k_oops();
	}

	/* LOG_HEXDUMP_DBG(data, length, "nt"); */
	printk("cnt %d nt %d len %d\n", counter-1, ((uint8_t*)data)[0], length);

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_func(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	if (!attr) {
		printk("Discover complete\n");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	printk("[ATTRIBUTE] handle %u\n", attr->handle);

	if (!bt_uuid_cmp(discover_params.uuid, &my_svc_uuid.uuid)) {
		memcpy(&uuid, &my_char_uuid, sizeof(my_char_uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	} else if (!bt_uuid_cmp(discover_params.uuid,
				&my_char_uuid.uuid)) {
		memcpy(&uuid, BT_UUID_GATT_CCC, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 2;
		discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			printk("Discover failed (err %d)\n", err);
		}
	} else {
		subscribe_params.notify = notify_func;
		subscribe_params.value = BT_GATT_CCC_NOTIFY;
		subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params);
		if (err && err != -EALREADY) {
			printk("Subscribe failed (err %d)\n", err);
		} else {
			printk("[SUBSCRIBED]\n");
		}

		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_STOP;
}

static bool eir_found(struct bt_data *data, void *user_data)
{
		int err = bt_le_scan_stop();
		if (err) {
			printk("bt_le_scan_stop err: %d\n", err);
		}

		printk("try to connect\n");
		err = bt_conn_le_create(user_data, BT_CONN_LE_CREATE_CONN,
					BT_LE_CONN_PARAM_DEFAULT, &default_conn);
		if (err) {
			printk("Create conn failed (err %d)\n", err);
			start_scan();
		}

		return false;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	if (rssi < -50) return;
	char dev[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, dev, sizeof(dev));
	printk("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i\n",
	       dev, type, ad->len, rssi);

	/* We're only interested in connectable events */
	if (type == BT_GAP_ADV_TYPE_ADV_IND ||
	    type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		bt_data_parse(ad, eir_found, (void *)addr);
	}
}

static void start_scan(void)
{
	int err;

	/* Use active scanning and disable duplicate filtering to handle any
	 * devices that might update their advertising data at runtime. */
	struct bt_le_scan_param scan_param = {
		.type       = BT_LE_SCAN_TYPE_ACTIVE,
		.options    = BT_LE_SCAN_OPT_NONE,
		.interval   = BT_GAP_SCAN_FAST_INTERVAL,
		.window     = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s (%u)\n", addr, conn_err);

		bt_conn_unref(default_conn);
		default_conn = NULL;

		start_scan();
		return;
	}

	printk("Connected: %s\n", addr);

	if (conn == default_conn) {
		memcpy(&uuid, &my_svc_uuid, sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.func = discover_func;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;

		err = bt_gatt_discover(default_conn, &discover_params);
		if (err) {
			printk("Discover failed(err %d)\n", err);
			return;
		}
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);
	counter = 0;

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;

	start_scan();
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
		      uint16_t latency, uint16_t timeout)
{
	printk("New conn interval: %d ms\n", (interval * 5) / 4);
	int err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		printk("bt_conn_set_security err: %d\n", err);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_updated = le_param_updated,
};

void main(void)
{
	int err;
	err = bt_enable(NULL);

	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	start_scan();
}
