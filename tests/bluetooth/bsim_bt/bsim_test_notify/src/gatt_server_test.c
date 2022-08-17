/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"

extern enum bst_result_t bst_result;

CREATE_FLAG(flag_is_connected);
CREATE_FLAG(flag_short_subscribe);
CREATE_FLAG(flag_long_subscribe);

static struct bt_conn *g_conn;

#define ARRAY_ITEM(i, _) i
const uint8_t chrc_data[] = { LISTIFY(CHRC_SIZE, ARRAY_ITEM, (,)) }; /* 1, 2, 3 ... */
const uint8_t long_chrc_data[] = { LISTIFY(LONG_CHRC_SIZE, ARRAY_ITEM, (,)) }; /* 1, 2, 3 ... */

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		FAIL("Failed to connect to %s (%u)\n", addr, err);
		return;
	}

	printk("Connected to %s\n", addr);

	g_conn = bt_conn_ref(conn);
	SET_FLAG(flag_is_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != g_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(g_conn);

	g_conn = NULL;
	UNSET_FLAG(flag_is_connected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static ssize_t read_test_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	printk("Read short\n");
	return bt_gatt_attr_read(conn, attr, buf, len, offset, (void *)chrc_data,
				 sizeof(chrc_data));
}

static ssize_t read_long_test_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				   uint16_t len, uint16_t offset)
{
	printk("Read long\n");
	return bt_gatt_attr_read(conn, attr, buf, len, offset, (void *)long_chrc_data,
				 sizeof(long_chrc_data));
}

static void short_subscribe(const struct bt_gatt_attr *attr, uint16_t value)
{
	const bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	if (notif_enabled) {
		SET_FLAG(flag_short_subscribe);
	}

	printk("Short notifications %s\n", notif_enabled ? "enabled" : "disabled");
}

