/*
 * Copyright (c) 2019-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr.h>
#include <arch/cpu.h>
#include <sys/byteorder.h>
#include <logging/log.h>
#include <sys/util.h>
#include <drivers/ipm.h>

#include <openamp/open_amp.h>
#include <metal/sys.h>
#include <metal/device.h>
#include <metal/alloc.h>

#include <ipc/rpmsg_service.h>

#include <net/buf.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/buf.h>
#include <bluetooth/hci_raw.h>

/* Debugging */
#include <nrf.h>
#include <nrfx.h>
#include <tracing/tracing.h>
#include <ksched.h>

/* #define LOG_LEVEL LOG_LEVEL_DBG */
#define LOG_LEVEL LOG_LEVEL_WRN
#define LOG_MODULE_NAME hci_rpmsg
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

static int endpoint_id;

static K_THREAD_STACK_DEFINE(tx_thread_stack, CONFIG_BT_HCI_TX_STACK_SIZE);
static struct k_thread tx_thread_data;
static K_FIFO_DEFINE(tx_queue);

#define HCI_RPMSG_CMD 0x01
#define HCI_RPMSG_ACL 0x02
#define HCI_RPMSG_SCO 0x03
#define HCI_RPMSG_EVT 0x04
#define HCI_RPMSG_ISO 0x05

static struct net_buf *hci_rpmsg_cmd_recv(uint8_t *data, size_t remaining)
{
	struct bt_hci_cmd_hdr *hdr = (void *)data;
	struct net_buf *buf;

	if (remaining < sizeof(*hdr)) {
		LOG_ERR("Not enought data for command header");
		return NULL;
	}

	buf = bt_buf_get_tx(BT_BUF_CMD, K_NO_WAIT, hdr, sizeof(*hdr));
	if (buf) {
		data += sizeof(*hdr);
		remaining -= sizeof(*hdr);
	} else {
		LOG_ERR("No available command buffers!");
		return NULL;
	}

	if (remaining != hdr->param_len) {
		LOG_ERR("Command payload length is not correct");
		net_buf_unref(buf);
		return NULL;
	}

	LOG_DBG("len %u", hdr->param_len);
	net_buf_add_mem(buf, data, remaining);

	return buf;
}

static struct net_buf *hci_rpmsg_acl_recv(uint8_t *data, size_t remaining)
{
	struct bt_hci_acl_hdr *hdr = (void *)data;
	struct net_buf *buf;

	if (remaining < sizeof(*hdr)) {
		LOG_ERR("Not enought data for ACL header");
		return NULL;
	}

	buf = bt_buf_get_tx(BT_BUF_ACL_OUT, K_NO_WAIT, hdr, sizeof(*hdr));
	if (buf) {
		data += sizeof(*hdr);
		remaining -= sizeof(*hdr);
	} else {
		LOG_ERR("No available ACL buffers!");
		return NULL;
	}

	if (remaining != sys_le16_to_cpu(hdr->len)) {
		LOG_ERR("ACL payload length is not correct");
		net_buf_unref(buf);
		return NULL;
	}

	LOG_DBG("len %u", remaining);
	net_buf_add_mem(buf, data, remaining);

	return buf;
}

static struct net_buf *hci_rpmsg_iso_recv(uint8_t *data, size_t remaining)
{
	struct bt_hci_iso_hdr *hdr = (void *)data;
	struct net_buf *buf;

	if (remaining < sizeof(*hdr)) {
		LOG_ERR("Not enough data for ISO header");
		return NULL;
	}

	buf = bt_buf_get_tx(BT_BUF_ISO_OUT, K_NO_WAIT, hdr, sizeof(*hdr));
	if (buf) {
		data += sizeof(*hdr);
		remaining -= sizeof(*hdr);
	} else {
		LOG_ERR("No available ISO buffers!");
		return NULL;
	}

	if (remaining != sys_le16_to_cpu(hdr->len)) {
		LOG_ERR("ISO payload length is not correct");
		net_buf_unref(buf);
		return NULL;
	}

	LOG_DBG("len %zu", remaining);
	net_buf_add_mem(buf, data, remaining);

	return buf;
}

