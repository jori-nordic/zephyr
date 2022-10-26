/* main.c - Application main entry point */

/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"

#define LOG_DEV_TYPE "Central"

static void start_scan(void);
static int stop_scan(void);

enum {
	BT_IS_SCANNING,
	BT_IS_CONNECTING,
	/* Total number of flags - must be at the end of the enum */
	BT_IS_NUM_FLAGS,
};

ATOMIC_DEFINE(status_flags, BT_IS_NUM_FLAGS);
static uint8_t volatile conn_count;
static struct bt_conn *conn_connecting;
#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
static struct bt_conn_le_data_len_param le_data_len_param;
#endif

struct conn_info {
	ATOMIC_DEFINE(flags, CONN_INFO_NUM_FLAGS);
	struct bt_conn *conn_ref;
	uint32_t notify_counter;
	uint32_t tx_notify_counter;
	struct bt_uuid_128 uuid;
	struct bt_gatt_discover_params discover_params;
	struct bt_gatt_exchange_params mtu_exchange_params;
	uint8_t vnd_value[CHARACTERISTIC_DATA_MAX_LEN];
	uint32_t __presistent_data_marker__;
	bt_addr_le_t peer_addr;
	bt_security_t security_level;
	struct bt_gatt_subscribe_params subscribe_params;
};

static uint8_t simulate_vnd;
static uint32_t conn_interval_max, notification_size;
static struct conn_info conn_infos[CONFIG_BT_MAX_CONN];

void vnd_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	TERM_PRINT("\e[93m---> %s:%s <---", __func__,
		   (value == BT_GATT_CCC_NOTIFY) ? "true" : "false");
	simulate_vnd = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static struct conn_info *get_new_conn_info_ref(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_t const *peer_addr = bt_conn_get_dst(conn);

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (bt_addr_le_cmp(&conn_infos[i].peer_addr, BT_ADDR_LE_ANY) != 0) {
			if (!bt_addr_le_cmp(&conn_infos[i].peer_addr, peer_addr)) {
				return &conn_infos[i];
			}
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (conn_infos[i].conn_ref == NULL) {
			return &conn_infos[i];
		}
	}

	return NULL;
}

static struct conn_info *get_conn_info_ref(struct bt_conn *conn_ref)
{
	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (conn_ref == conn_infos[i].conn_ref) {
			return &conn_infos[i];
		}
	}

	return NULL;
}

static bool check_if_peer_connected(const bt_addr_le_t *addr)
{

	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (conn_infos[i].conn_ref != NULL) {
			if (!bt_addr_le_cmp(bt_conn_get_dst(conn_infos[i].conn_ref), addr)) {
				return true;
			}
		}
	}

	return false;
}

static void update_conn_info_peer_addr(struct conn_info *conn_info_ref, const bt_addr_le_t *addr)
{
	bt_addr_le_copy(&conn_info_ref->peer_addr, addr);
}

static void reset_conn_info_ref(struct conn_info *conn_info_ref)
{
	memset(conn_info_ref, 0x00, offsetof(struct conn_info, __presistent_data_marker__));
}

static void send_update_conn_params_req(struct bt_conn *conn)
{
	struct conn_info *conn_info_ref;

	conn_info_ref = get_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return;
	}

	if (atomic_test_bit(conn_info_ref->flags, CONN_INFO_CONN_PARAMS_UPDATED) == false) {
		int err;
		char addr[BT_ADDR_LE_STR_LEN];
		/** Default LE connection parameters:
		 *    Connection Interval: 30-50 ms
		 *    Latency: 0
		 *    Timeout: 4 s
		 */
		struct bt_le_conn_param param = *BT_LE_CONN_PARAM_DEFAULT;

		/* Connection interval multiplier with 1.25 step */
		param.interval_min = 10;
		param.interval_max = conn_interval_max;

		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		TERM_PRINT("Updating %s... connection parameters", addr);

		err = bt_conn_le_param_update(conn, &param);
		if (err) {
			FAIL("Updating connection parameters failed %s", addr);
			return;
		}

		TERM_SUCCESS("Updating connection parameters succeeded %s", addr);
	}
}