static void long_subscribe(const struct bt_gatt_attr *attr, uint16_t value)
{
	const bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	if (notif_enabled) {
		SET_FLAG(flag_long_subscribe);
	}

	printk("Long notifications %s\n", notif_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(test_svc, BT_GATT_PRIMARY_SERVICE(TEST_SERVICE_UUID),
		       BT_GATT_CHARACTERISTIC(TEST_CHRC_UUID,
					      BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_test_chrc, NULL, NULL),
		       BT_GATT_CUD("Short test_svc format description", BT_GATT_PERM_READ),
		       BT_GATT_CCC(short_subscribe, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
		       BT_GATT_CHARACTERISTIC(TEST_LONG_CHRC_UUID,
					      BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_long_test_chrc, NULL, NULL),
		       BT_GATT_CCC(long_subscribe, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static volatile size_t num_notifications_sent;

static void notification_sent(struct bt_conn *conn, void *user_data)
{
	size_t *length = user_data;

	printk("Sent notification #%u with length %d\n", num_notifications_sent++, *length);
}

/*
 * BT_ATT_PREPARE_COUNT has to be adjusted in order to use the largest size.
 * Partial writes will work, but partial reads are not implemented in the host
 * test adapter yet. */
typedef struct {
	uint16_t len;
	uint8_t buf[513];
} gatt_attr_data_t;

ssize_t on_attr_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags) {
	return len;
}
ssize_t on_attr_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset) {
	return len;
}
void on_gatt_ccc_changed_cb(const struct bt_gatt_attr *attr, uint16_t value) {
	const bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	if (notif_enabled) {
		SET_FLAG(flag_short_subscribe);
	}

	printk("Custom notifications %s\n", notif_enabled ? "enabled" : "disabled");
}

static ssize_t on_attr_delayed_read_cb(struct bt_conn *conn,
				       const struct bt_gatt_attr *attr,
				       void *buf, uint16_t len, uint16_t offset)
{
	gatt_attr_data_t *data = attr->user_data;
	/* Sleep when retrieving this attribute.
	 * This will ensure that the second request is sent on another bearer,
	 * as a response has not been received for the request on
	 * the first bearer.
	 */
	k_sleep(K_MSEC(500));
	ssize_t length = bt_gatt_attr_read(conn, attr, buf, len, offset,
					   data->buf, data->len);
	return length;
}
static struct bt_uuid_16 primary_service_uuid = BT_UUID_INIT_16(0xf13a);
static struct bt_uuid_16 char_1_uuid = BT_UUID_INIT_16(0xf13b);
static struct bt_uuid_16 char_2_uuid = BT_UUID_INIT_16(0xf13c);
static struct bt_uuid_16 char_3_uuid = BT_UUID_INIT_16(0xf13d);
static struct bt_uuid_16 char_4_uuid = BT_UUID_INIT_16(0xf13e);
static gatt_attr_data_t char1_data, char2_data, char3_data, char4_data;

static struct bt_gatt_attr gatt_service_attrs[] = {
	BT_GATT_PRIMARY_SERVICE(&primary_service_uuid.uuid),
	BT_GATT_CHARACTERISTIC(&char_1_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       on_attr_read_cb, on_attr_write_cb, &char1_data),
	BT_GATT_CCC(on_gatt_ccc_changed_cb,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&char_2_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
				       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       on_attr_read_cb, on_attr_write_cb, &char2_data),
	BT_GATT_CCC(on_gatt_ccc_changed_cb,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(
		&char_3_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
		BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, on_attr_delayed_read_cb,
		on_attr_write_cb, &char3_data),
	BT_GATT_CHARACTERISTIC(&char_4_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       on_attr_read_cb, on_attr_write_cb, &char4_data),
};

static struct bt_gatt_service gatt_service =
	BT_GATT_SERVICE(gatt_service_attrs);

static inline void short_notify(void)
{
	static size_t length = CHRC_SIZE;

	const size_t max_cnt = CONFIG_BT_L2CAP_TX_BUF_COUNT;
	struct bt_gatt_notify_params params[max_cnt];

	int err;

	(void)memset(params, 0, sizeof(params));

	struct bt_gatt_attr *vnd_ind_attr;
	vnd_ind_attr = bt_gatt_find_by_uuid(NULL, 0, &char_1_uuid.uuid);
	printk("find-by-uuid: %p direct %p\n", vnd_ind_attr, &gatt_service_attrs[1]);

	for (uint16_t i = 0U; i < max_cnt; i++) {
		/* params[i].attr = &gatt_service_attrs[1]; */
		params[i].attr = vnd_ind_attr;
		params[i].data = chrc_data;
		params[i].len = CHRC_SIZE;
		params[i].func = notification_sent;
		params[i].user_data = &length;
		params[i].uuid = NULL;
	}

	printk("Sending %d notifications\n\n", max_cnt);

	do {
		err = bt_gatt_notify_multiple(g_conn, max_cnt, params);

		if (err == -ENOMEM) {
			k_sleep(K_MSEC(10));
		} else if (err) {
			FAIL("Short notify failed (err %d)\n", err);
		}
	} while (err);
}

static inline void long_notify(void)
{
	static size_t length = LONG_CHRC_SIZE;
	static struct bt_gatt_notify_params params = {
		.attr = &attr_test_svc[5],
		.data = long_chrc_data,
		.len = LONG_CHRC_SIZE,
		.func = notification_sent,
		.user_data = &length,
		.uuid = NULL,
	};
	int err;

	do {
		err = bt_gatt_notify_cb(g_conn, &params);

		if (err == -ENOMEM) {
			k_sleep(K_MSEC(10));
		} else if (err) {
			FAIL("Long notify failed (err %d)\n", err);
		}
	} while (err);
}

static void test_main(void)
{
	int err;
	const struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	};

	err = bt_enable(NULL);
	if (err != 0) {
		FAIL("Bluetooth init failed (err %d)\n", err);
		return;
	}

	err = bt_gatt_service_register(&gatt_service);
	if (err) {
		FAIL("register err %d\n", err);
	}

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err != 0) {
		FAIL("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");

	WAIT_FOR_FLAG(flag_is_connected);

	while (bt_eatt_count(g_conn) < CONFIG_BT_EATT_MAX) {
		k_sleep(K_MSEC(100));
	}
	printk("EATT connected\n");

	WAIT_FOR_FLAG(flag_short_subscribe);
	WAIT_FOR_FLAG(flag_long_subscribe);

	for (int i = 0; i < NOTIFICATION_COUNT / 2; i++) {
		short_notify();
		long_notify();
	}

	while (num_notifications_sent < NOTIFICATION_COUNT) {
		k_sleep(K_MSEC(100));
	}

	PASS("GATT server passed\n");
}

static const struct bst_test_instance test_gatt_server[] = {
	{
		.test_id = "gatt_server",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_gatt_server_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_gatt_server);
}
