/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/check.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(peripheral, LOG_LEVEL_INF);

#include "bstests.h"
#include "bs_types.h"
#include "bs_tracing.h"
#include "bstests.h"
#include "bs_pc_backchannel.h"

#define DEFAULT_CONN_INTERVAL	   20
#define PERIPHERAL_DEVICE_NAME	   "Zephyr Peripheral"
#define PERIPHERAL_DEVICE_NAME_LEN (sizeof(PERIPHERAL_DEVICE_NAME) - 1)

#define NOTIFICATION_DATA_PREFIX     "Counter:"
#define NOTIFICATION_DATA_PREFIX_LEN (sizeof(NOTIFICATION_DATA_PREFIX) - 1)

#define CHARACTERISTIC_DATA_MAX_LEN 260
#define NOTIFICATION_DATA_LEN	    MAX(200, (CONFIG_BT_L2CAP_TX_MTU - 4))
BUILD_ASSERT(NOTIFICATION_DATA_LEN <= CHARACTERISTIC_DATA_MAX_LEN);

#define PERIPHERAL_SERVICE_UUID_VAL                                                                \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define PERIPHERAL_CHARACTERISTIC_UUID_VAL                                                         \
	BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

#define PERIPHERAL_SERVICE_UUID	       BT_UUID_DECLARE_128(PERIPHERAL_SERVICE_UUID_VAL)
#define PERIPHERAL_CHARACTERISTIC_UUID BT_UUID_DECLARE_128(PERIPHERAL_CHARACTERISTIC_UUID_VAL)

static struct bt_uuid_128 vnd_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdea0));

static struct bt_uuid_128 vnd_enc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdea1));

static void start_scan(void);
static void stop_scan(void);

enum {
	BT_IS_SCANNING,
	BT_IS_CONNECTING,
	/* Total number of flags - must be at the end of the enum */
	BT_IS_NUM_FLAGS,
};

enum {
	CONN_INFO_CONN_PARAMS_UPDATED,
	CONN_INFO_LL_DATA_LEN_TX_UPDATED,
	CONN_INFO_LL_DATA_LEN_RX_UPDATED,
	CONN_INFO_MTU_EXCHANGED,
	CONN_INFO_DISCOVERING,
	CONN_INFO_SUBSCRIBED,
	/* Total number of flags - must be at the end of the enum */
	CONN_INFO_NUM_FLAGS,
};

ATOMIC_DEFINE(status_flags, BT_IS_NUM_FLAGS);
static uint8_t volatile conn_count;
static struct bt_conn *conn_connecting;
#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
static struct bt_conn_le_data_len_param le_data_len_param;
#endif
static struct bt_gatt_exchange_params mtu_exchange_params;

struct conn_info {
	ATOMIC_DEFINE(flags, CONN_INFO_NUM_FLAGS);
	struct bt_conn *conn_ref;
	uint32_t notify_counter;
	uint32_t tx_notify_counter;
	struct bt_uuid_128 uuid;
	struct bt_gatt_discover_params discover_params;
	struct bt_gatt_subscribe_params subscribe_params;
	bt_addr_le_t addr;
};

void clear_info(struct conn_info *info)
{
	/* clear everything except the address + sub params (lifetime > connection) */
	memset(&info->flags, 0, sizeof(info->flags));
	memset(&info->conn_ref, 0, sizeof(info->conn_ref));
	memset(&info->notify_counter, 0, sizeof(info->notify_counter));
	memset(&info->tx_notify_counter, 0, sizeof(info->tx_notify_counter));
}

static struct conn_info conn_infos[CONFIG_BT_MAX_CONN] = {0};

static uint32_t conn_interval_max, notification_size;
static uint8_t simulate_vnd;
static uint8_t vnd_value[CHARACTERISTIC_DATA_MAX_LEN];

static void vnd_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	simulate_vnd = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

