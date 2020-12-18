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
/* #define NRF_NETWORK */
/* #define NRFX_TEMP_ENABLED */
/* #include <nrfx/mdk/nrf.h> */
/* #include <hal/nrf_temp.h> */

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

struct rssi_wq_info {
    struct k_work work;
    int8_t rssi;
} rssi_wq_info;

static const struct device *temp_dev;

#define TEMP_IRQn 16
typedef struct {                                /*!< (@ 0x41010000) TEMP_NS Structure                                          */
  __OM  uint32_t  TASKS_START;                  /*!< (@ 0x00000000) Start temperature measurement                              */
  __OM  uint32_t  TASKS_STOP;                   /*!< (@ 0x00000004) Stop temperature measurement                               */
  __IM  uint32_t  RESERVED[30];
  __IOM uint32_t  SUBSCRIBE_START;              /*!< (@ 0x00000080) Subscribe configuration for task START                     */
  __IOM uint32_t  SUBSCRIBE_STOP;               /*!< (@ 0x00000084) Subscribe configuration for task STOP                      */
  __IM  uint32_t  RESERVED1[30];
  __IOM uint32_t  EVENTS_DATARDY;               /*!< (@ 0x00000100) Temperature measurement complete, data ready               */
  __IM  uint32_t  RESERVED2[31];
  __IOM uint32_t  PUBLISH_DATARDY;              /*!< (@ 0x00000180) Publish configuration for event DATARDY                    */
  __IM  uint32_t  RESERVED3[96];
  __IOM uint32_t  INTENSET;                     /*!< (@ 0x00000304) Enable interrupt                                           */
  __IOM uint32_t  INTENCLR;                     /*!< (@ 0x00000308) Disable interrupt                                          */
  __IM  uint32_t  RESERVED4[127];
  __IM  int32_t   TEMP;                         /*!< (@ 0x00000508) Temperature in degC (0.25deg steps)                        */
  __IM  uint32_t  RESERVED5[5];
  __IOM uint32_t  A0;                           /*!< (@ 0x00000520) Slope of 1st piece wise linear function                    */
  __IOM uint32_t  A1;                           /*!< (@ 0x00000524) Slope of 2nd piece wise linear function                    */
  __IOM uint32_t  A2;                           /*!< (@ 0x00000528) Slope of 3rd piece wise linear function                    */
  __IOM uint32_t  A3;                           /*!< (@ 0x0000052C) Slope of 4th piece wise linear function                    */
  __IOM uint32_t  A4;                           /*!< (@ 0x00000530) Slope of 5th piece wise linear function                    */
  __IOM uint32_t  A5;                           /*!< (@ 0x00000534) Slope of 6th piece wise linear function                    */
  __IM  uint32_t  RESERVED6[2];
  __IOM uint32_t  B0;                           /*!< (@ 0x00000540) y-intercept of 1st piece wise linear function              */
  __IOM uint32_t  B1;                           /*!< (@ 0x00000544) y-intercept of 2nd piece wise linear function              */
  __IOM uint32_t  B2;                           /*!< (@ 0x00000548) y-intercept of 3rd piece wise linear function              */
  __IOM uint32_t  B3;                           /*!< (@ 0x0000054C) y-intercept of 4th piece wise linear function              */
  __IOM uint32_t  B4;                           /*!< (@ 0x00000550) y-intercept of 5th piece wise linear function              */
  __IOM uint32_t  B5;                           /*!< (@ 0x00000554) y-intercept of 6th piece wise linear function              */
  __IM  uint32_t  RESERVED7[2];
  __IOM uint32_t  T0;                           /*!< (@ 0x00000560) End point of 1st piece wise linear function                */
  __IOM uint32_t  T1;                           /*!< (@ 0x00000564) End point of 2nd piece wise linear function                */
  __IOM uint32_t  T2;                           /*!< (@ 0x00000568) End point of 3rd piece wise linear function                */
  __IOM uint32_t  T3;                           /*!< (@ 0x0000056C) End point of 4th piece wise linear function                */
  __IOM uint32_t  T4;                           /*!< (@ 0x00000570) End point of 5th piece wise linear function                */
} NRF_TEMP_Type;                                /*!< Size = 1396 (0x574)                                                       */

#define NRF_TEMP_NS_BASE            0x41010000UL
#define NRF_TEMP                 ((NRF_TEMP_Type*)          NRF_TEMP_NS_BASE)

/* Maybe get temp in its own thread */
void hts_init(void)
{
	uint32_t ms = 0;
	printk("Trying manual TEMP access\n");

	/* NVIC_DisableIRQ(TEMP_IRQn); */
	NRF_TEMP->EVENTS_DATARDY = 0;
	/* NVIC_ClearPendingIRQ(TEMP_IRQn); */
	NRF_TEMP->INTENSET = 1;
	NRF_TEMP->TASKS_START = 1;
	while(NRF_TEMP->EVENTS_DATARDY == 0)
	{
		ms++;
		k_msleep(1);
	}

	uint32_t temp = NRF_TEMP->TEMP;
	NRF_TEMP->TASKS_STOP = 1;
	NRF_TEMP->INTENCLR = 1;
	NRF_TEMP->EVENTS_DATARDY = 0;

	/* NVIC_ClearPendingIRQ(TEMP_IRQn); */
	printk("Temp: %d, ms: %d\n", temp, ms);

	/* temp_dev = device_get_binding("TEMP_0"); */

	/* if (!temp_dev) { */
	/* 	printk("error: no temp device\n"); */
	/* 	return; */
	/* } */

	/* printk("temp device is %p, name is %s\n", temp_dev, */
	/*        temp_dev->name); */
}

float get_chip_temp(void)
{
	int r;
	struct sensor_value temp_value;
	float temperature;

	if(!temp_dev)
		return 0;

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
	/* printk("RSSI: before: %d, after: %d, temp: %d\n", wq_info->rssi, comp_rssi, (int)get_chip_temp()); */
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