static uint8_t notify_func(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
			   const void *data, uint16_t length)
{
	const char *data_ptr = (const char *)data + NOTIFICATION_DATA_PREFIX_LEN;
	uint32_t received_counter;
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!data) {
		TERM_INFO("[UNSUBSCRIBED] addr %s", addr);
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	conn_info_ref = get_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return BT_GATT_ITER_CONTINUE;
	}

	received_counter = strtoul(data_ptr, NULL, 0);

	if ((conn_info_ref->notify_counter % 30) == 0) {
		TERM_PRINT("[NOTIFICATION] addr %s conn %u data %s length %u cnt %u", addr,
			   (conn_info_ref - conn_infos), data, length, received_counter);
	}

	if (conn_info_ref->notify_counter != received_counter) {
		TERM_WARN("expected counter : %u , received counter : %u",
			  conn_info_ref->notify_counter, received_counter);
	}

	conn_info_ref->notify_counter++;

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;
	char uuid_str[BT_UUID_STR_LEN];
	struct conn_info *conn_info_ref;

	conn_info_ref = get_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return BT_GATT_ITER_STOP;
	}

	if (!attr) {
		TERM_INFO("Discover complete");
		(void)memset(params, 0, sizeof(*params));
		return BT_GATT_ITER_STOP;
	}

	bt_uuid_to_str(params->uuid, uuid_str, sizeof(uuid_str));
	TERM_PRINT("UUID found : %s", uuid_str);

	TERM_PRINT("[ATTRIBUTE] handle %u", attr->handle);

	if (conn_info_ref->discover_params.type == BT_GATT_DISCOVER_PRIMARY) {
		TERM_PRINT("Primary Service Found");
		memcpy(&conn_info_ref->uuid, CHARACTERISTIC_UUID, sizeof(conn_info_ref->uuid));
		conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
		conn_info_ref->discover_params.start_handle = attr->handle + 1;
		conn_info_ref->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &conn_info_ref->discover_params);
		if (err) {
			FAIL("Discover failed (err %d)", err);
		}
	} else if (conn_info_ref->discover_params.type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		TERM_PRINT("Service Characteristic Found");
		memcpy(&conn_info_ref->uuid, BT_UUID_GATT_CCC, sizeof(conn_info_ref->uuid));
		conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
		conn_info_ref->discover_params.start_handle = attr->handle + 2;
		conn_info_ref->discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		conn_info_ref->subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &conn_info_ref->discover_params);
		if (err) {
			FAIL("Discover failed (err %d)", err);
		}
	} else {
		conn_info_ref->subscribe_params.notify = notify_func;
		conn_info_ref->subscribe_params.value = BT_GATT_CCC_NOTIFY;
		conn_info_ref->subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &conn_info_ref->subscribe_params);
		if (err && err != -EALREADY) {
			FAIL("Subscribe failed (err %d)", err);
		} else {
			struct conn_info *conn_info_ref;
			char addr[BT_ADDR_LE_STR_LEN];

			conn_info_ref = get_conn_info_ref(conn);
			CHECKIF(conn_info_ref == NULL) {
				TERM_WARN("Invalid reference returned");
				return BT_GATT_ITER_STOP;
			}

			bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

			atomic_set_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED_TO_SERVICE);
			TERM_INFO("[SUBSCRIBED] addr %s conn %u", addr,
				  (conn_info_ref - conn_infos));
		}
	}

	return BT_GATT_ITER_STOP;
}

static bool eir_found(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;

	TERM_PRINT("[AD]: %u data_len %u", data->type, data->data_len);

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		TERM_PRINT("Name Tag found!");
		TERM_PRINT("Device name : %.*s", data->data_len, data->data);

		if (!strncmp(data->data, PERIPHERAL_DEVICE_NAME, PERIPHERAL_DEVICE_NAME_LEN)) {
			int err;
			char dev[BT_ADDR_LE_STR_LEN];
			struct bt_le_conn_param *param;

			if (check_if_peer_connected(addr) == true) {
				TERM_WARN("Peer is already connected or in disconnecting state");
				break;
			}

			CHECKIF(atomic_test_and_set_bit(status_flags, BT_IS_CONNECTING) == true) {
				TERM_WARN("A connecting procedure is ongoing");
				break;
			}

			if (stop_scan()) {
				atomic_clear_bit(status_flags, BT_IS_CONNECTING);
				break;
			}

			param = BT_LE_CONN_PARAM_DEFAULT;
			bt_addr_le_to_str(addr, dev, sizeof(dev));
			TERM_INFO("Connecting to %s", dev);
			err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param,
						&conn_connecting);
			if (err) {
				FAIL("Create conn failed (err %d)", err);
				atomic_clear_bit(status_flags, BT_IS_CONNECTING);
			}

			return false;
		}

		break;
	}

	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char dev[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(addr, dev, sizeof(dev));
	TERM_PRINT("------------------------------------------------------");
	TERM_INFO("[DEVICE]: %s, AD evt type %u, AD data len %u, RSSI %i", dev, type, ad->len,
		  rssi);
	TERM_PRINT("------------------------------------------------------");

	/* We're only interested in connectable events */
	if (type == BT_GAP_ADV_TYPE_SCAN_RSP) {
		bt_data_parse(ad, eir_found, (void *)addr);
	}
}

