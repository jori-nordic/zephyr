#include "utils.h"
#include "argparse.h"
#include "bs_pc_backchannel.h"

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

/* Custom Service Variables */
static struct bt_uuid_128 test_uuid = BT_UUID_INIT_128(
	0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static struct bt_uuid_128 test_uuid_2 = BT_UUID_INIT_128(
	0xf1, 0xdd, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static struct bt_uuid_128 test_chrc_uuid = BT_UUID_INIT_128(
	0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
	0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static uint8_t test_value[] = { 'T', 'e', 's', 't', '\0' };

static ssize_t read_test(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	printk("Client has read from test char\n");
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 strlen(value));
}

static ssize_t write_test(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 const void *buf, uint16_t len, uint16_t offset,
			 uint8_t flags)
{
	printk("Client has written to test char\n");
	uint8_t *value = attr->user_data;

	if (offset + len > sizeof(test_value)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	memcpy(value + offset, buf, len);

	return len;
}

static struct bt_gatt_attr test_attrs[] = {
	/* Vendor Primary Service Declaration */
	BT_GATT_PRIMARY_SERVICE(&test_uuid),

	BT_GATT_CHARACTERISTIC(&test_chrc_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT |
			       BT_GATT_PERM_WRITE_ENCRYPT,
			       read_test, write_test, test_value),
};

static struct bt_gatt_attr test_attrs_2[] = {
	/* Vendor Primary Service Declaration */
	BT_GATT_PRIMARY_SERVICE(&test_uuid_2),

	BT_GATT_CHARACTERISTIC(&test_chrc_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ_ENCRYPT |
			       BT_GATT_PERM_WRITE_ENCRYPT,
			       read_test, write_test, test_value),
};

static struct bt_gatt_service test_svc = BT_GATT_SERVICE(test_attrs);
static struct bt_gatt_service test_svc_2 = BT_GATT_SERVICE(test_attrs_2);

void gatt_register_service(bool other)
{
	int err;

	if (other) {
		err = bt_gatt_service_register(&test_svc);
	} else {
		printk("................................. registering gatt\n");
		err = bt_gatt_service_register(&test_svc_2);
	}

	ASSERT(!err, "Failed to register GATT service (err %d)\n", err);
}

/* We need to discover:
 * - Dynamic service
 * - Client Features (to set robust caching)
 * - DB hash handle (to get out of the change-unaware state)
 */
enum GATT_HANDLES {
CLIENT_FEATURES,
DB_HASH,
TEST_CHAR,
NUM_HANDLES,
};

uint16_t gatt_handles[NUM_HANDLES] = {0};

DEFINE_FLAG(flag_discovered);

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	if (attr == NULL) {
		for (int i = 0; i < ARRAY_SIZE(gatt_handles); i++) {
			printk("handle[%d] = 0x%x\n", i, gatt_handles[i]);

			if (gatt_handles[i] == 0) {
				FAIL("Did not discover all characteristics\n");
			}
		}

		(void)memset(params, 0, sizeof(*params));

		SET_FLAG(flag_discovered);

		return BT_GATT_ITER_STOP;
	}

	if (params->type == BT_GATT_DISCOVER_PRIMARY &&
	    bt_uuid_cmp(params->uuid, &test_uuid.uuid) == 0) {
		printk("Found test service\n");
		params->uuid = NULL;
		params->start_handle = attr->handle + 1;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, params);
		if (err != 0) {
			FAIL("Discover failed (err %d)\n", err);
		}

		return BT_GATT_ITER_STOP;
	} else if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		const struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

		if (bt_uuid_cmp(chrc->uuid, BT_UUID_GATT_CLIENT_FEATURES) == 0) {
			printk("Found client supported features\n");
			gatt_handles[CLIENT_FEATURES] = chrc->value_handle;

		} else if (bt_uuid_cmp(chrc->uuid, BT_UUID_GATT_DB_HASH) == 0) {
			printk("Found db hash\n");
			gatt_handles[DB_HASH] = chrc->value_handle;

		} else 	if (bt_uuid_cmp(chrc->uuid, &test_chrc_uuid.uuid) == 0) {
			printk("Found test characteristic\n");
			gatt_handles[TEST_CHAR] = chrc->value_handle;
		}
	}

	return BT_GATT_ITER_CONTINUE;
}

void gatt_discover(void)
{
	static struct bt_gatt_discover_params discover_params;
	int err;

	printk("Discovering services and characteristics\n");
	UNSET_FLAG(flag_discovered);

	discover_params.uuid = NULL;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	err = bt_gatt_discover(get_conn(), &discover_params);
	if (err != 0) {
		FAIL("Discover failed(err %d)\n", err);
	}

	WAIT_FOR_FLAG(flag_discovered);
	printk("Discover complete\n");
}

DEFINE_FLAG(flag_written);

static void write_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	if (err != BT_ATT_ERR_SUCCESS) {
		FAIL("Write failed: 0x%02X\n", err);
	}

	SET_FLAG(flag_written);
}

#define CF_BIT_ROBUST_CACHING 0
void activate_robust_caching(void)
{
	int err;

	static const uint8_t csf = BIT(CF_BIT_ROBUST_CACHING);
	static struct bt_gatt_write_params write_params = {
		.func = write_cb,
		.offset = 0,
		.data = &csf,
		.length = sizeof(csf),
	};

	write_params.handle = gatt_handles[CLIENT_FEATURES];

	UNSET_FLAG(flag_written);
	err = bt_gatt_write(get_conn(), &write_params);

	ASSERT(!err, "Failed to enable robust caching\n");

	WAIT_FOR_FLAG(flag_written);
	printk("Robust caching enabled\n");
}

DEFINE_FLAG(flag_read);

static uint8_t _expect_success(struct bt_conn *conn, uint8_t err,
			       struct bt_gatt_read_params *params,
			       const void *data, uint16_t length)
{
	/* printk("GATT read cb: err 0x%02X\n", err); */
	ASSERT(err == 0, "Failed to read: err 0x%x", err);

	SET_FLAG(flag_read);

	return 0;
}

static uint8_t _expect_out_of_sync_cb(struct bt_conn *conn, uint8_t err,
				     struct bt_gatt_read_params *params,
				     const void *data, uint16_t length)
{
	/* printk("GATT read cb: err 0x%02X\n", err); */
	ASSERT(err == BT_ATT_ERR_DB_OUT_OF_SYNC, "Didn't get expected error code: err 0x%x", err);

	SET_FLAG(flag_read);

	return 0;
}

void read_test_char(bool expect_err)
{
	int err;

	struct bt_gatt_read_params read_params = {
		.handle_count = 1,
		.single = {
			.handle = gatt_handles[TEST_CHAR],
			.offset = 0,
		},
	};

	if (expect_err) {
		read_params.func = _expect_out_of_sync_cb;
	} else {
		read_params.func = _expect_success;
	}

	UNSET_FLAG(flag_read);

	err = bt_gatt_read(get_conn(), &read_params);
	ASSERT(!err, "Failed to read char\n");

	WAIT_FOR_FLAG(flag_read);
}