/* Vendor Primary Service Declaration */
BT_GATT_SERVICE_DEFINE(
	vnd_svc, BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
	BT_GATT_CHARACTERISTIC(&vnd_enc_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL, NULL,
			       NULL),
	BT_GATT_CCC(vnd_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

static struct conn_info *get_new_conn_info_ref(const bt_addr_le_t *addr)
{
	/* try to find per-addr first */
	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (bt_addr_le_eq(&conn_infos[i].addr, addr)) {
			return &conn_infos[i];
		}
	}

	/* try to allocate if addr not found */
	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) {
		if (conn_infos[i].conn_ref == NULL) {
			bt_addr_le_copy(&conn_infos[i].addr, addr);

			return &conn_infos[i];
		}
	}

	__ASSERT(0, "ran out of contexts");
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

static bool is_connected(struct bt_conn *conn)
{
	struct bt_conn_info info;

	int err = bt_conn_get_info(conn, &info);
	__ASSERT(!err, "Couldn't get conn info %d", err);

	return info.state == BT_CONN_STATE_CONNECTED;
}

static struct conn_info *get_connected_conn_info_ref(struct bt_conn *conn)
{
	if (is_connected(conn)) {
		return get_conn_info_ref(conn);
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

/* static bool check_all_flags_set(int bit) */
/* { */
/* 	for (size_t i = 0; i < ARRAY_SIZE(conn_infos); i++) { */
/* 		if (atomic_test_bit(conn_infos[i].flags, bit) == false) { */
/* 			return false; */
/* 		} */
/* 	} */

/* 	return true; */
/* } */

static void send_update_conn_params_req(struct bt_conn *conn)
{
	struct conn_info *conn_info_ref;

	conn_info_ref = get_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref);

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

		LOG_DBG("Updating connection parameters: %s", addr);

		err = bt_conn_le_param_update(conn, &param);
		if (err) {
			LOG_ERR("Updating connection parameters failed %s", addr);
			return;
		}

		LOG_INF("Updating connection parameters succeeded %s", addr);
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
		LOG_INF("[UNSUBSCRIBED] addr %s", addr);
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	conn_info_ref = get_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref);

	received_counter = strtoul(data_ptr, NULL, 0);

	LOG_DBG("[NOTIFICATION] addr %s data %s length %u cnt %u",
		addr, data, length, received_counter);

	__ASSERT(conn_info_ref->notify_counter == received_counter,
		 "addr %s expected counter : %u , received counter : %u", addr, conn_info_ref->notify_counter,
		 received_counter);
	conn_info_ref->notify_counter++;

	return BT_GATT_ITER_CONTINUE;
}

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;
	char uuid_str[BT_UUID_STR_LEN];

	if (!attr) {
		/* We might be called from the ATT disconnection callback if we
		 * have an ongoing procedure.
		 */
		__ASSERT_NO_MSG(!is_connected(conn));
		return BT_GATT_ITER_STOP;
	}
	__ASSERT(attr, "Did not find requested UUID");

	bt_uuid_to_str(params->uuid, uuid_str, sizeof(uuid_str));
	LOG_DBG("UUID found : %s", uuid_str);

	LOG_DBG("[ATTRIBUTE] handle %u", attr->handle);

	struct conn_info *conn_info_ref = get_connected_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref);

	if (conn_info_ref->discover_params.type == BT_GATT_DISCOVER_PRIMARY) {
		LOG_DBG("Primary Service Found");
		memcpy(&conn_info_ref->uuid, PERIPHERAL_CHARACTERISTIC_UUID, sizeof(conn_info_ref->uuid));
		conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
		conn_info_ref->discover_params.start_handle = attr->handle + 1;
		conn_info_ref->discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, &conn_info_ref->discover_params);
		if (err == -ENOMEM || err == -ENOTCONN) {
			goto retry;
		}

		__ASSERT(!err, "Discover failed (err %d)", err);

	} else if (conn_info_ref->discover_params.type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		LOG_DBG("Service Characteristic Found");
		memcpy(&conn_info_ref->uuid, BT_UUID_GATT_CCC, sizeof(conn_info_ref->uuid));
		conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
		conn_info_ref->discover_params.start_handle = attr->handle + 2;
		conn_info_ref->discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		conn_info_ref->subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &conn_info_ref->discover_params);
		if (err == -ENOMEM || err == -ENOTCONN) {
			goto retry;
		}

		__ASSERT(!err, "Discover failed (err %d)", err);

	} else {
		conn_info_ref->subscribe_params.notify = notify_func;
		conn_info_ref->subscribe_params.value = BT_GATT_CCC_NOTIFY;
		conn_info_ref->subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &conn_info_ref->subscribe_params);
		if (err == -ENOMEM || err == -ENOTCONN) {
			goto retry;
		}

		if (err != -EALREADY) {
			__ASSERT(!err, "Subscribe failed (err %d)", err);
		}

		__ASSERT_NO_MSG(atomic_test_bit(conn_info_ref->flags, CONN_INFO_DISCOVERING));
		__ASSERT_NO_MSG(!atomic_test_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED));
		atomic_set_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED);

		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
		LOG_INF("[SUBSCRIBED] addr %s", addr);
	}

	return BT_GATT_ITER_STOP;