static void start_scan(void)
{
	int err;

	CHECKIF(atomic_test_and_set_bit(status_flags, BT_IS_SCANNING) == true) {
		TERM_WARN("A scanning procedure is ongoing");
		return;
	}

	/* Use active scanning and disable duplicate filtering to handle any
	 * devices that might update their advertising data at runtime.
	 */
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		FAIL("Scanning failed to start (err %d)", err);
		atomic_clear_bit(status_flags, BT_IS_SCANNING);
		return;
	}

	TERM_INFO("Scanning successfully started");
}

static int stop_scan(void)
{
	int err;

	CHECKIF(atomic_test_bit(status_flags, BT_IS_SCANNING) == false) {
		TERM_WARN("No scanning procedure is ongoing");
		return -EALREADY;
	}

	err = bt_le_scan_stop();
	if (err) {
		FAIL("Stop LE scan failed (err %d)", err);
		return err;
	}

	atomic_clear_bit(status_flags, BT_IS_SCANNING);
	TERM_INFO("Scanning successfully stopped");
	return 0;
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	int err;
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		FAIL("Failed to connect to %s (%u)", addr, conn_err);

		bt_conn_unref(conn_connecting);
		conn_connecting = NULL;
		atomic_clear_bit(status_flags, BT_IS_CONNECTING);

		return;
	}

	TERM_SUCCESS("Connection %p established : %s", conn, addr);

	conn_count++;
	TERM_INFO("Active connections count : %u", conn_count);

	conn_info_ref = get_new_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return;
	}
	TERM_PRINT("Connection reference store index %u", (conn_info_ref - conn_infos));
	conn_info_ref->conn_ref = conn_connecting;

	if (conn == conn_connecting) {
		conn_connecting = NULL;
		atomic_clear_bit(status_flags, BT_IS_CONNECTING);
	}

#if defined(CONFIG_BT_SMP)
	if (conn_info_ref->security_level < BT_SECURITY_L2) {
		err = bt_conn_set_security(conn, BT_SECURITY_L2);

		if (!err) {
			conn_info_ref->security_level = BT_SECURITY_L2;
			TERM_SUCCESS("Security level is set to : %u", BT_SECURITY_L2);
		} else {
			FAIL("Failed to set security (%d).", err);
		}
	}
#endif /* CONFIG_BT_SMP */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	int conn_ref_index;
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	conn_info_ref = get_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return;
	}

	conn_ref_index = (conn_info_ref - conn_infos);
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	TERM_WARN("Connection (%d) @ %p with Peer %s terminated (reason 0x%02x)", conn_ref_index,
		  conn, addr, reason);
	bt_conn_unref(conn);

	conn_info_ref->conn_ref = NULL;
	reset_conn_info_ref(conn_info_ref);

	conn_count--;
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	TERM_PRINT("LE conn param req: %s int (0x%04x (~%u ms), 0x%04x (~%u ms)) lat %d to %d",
		   addr, param->interval_min, (uint32_t)(param->interval_min * 1.25),
		   param->interval_max, (uint32_t)(param->interval_max * 1.25), param->latency,
		   param->timeout);

	send_update_conn_params_req(conn);

	/* Reject the current connection parameters request */
	return false;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	TERM_INFO("LE conn param updated: %s int 0x%04x (~%u ms) lat %d to %d", addr, interval,
		  (uint32_t)(interval * 1.25), latency, timeout);

	conn_info_ref = get_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return;
	}

	atomic_set_bit(conn_info_ref->flags, CONN_INFO_CONN_PARAMS_UPDATED);
}

