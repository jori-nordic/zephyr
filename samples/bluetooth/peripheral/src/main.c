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
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#define NOTIFICATIONS_SPAM_THREAD_STACK_SIZE 1024
#define NOTIFICATIONS_SPAM_THREAD_PRIORITY 11

K_THREAD_STACK_DEFINE(notifications_spam_thread_stack_area, NOTIFICATIONS_SPAM_THREAD_STACK_SIZE);
static struct k_thread notifications_spam_thread_id;

static struct bt_conn *default_conn = NULL;
static volatile uint8_t counter = 0;

static void advertise(void)
{
	struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	};

	int err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{

	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		printk("Connected\n");
		default_conn = bt_conn_ref(conn);
		counter = 0;
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);

	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
}

void security_changed(struct bt_conn *conn, bt_security_t level,
		      enum bt_security_err err)
{
	if (err != BT_SECURITY_ERR_SUCCESS) {
		printk("Pairing failed with: %d\n", err);
	} else {
		printk("Security Changed: L%d\n", level);
	}
}

void le_param_updated(struct bt_conn *conn, uint16_t interval,
		      uint16_t latency, uint16_t timeout)
{
	printk("New conn interval: %d ms\n", (interval * 5) / 4);
	printk("spam_start\n");
	k_thread_resume(&notifications_spam_thread_id);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
	.le_param_updated = le_param_updated,
};

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
	.cancel = auth_cancel,
};

static struct bt_uuid_128 my_svc_uuid =
	BT_UUID_INIT_128(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78,
			 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const struct bt_uuid_128 my_char_uuid = BT_UUID_INIT_128(
	0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

BT_GATT_SERVICE_DEFINE(my_svc,
	BT_GATT_PRIMARY_SERVICE(&my_svc_uuid),
	BT_GATT_CHARACTERISTIC(&my_char_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, 0),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

void notifications_spam_thread_entry_point(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	printk("first suspend t\n");
	k_thread_suspend(&notifications_spam_thread_id);
	printk("resume t\n");

	while (1) {
		if (default_conn) {
			char notif[20];
			snprintk(notif, sizeof(notif), "%c-pkt-len-20-ascstr",
				 counter++);
			printk("notify %d\n", counter);
			int err = bt_gatt_notify(default_conn, &my_svc.attrs[1], notif,
						 sizeof(notif));
			if (err) {
				printk("bt_gatt_notify err: %d\n", err);
				printk("suspending spam thread\n");
				counter = 0;
				k_thread_suspend(&notifications_spam_thread_id);
				printk("resume t\n");
			}
		}
		else {
			printk("suspending spam thread\n");
			counter = 0;
			k_thread_suspend(&notifications_spam_thread_id);
			printk("resume t\n");
		}
	}
}

void main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_conn_cb_register(&conn_callbacks);
	bt_conn_auth_cb_register(&auth_cb_display);

	printk("Bluetooth initialized\n");
	advertise();

	k_thread_create(&notifications_spam_thread_id,
			notifications_spam_thread_stack_area,
			K_THREAD_STACK_SIZEOF(notifications_spam_thread_stack_area),
			notifications_spam_thread_entry_point, NULL, NULL, NULL,
			NOTIFICATIONS_SPAM_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&notifications_spam_thread_id, "spam_thread");

	/* Terminate main thread */
}
