#include "utils.h"
#include "argparse.h"
#include "bs_pc_backchannel.h"

DEFINE_FLAG(flag_is_connected);
DEFINE_FLAG(flag_test_end);

void wait_connected(void)
{
	UNSET_FLAG(flag_is_connected);
	WAIT_FOR_FLAG(flag_is_connected);
	printk("connected\n");

}

void wait_disconnected(void)
{
	SET_FLAG(flag_is_connected);
	WAIT_FOR_FLAG_UNSET(flag_is_connected);
	printk("disconnected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	bt_conn_unref(conn);
	UNSET_FLAG(flag_is_connected);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		return;
	}

	bt_conn_ref(conn);
	SET_FLAG(flag_is_connected);
}

DEFINE_FLAG(flag_encrypted);

void security_changed(struct bt_conn *conn, bt_security_t level,
		      enum bt_security_err err)
{
	ASSERT(err == 0, "Error setting security\n");

	printk("Encrypted\n");
	SET_FLAG(flag_encrypted);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void scan_connect_to_first_result_device_found(const bt_addr_le_t *addr, int8_t rssi,
						      uint8_t type, struct net_buf_simple *ad)
{
	struct bt_conn *conn;
	char addr_str[BT_ADDR_LE_STR_LEN];
	int err;

	/* We're only interested in connectable events */
	if (type != BT_HCI_ADV_IND && type != BT_HCI_ADV_DIRECT_IND) {
		FAIL("Unexpected advertisement type.");
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Got scan result, connecting.. dst %s, RSSI %d\n",
	       addr_str, rssi);

	err = bt_le_scan_stop();
	ASSERT(!err, "Err bt_le_scan_stop %d", err);

	err = bt_conn_le_create(addr,
				BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT,
				&conn);
	ASSERT(!err, "Err bt_conn_le_create %d", err);
}

void scan_connect_to_first_result(void)
{
	int err;

	printk("start scanner\n");
	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE,
			       scan_connect_to_first_result_device_found);
	ASSERT(!err, "Err bt_le_scan_start %d", err);
}

void advertise_connectable(void)
{
	printk("start advertiser\n");
	int err;
	struct bt_le_adv_param param = {};

	param.interval_min = 0x0020;
	param.interval_max = 0x4000;
	param.options |= BT_LE_ADV_OPT_ONE_TIME;
	param.options |= BT_LE_ADV_OPT_CONNECTABLE;

	err = bt_le_adv_start(&param, NULL, 0, NULL, 0);
	ASSERT(err == 0, "Advertising failed to start (err %d)\n", err);
}

void disconnect(struct bt_conn *conn)
{
	int err;

	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ASSERT(!err, "Failed to initate disconnect (err %d)", err);

	printk("Waiting for disconnection...\n");
	WAIT_FOR_FLAG_UNSET(flag_is_connected);
}

static void get_active_conn_cb(struct bt_conn *src, void *dst)
{
	*(struct bt_conn **)dst = src;
}

/* TODO: move to public */
struct bt_conn* get_conn(void)
{
	struct bt_conn *ret;

	bt_conn_foreach(BT_CONN_TYPE_LE, get_active_conn_cb, &ret);

	return ret;
}

DEFINE_FLAG(flag_pairing_complete);

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	FAIL("Pairing failed (unexpected): reason %u", reason);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	ASSERT(bonded, "Bonding failed\n");

	printk("Paired\n");
	SET_FLAG(flag_pairing_complete);
}

static struct bt_conn_auth_info_cb bt_conn_auth_info_cb = {
	.pairing_failed = pairing_failed,
	.pairing_complete = pairing_complete,
};

/* Public functions */
void set_security(struct bt_conn *conn, bt_security_t sec)
{
	int err;

	UNSET_FLAG(flag_encrypted);

	err = bt_conn_set_security(conn, sec);
	ASSERT(!err, "Err bt_conn_set_security %d", err);

	WAIT_FOR_FLAG(flag_encrypted);
}

void wait_secured(void)
{
	UNSET_FLAG(flag_encrypted);
	WAIT_FOR_FLAG(flag_encrypted);
}

void bond(struct bt_conn *conn)
{
	UNSET_FLAG(flag_pairing_complete);

	int err = bt_conn_auth_info_cb_register(&bt_conn_auth_info_cb);
	ASSERT(!err, "bt_conn_auth_info_cb_register failed.\n");

	set_security(conn, BT_SECURITY_L2);

	WAIT_FOR_FLAG(flag_pairing_complete);
}

void wait_bonded(void)
{
	UNSET_FLAG(flag_encrypted);
	UNSET_FLAG(flag_pairing_complete);

	int err = bt_conn_auth_info_cb_register(&bt_conn_auth_info_cb);
	ASSERT(!err, "bt_conn_auth_info_cb_register failed.\n");

	WAIT_FOR_FLAG(flag_encrypted);
	WAIT_FOR_FLAG(flag_pairing_complete);
}

struct bt_conn* connect_as_central(void)
{
	struct bt_conn *conn;

	scan_connect_to_first_result();
	wait_connected();
	conn = get_conn();

	return conn;
}

struct bt_conn* connect_as_peripheral(void)
{
	struct bt_conn *conn;

	advertise_connectable();
	wait_connected();
	conn = get_conn();

	return conn;
}

/* TODO: move to backchannel.c/h */
#define CHANNEL_WAIT 1
#define CHANNEL_SEND 0
#define MSG_SIZE 1

void backchannel_init(void)
{
	uint device_number = get_device_nbr();
	uint channel_numbers[2] = { 0, 0, };
	uint device_numbers[2];
	uint num_ch;
	uint *ch;

	/* No backchannels to next/prev device if only device */
	if (get_test_round() == 0 && is_final_round()) {
		return;
	}

	if (get_test_round() == 0) {
		device_numbers[0] = get_device_nbr() + 1;
		num_ch = 1;

	} else if (is_final_round()){
		device_numbers[0] = get_device_nbr() - 1;
		num_ch = 1;

	} else {
		device_numbers[0] = get_device_nbr() + 1;
		device_numbers[1] = get_device_nbr() - 1;
		num_ch = 2;
	}

	printk("Opening backchannels\n");
	for (int i=0; i<num_ch; i++) {
		printk("num[%u] = %d\n", i, device_numbers[i]);
	}

	ch = bs_open_back_channel(device_number, device_numbers,
				  channel_numbers, num_ch);
	if (!ch) {
		FAIL("Unable to open backchannel\n");
	}
}

void backchannel_sync_send(void)
{
	uint8_t sync_msg[MSG_SIZE] = { get_device_nbr() };

	printk("Sending sync\n");
	bs_bc_send_msg(CHANNEL_SEND, sync_msg, ARRAY_SIZE(sync_msg));
}

void backchannel_sync_wait(void)
{
	uint8_t sync_msg[MSG_SIZE];
	uint channel;

	if (is_final_round()){
		channel = 0;
	} else {
		channel = 1;
	}

	while (true) {
		if (bs_bc_is_msg_received(channel) > 0) {
			bs_bc_receive_msg(channel, sync_msg,
					  ARRAY_SIZE(sync_msg));
			if (sync_msg[0] != get_device_nbr()) {
				/* Received a message from another device, exit */
				break;
			}
		}

		k_sleep(K_MSEC(1));
	}

	printk("Sync received\n");
}

void signal_next_test_round(void)
{
	backchannel_sync_send();
	PASS("round %d over\n", get_test_round());
}

void wait_for_round_start(void)
{
	if (get_test_round() != 0) {
		backchannel_sync_wait();
	}
}
