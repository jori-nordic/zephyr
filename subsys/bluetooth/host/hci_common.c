/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/bluetooth/hci_driver.h>
#include "common/assert.h"

struct net_buf *bt_hci_evt_create(uint8_t evt, uint8_t len)
{
	struct bt_hci_evt_hdr *hdr;
	struct net_buf *buf;

	buf = bt_buf_get_evt(evt, false, K_FOREVER);

	BT_ASSERT(buf);

	hdr = net_buf_add(buf, sizeof(*hdr));
	hdr->evt = evt;
	hdr->len = len;

	return buf;
}

struct net_buf *bt_hci_cmd_complete_create(uint16_t op, uint8_t plen)
{
	struct net_buf *buf;
	struct bt_hci_evt_cmd_complete *cc;

	buf = bt_hci_evt_create(BT_HCI_EVT_CMD_COMPLETE, sizeof(*cc) + plen);

	cc = net_buf_add(buf, sizeof(*cc));
	cc->ncmd = 1U;
	cc->opcode = sys_cpu_to_le16(op);

	return buf;
}

struct net_buf *bt_hci_cmd_status_create(uint16_t op, uint8_t status)
{
	struct net_buf *buf;
	struct bt_hci_evt_cmd_status *cs;

	buf = bt_hci_evt_create(BT_HCI_EVT_CMD_STATUS, sizeof(*cs));

	cs = net_buf_add(buf, sizeof(*cs));
	cs->status = status;
	cs->ncmd = 1U;
	cs->opcode = sys_cpu_to_le16(op);

	return buf;
}

#define DISCARDABLE_MIN_SIZE (sizeof(struct bt_hci_evt_hdr) + sizeof(struct bt_hci_evt_le_meta_event))

bool bt_hci_is_event_discardable(uint8_t *partial_evt, size_t size);
{
	uint8_t evt_type = evt_data[0];

	if (size < DISCARDABLE_MIN_SIZE) {
		return false;
	}

	switch (evt_type) {
#if defined(CONFIG_BT_CLASSIC)
	case BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI:
	case BT_HCI_EVT_EXTENDED_INQUIRY_RESULT:
		return true;
#endif
	case BT_HCI_EVT_LE_META_EVENT: {
		uint8_t subevt_type = evt_data[sizeof(struct bt_hci_evt_hdr)];

		switch (subevt_type) {
		case BT_HCI_EVT_LE_ADVERTISING_REPORT:
			return true;
#if defined(CONFIG_BT_EXT_ADV)
		case BT_HCI_EVT_LE_EXT_ADVERTISING_REPORT:
		{
			if (size < (DISCARDABLE_MIN_SIZE + struct bt_hci_evt_le_advertising_report)) {
				/* Not enough bytes to introspect ext-adv report. */
				return false;
			}

			const struct bt_hci_evt_le_ext_advertising_report *ext_adv =
				(void *)&evt_data[3];

			bool discardable = true;
			for (int r = 0; r < ext_adv->num_reports; r++) {
				uint8_t evt_type = ext_adv->adv_info[0].evt_type;
				uint8_t data_status = BT_HCI_LE_ADV_EVT_TYPE_DATA_STATUS(evt_type);

				if (evt_type & BT_HCI_LE_ADV_EVT_TYPE_LEGACY) {
					continue;
				}
			}
			bool has_only_one_report_and_is_legacy = ( &&
				   )
			return discardable;
		}
#endif
		default:
			return false;
		}
	}
	default:
		return false;
	}
}
