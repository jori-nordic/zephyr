/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/l2cap.h>

#include "hci_core.h"
#include "conn_internal.h"
#include "iso_internal.h"

#include <zephyr/bluetooth/hci.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_buf, CONFIG_BT_LOG_LEVEL);

#if defined(CONFIG_BT_CONN)
#if defined(CONFIG_BT_ISO)
#define MAX_EVENT_COUNT CONFIG_BT_MAX_CONN + CONFIG_BT_ISO_MAX_CHAN
#else
#define MAX_EVENT_COUNT CONFIG_BT_MAX_CONN
#endif /* CONFIG_BT_ISO */
#elif defined(CONFIG_BT_ISO)
#define MAX_EVENT_COUNT CONFIG_BT_ISO_MAX_CHAN
#endif /* CONFIG_BT_CONN */

#if defined(CONFIG_BT_CONN) || defined(CONFIG_BT_ISO)
#define NUM_COMLETE_EVENT_SIZE BT_BUF_EVT_SIZE(                        \
	sizeof(struct bt_hci_cp_host_num_completed_packets) +          \
	MAX_EVENT_COUNT * sizeof(struct bt_hci_handle_count))
/* Dedicated pool for HCI_Number_of_Completed_Packets. This event is always
 * consumed synchronously by bt_recv_prio() so a single buffer is enough.
 * Having a dedicated pool for it ensures that exhaustion of the RX pool
 * cannot block the delivery of this priority event.
 */
NET_BUF_POOL_FIXED_DEFINE(num_complete_pool, 1, NUM_COMLETE_EVENT_SIZE,
			  sizeof(struct bt_buf_data), NULL);
#endif /* CONFIG_BT_CONN || CONFIG_BT_ISO */

NET_BUF_POOL_FIXED_DEFINE(discardable_pool, CONFIG_BT_BUF_EVT_DISCARDABLE_COUNT,
			  BT_BUF_EVT_SIZE(CONFIG_BT_BUF_EVT_DISCARDABLE_SIZE),
			  sizeof(struct bt_buf_data), NULL);

#if defined(CONFIG_BT_HCI_ACL_FLOW_CONTROL)
NET_BUF_POOL_DEFINE(acl_in_pool, CONFIG_BT_BUF_ACL_RX_COUNT,
		    BT_BUF_ACL_SIZE(CONFIG_BT_BUF_ACL_RX_SIZE),
		    sizeof(struct acl_data), bt_hci_host_num_completed_packets);

NET_BUF_POOL_FIXED_DEFINE(evt_pool, CONFIG_BT_BUF_EVT_RX_COUNT,
			  BT_BUF_EVT_RX_SIZE, sizeof(struct bt_buf_data),
			  NULL);
#else
NET_BUF_POOL_FIXED_DEFINE(hci_rx_pool, BT_BUF_RX_COUNT,
			  BT_BUF_RX_SIZE, sizeof(struct bt_buf_data),
			  NULL);
#endif /* CONFIG_BT_HCI_ACL_FLOW_CONTROL */

struct net_buf *bt_buf_get_rx(enum bt_buf_type type, k_timeout_t timeout)
{
	struct net_buf *buf;

	__ASSERT(type == BT_BUF_EVT || type == BT_BUF_ACL_IN ||
		 type == BT_BUF_ISO_IN, "Invalid buffer type requested");

	if (IS_ENABLED(CONFIG_BT_ISO_RX) && type == BT_BUF_ISO_IN) {
		return bt_iso_get_rx(timeout);
	}

#if defined(CONFIG_BT_HCI_ACL_FLOW_CONTROL)
	if (type == BT_BUF_EVT) {
		buf = net_buf_alloc(&evt_pool, timeout);
	} else {
		buf = net_buf_alloc(&acl_in_pool, timeout);
	}
#else
	buf = net_buf_alloc(&hci_rx_pool, timeout);
#endif

	if (buf) {
		net_buf_reserve(buf, BT_BUF_RESERVE);
		bt_buf_set_type(buf, type);
	}

	return buf;
}

struct net_buf *bt_buf_get_evt(uint8_t evt, bool discardable,
			       k_timeout_t timeout)
{
	struct net_buf *buf;

	switch (evt) {
#if defined(CONFIG_BT_CONN) || defined(CONFIG_BT_ISO)
	case BT_HCI_EVT_NUM_COMPLETED_PACKETS:
		buf = net_buf_alloc(&num_complete_pool, timeout);
		break;
#endif /* CONFIG_BT_CONN || CONFIG_BT_ISO */
	default:
		if (discardable) {
			buf = net_buf_alloc(&discardable_pool, timeout);
		} else {
			return bt_buf_get_rx(BT_BUF_EVT, timeout);
		}
	}

	if (buf) {
		net_buf_reserve(buf, BT_BUF_RESERVE);
		bt_buf_set_type(buf, BT_BUF_EVT);
	}

	return buf;
}

