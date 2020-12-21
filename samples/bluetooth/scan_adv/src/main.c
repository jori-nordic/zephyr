/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <drivers/sensor.h>
#include <device.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

struct rssi_wq_info {
    struct k_work work;
    int8_t rssi;
} rssi_wq_info;

static const struct device *temp_dev;

/* Maybe get temp in its own thread */
void hts_init(void)
{
	uint32_t temp = 0;
	uint32_t ms = 0;
	printk("Trying manual TEMP access\n");

	printk("Temp: %d, ms: %d\n", temp, ms);

	temp_dev = device_get_binding("TEMP_0");

	if (!temp_dev) {
		printk("error: no temp device\n");
		return;
	}

	printk("temp device is %p, name is %s\n", temp_dev,
	       temp_dev->name);
}

float get_chip_temp(void)
{
	int r;
	struct sensor_value temp_value;
	float temperature;

	if(!temp_dev)
	{
		return 0;
	}

	r = sensor_sample_fetch(temp_dev);
	if (r) {
		printk("sensor_sample_fetch failed return: %d\n", r);
	}

	r = sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP,
				&temp_value);
	if (r) {
		printk("sensor_channel_get failed return: %d\n", r);
	}

	temperature = (float)sensor_value_to_double(&temp_value);

	return (float)temperature;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		    struct net_buf_simple *buf)
{
	rssi_wq_info.rssi = rssi;
	k_work_submit(&rssi_wq_info.work);
}

void correct_rssi(struct k_work *item)
{
    struct rssi_wq_info *wq_info =
	    CONTAINER_OF(item, struct rssi_wq_info, work);

    /* TODO: replace with correct calc */
    int8_t comp_rssi = wq_info->rssi + 10;

    if(wq_info->rssi > -60)
    {
	printk("RSSI: before: %d, after: %d, temp: %d\n", wq_info->rssi, comp_rssi, (int)get_chip_temp());
	return;
    }
}

void main(void)
{
	int err;

	printk("Starting Scanner RSSI nRF53 fix sample\n");

	/* Init the on-chip temperature sensor */
	hts_init();

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}
	printk("Bluetooth initialized\n");

	/* Initialize work item for compensating RSSI values */
	k_work_init(&rssi_wq_info.work, correct_rssi);

	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, scan_cb);
	if (err) {
		printk("Starting scanning failed (err %d)\n", err);
		return;
	}

	printk("Waiting for callbacks\n");
	while(1)
	{
		k_msleep(100);
	}
}
