/* main.c - Application main entry point */

/*
 * Copyright (c) 2024 Giancarlo Stasi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/bluetooth/addr.h>
#include <host/hci_core.h>
#include <common/bt_str.h>

#if !defined(CONFIG_BT_EXT_ADV) && !defined(CONFIG_BT_CTLR_VS_SCAN_REQ_RX)
#error "Either BT_EXT_ADV or BT_CTLR_VS_SCAN_REQ_RX must be selected to have scan request callback"
#endif
#if !defined(CONFIG_BT_HCI_VS_EVT_USER)
#error "CONFIG_BT_HCI_VS_EVT_USER must be selected"
#endif

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LENGTH (sizeof(DEVICE_NAME) - 1)

/* Advertising Interval: the longer, the less energy consumption.
 * Units: 0.625 milliseconds.
 * The Minimum Advertising Interval and Maximum Advertising Interval should not be the same value
 * (as stated in Bluetooth Core Spec 5.2, section 7.8.5)
 */
#define ADV_MIN_INTERVAL_625MS  BT_GAP_ADV_SLOW_INT_MIN
#define ADV_MAX_INTERVAL_625MS  BT_GAP_ADV_SLOW_INT_MAX

#if defined(CONFIG_BT_PERIPHERAL)
#define ADV_OPTIONS             (BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_SCANNABLE | \
				 BT_LE_ADV_OPT_NOTIFY_SCAN_REQ)
#else
#define ADV_OPTIONS             (BT_LE_ADV_OPT_SCANNABLE | BT_LE_ADV_OPT_NOTIFY_SCAN_REQ)
#endif

/* Lower the value, closer the scanner. */
/* #define ADV_TX_POWER RADIO_TXPOWER_TXPOWER_Neg8dBm */

static const struct bt_le_adv_param parameters = {
	.options = ADV_OPTIONS,
	.interval_min = ADV_MIN_INTERVAL_625MS,
	.interval_max = ADV_MAX_INTERVAL_625MS,
};

static const struct bt_data advertisement_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LENGTH),
};

static const struct bt_data scan_response_data[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, 0),
};

#if defined(CONFIG_BT_EXT_ADV)
static struct bt_le_ext_adv *ext_adv;
#endif

#if defined(CONFIG_BT_PERIPHERAL) && defined(CONFIG_BT_EXT_ADV)
static volatile bool disconnect_event_happened;
#endif

/* Allow different TX power per role/connection. It requires CONFIG_BT_CTLR_TX_PWR_DYNAMIC_CONTROL.
 * same as in samples/bluetooth/hci_pwr_ctrl/src
 */
static void set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl)
{
	struct bt_hci_cp_vs_write_tx_power_level *cp;
	struct bt_hci_rp_vs_write_tx_power_level *rp;
	struct net_buf *buf, *rsp = NULL;
	int err;

	buf = bt_hci_cmd_create(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL, sizeof(*cp));
	if (!buf) {
		printk("Unable to allocate command buffer\n");
		return;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);
	cp->handle_type = handle_type;
	cp->tx_power_level = tx_pwr_lvl;

	err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL,
				   buf, &rsp);
	if (err) {
		uint8_t reason = rsp ?
			((struct bt_hci_rp_vs_write_tx_power_level *)
			  rsp->data)->status : 0;
		printk("Set Tx power err: %d reason 0x%02x\n", err, reason);
		return;
	}

	rp = (void *)rsp->data;
	printk("Actual Tx Power: %d\n", rp->selected_tx_power);

	net_buf_unref(rsp);
}

#if defined(CONFIG_BT_CTLR_VS_SCAN_REQ_RX)
/* BLE standard doesn't allow the scan request event with legacy advertisements.
 * Ref: Bluetooth Core Specification v5.3, section 7.7.65.19 "LE Scan Request Received event" :
 *      "This event shall only be generated if advertising was enabled using the
 *       HCI_LE_Set_Extended_Advertising_Enable command."
 * Added a Vendor Specific command to add this feature and save ~ 1.5 kB SRAM.
 * (Higher RAM saving up to 5 kB observed in larger applications).
 */