retry:
	/* if we're out of buffers or metadata contexts, restart discovery
	 * later.
	 */
	LOG_DBG("out of memory/not connected, retry sub later");
	atomic_clear_bit(conn_info_ref->flags, CONN_INFO_DISCOVERING);

	return BT_GATT_ITER_STOP;
}

static bool parse_ad(struct bt_data *data, void *user_data)
{
	bt_addr_le_t *addr = user_data;

	LOG_DBG("[AD]: %u data_len %u", data->type, data->data_len);

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		LOG_INF("------------------------------------------------------");
		LOG_INF("Device name : %.*s", data->data_len, data->data);

		if (check_if_peer_connected(addr) == true) {
			LOG_ERR("Peer is already connected or in disconnecting state");
			break;
		}

		__ASSERT(!atomic_test_bit(status_flags, BT_IS_CONNECTING), "A connection procedure is ongoing");
		atomic_set_bit(status_flags, BT_IS_CONNECTING);

		stop_scan();

		char addr_str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
		LOG_INF("Connecting to %s", addr_str);

		struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
		int err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param,
					    &conn_connecting);

		__ASSERT(!err, "Create conn failed (err %d)", err);

		return false;

		break;
	}

	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	/* We're only interested in connectable events */
	if (type == BT_GAP_ADV_TYPE_SCAN_RSP) {
		bt_data_parse(ad, parse_ad, (void *)addr);
	}
}

static void start_scan(void)
{
	int err;
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	atomic_set_bit(status_flags, BT_IS_SCANNING);

	err = bt_le_scan_start(&scan_param, device_found);
	__ASSERT(!err, "Scanning failed to start (err %d)", err);

	LOG_INF("Started scanning");
}

