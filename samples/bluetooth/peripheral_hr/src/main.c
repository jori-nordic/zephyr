#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/input/input.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, 4);


static volatile bool subscribed;

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	/* assume we only get it for the `custom_gatt_service` */
	if (value != 0) {
		subscribed = 1;
	} else {
		subscribed = 0;
	}
}

#define custom_service_uuid                                                                          \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xf0debc9a, 0x7856, 0x3412, 0x7856, 0x341278563412))
#define custom_characteristic_uuid                                                                   \
	BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0xf2debc9a, 0x7856, 0x3412, 0x7856, 0x341278563412))

BT_GATT_SERVICE_DEFINE(custom_gatt_service, BT_GATT_PRIMARY_SERVICE(custom_service_uuid),
		       BT_GATT_CHARACTERISTIC(custom_characteristic_uuid,
					      (BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY),
					      BT_GATT_PERM_READ, NULL, NULL,
					      NULL),
		       BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err 0x%02x)\n", err);
	} else {
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason 0x%02x)\n", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

#define BT_LE_ADV_CUSTOM BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE |	\
					 BT_LE_ADV_OPT_USE_IDENTITY,	\
					 BT_GAP_ADV_FAST_INT_MIN_2,	\
					 BT_GAP_ADV_FAST_INT_MAX_2, NULL)

static void send_data(uint8_t mask)
{
	const struct bt_gatt_attr *attr = &custom_gatt_service.attrs[2];

	if (!subscribed) {
		LOG_DBG("not subscribed");
		return;
	}

	int err = bt_gatt_notify(NULL, attr, &mask, 1);

	if (err) {
		LOG_DBG("Failed to notify: %d", err);
	}
}

static void set_clear(uint8_t *m, int index, int value)
{
	if (value) {
		*m |= BIT(index);
	} else {
		*m &= ~BIT(index);
	}
}

static void input_cb(struct input_event *evt)
{
	static uint8_t mask = 0;

	LOG_DBG("%s: sync %u type %u code 0x%x value %d", __func__,
		evt->sync, evt->type, evt->code, evt->value);

	if (evt->code == INPUT_KEY_0) {
		LOG_ERR("left %d", evt->value);
		set_clear(&mask, 0, evt->value);
	}

	if (evt->code == INPUT_KEY_1) {
		LOG_ERR("right %d", evt->value);
		set_clear(&mask, 1, evt->value);
	}

	send_data(mask);
}

/* Invoke callback for all input devices */
INPUT_CALLBACK_DEFINE(NULL, input_cb);

int main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	printk("Starting Legacy Advertising (connectable and scannable)\n");
	err = bt_le_adv_start(BT_LE_ADV_CUSTOM, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising successfully started\n");

	return 0;
}
