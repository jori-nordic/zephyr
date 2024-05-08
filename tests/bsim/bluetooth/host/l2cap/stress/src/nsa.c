#include <zephyr/net/buf.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(nsa, LOG_LEVEL_DBG);

/* FIXME: this needs to be atomic */
static int packets;
static bool hodl = false;

int __real_bt_send(struct net_buf *buf);
int __real_bt_recv(struct net_buf *buf);

void start_holding(void)
{
	hodl = true;
}

bool hold_ncp_event(struct net_buf *buf)
{
	/* purpose of this fn is to count and hold the "Number of Completed
	 * Packets" events for connection handle 0.
	 *
	 * We will make a synthetic one after some time has elapsed.
	 *
	 * Doing this hopefully doesn't block other connections from sending
	 * data (but it probably will).
	 */
	if (hodl == false) {
		return false;
	}

	if (bt_buf_get_type(buf) != BT_BUF_EVT) {
		return false;
	}

	struct net_buf_simple_state state;

	net_buf_simple_save(&buf->b, &state);

	struct bt_hci_evt_hdr *hdr = net_buf_pull_mem(buf, sizeof(*hdr));

	if (hdr->evt != BT_HCI_EVT_NUM_COMPLETED_PACKETS) {
		net_buf_simple_restore(&buf->b, &state);

		return false;
	}

	struct bt_hci_evt_num_completed_packets *evt = (void *)buf->data;

	/* assume packet is correct */
	/* assume we only have one handle per NCP event (current ZLL behavior) */
	__ASSERT_NO_MSG(evt->num_handles == 1);

	uint16_t handle = sys_le16_to_cpu(evt->h[0].handle);
	uint16_t count = sys_le16_to_cpu(evt->h[0].count);

	if (handle != 0) {
		net_buf_simple_restore(&buf->b, &state);

		return false;
	}

	packets += count;

	LOG_INF("witholding %d packets", packets);

	net_buf_unref(buf);

	return true;
}

/* stolen from controller/hci/hci.c */
static void hci_evt_create(struct net_buf *buf, uint8_t evt, uint8_t len)
{
	struct bt_hci_evt_hdr *hdr;

	hdr = net_buf_add(buf, sizeof(*hdr));
	hdr->evt = evt;
	hdr->len = len;
}

/* also stolen from controller/hci/hci.c */
static void hci_num_cmplt_encode(struct net_buf *buf, uint16_t handle, uint8_t num)
{
	struct bt_hci_evt_num_completed_packets *ep;
	struct bt_hci_handle_count *hc;
	uint8_t num_handles;
	uint8_t len;

	num_handles = 1U;

	len = (sizeof(*ep) + (sizeof(*hc) * num_handles));
	hci_evt_create(buf, BT_HCI_EVT_NUM_COMPLETED_PACKETS, len);

	ep = net_buf_add(buf, len);
	ep->num_handles = num_handles;
	hc = &ep->h[0];
	hc->handle = sys_cpu_to_le16(handle);
	hc->count = sys_cpu_to_le16(num);
}

void release_the_brakes(void)
{
	hodl = false;

	if (packets != 0) {
		LOG_INF("sending back %d packets", packets);

		/* send a synthetic NCP event with the amount of packets that
		 * the controller has already acknowledged.
		 */
		struct net_buf *buf = bt_buf_get_evt(BT_HCI_EVT_NUM_COMPLETED_PACKETS, false, K_FOREVER);
		__ASSERT_NO_MSG(buf);

		hci_num_cmplt_encode(buf, 0, packets);
		packets = 0;

		int ret = __real_bt_recv(buf);
		__ASSERT_NO_MSG(!ret);
	}
}

int __wrap_bt_send(struct net_buf *buf)
{
	/* LOG_HEXDUMP_DBG(buf->data, buf->len, "h->c"); */

	return __real_bt_send(buf);
}

int __wrap_bt_recv(struct net_buf *buf)
{
	/* LOG_HEXDUMP_DBG(buf->data, buf->len, "c->h"); */

	if (hold_ncp_event(buf)) {
		return 0;
	}

	return __real_bt_recv(buf);
}
