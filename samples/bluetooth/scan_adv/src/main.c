/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#define DEVICE_NAME "beacon"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

void scan_rsp_cb(struct bt_le_ext_adv *adv,
		 struct bt_le_ext_adv_scanned_info *info)
{
	printk("Scanned by: ");
	for(int i=0; i<6; i++)
		printk("%u", info->addr->a.val[i]);
	printk("\n");
	/* printk("%s\n", __func__); */
}

void connected_cb(struct bt_le_ext_adv *adv,
		  struct bt_le_ext_adv_connected_info *info)
{
	printk("%s\n", __func__);
}

void sent_cb(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_sent_info *info)
{
	printk("%s: num_sent = %u\n", __func__, info->num_sent);
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xaa, 0xfe),
	BT_DATA_BYTES(BT_DATA_SVC_DATA16,
		      0xaa, 0xfe, /* Eddystone UUID */
		      0x10, /* Eddystone-URL frame type */
		      0x00, /* Calibrated Tx power at 0m */
		      0x00, /* URL Scheme Prefix http://www. */
		      'z', 'e', 'p', 'h', 'y', 'r',
		      'p', 'r', 'o', 'j', 'e', 'c', 't',
		      0x08) /* .org */
};

/* Set Scan Response data */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

struct bt_le_ext_adv_start_param adv_start_param = {0,255};
struct bt_le_ext_adv* p_ext_adv;

struct bt_le_ext_adv_cb adv_cb = {
	.sent = sent_cb,
	.connected = connected_cb,
	.scanned = scan_rsp_cb,
};

void main(void)
{
	int err = 0;

	printk("Starting Scanner/Advertiser Demo\n");

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	k_msleep(100);
	bt_le_ext_adv_create(BT_LE_ADV_NCONN,
	/* bt_le_ext_adv_create(BT_LE_ADV_NCONN_NAME, */
			     &adv_cb,
			     &p_ext_adv);

	k_msleep(100);
	bt_le_ext_adv_set_data(p_ext_adv,
			       ad, ARRAY_SIZE(ad),
			       sd, ARRAY_SIZE(sd));

	/* Start advertising */
	k_msleep(100);
	bt_le_ext_adv_start(p_ext_adv,
			    &adv_start_param);

	bt_le_ext_adv_set_data(p_ext_adv,
			       ad, ARRAY_SIZE(ad),
			       sd, ARRAY_SIZE(sd));

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	for(;;)
	{
		k_msleep(1000);
	}
}
