/* TODO: include-what-you-use */
#include <errno.h>
#include <stddef.h>

#include <sys/types.h>

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>

#include <zephyr/init.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/check.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/bluetooth/hci_driver.h>

#include <zephyr/sys/__assert.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_driver, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

#define EC(x) do {				\
		err = x;			\
		if (err) return err;		\
	} while(0)

/* --------------- UART driver interface --------------- */
/* TODO: implement for polling, IRQ & async */
typedef void drv_signal_t(int err);

struct drv_callbacks {
	drv_signal_t *rx;
	drv_signal_t *tx;
	drv_signal_t *err;
};

struct drv_ctx {
	struct drv_callbacks cbs;
	const struct device *dev;

	ssize_t txlen;
	ssize_t txd;

	ssize_t rxlen;
	ssize_t rxd;
};

void _uart_cb(const struct device *dev,
	      struct uart_event *evt, void *user_data)
{
	LOG_DBG("uart evt: %x", evt->type);
	struct drv_ctx *ctx = (struct drv_ctx *)user_data;

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("TX done");
		ctx->cbs.tx(0);
		return;
	case UART_TX_ABORTED:
		LOG_DBG("TX aborted");
		ctx->cbs.tx(-1);
		return;
	case UART_RX_BUF_RELEASED:
		LOG_DBG("buf released");
		return;
	case UART_RX_STOPPED:
		LOG_DBG("RX stopped");
		ctx->cbs.rx(-1);
		return;
	case UART_RX_DISABLED:
		LOG_DBG("RX disabled: rxlen %d rxd %d", ctx->rxlen, ctx->rxd);
		if (ctx->rxlen == ctx->rxd) {
			ctx->cbs.rx(0);
		}
		return;
	case UART_RX_RDY:
		/* we seem to get this multiple times even if we req 1 byte only? */
		LOG_DBG("RX ready: len %d off %d", evt->data.rx.len, evt->data.rx.offset);
		LOG_HEXDUMP_DBG(evt->data.rx.buf + evt->data.rx.offset, evt->data.rx.len, "");

		ctx->rxd = evt->data.rx.offset + evt->data.rx.len;

		if (ctx->rxlen == ctx->rxd) {
			LOG_ERR("RX done");
		}
		return;
	default:
		LOG_ERR("unhandled uart evt: %x", evt->type);
	}
}

int drv_init(struct drv_ctx *context)
{
	int err;

	if (!device_is_ready(context->dev)) {
		return -1;
	}

	BUILD_ASSERT(IS_ENABLED(CONFIG_UART_ASYNC_API));
	EC(uart_callback_set(context->dev, _uart_cb, context));

	/* sanity-check that uart is enabled */
	return 0;
}

int drv_request_rx(struct drv_ctx *ctx, uint8_t *buf, ssize_t maxlen)
{
	LOG_DBG("######################");
	LOG_DBG("buf %p len %d", buf, maxlen);

	ctx->rxlen = maxlen;
	ctx->rxd = 0;

	return uart_rx_enable(ctx->dev, buf, maxlen, SYS_FOREVER_US);;
}

int drv_request_tx(struct drv_ctx *ctx, uint8_t *buf, ssize_t maxlen)
{
	LOG_DBG("----------------------");
	LOG_DBG("buf %p len %d", buf, maxlen);

	ctx->txlen = maxlen;
	ctx->txd = 0;

	return uart_tx(ctx->dev, buf, maxlen, SYS_FOREVER_US);
}

int drv_finalize_rx(struct drv_ctx *ctx, ssize_t *len)
{
	*len = ctx->rxlen;
	ctx->rxlen = 0;
	ctx->rxd = 0;

	return 0;
}

int drv_finalize_tx(struct drv_ctx *ctx, ssize_t *len)
{
	*len = ctx->txlen;
	ctx->txlen = 0;
	ctx->txd = 0;

	return 0;
}

/* --------------- Packetizer interface ---------------- */
enum h4_packet_types {
	H4_PKT_NONE,
	H4_PKT_CMD,
	H4_PKT_ACL,
	H4_PKT_SCO,
	H4_PKT_EVT,
	H4_PKT_ISO,
	H4_PKT_MAX_TYPES
};

static ssize_t get_header_len(uint8_t h4_type)
{
	static ssize_t hdr_sizes[] = {
		-1,
		BT_HCI_CMD_HDR_SIZE,
		BT_HCI_ACL_HDR_SIZE,
		BT_HCI_SCO_HDR_SIZE,
		BT_HCI_EVT_HDR_SIZE,
		BT_HCI_ISO_HDR_SIZE
	};

	if (h4_type >= ARRAY_SIZE(hdr_sizes)) {
		LOG_ERR("unkown H4 type: 0x%x", h4_type);
		return -1;
	}

	LOG_DBG("header len type %x len %d", h4_type, hdr_sizes[h4_type]);

	return hdr_sizes[h4_type];
}

static ssize_t len_offset(uint8_t h4_type)
{
	switch(h4_type) {
	case H4_PKT_ACL:
		return 1 + 2;
	case H4_PKT_SCO:
		return 1 + 2;
	case H4_PKT_EVT:
		return 1 + 1;
	case H4_PKT_ISO:
		return 1 + 2;
	default:
		return -1;
	}
}