#ifdef ZTEST_UNITTEST
#if defined(CONFIG_BT_HCI_ACL_FLOW_CONTROL)
struct net_buf_pool *bt_buf_get_evt_pool(void)
{
	return &evt_pool;
}

struct net_buf_pool *bt_buf_get_acl_in_pool(void)
{
	return &acl_in_pool;
}
#else
struct net_buf_pool *bt_buf_get_hci_rx_pool(void)
{
	return &hci_rx_pool;
}
#endif /* CONFIG_BT_HCI_ACL_FLOW_CONTROL */

#if defined(CONFIG_BT_BUF_EVT_DISCARDABLE_COUNT)
struct net_buf_pool *bt_buf_get_discardable_pool(void)
{
	return &discardable_pool;
}
#endif /* CONFIG_BT_BUF_EVT_DISCARDABLE_COUNT */

#if defined(CONFIG_BT_CONN) || defined(CONFIG_BT_ISO)
struct net_buf_pool *bt_buf_get_num_complete_pool(void)
{
	return &num_complete_pool;
}
#endif /* CONFIG_BT_CONN || CONFIG_BT_ISO */
#endif /* ZTEST_UNITTEST */

/* Create a "view" or "window" into an existing buffer.
 * - enforces one active view at a time per-buffer
 * -> this restriction enables prepending data (ie. for headers)
 * - forbids appending data to the view
 * - pulls the size of the view from said buffer.
 *
 * The "virtual buffer" that is generated has to be allocated from a buffer
 * pool. This is to allow refcounting and attaching a destroy callback.
 * The configured size of the buffers in that pool should be zero-length.
 *
 * The user-data size is application-dependent, but should be minimized to save
 * memory. user_data is not used by the view API.
 *
 * The view mechanism needs to store extra metadata in order to unlock the
 * original buffer when the view is destroyed.
 *
 * The storage and allocation of the view buf pool and the view metadata is the
 * application's responsibility.
 */
struct net_buf *bt_buf_make_view(struct net_buf *view, struct net_buf *parent,
				 size_t winsize, size_t headroom, struct bt_buf_view_meta *meta)
{
	__ASSERT_NO_MSG(winsize);
	__ASSERT_NO_MSG(view);
	/* The whole point of this API is to allow prepending data. If the
	 * headroom is 0, that will not happen.
	 */
	__ASSERT_NO_MSG(net_buf_headroom(parent) > 0);
	/* check that we actually have enough headroom */
	__ASSERT_NO_MSG(headroom <= net_buf_headroom(parent));

	/* `parent` should have been just re-used instead of trying to make a
	 * view into it.
	 */
	__ASSERT_NO_MSG(winsize < parent->len);

	LOG_DBG("make-view %p viewsize %u meta %p", view, winsize, meta);

	/* Keeping a ref is the caller's responsibility */
	net_buf_simple_clone(&parent->b, &view->b);
	view->size = winsize + (parent->data - parent->__buf);
	view->len = winsize;
	view->flags = NET_BUF_EXTERNAL_DATA;

	/* we have a view, eat `winsize`'s worth of data from the parent */
	(void)net_buf_pull(parent, winsize);

	/* save backup & "clip" the buffer so the next `make_view` will fail */
	meta->parent = parent;
	meta->backup.__buf = parent->__buf; /* null headroom */
	parent->__buf = parent->data;

	meta->backup.size = parent->size; /* null tailroom */
	parent->size = parent->len;

	return view;
}

void bt_buf_destroy_view(struct net_buf *view, struct bt_buf_view_meta *meta)
{
	LOG_DBG("destroy-view %p meta %p", view, meta);
	__ASSERT_NO_MSG(meta->parent);

	/* "unclip" the parent buf */
	meta->parent->__buf = meta->backup.__buf;
	meta->parent->size = meta->backup.size;

	memset(meta, 0, sizeof(*meta));
	net_buf_destroy(view);
}

bool bt_buf_has_view(struct net_buf *parent)
{
	/* This is enforced by `make_view`. see comment there. */
	return net_buf_headroom(parent) == 0;
}