#if defined(CONFIG_BT_SMP)
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	int conn_ref_index;
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	conn_info_ref = get_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return;
	}

	conn_ref_index = (conn_info_ref - conn_infos);
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		TERM_WARN("Security for %p failed: %s level %u err %d", conn, addr, level, err);
		return;
	}

	TERM_INFO("\e[95mSecurity for conn (%d) @ %p changed: %s level %u", conn_ref_index, conn,
		  addr, level);

	atomic_set_bit(conn_info_ref->flags, CONN_INFO_SECURITY_LEVEL_UPDATED);
}
#endif /* CONFIG_BT_SMP */

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_req = le_param_req,
	.le_param_updated = le_param_updated,
#if defined(CONFIG_BT_SMP)
	.security_changed = security_changed,
#endif /* CONFIG_BT_SMP */
#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
	.le_data_len_updated = le_data_len_updated,
#endif /* CONFIG_BT_USER_DATA_LEN_UPDATE */
};

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	TERM_INFO("Updated MTU: TX: %d RX: %d bytes", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	int conn_ref_index;
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	conn_info_ref = get_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return;
	}

	conn_ref_index = (conn_info_ref - conn_infos);

	TERM_PRINT("MTU exchange addr %s conn %u %s", addr, conn_ref_index,
		   err == 0U ? "successful" : "failed");

	atomic_set_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED);
}

static void exchange_mtu(struct bt_conn *conn, void *data)
{
	int conn_ref_index;
	struct conn_info *conn_info_ref;

	conn_info_ref = get_conn_info_ref(conn);
	if (conn_info_ref == NULL) {
		return;
	}

	conn_ref_index = (conn_info_ref - conn_infos);

	if (atomic_test_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED) == false &&
	    atomic_test_bit(conn_info_ref->flags, CONN_INFO_INITIATE_MTU_EXCHANGE_REQ) == false) {
		int err;
		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		TERM_PRINT("MTU conn (%u): %u", conn_ref_index, bt_gatt_get_mtu(conn));
		TERM_PRINT("Updating MTU for %s...", addr);

		conn_info_ref->mtu_exchange_params.func = mtu_exchange_cb;

		err = bt_gatt_exchange_mtu(conn, &conn_info_ref->mtu_exchange_params);
		if (err) {
			FAIL("MTU exchange failed (err %d)", err);
		} else {
			TERM_PRINT("Exchange pending...");
		}

		atomic_set_bit(conn_info_ref->flags, CONN_INFO_INITIATE_MTU_EXCHANGE_REQ);
	}
}

static void subscribe_to_service(struct bt_conn *conn, void *data)
{
	struct conn_info *conn_info_ref;

	conn_info_ref = get_conn_info_ref(conn);
	if (conn_info_ref == NULL) {
		return;
	}

#if defined(CONFIG_BT_SMP)
	/* Characterstic subscription requires secuirty level update */
	if (atomic_test_bit(conn_info_ref->flags, CONN_INFO_SECURITY_LEVEL_UPDATED) == false) {
		return;
	}
#endif

	if (atomic_test_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED_TO_SERVICE) == false &&
	    atomic_test_bit(conn_info_ref->flags, CONN_INFO_INITIATE_SUBSCRIBTION_REQ) == false) {
		int err;
		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		if (conn_info_ref->subscribe_params.notify != NULL) {
			return;
		}

		TERM_PRINT("Discovering peer %s primary service", addr);

		update_conn_info_peer_addr(conn_info_ref, bt_conn_get_dst(conn));

		memcpy(&conn_info_ref->uuid, SERVICE_UUID, sizeof(conn_info_ref->uuid));
		conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
		conn_info_ref->discover_params.func = discover_func;
		conn_info_ref->discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		conn_info_ref->discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		conn_info_ref->discover_params.type = BT_GATT_DISCOVER_PRIMARY;

		err = bt_gatt_discover(conn, &conn_info_ref->discover_params);
		if (err) {
			FAIL("Discover failed(err %d)", err);
			return;
		}

		atomic_set_bit(conn_info_ref->flags, CONN_INFO_INITIATE_SUBSCRIBTION_REQ);
	}
}