static ssize_t get_payload_len(uint8_t *pp, ssize_t pp_len)
{
	uint8_t h4_type = pp[0];
	ssize_t pos = len_offset(h4_type);

	if (pos < 0) {
		return pos;
	}

	switch(h4_type) {
	case H4_PKT_ACL:
		return sys_get_le16(&pp[pos]);
	case H4_PKT_SCO:
		return pp[pos];
	case H4_PKT_EVT:
		return pp[pos];
	case H4_PKT_ISO:
		/* 14 bits */
		return sys_get_le16(&pp[pos]) & 0x3FFF;
	default:
		LOG_ERR("invalid H4 type: 0x%x", h4_type);
		return -1;
	}
}

ssize_t pkt_next_rx_len(uint8_t *pp, ssize_t pp_len)
{
	/* pp -> partially-built packet */

	if (pp_len < 0) {
		LOG_ERR("invalid len");
		return -1;
	}

	if (pp_len == 0) {
		/* Want H4 header */
		LOG_DBG("want h4 type");
		return 1;
	}

	if (pp_len == 1) {
		LOG_DBG("want HCI header");
		return get_header_len(pp[0]);
	}

	/* compiler will optimize this */
	if (pp_len > 1) {
		LOG_DBG("want payload len");
		/* retrieve length of the rest of the packet */
		return get_payload_len(pp, pp_len);
	}

	/* we should not get called again after this point */

	/* LOG_DBG("don't know what to expect"); */
	/* LOG_HEXDUMP_DBG(pp, pp_len, "packet dump"); */

	return -1;
}

/* --------------- Runner ------------------------------ */

#define RXBUF_SIZE 64		/* TODO: kconfig */
static uint8_t rxbuf[RXBUF_SIZE];

static K_SEM_DEFINE(rx_sem, 0, 1);
static K_SEM_DEFINE(tx_sem, 0, 1);

drv_signal_t h4_err;
void h4_err_signal(int err)
{
	if (err) {
		LOG_ERR("driver general error: %d", err);
	}
	k_sem_give(&rx_sem);
	k_sem_give(&tx_sem);
}

drv_signal_t h4_rxdone;
void h4_rx_signal(int err)
{
	if (err) {
		LOG_ERR("driver RX error: %d", err);
	}
	k_sem_give(&rx_sem);
}

drv_signal_t h4_txdone;
void h4_tx_signal(int err)
{
	if (err) {
		LOG_ERR("driver TX error: %d", err);
	}
	k_sem_give(&tx_sem);
}

struct drv_callbacks h4_cbs = {
.rx = h4_rx_signal,
.tx = h4_tx_signal,
.err = h4_err_signal,
};

static struct drv_ctx h4_drv_ctx;

int rx_blocking(uint8_t *dst, ssize_t len)
{
	int err;
	ssize_t olen = len;
	ssize_t tlen = 0;

	/* driver can signal even if it received less */
	while (len > 0) {
		EC(drv_request_rx(&h4_drv_ctx, dst, len));
		LOG_DBG("requested RX");
		k_sem_take(&rx_sem, K_FOREVER);

		LOG_ERR("finalizing RX");
		EC(drv_finalize_rx(&h4_drv_ctx, &tlen));
		LOG_DBG("rxd %d out of %d", tlen, len);
		len -= tlen;
	}

	LOG_HEXDUMP_DBG(dst, olen, "data");

	return 0;
}

extern struct net_buf *bt_buf_get_rxx(uint8_t h4_peek[4]);
static struct net_buf *get_host_rx_buf(uint8_t *pp, ssize_t pp_len)
{
	return bt_buf_get_rxx(pp);
}

static int kill_dash_nine(uint8_t *scratch, ssize_t scratch_size, ssize_t len)
{
	while (len) {
		int err;

		ssize_t chonk = MIN(scratch_size, len);
		EC(rx_blocking(scratch, chonk));

		len -= chonk;
	}

	return 0;
}