static void stop_scan(void)
{
	int err;

	__ASSERT(atomic_test_bit(status_flags, BT_IS_SCANNING), "No scanning procedure is ongoing");
	atomic_clear_bit(status_flags, BT_IS_SCANNING);

	err = bt_le_scan_stop();
	__ASSERT(!err, "Stop LE scan failed (err %d)", err);

	LOG_INF("Stopped scanning");
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	int err;
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	__ASSERT(conn_err == BT_HCI_ERR_SUCCESS, "Failed to connect to %s (%u)", addr, conn_err);

	LOG_INF("Connection %p established : %s", conn, addr);

	conn_count++;
	LOG_DBG("connected to %u devices", conn_count);

	conn_info_ref = get_new_conn_info_ref(bt_conn_get_dst(conn));
	__ASSERT_NO_MSG(conn_info_ref->conn_ref == NULL);

	conn_info_ref->conn_ref = conn_connecting;

#if defined(CONFIG_BT_SMP)
	err = bt_conn_set_security(conn, BT_SECURITY_L2);

	if (!err) {
		LOG_INF("Security level is set to : %u", BT_SECURITY_L2);
	} else {
		LOG_ERR("Failed to set security (%d).", err);
	}
#endif /* CONFIG_BT_SMP */
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

	conn_info_ref = get_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref->conn_ref != NULL);
	__ASSERT_NO_MSG(reason == BT_HCI_ERR_REMOTE_POWER_OFF);

	bt_conn_unref(conn);
	clear_info(conn_info_ref);

	conn_count--;		/* TODO: should be atomic? */
	LOG_DBG("Connection reference store index %u", (conn_info_ref - conn_infos));
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("LE conn param req: %s int (0x%04x (~%u ms), 0x%04x (~%u ms)) lat %d to %d",
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

	LOG_INF("LE conn param updated: %s int 0x%04x (~%u ms) lat %d to %d", addr, interval,
		(uint32_t)(interval * 1.25), latency, timeout);

	conn_info_ref = get_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref);

	atomic_set_bit(conn_info_ref->flags, CONN_INFO_CONN_PARAMS_UPDATED);

	__ASSERT_NO_MSG(conn == conn_connecting);
	if (conn == conn_connecting) {
		conn_connecting = NULL;
		atomic_clear_bit(status_flags, BT_IS_CONNECTING);
	}
}

#if defined(CONFIG_BT_SMP)
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	__ASSERT(!err, "Security for %s failed", addr);
	LOG_INF("Security for %s changed: level %u", addr, level);
}
#endif /* CONFIG_BT_SMP */

#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
static void le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
	int conn_ref_index;
	struct conn_info *conn_info_ref;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_DBG("Data length updated: %s max tx %u (%u us) max rx %u (%u us)", addr,
		info->tx_max_len, info->tx_max_time, info->rx_max_len, info->rx_max_time);

	conn_info_ref = get_conn_info_ref(conn);
	__ASSERT_NO_MSG(conn_info_ref);

	if (info->rx_max_len == BT_GAP_DATA_LEN_MAX) {
		LOG_INF("RX Data length updated %s: conn %u", addr);
		atomic_set_bit(conn_info_ref->flags, CONN_INFO_LL_DATA_LEN_RX_UPDATED);
	}
	if (info->tx_max_len == BT_GAP_DATA_LEN_MAX) {
		LOG_INF("TX Data length updated %s: conn %u", addr);
		atomic_set_bit(conn_info_ref->flags, CONN_INFO_LL_DATA_LEN_TX_UPDATED);
	}
}
#endif /* CONFIG_BT_USER_DATA_LEN_UPDATE */

static void identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
			      const bt_addr_le_t *identity)
{
	char addr_identity[BT_ADDR_LE_STR_LEN];
	char addr_rpa[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
	bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

	LOG_ERR("Identity resolved %s -> %s", addr_rpa, addr_identity);

	/* overwrite RPA */
	struct conn_info *conn_info_ref = get_conn_info_ref(conn);
	bt_addr_le_copy(&conn_info_ref->addr, identity);
}

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
	.identity_resolved = identity_resolved,
};

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	LOG_INF("Updated MTU: TX: %d RX: %d bytes", tx, rx);
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
		LOG_WRN("Invalid reference returned");
		return;
	}

	conn_ref_index = (conn_info_ref - conn_infos);

	LOG_DBG("MTU exchange addr %s conn %u %s", addr, conn_ref_index,
		   err == 0U ? "successful" : "failed");

	atomic_set_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED);
}

