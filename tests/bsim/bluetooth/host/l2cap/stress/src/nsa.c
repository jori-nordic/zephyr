#include <zephyr/net/buf.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(undersea_cable_tap, LOG_LEVEL_DBG);

int __real_bt_send(struct net_buf *buf);

int __wrap_bt_send(struct net_buf *buf)
{
	LOG_HEXDUMP_DBG(buf->data, buf->len, "h->c");

	/* TODO: Mess with packet here. Eg. swallow it and re-send it later, or
	 * flip some bits, anything really.
	 */

	return __real_bt_send(buf);
}

int __real_bt_recv(struct net_buf *buf);

int __wrap_bt_recv(struct net_buf *buf)
{
	LOG_HEXDUMP_DBG(buf->data, buf->len, "c->h");

	/* TODO: Also mess with packet here. */

	return __real_bt_recv(buf);
}