static void notify_peer(struct bt_conn *conn, void *data)
{
	int err, conn_ref_index;
	uint32_t dyn_data_size;
	struct bt_gatt_attr *vnd_ind_attr = (struct bt_gatt_attr *)data;
	struct conn_info *conn_info_ref;

	/* Check if the peer has subscribed to the service */
	if (bt_gatt_is_subscribed(conn, vnd_ind_attr, BT_GATT_CCC_NOTIFY) == false) {
		return;
	}

	conn_info_ref = get_conn_info_ref(conn);
	CHECKIF(conn_info_ref == NULL) {
		TERM_WARN("Invalid reference returned");
		return;
	}

	conn_ref_index = (conn_info_ref - conn_infos);

	dyn_data_size = MIN((bt_gatt_get_mtu(conn) - 3), notification_size);
	if (dyn_data_size == 0) {
		TERM_WARN("Invalid MTU size");
		return;
	}

	if (conn_info_ref->tx_notify_counter <= 5 || conn_info_ref->tx_notify_counter % 10 == 0) {
		TERM_PRINT(
			"\e[96mSending notification over connection (%d) @ %p size %d counter : %d",
			conn_ref_index, conn, dyn_data_size, conn_info_ref->tx_notify_counter);
	}

	memset(conn_info_ref->vnd_value, 0x00, sizeof(conn_info_ref->vnd_value));
	snprintk(conn_info_ref->vnd_value, notification_size, "%s%u", NOTIFICATION_DATA_PREFIX,
		 conn_info_ref->tx_notify_counter);
	err = bt_gatt_notify(conn, vnd_ind_attr, conn_info_ref->vnd_value, dyn_data_size);
	if (err) {
		TERM_WARN("Couldn't send GATT notification");
		return;
	}

	conn_info_ref->tx_notify_counter++;
}

void test_central_main(void)
{
	int err;
	struct bt_gatt_attr *vnd_ind_attr;

	memset(&conn_infos, 0x00, sizeof(conn_infos));

	err = bt_enable(NULL);

	if (err) {
		FAIL("Bluetooth init failed (err %d)", err);
		return;
	}

	TERM_PRINT("Bluetooth initialized");

	bt_gatt_cb_register(&gatt_callbacks);

	vnd_ind_attr = common_get_prim_srvc_attr();

	start_scan();

	while (true) {

		/* Connect to peripherals when there is a room */
		if (conn_count < CONFIG_BT_MAX_CONN &&
		    !atomic_test_bit(status_flags, BT_IS_SCANNING) == true &&
		    !atomic_test_bit(status_flags, BT_IS_CONNECTING) == true) {
			start_scan();
		}

		k_sleep(K_MSEC(100));

		if (conn_count > 0) {
			bt_conn_foreach(BT_CONN_TYPE_LE, subscribe_to_service, NULL);
			k_sleep(K_MSEC(100));
			bt_conn_foreach(BT_CONN_TYPE_LE, notify_peer, vnd_ind_attr);
			k_sleep(K_MSEC(100));
			bt_conn_foreach(BT_CONN_TYPE_LE, exchange_mtu, NULL);
		}

		k_sleep(K_MSEC(500));
	}
}

void test_init(void)
{
	extern enum bst_result_t bst_result;

	TERM_INFO("Initializing Test");
	bst_result = Passed;
}

static void test_args(int argc, char **argv)
{
	conn_interval_max = DEFAULT_CONN_INTERVAL;
	notification_size = NOTIFICATION_DATA_LEN;

	if (argc >= 1) {
		char const *ptr;

		ptr = strstr(argv[0], "notify_size=");
		if (ptr != NULL) {
			ptr += strlen("notify_size=");
			notification_size = atol(ptr);
			notification_size = MIN(NOTIFICATION_DATA_LEN, notification_size);
		}
	}

	if (argc == 2) {
		char const *ptr;

		ptr = strstr(argv[1], "conn_interval=");
		if (ptr != NULL) {
			ptr += strlen("conn_interval=");
			conn_interval_max = atol(ptr);
		}
	}

	bs_trace_raw(0, "Connection interval max : %d\n", conn_interval_max);
	bs_trace_raw(0, "Notification data size : %d\n", notification_size);
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "central",
		.test_descr = "Central Connection Stress",
		.test_args_f = test_args,
		.test_post_init_f = test_init,
		.test_main_f = test_central_main
	},
	BSTEST_END_MARKER
};

struct bst_test_list *test_main_conn_stress_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}

extern struct bst_test_list *test_main_conn_stress_install(struct bst_test_list *tests);

bst_test_install_t test_installers[] = {
	test_main_conn_stress_install,
	NULL
};

void main(void)
{
	bst_main();
}
