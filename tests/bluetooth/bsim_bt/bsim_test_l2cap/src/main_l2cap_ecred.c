/* main_l2cap_ecred.c - Application main entry point */

/*
 * Copyright (c) 2022 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"
#include <zephyr/sys/__assert.h>

#define LOG_MODULE_NAME main_l2cap_ecred
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME, LOG_LEVEL_DBG);

extern enum bst_result_t bst_result;

static struct bt_conn *connections[CONFIG_BT_MAX_CONN] = {0};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

CREATE_FLAG(is_connected);
CREATE_FLAG(flag_l2cap_connected);

#define LOCAL_HOST_L2CAP_NETBUF_COUNT 20
#define HOST_LIB_MAX_L2CAP_DATA_LEN 200
#define L2CAP_QUEUE_SIZE 2
#define INIT_CREDITS 20

uint16_t l2cap_mtu = 250;

NET_BUF_POOL_DEFINE(local_l2cap_tx_pool, LOCAL_HOST_L2CAP_NETBUF_COUNT,
		    BT_L2CAP_BUF_SIZE(HOST_LIB_MAX_L2CAP_DATA_LEN), 8,
		    NULL);

/* Only one SDU per link will be received at a time */
NET_BUF_POOL_DEFINE(local_l2cap_rx_pool, CONFIG_BT_MAX_CONN,
		    BT_L2CAP_BUF_SIZE(HOST_LIB_MAX_L2CAP_DATA_LEN), 8,
		    NULL);

/* static uint8_t tx_data[HOST_LIB_MAX_L2CAP_DATA_LEN]; */

int l2cap_chan_send(uint32_t l2cap_handle, uint8_t *data, size_t len)
{
	struct bt_l2cap_chan *chan = (struct bt_l2cap_chan *)l2cap_handle;

	struct net_buf *buf = net_buf_alloc(&local_l2cap_tx_pool, K_NO_WAIT);
	if (buf == NULL) {
		return -ENOMEM;
	}

	net_buf_reserve(buf, BT_L2CAP_CHAN_SEND_RESERVE);
	net_buf_add_mem(buf, data, len);

	int ret = bt_l2cap_chan_send(chan, buf);
	if (ret < 0) {
		net_buf_unref(buf);
	}

	return ret;
}

struct net_buf *alloc_buf_cb(struct bt_l2cap_chan *chan)
{
	return net_buf_alloc(&local_l2cap_rx_pool, K_NO_WAIT);
}

void sent_cb(struct bt_l2cap_chan *chan)
{
	/* uint32_t conn_index = bt_conn_index(chan->conn); */
	/* TODO: do something here */
}

int recv_cb(struct bt_l2cap_chan *chan, struct net_buf *buf)
{
	/* uint32_t conn_index = bt_conn_index(chan->conn); */

	return 0;
}

void l2cap_chan_connected_cb(struct bt_l2cap_chan *l2cap_chan)
{
	struct bt_l2cap_le_chan *chan =
		CONTAINER_OF(l2cap_chan, struct bt_l2cap_le_chan, chan);

	SET_FLAG(flag_l2cap_connected);
	LOG_DBG("%x (tx mtu %d mps %d) (tx mtu %d mps %d)",
		l2cap_chan,
		chan->tx.mtu,
		chan->tx.mps,
		chan->rx.mtu,
		chan->rx.mps);
}

void l2cap_chan_disconnected_cb(struct bt_l2cap_chan *chan)
{
	UNSET_FLAG(flag_l2cap_connected);
	LOG_DBG("%x", chan);
}

static struct bt_l2cap_chan_ops ops = {
	.connected = l2cap_chan_connected_cb,
	.disconnected = l2cap_chan_disconnected_cb,
	.alloc_buf = alloc_buf_cb,
	.recv = recv_cb,
	.sent = sent_cb,
};

#define HOST_MAX_L2CAP_CHANNELS 10

static struct bt_l2cap_le_chan l2cap_channels[HOST_MAX_L2CAP_CHANNELS];

struct bt_l2cap_le_chan *get_free_l2cap_le_chan(void)
{
	for (int i = 0; i < HOST_MAX_L2CAP_CHANNELS; i++) {
		struct bt_l2cap_le_chan *le_chan = &l2cap_channels[i];

		if (le_chan->state != BT_L2CAP_DISCONNECTED) {
			continue;
		}

		memset(le_chan, 0, sizeof(*le_chan));
		return le_chan;
	}

	return NULL;
}

int server_accept_cb(struct bt_conn *conn, struct bt_l2cap_chan **chan)
{
	struct bt_l2cap_le_chan *le_chan = NULL;

	le_chan = get_free_l2cap_le_chan();
	if (le_chan == NULL) {
		return -ENOMEM;
	}

	memset(le_chan, 0, sizeof(*le_chan));
	le_chan->chan.ops = &ops;
	le_chan->rx.mtu = l2cap_mtu;
	le_chan->rx.init_credits = INIT_CREDITS;
	le_chan->tx.init_credits = INIT_CREDITS;
	*chan = &le_chan->chan;

	return 0;
}

static struct bt_l2cap_server test_l2cap_server = {
	.accept = server_accept_cb
};

static int l2cap_server_register(bt_security_t sec_level)
{
	test_l2cap_server.psm = 0;
	test_l2cap_server.sec_level = sec_level;

	__ASSERT_NO_MSG(bt_l2cap_server_register(&test_l2cap_server) == 0);
	return test_l2cap_server.psm;
}

