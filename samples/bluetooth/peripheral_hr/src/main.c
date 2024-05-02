#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

#define MYPIN 20

void output(bool high)
{
	/* But, but, my driver APIs */
	NRF_P0->DIRSET = (1 << MYPIN);

	if (high) {
		NRF_P0->OUTCLR = (1 << MYPIN);
	} else {
		NRF_P0->OUTSET = (1 << MYPIN);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
		output(false);
	} else {
		printk("Connected\n");
		output(true);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);
	output(false);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

#define BT_LE_ADV_CUSTOM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE |	\
					 BT_LE_ADV_OPT_USE_IDENTITY,	\
					 BT_GAP_ADV_FAST_INT_MIN_2,	\
					 BT_GAP_ADV_FAST_INT_MAX_2, NULL)

int main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	output(false);

	printk("Starting Legacy Advertising (connectable and scannable)\n");
	err = bt_le_adv_start(BT_LE_ADV_CUSTOM, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising successfully started\n");

	return 0;
}