static void exchange_mtu(struct bt_conn *conn, void *data)
{
	int conn_ref_index;
	struct conn_info *conn_info_ref;

	conn_info_ref = get_connected_conn_info_ref(conn);
	if (conn_info_ref == NULL) {
		LOG_INF("not connected");
		return;
	}

	conn_ref_index = (conn_info_ref - conn_infos);

	if (atomic_test_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED) == false) {
		int err;
		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		LOG_DBG("MTU conn (%u): %u", conn_ref_index, bt_gatt_get_mtu(conn));
		LOG_DBG("Updating MTU for %s...", addr);

		mtu_exchange_params.func = mtu_exchange_cb;

		err = bt_gatt_exchange_mtu(conn, &mtu_exchange_params);
		if (err) {
			LOG_ERR("MTU exchange failed (err %d)", err);
		} else {
			LOG_DBG("Exchange pending...");
		}

		while (conn_info_ref->conn_ref &&
		       !atomic_test_bit(conn_info_ref->flags, CONN_INFO_MTU_EXCHANGED)) {
			k_sleep(K_MSEC(10));
		}

		LOG_INF("Updating MTU succeeded %s", addr);
	}
}

#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
static uint16_t tx_time_calc(uint8_t phy, uint16_t max_len)
{
	/* Access address + header + payload + MIC + CRC */
	uint16_t total_len = 4 + 2 + max_len + 4 + 3;

	switch (phy) {
	case BT_GAP_LE_PHY_1M:
		/* 1 byte preamble, 8 us per byte */
		return 8 * (1 + total_len);
	case BT_GAP_LE_PHY_2M:
		/* 2 byte preamble, 4 us per byte */
		return 4 * (2 + total_len);
	case BT_GAP_LE_PHY_CODED:
		/* S8: Preamble + CI + TERM1 + 64 us per byte + TERM2 */
		return 80 + 16 + 24 + 64 * (total_len) + 24;
	default:
		return 0;
	}
}

static void update_ll_max_data_length(struct bt_conn *conn, void *data)
{
	int conn_ref_index;
	struct conn_info *conn_info_ref;

	conn_info_ref = get_connected_conn_info_ref(conn);
	if (conn_info_ref == NULL) {
		LOG_INF("not connected");
		return;
	}

	conn_ref_index = (conn_info_ref - conn_infos);

	if (atomic_test_bit(conn_info_ref->flags, CONN_INFO_LL_DATA_LEN_TX_UPDATED) == false) {
		int err;
		char addr[BT_ADDR_LE_STR_LEN];

		le_data_len_param = *BT_LE_DATA_LEN_PARAM_DEFAULT;

		/* Update LL transmission payload size in bytes*/
		le_data_len_param.tx_max_len = BT_GAP_DATA_LEN_MAX;
		/* Update LL transmission payload time in us*/
		le_data_len_param.tx_max_time =
			tx_time_calc(BT_GAP_LE_PHY_2M, le_data_len_param.tx_max_len);
		LOG_DBG("Calculated tx time: %d", le_data_len_param.tx_max_time);

		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		LOG_DBG("Updating LL data length for addr %s conn %u", addr, conn_ref_index);
		err = bt_conn_le_data_len_update(conn, &le_data_len_param);
		if (err) {
			LOG_ERR("Updating LL data length failed addr %s conn %u", addr,
				 conn_ref_index);
			return;
		}

		while (atomic_test_bit(conn_info_ref->flags, CONN_INFO_LL_DATA_LEN_TX_UPDATED) ==
		       false) {
			k_sleep(K_MSEC(10));
		}

		LOG_INF("Updating LL data length succeeded addr %s conn %u", addr,
			     conn_ref_index);
	}
}
#endif /* CONFIG_BT_USER_DATA_LEN_UPDATE */