void rx_packet(struct drv_ctx *ctx)
{
	int err;

	LOG_DBG("start H4 RX packet");

	/* RX loop */
	ssize_t rxlen = 0;
	ssize_t remaining = 0;
	ssize_t next_rx = 0;
	struct net_buf *host_rxbuf;

	/* RX H4 type */
	do {
		LOG_DBG("RX H4 type");
		next_rx = pkt_next_rx_len(rxbuf, rxlen);
		LOG_DBG("next_rx_len: %d", next_rx);
		__ASSERT(next_rx > 0, "invalid size: %d", next_rx);

		err = rx_blocking(&rxbuf[rxlen], next_rx);
		__ASSERT(!err, "error receiving: %d", err);

		rxlen += next_rx;
	} while(0);


	/* RX header */
	do {
		LOG_DBG("RX header");
		next_rx = pkt_next_rx_len(rxbuf, rxlen);
		LOG_DBG("next_rx_len: %d", next_rx);

		/* Header might be invalid. Start from the top. */
		if (next_rx < 0) {
			LOG_DBG("start again");
			return;
		}

		err = rx_blocking(&rxbuf[rxlen], next_rx);
		__ASSERT(!err, "error receiving: %d", err);

		rxlen += next_rx;
	} while(0);

	/* RX rest of the scratch buffer */
	do {
		LOG_DBG("RX scratchbuf");
		next_rx = pkt_next_rx_len(rxbuf, rxlen);
		LOG_DBG("next_rx_len: %d", next_rx);
		__ASSERT(next_rx >= 0, "invalid size: %d", next_rx);

		remaining = next_rx;
		next_rx = MIN(next_rx, RXBUF_SIZE - rxlen);

		err = rx_blocking(&rxbuf[rxlen], next_rx);
		__ASSERT(!err, "error receiving: %d", err);

		rxlen += next_rx;
		remaining -= next_rx;
	} while(0);

	/* Get a buf from the host & RX rest of the packet */
	host_rxbuf = get_host_rx_buf(rxbuf, rxlen);

	/* size sanity-check */
	if (host_rxbuf) {
		LOG_DBG("size sanity-check");
		ssize_t packet_len = rxlen + remaining - 1;
		CHECKIF (net_buf_tailroom(host_rxbuf) < packet_len) {
			LOG_ERR("I like big bufs and I cannot lie (%d < %d)",
				net_buf_tailroom(host_rxbuf), packet_len);
			net_buf_unref(host_rxbuf);
			host_rxbuf = NULL;
		}
	}

	if (host_rxbuf) {
		LOG_DBG("memcpy & rx last");
		net_buf_add_mem(host_rxbuf, &rxbuf[1], rxlen - 1);

		/* /r/restoftheowl */
		err = rx_blocking(net_buf_add(host_rxbuf, remaining), remaining);
		__ASSERT(!err, "error receiving: %d", err);

		LOG_HEXDUMP_DBG(host_rxbuf->data,
				host_rxbuf->len,
				"Final RX buffer");

		bt_recv(host_rxbuf);
	} else {
		LOG_DBG("discard");
		kill_dash_nine(rxbuf, RXBUF_SIZE, remaining);
	}

	/* get next packet */
	rxlen = 0;
}

int tx_blocking(uint8_t *dst, ssize_t len)
{
	int err;
	ssize_t tlen = 0;

	/* driver can signal even if it transmitted less */
	while (len > 0) {
		EC(drv_request_tx(&h4_drv_ctx, dst, len));
		k_sem_take(&tx_sem, K_FOREVER);

		EC(drv_finalize_tx(&h4_drv_ctx, &tlen));
		len -= tlen;
	}

	return 0;
}

static int h4_push_header(struct net_buf *buf)
{
	uint8_t hdr;
	enum bt_buf_type type = bt_buf_get_type(buf);

	LOG_DBG("buf type: %x", type);

	switch(type) {
	case BT_BUF_CMD:
		hdr = H4_PKT_CMD;
		break;
	case BT_BUF_ACL_OUT:
		hdr = H4_PKT_ACL;
		break;
	case BT_BUF_ISO_OUT:
		hdr = H4_PKT_ISO;
		break;
	default:
		return -1;
	}

	net_buf_push_u8(buf, hdr);

	return 0;
}

/* imported from original H4 driver */

static int h4_send(struct net_buf *buf)
{
	int err;

	EC(h4_push_header(buf));

	LOG_HEXDUMP_DBG(buf->data, buf->len, "TX packet");

	tx_blocking(buf->data, buf->len);

	net_buf_unref(buf);

	return 0;
}

static K_KERNEL_STACK_DEFINE(rx_thread_stack, CONFIG_BT_DRV_RX_STACK_SIZE);
static struct k_thread rx_thread_data;

static const struct device *const h4_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_uart));

static void rx_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	memcpy(&h4_drv_ctx.cbs, &h4_cbs, sizeof(h4_cbs));
	h4_drv_ctx.dev = h4_dev;

	int err = drv_init(&h4_drv_ctx);
	__ASSERT(!err, "error initializing driver: %d", err);

	LOG_DBG("start H4 RX loop");

	while (1) {
		rx_packet(&h4_drv_ctx);
	}
}

static int h4_open(void)
{
	k_tid_t tid;

	LOG_ERR("OPEN");

	tid = k_thread_create(&rx_thread_data, rx_thread_stack,
			      K_KERNEL_STACK_SIZEOF(rx_thread_stack),
			      rx_thread, NULL, NULL, NULL,
			      K_PRIO_COOP(CONFIG_BT_RX_PRIO),
			      0, K_NO_WAIT);
	k_thread_name_set(tid, "H4_RX");

	return 0;
}

static const struct bt_hci_driver drv = {
	.name		= "H:4",
	.bus		= BT_HCI_DRIVER_BUS_UART,
	.open		= h4_open,
	.send		= h4_send,
};

static int bt_uart_init(void)
{
	if (!device_is_ready(h4_dev)) {
		LOG_ERR("H4 device not ready");
		return -ENODEV;
	}

	bt_hci_driver_register(&drv);

	return 0;
}

SYS_INIT(bt_uart_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