static void hci_rpmsg_rx(uint8_t *data, size_t len)
{
	uint8_t pkt_indicator;
	struct net_buf *buf = NULL;
	size_t remaining = len;

	LOG_HEXDUMP_DBG(data, len, "RPMSG data:");

	pkt_indicator = *data++;
	remaining -= sizeof(pkt_indicator);

	switch (pkt_indicator) {
	case HCI_RPMSG_CMD:
		buf = hci_rpmsg_cmd_recv(data, remaining);
		break;

	case HCI_RPMSG_ACL:
		buf = hci_rpmsg_acl_recv(data, remaining);
		break;

	case HCI_RPMSG_ISO:
		buf = hci_rpmsg_iso_recv(data, remaining);
		break;

	default:
		LOG_ERR("Unknown HCI type %u", pkt_indicator);
		return;
	}

	if (buf) {
		net_buf_put(&tx_queue, buf);

		LOG_HEXDUMP_DBG(buf->data, buf->len, "Final net buffer:");
	}
}

static void tx_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		struct net_buf *buf;
		int err;

		/* Wait until a buffer is available */
		buf = net_buf_get(&tx_queue, K_FOREVER);
		/* Pass buffer to the stack */
		err = bt_send(buf);
		if (err) {
			LOG_ERR("Unable to send (err %d)", err);
			net_buf_unref(buf);
		}

		/* Give other threads a chance to run if tx_queue keeps getting
		 * new data all the time.
		 */
		k_yield();
	}
}

static int hci_rpmsg_send(struct net_buf *buf)
{
	uint8_t pkt_indicator;

	LOG_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf),
		buf->len);

	LOG_HEXDUMP_DBG(buf->data, buf->len, "Controller buffer:");

	switch (bt_buf_get_type(buf)) {
	case BT_BUF_ACL_IN:
		pkt_indicator = HCI_RPMSG_ACL;
		break;
	case BT_BUF_EVT:
		pkt_indicator = HCI_RPMSG_EVT;
		break;
	case BT_BUF_ISO_IN:
		pkt_indicator = HCI_RPMSG_ISO;
		break;
	default:
		LOG_ERR("Unknown type %u", bt_buf_get_type(buf));
		net_buf_unref(buf);
		return -EINVAL;
	}
	net_buf_push_u8(buf, pkt_indicator);

	LOG_HEXDUMP_DBG(buf->data, buf->len, "Final HCI buffer:");

NRF_P1_NS->OUTSET = 1<<1;
	rpmsg_service_send(endpoint_id, buf->data, buf->len);
NRF_P1_NS->OUTCLR = 1<<1;

	net_buf_unref(buf);

	return 0;
}

#if defined(CONFIG_BT_CTLR_ASSERT_HANDLER)
void bt_ctlr_assert_handle(char *file, uint32_t line)
{
	LOG_ERR("Controller assert in: %s at %d", file, line);
}
#endif /* CONFIG_BT_CTLR_ASSERT_HANDLER */

int endpoint_cb(struct rpmsg_endpoint *ept, void *data, size_t len, uint32_t src,
		void *priv)
{
NRF_P1_NS->OUTSET = 1<<0;
	LOG_INF("Received message of %u bytes.", len);
	hci_rpmsg_rx((uint8_t *) data, len);
NRF_P1_NS->OUTCLR = 1<<0;

	return RPMSG_SUCCESS;
}

k_tid_t main_tid = 0;
void main(void)
{
	int err;

	/* incoming events and data from the controller */
	static K_FIFO_DEFINE(rx_queue);

	LOG_DBG("Start");

	/* Enable the raw interface, this will in turn open the HCI driver */
	bt_enable_raw(&rx_queue);

	/* Spawn the TX thread and start feeding commands and data to the
	 * controller
	 */
	k_thread_create(&tx_thread_data, tx_thread_stack,
			K_THREAD_STACK_SIZEOF(tx_thread_stack), tx_thread,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&tx_thread_data, "HCI rpmsg TX");

#if defined(CONFIG_SOC_NRF5340_CPUNET_QKAA)
	printk("######## Hello from RPMSG\n");
	printk("Reset reason: 0x%x\n", NRF_RESET_NS->RESETREAS);
	/* NRF_RESET_NS->RESETREAS = 0; */
	NRF_P1_NS->DIRSET = (1<<16) - 1;
	NRF_P1_NS->OUTSET = (1<<16) - 1;
	k_msleep(1);
	NRF_P1_NS->OUTCLR = (1<<16) - 1;
#endif

	/* Use constant-latency mode */
	/* does not fix */
	NRF_POWER_NS->TASKS_CONSTLAT = 1;

	main_tid = k_current_get();

	while (1) {
		struct net_buf *buf;

		buf = net_buf_get(&rx_queue, K_FOREVER);
		err = hci_rpmsg_send(buf);
		/* LOG_ERR("aha"); */
		if (err) {
			LOG_ERR("Failed to send (err %d)", err);
		}
	}
}