static void subscribe_to_service(struct bt_conn *conn, void *data)
{
	struct conn_info *conn_info_ref;

	conn_info_ref = get_connected_conn_info_ref(conn);
	if (conn_info_ref == NULL) {
		LOG_INF("not connected");
		return;
	}

	if (!atomic_test_and_set_bit(conn_info_ref->flags, CONN_INFO_DISCOVERING) &&
	    !atomic_test_bit(conn_info_ref->flags, CONN_INFO_SUBSCRIBED)) {
		int err;
		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		memcpy(&conn_info_ref->uuid, PERIPHERAL_SERVICE_UUID, sizeof(conn_info_ref->uuid));
		memset(&conn_info_ref->discover_params, 0, sizeof(conn_info_ref->discover_params));

		conn_info_ref->discover_params.uuid = &conn_info_ref->uuid.uuid;
		conn_info_ref->discover_params.func = discover_func;
		conn_info_ref->discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		conn_info_ref->discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		conn_info_ref->discover_params.type = BT_GATT_DISCOVER_PRIMARY;

		err = bt_gatt_discover(conn, &conn_info_ref->discover_params);
		if (err == -ENOMEM || err == -ENOTCONN) {
			LOG_DBG("out of memory, retry sub later");
			atomic_clear_bit(conn_info_ref->flags, CONN_INFO_DISCOVERING);
			return;
		}

		__ASSERT(!err, "Subscribe failed (err %d)", err);
	}
}

static void notify_peers(struct bt_conn *conn, void *data)
{
	int err;
	struct bt_gatt_attr *vnd_ind_attr = (struct bt_gatt_attr *)data;
	struct conn_info *conn_info_ref;

	conn_info_ref = get_connected_conn_info_ref(conn);
	if (conn_info_ref == NULL) {
		LOG_INF("not connected");
		return;
	}

	memset(vnd_value, 0x00, sizeof(vnd_value));
	snprintk(vnd_value, notification_size, "%s%u", NOTIFICATION_DATA_PREFIX,
		 conn_info_ref->tx_notify_counter);
	err = bt_gatt_notify(conn, vnd_ind_attr, vnd_value, notification_size);
	if (err) {
		LOG_ERR("Couldn't send GATT notification");
		return;
	} else {
		char addr[BT_ADDR_LE_STR_LEN];
		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

		LOG_DBG("central notified: %s %d", addr, conn_info_ref->tx_notify_counter);
	}

	conn_info_ref->tx_notify_counter++;
}

void test_central_main(void)
{
	int err;
	struct bt_gatt_attr *vnd_ind_attr;
	char str[BT_UUID_STR_LEN];

	memset(&conn_infos, 0x00, sizeof(conn_infos));

	err = bt_enable(NULL);

	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}

	LOG_DBG("Bluetooth initialized");

	bt_gatt_cb_register(&gatt_callbacks);

	vnd_ind_attr = bt_gatt_find_by_uuid(vnd_svc.attrs, vnd_svc.attr_count, &vnd_enc_uuid.uuid);

	bt_uuid_to_str(&vnd_enc_uuid.uuid, str, sizeof(str));
	LOG_DBG("Indicate VND attr %p (UUID %s)", vnd_ind_attr, str);

	start_scan();

	while (true) {
		/* reconnect peripherals when they drop out */
		if (conn_count < CONFIG_BT_MAX_CONN &&
		    !atomic_test_bit(status_flags, BT_IS_SCANNING) &&
		    !atomic_test_bit(status_flags, BT_IS_CONNECTING)) {
			start_scan();
		} else {
			if (atomic_test_bit(status_flags, BT_IS_CONNECTING)) {
				char addr[BT_ADDR_LE_STR_LEN];
				bt_addr_le_to_str(bt_conn_get_dst(conn_connecting), addr, sizeof(addr));
				LOG_DBG("already connecting to: %s", addr);
			}
		}

		bt_conn_foreach(BT_CONN_TYPE_LE, exchange_mtu, NULL);
#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
		bt_conn_foreach(BT_CONN_TYPE_LE, update_ll_max_data_length, NULL);
#endif /* CONFIG_BT_USER_DATA_LEN_UPDATE */
		bt_conn_foreach(BT_CONN_TYPE_LE, subscribe_to_service, NULL);
		bt_conn_foreach(BT_CONN_TYPE_LE, notify_peers, vnd_ind_attr);
		k_msleep(10);
	}
}

void test_init(void)
{
	extern enum bst_result_t bst_result;

	LOG_INF("Initializing Test");
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
