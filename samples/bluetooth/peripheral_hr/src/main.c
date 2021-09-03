/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/printk.h>
#include <sys/byteorder.h>
#include <zephyr.h>
#include <logging/log.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

LOG_MODULE_REGISTER(main);

#define TEST_SERVICE_UUID        BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define TEST_CHARACTERISTIC_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)
#define TEST_DESCRIPTOR_UUID     BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      TEST_SERVICE_UUID)
};

#define DAT_LEN 512
char test_gatt_data[DAT_LEN] = {0x01};

static ssize_t read_current_time(struct bt_conn *conn,
				 const struct bt_gatt_attr *attr, void *buf,
				 uint16_t len, uint16_t offset)
{
	LOG_INF("Read attribute");
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 test_gatt_data,
				 sizeof(test_gatt_data));
}

static ssize_t write_current_time(struct bt_conn *conn,
				  const struct bt_gatt_attr *attr,
				  const void *buf, uint16_t len,
				  uint16_t offset, uint8_t flags)
{
	LOG_INF("Write attribute");

	if(len > DAT_LEN) {
		LOG_INF("Len too high, truncating");
		len = DAT_LEN;
	}
	memcpy(test_gatt_data, buf, len);

	LOG_HEXDUMP_INF(test_gatt_data, len, "Processed data: ");

	return len;
}

void on_gatt_ccc_changed_cb(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
	printk("CCC changed\n");
}

BT_GATT_SERVICE_DEFINE(
	cts,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(TEST_SERVICE_UUID)),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(TEST_CHARACTERISTIC_UUID),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       read_current_time, write_current_time,
			       (void*)test_gatt_data),
	BT_GATT_DESCRIPTOR(BT_UUID_DECLARE_128(TEST_DESCRIPTOR_UUID), BT_GATT_PERM_READ,
			   NULL, NULL, NULL)
);

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(void)
{
	int err;

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

void main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_ready();
	memcpy(test_gatt_data, "Hello world !", sizeof("Hello world !"));

	bt_conn_cb_register(&conn_callbacks);
	bt_conn_auth_cb_register(&auth_cb_display);

	/* Implement notification. At the moment there is no suitable way
	 * of starting delayed work so we do it here
	 */
	while (1) {
		k_sleep(K_SECONDS(1));

	}
}