/* Make sure we register endpoint before RPMsg Service is initialized. */
int register_endpoint(const struct device *arg)
{
	int status;

	status = rpmsg_service_register_endpoint("nrf_bt_hci", endpoint_cb);

	if (status < 0) {
		LOG_ERR("Registering endpoint failed with %d", status);
		return status;
	}

	endpoint_id = status;

	return 0;
}

SYS_INIT(register_endpoint, POST_KERNEL, CONFIG_RPMSG_SERVICE_EP_REG_PRIORITY);


void sys_trace_thread_switched_out()
{
	if (k_current_get() == main_tid) {
		NRF_P1_NS->OUTCLR = 1 << 7;
	}
	if (!z_is_idle_thread_object(k_current_get())) {
		NRF_P1_NS->OUTCLR = 1 << 5;
	}
}

void sys_trace_thread_switched_in()
{
	if (k_current_get() == main_tid) {
		NRF_P1_NS->OUTSET = 1 << 7;
	}
	if (!z_is_idle_thread_object(k_current_get())) {
		NRF_P1_NS->OUTSET = 1 << 5;
	}
}

void sys_trace_thread_priority_set(struct k_thread *thread)
{
}

void sys_trace_thread_create(struct k_thread *thread) {}

void sys_trace_thread_abort(struct k_thread *thread) {}

void sys_trace_thread_suspend(struct k_thread *thread)
{
	if (k_current_get() == main_tid) {
		NRF_P1_NS->OUTCLR = 1 << 7;
	};
}

void sys_trace_thread_resume(struct k_thread *thread)
{
	if (k_current_get() == main_tid) {
		NRF_P1_NS->OUTSET = 1 << 7;
	};
}

void sys_trace_thread_ready(struct k_thread *thread) {	\
	if(1) {	\
		/* NRF_P1_NS->OUTSET = 1<<5;				\ */
	};								\
	}

void sys_trace_thread_pend(struct k_thread *thread) {	\
	if(1) {	\
		/* NRF_P1_NS->OUTCLR = 1<<5;				\ */
	};								\
	}

void sys_trace_thread_info(struct k_thread *thread) {}

void sys_trace_thread_name_set(struct k_thread *thread) {}

void sys_trace_isr_enter()
{
	/* if (1) { */
	/* 	NRF_P1_NS->OUTSET = 1 << 1; */
	/* }; */

	/* int8_t active = (((SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) >> */
	/* 		  SCB_ICSR_VECTACTIVE_Pos) - */
	/* 		 16); */

	/* if(active <0) active = 0; */
	/* for(; active > 0; active--) */
	/* { */
	/* 	NRF_P1_NS->OUTSET = 1 << 0; */
	/* 	__NOP(); */
	/* 	__NOP(); */
	/* 	__NOP(); */
	/* 	__NOP(); */
	/* 	NRF_P1_NS->OUTCLR = 1 << 0; */
	/* 	__NOP(); */
	/* 	__NOP(); */
	/* 	__NOP(); */
	/* 	__NOP(); */
	/* } */
}

void sys_trace_isr_exit()
{
	/* if (1) { */
	/* 	NRF_P1_NS->OUTCLR = 1 << 1; */
	/* }; */
}

void sys_trace_isr_exit_to_scheduler()
{
	/* if (1) { */
	/* 	NRF_P1_NS->OUTCLR = 1 << 1; */
	/* }; */
}

void sys_trace_void(int id)
{
}

void sys_trace_end_call(int id) {}

void sys_trace_idle() {}

void sys_trace_semaphore_init(struct k_sem *sem) {}

void sys_trace_semaphore_take(struct k_sem *sem)
{
	/* if (1) { */
	/* 	NRF_P1_NS->OUTSET = 1 << 8; */
	/* }; */
}

void sys_trace_semaphore_give(struct k_sem *sem)
{
	/* if (1) { */
	/* 	NRF_P1_NS->OUTCLR = 1 << 8; */
	/* }; */
}

void sys_trace_mutex_init(struct k_mutex *mutex) {}

void sys_trace_mutex_lock(struct k_mutex *mutex) {}

void sys_trace_mutex_unlock(struct k_mutex *mutex) {}

#if 0
/* Try creating a thread to keep CPU from entering idle */
#define MY_STACK_SIZE 500
#define MY_PRIORITY K_LOWEST_APPLICATION_THREAD_PRIO

static void my_entry_point(void *p1, void *p2, void *p3) {
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while(1)
		k_busy_wait(1000);
};

K_THREAD_DEFINE(my_tid, MY_STACK_SIZE,
                my_entry_point, NULL, NULL, NULL,
                MY_PRIORITY, 0, 0);

#endif
