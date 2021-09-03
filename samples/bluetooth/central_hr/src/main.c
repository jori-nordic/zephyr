/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <errno.h>
#include <zephyr.h>
#include <sys/printk.h>
#include <logging/log.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <sys/byteorder.h>

LOG_MODULE_REGISTER(main);

#define TEST_SERVICE_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define TEST_CHARACTERISTIC_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)

static void start_scan(void);

static struct bt_conn *default_conn;

static struct bt_uuid_128 uuid = BT_UUID_INIT_128(0);
static struct bt_gatt_discover_params discover_params;

uint32_t char_handle = 0;

static uint8_t discover_func(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	if (!attr) {
		LOG_INF("No more attributes, discovery complete");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	LOG_INF("[ATTRIBUTE] handle %u", attr->handle);

	if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_DECLARE_128(TEST_SERVICE_UUID))) {
		memcpy(&uuid, BT_UUID_DECLARE_128(TEST_CHARACTERISTIC_UUID), sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.start_handle = attr->handle + 1;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		LOG_INF("Discovered service");

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_INF("Discover failed (err %d)", err);
		}
	} else if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_DECLARE_128(TEST_CHARACTERISTIC_UUID))) {
		char_handle = attr->handle +1;
		LOG_INF("Discovered test characteristic 0x%x", char_handle);
		LOG_INF("Discovery complete");
	}

	return BT_GATT_ITER_STOP;
}

static bool eir_found(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;

	switch (data->type) {
	case BT_DATA_UUID128_ALL:
		if(data->data_len < 16) return false;
		LOG_INF("UUID128, len %d", data->data_len);

		struct bt_le_conn_param *param;
		struct bt_uuid_128 uuid;
		int err;

		bt_uuid_create(&uuid.uuid, data->data, 16);
		LOG_HEXDUMP_INF(&uuid.val[0], data->data_len, "Adv data: ");

		if (bt_uuid_cmp(&uuid.uuid, BT_UUID_DECLARE_128(TEST_SERVICE_UUID)))
			return false;

		LOG_INF("Compare OK");

		err = bt_le_scan_stop();
		if (err) {
			LOG_INF("Stop LE scan failed (err %d)", err);
			return false;
		}

		param = BT_LE_CONN_PARAM_DEFAULT;
		err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
					param, &default_conn);
		if (err) {
			LOG_INF("Create conn failed (err %d)", err);
			start_scan();
		}

		return false;
	}

	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, dev, sizeof(dev));

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
		LOG_INF("Scanning failed to start (err %d)", err);
		return;
	}

	LOG_INF("Scanning successfully started");
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		LOG_INF("Failed to connect to %s (%u)", addr, conn_err);

		bt_conn_unref(default_conn);
		default_conn = NULL;

		start_scan();
		return;
	}

	LOG_INF("Connected: %s", log_strdup(addr));

	if (conn == default_conn) {
		memcpy(&uuid, BT_UUID_DECLARE_128(TEST_SERVICE_UUID), sizeof(uuid));
		discover_params.uuid = &uuid.uuid;
		discover_params.func = discover_func;
		discover_params.start_handle = BT_ATT_FIRST_ATTTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTTRIBUTE_HANDLE;
		discover_params.type = BT_GATT_DISCOVER_PRIMARY;

		err = bt_gatt_discover(default_conn, &discover_params);
		if (err) {
			LOG_INF("Discover failed(err %d)", err);
			return;
		}
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason 0x%02x)", log_strdup(addr), reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;
	char_handle = 0;

	start_scan();
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

#define DAT_LEN 512
static uint8_t gatt_data[DAT_LEN] = {0};

void gatt_cb(struct bt_conn *conn, uint8_t err,
	     struct bt_gatt_write_params *params)
{
	LOG_INF("Gatt write callback: err %d", err);
}

uint8_t gatt_read_cb(struct bt_conn *conn, uint8_t err,
		     struct bt_gatt_read_params *params,
		     const void *data, uint16_t length)
{
	LOG_INF("Gatt read callback: err %d len %d", err, length);

	memcpy(gatt_data, data, length);
	LOG_HEXDUMP_INF(gatt_data, length, "Read data: ");

	return BT_GATT_ITER_STOP;
}

static void gen_data(uint8_t* buf, uint16_t len)
{
	for(int i=0; i<len; i++) {
		buf[i] = (i % 256) & 0xFF;
	}
}

static bool big_mtu = false;

void gatt_mtu_cb(struct bt_conn *conn, uint8_t err,
		 struct bt_gatt_exchange_params *params)
{
	LOG_INF("MTU exchange callback");
	big_mtu = true;
}

void main(void)
{
	int err;
	err = bt_enable(NULL);
	struct bt_gatt_write_params gatt_params;
	struct bt_gatt_read_params gatt_read_params;
	struct bt_gatt_exchange_params gatt_mtu_params;

	if (err) {
		LOG_INF("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_INF("Bluetooth initialized");

	bt_conn_cb_register(&conn_callbacks);

	start_scan();

	while(1)
	{
		/* Wait for discovery to complete */
		while(!char_handle) {k_msleep(2000);};

		/* Prepare gatt write */
		gatt_params.data = gatt_data;
		gatt_params.handle = char_handle;
		gatt_params.length = 190;
		gatt_params.offset = 0;
		gatt_params.func = gatt_cb;

		/* Prepare gatt read */
		gatt_read_params.func = gatt_read_cb;
		gatt_read_params.handle_count = 1;
		gatt_read_params.single.handle = char_handle;
		gatt_read_params.single.offset = 0;

		/* Increase MTU */
		gatt_mtu_params.func = gatt_mtu_cb;
		bt_gatt_exchange_mtu(default_conn, &gatt_mtu_params);

		while(!big_mtu) {k_msleep(2000);}

		LOG_INF("Entering main loop");
		LOG_INF("Char handle: 0x%x", char_handle);
		while(char_handle) { /* Stops when disconnected */
			LOG_INF("Generate");
			gen_data(gatt_data, sizeof(gatt_data));
			LOG_INF("Write");
			bt_gatt_write(default_conn, &gatt_params);
			LOG_INF("Read");
			bt_gatt_read(default_conn, &gatt_read_params);

			LOG_INF("Clear");
			memset(gatt_data, 0x00, sizeof(gatt_data));
			LOG_INF("Write");
			bt_gatt_write(default_conn, &gatt_params);
			LOG_INF("Read");
			bt_gatt_read(default_conn, &gatt_read_params);
			k_msleep(10000);
		}
	}
}