static void enable_legacy_adv_scan_request_event(bool enable)
{
	struct bt_hci_cp_vs_set_scan_req_reports *cp;
	struct net_buf *buf;
	int err;

	buf = bt_hci_cmd_create(BT_HCI_OP_VS_SET_SCAN_REQ_REPORTS, sizeof(*cp));
	if (!buf) {
		printk("%s: Unable to allocate HCI command buffer\n", __func__);
		return;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->enable = (uint8_t) enable;

	err = bt_hci_cmd_send(BT_HCI_OP_VS_SET_SCAN_REQ_REPORTS, buf);
	if (err) {
		printk("Set legacy cb err: %d\n", err);
		return;
	}
}
#endif /* defined(CONFIG_BT_CTLR_VS_SCAN_REQ_RX) */

#if defined(CONFIG_BT_EXT_ADV)

static void scanned(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_scanned_info *info)
{
	char address[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(info->addr, address, sizeof(address));
	printk("%s (address %s)\n", __func__, address);
}

static const struct bt_le_ext_adv_cb ext_adv_callbacks = {
	.sent = NULL,
	.connected = NULL,
	.scanned = scanned,
};

#elif defined(CONFIG_BT_CTLR_VS_SCAN_REQ_RX)

static void scanned(struct bt_le_ext_adv_scanned_info *info, int8_t rssi)
{
	char address[BT_ADDR_LE_STR_LEN] = "fake_addr";

	bt_addr_le_to_str(info->addr, address, sizeof(address));
	printk("%s (address %s rssi %d)\n", __func__, address, rssi);
}

static const struct bt_le_vs_cb  vs_callbacks = {
	.scanned = scanned,
};

#endif /* defined(CONFIG_BT_EXT_ADV) */

#if defined(CONFIG_BT_EXT_ADV)
static void create_advertising(void)
{
	int err;

	/* Create a scannable advertising set */
	err = bt_le_ext_adv_create(&parameters, &ext_adv_callbacks, &ext_adv);
	if (err) {
		printk("Create advertising set err %d\n", err);
		return;
	}

	/* Set's advertising or scan response data */
	err = bt_le_ext_adv_set_data(ext_adv, advertisement_data, ARRAY_SIZE(advertisement_data),
				     scan_response_data, ARRAY_SIZE(scan_response_data));
	if (err) {
		printk("Set adv or scan response data err %d\n", err);
		return;
	}

	printk("Advertising set successfully created (%s)\n", CONFIG_BT_DEVICE_NAME);
}
#endif /* defined(CONFIG_BT_EXT_ADV) */

struct bt_le_ext_adv *bt_le_adv_lookup_legacy(void);

#if defined(CONFIG_BT_CTLR_VS_SCAN_REQ_RX) && defined(CONFIG_BT_HCI_VS_EVT_USER)
static bool vs_scanned(struct net_buf_simple *buf)
{
	struct bt_hci_evt_vs_scan_req_rx *evt;
	struct bt_le_ext_adv *adv;

	struct bt_hci_evt_vs *vs;

	vs = net_buf_simple_pull_mem(buf, sizeof(*vs));

	evt = (struct bt_hci_evt_vs_scan_req_rx *)buf->data;

	adv = bt_le_adv_lookup_legacy();

	printk("%s peer %s rssi %d\n", __func__, bt_addr_le_str(&evt->addr), evt->rssi);

	if (!adv) {
		printk("%s: No valid adv\n", __func__);
		return false;
	}

	bt_addr_le_t id_addr;

	if (bt_addr_le_is_resolved(&evt->addr)) {
		bt_addr_le_copy_resolved(&id_addr, &evt->addr);
	} else {
		bt_addr_le_copy(&id_addr, bt_lookup_id_addr(adv->id, &evt->addr));
	}

	printk("%s (address %s rssi %d)\n", __func__, bt_addr_le_str(&id_addr), evt->rssi);
	return false; /* TODO: return true; */
}
#endif /* defined(CONFIG_BT_CTLR_VS_SCAN_REQ_RX) && defined(CONFIG_BT_HCI_VS_EVT_USER) */

static int start_advertising(void)
{
	int err;

#if defined(CONFIG_BT_CTLR_VS_SCAN_REQ_RX)
#if defined(CONFIG_BT_HCI_VS_EVT_USER)
	err = bt_hci_register_vnd_evt_cb(vs_scanned);
#endif
	err = bt_le_vs_scan_req_cb(&vs_callbacks);
	if (err) {
		printk("Add Vendor specific callbacks err %d\n", err);
		return err;
	}
	enable_legacy_adv_scan_request_event(true);
	err = bt_le_adv_start(&parameters, advertisement_data, ARRAY_SIZE(advertisement_data),
			      scan_response_data, ARRAY_SIZE(scan_response_data));
	if (err) {
		printk("Start legacy adv err %d\n", err);
		return err;
	}
#elif defined(CONFIG_BT_EXT_ADV)
	err = bt_le_ext_adv_start(ext_adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		printk("Start ext adv err %d\n", err);
		return err;
	}
#endif /* defined(CONFIG_BT_CTLR_VS_SCAN_REQ_RX) */
	printk("Advertising successfully started (%s)\n", CONFIG_BT_DEVICE_NAME);
	return 0;
}

static void periodic_checks(void)
{
#if defined(CONFIG_BT_PERIPHERAL) && defined(CONFIG_BT_EXT_ADV)
	if (disconnect_event_happened) {
		int rv = start_advertising();

		if (rv != 0) {
			printk("start adv err: %d\n", rv);
		} else {
			disconnect_event_happened = false;
		}
	}
#endif /* defined(CONFIG_BT_PERIPHERAL) && defined(CONFIG_BT_EXT_ADV) */
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
		printk("Connected at %s\n", addr);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);

#if defined(CONFIG_BT_PERIPHERAL)
#if defined(CONFIG_BT_EXT_ADV)
	disconnect_event_happened = true;
#else
	(void) start_advertising(); /* gives errno -12 = -ENOMEM with if CONFIG_BT_EXT_ADV */
#endif
#endif
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

#if defined(CONFIG_BT_EXT_ADV)
	create_advertising();
#endif
	err = start_advertising();

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

#if defined(CONFIG_BT_EXT_ADV)
	printk("Extended advertisement Scan Request sample started\n");
#else
	printk("Vendor-Specific Scan Request sample started\n");
#endif
}

int main(void)
{
	int err;

#if defined(CONFIG_BT_EXT_ADV)
	printk("Starting Extended advertisement Scan Request sample\n");
#else
	printk("Starting Vendor-Specific Scan Request sample\n");
#endif

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
	}

	/* printk("Set Tx power level to %ld\n", ADV_TX_POWER); */
	/* set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, ADV_TX_POWER); */

	/* Wait for 5 seconds to give a chance users/testers
	 * to check that default Tx power is indeed the one
	 * selected in Kconfig.
	 */
	printk("Wait for scan\n");
	k_sleep(K_SECONDS(5));

	while (1) {
		printk("Periodic check..\n");
		periodic_checks();
		k_sleep(K_SECONDS(5));
	}
	return 0;
}
