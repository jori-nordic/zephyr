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

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/l2cap.h>

#define TRIGGER_SDU_BUG 1

#define HOST_L2CAP_NETBUF_COUNT 5
#define HOST_MAX_L2CAP_CHANNELS 4

#define MAX_L2CAP_DATA_LEN 128

bool periph_connected = 0;
bool l2cap_chan_connected = 0;
uint32_t l2cap_handle = 0;

NET_BUF_POOL_DEFINE(l2cap_pool, HOST_L2CAP_NETBUF_COUNT,
		    BT_L2CAP_BUF_SIZE(MAX_L2CAP_DATA_LEN), 0, NULL);

static struct bt_l2cap_le_chan l2cap_channels[HOST_MAX_L2CAP_CHANNELS];


#if !(TRIGGER_SDU_BUG)
static struct net_buf *l2cap_chan_alloc_buf_cb(struct bt_l2cap_chan *chan);
#endif
static void l2cap_chan_connected_cb(struct bt_l2cap_chan *chan);
static void l2cap_chan_status_cb(struct bt_l2cap_chan *chan, atomic_t *status);
static int l2cap_chan_recv_cb(struct bt_l2cap_chan *chan, struct net_buf *buf);
static void l2cap_chan_sent_cb(struct bt_l2cap_chan *chan);

static struct bt_l2cap_chan_ops l2cap_chan_ops = {
#if !(TRIGGER_SDU_BUG)
	.alloc_buf = l2cap_chan_alloc_buf_cb,
#endif
	.connected = l2cap_chan_connected_cb,
	.status = l2cap_chan_status_cb,
	.recv = l2cap_chan_recv_cb,
	.sent = l2cap_chan_sent_cb
};

static int l2cap_chan_recv_cb(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	printk("%s\n", __func__);
	return 0;
}

static void l2cap_chan_sent_cb(struct bt_l2cap_chan *chan)
{
	printk("%s\n", __func__);
}

#if !(TRIGGER_SDU_BUG)
static struct net_buf *l2cap_chan_alloc_buf_cb(struct bt_l2cap_chan *chan)
{
	printk("%s\n", __func__);
	return net_buf_alloc(&l2cap_pool, K_NO_WAIT);
}
#endif

static void l2cap_chan_status_cb(struct bt_l2cap_chan *chan, atomic_t *status)
{
	printk("%s\n", __func__);
	return;
}

static void l2cap_chan_connected_cb(struct bt_l2cap_chan *l2cap_chan)
{
	printk("%s\n", __func__);
	l2cap_chan_connected = 1;
}

static struct bt_l2cap_le_chan *get_free_l2cap_le_chan(void)
{
	printk("%s\n", __func__);
	for (int i = 0; i < HOST_MAX_L2CAP_CHANNELS; i++) {
		struct bt_l2cap_le_chan *le_chan = &l2cap_channels[i];

		if (le_chan->chan.status != BT_L2CAP_DISCONNECTED) {
			continue;
		}

		memset(le_chan, 0, sizeof(le_chan));
		return le_chan;
	}

	return NULL;
}

static int l2cap_server_accept_cb(struct bt_conn *conn,
				  struct bt_l2cap_chan **chan)
{
	printk("%s\n", __func__);

	/* struct bt_l2cap_le_chan *le_chan = get_free_l2cap_le_chan(); */
	struct bt_l2cap_le_chan *le_chan = &l2cap_channels[0];

	if (le_chan == NULL) {
		return -ENOMEM;
	}

	memset(le_chan, 0, sizeof(*le_chan));
	le_chan->chan.ops = &l2cap_chan_ops;
	le_chan->rx.mtu = MAX_L2CAP_DATA_LEN;
	*chan = &le_chan->chan;

	return 0;
}

static struct bt_l2cap_server l2cap_server = { .accept = l2cap_server_accept_cb };

int test_bt_l2cap_server_register(uint16_t psm, bt_security_t sec_level)
{
	printk("%s\n", __func__);
	l2cap_server.psm = psm;
	l2cap_server.sec_level = sec_level;

	return bt_l2cap_server_register(&l2cap_server);
}

struct bt_conn *default_conn;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x0d, 0x18, 0x0f, 0x18, 0x0a, 0x18),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		default_conn = bt_conn_ref(conn);
		printk("Connected\n");
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
	(void)l2cap_server;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_ready();

	printk("Registering server\n");
	test_bt_l2cap_server_register(0, BT_SECURITY_L1);
	printk("Registering callbacks\n");
	bt_conn_cb_register(&conn_callbacks);
	bt_conn_auth_cb_register(&auth_cb_display);
	printk("Main loop\n");

	while (1) {
		k_sleep(K_SECONDS(1));

	}
}