static int l2cap_chan_connect(struct bt_conn *conn, uint16_t psm)
{
	__ASSERT_NO_MSG(conn != NULL);

	struct bt_l2cap_le_chan *le_chan = get_free_l2cap_le_chan();

	if (le_chan == NULL) {
		return -ENOMEM;
	}

	le_chan->chan.ops = &ops;
	le_chan->rx.mtu = l2cap_mtu;
	le_chan->rx.init_credits = INIT_CREDITS;
	le_chan->tx.init_credits = INIT_CREDITS;

	__ASSERT_NO_MSG(bt_l2cap_chan_connect(conn, &le_chan->chan, psm) == 0);

	return 0;
}

static int get_connection(struct bt_conn *conn)
{
	for (int i=0; i<CONFIG_BT_MAX_CONN; i++) {
		if (conn == connections[i]) return i;
	}

	return -1;
}

static int register_connection(struct bt_conn *conn)
{
	for (int i=0; i<CONFIG_BT_MAX_CONN; i++) {
		if (connections[i] == NULL) {
			LOG_DBG("[%d] %p", i, conn);
			connections[i] = conn;
			bt_conn_ref(conn);
			return i;
		}
	}

	return -1;
}

static int deregister_connection(struct bt_conn *conn)
{
	int id = get_connection(conn);

	if (id>=0) {
		LOG_DBG("[%d] %p", id, conn);
		connections[id] = NULL;
		bt_conn_unref(conn);
		return id;
	} else {
		FAIL("Connection doesn't exist\n");
		return -1;
	}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		FAIL("Failed to connect to %s (%u)", addr, conn_err);
		deregister_connection(conn);
		return;
	}

	register_connection(conn);
	LOG_DBG("%s", addr);

	SET_FLAG(is_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("%s (reason 0x%02x)", addr, reason);

	deregister_connection(conn);
	UNSET_FLAG(is_connected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void test_peripheral_main(void)
{
	LOG_DBG("*L2CAP ECRED Peripheral started*");
	int err;

	err = bt_enable(NULL);
	if (err) {
		FAIL("Can't enable Bluetooth (err %d)", err);
		return;
	}

	LOG_DBG("Peripheral Bluetooth initialized.");
	LOG_DBG("Connectable advertising...");
	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		FAIL("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_DBG("Advertising started.");
	LOG_DBG("Peripheral waiting for connection...");
	WAIT_FOR_FLAG_SET(is_connected);
	LOG_DBG("Peripheral Connected.");

	int psm = l2cap_server_register(BT_SECURITY_L1);
	LOG_DBG("Registered server PSM %x", psm);

	WAIT_FOR_FLAG_UNSET(is_connected);
	PASS("L2CAP ECRED Peripheral tests Passed\n");
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	struct bt_le_conn_param *param;
	struct bt_conn *conn;
	int err;

	err = bt_le_scan_stop();
	if (err) {
		FAIL("Stop LE scan failed (err %d)", err);
		return;
	}

	char str[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, str, sizeof(str));

	LOG_DBG("Connecting to %s", str);

	param = BT_LE_CONN_PARAM_DEFAULT;
	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &conn);
	if (err) {
		FAIL("Create conn failed (err %d)", err);
		return;
	}
}

static void test_central_main(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	LOG_DBG("*L2CAP ECRED Central started*");
	int err;

	err = bt_enable(NULL);
	if (err) {
		FAIL("Can't enable Bluetooth (err %d)\n", err);
		return;
	}
	LOG_DBG("Central Bluetooth initialized.");

	/* Connect all peripherals */
	for (int i=0; i<4; i++) {
		UNSET_FLAG(is_connected);
		LOG_DBG("\nconnecting peripheral %d", i);
		err = bt_le_scan_start(&scan_param, device_found);
		if (err) {
			FAIL("Scanning failed to start (err %d)\n", err);
			return;
		}

		LOG_DBG("Scanning successfully started");

		LOG_DBG("Central waiting for connection...");
		WAIT_FOR_FLAG_SET(is_connected);
		LOG_DBG("Central Connected.");
	}

	/* Connect L2CAP channels */
	LOG_WRN("Connect L2CAP channels");
	for (int i=0; i<CONFIG_BT_MAX_CONN; i++) {
		struct bt_conn *conn = connections[i];
		if (conn) {
			UNSET_FLAG(flag_l2cap_connected);
			l2cap_chan_connect(conn, 0x080);
			WAIT_FOR_FLAG_SET(flag_l2cap_connected);
		}
	}

	/* Disconnect all peripherals */
	LOG_DBG("Central Disconnecting....");
	for (int i=0; i<CONFIG_BT_MAX_CONN; i++) {
		struct bt_conn *conn = connections[i];
		if (conn) {
			SET_FLAG(is_connected);
			err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			LOG_DBG("Central tried to disconnect");

			if (err) {
				FAIL("Disconnection failed (err %d)", err);
				return;
			}
			WAIT_FOR_FLAG_UNSET(is_connected);
		}
	}

	LOG_DBG("Central Disconnected.");

	PASS("L2CAP ECRED Central tests Passed\n");
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "peripheral",
		.test_descr = "Peripheral L2CAP ECRED",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_peripheral_main
	},
	{
		.test_id = "central",
		.test_descr = "Central L2CAP ECRED",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_central_main
	},
	BSTEST_END_MARKER
};

struct bst_test_list *test_main_l2cap_ecred_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}
