/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <nrf.h>
#include <hal/nrf_radio.h>

#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static uint8_t packet[100];

/* 0x01 -> random 0xc0 -> static */
static uint8_t adva[6] = {0xc0, 0x01, 0x13, 0x37, 0x42, 0xc0};

/* b'\x10\tBluetooth-Shell' */
/* static uint8_t advdata[] = {0x10, 0x09, 0x42, 0x6c, 0x75, 0x65, 0x74, 0x6f, 0x6f, 0x74, 0x68, 0x2d, 0x53, 0x68, 0x65, 0x6c, 0x6c}; */
static uint8_t advdata[] = {0x02, 0x01, 0x06, 0x09, 0x09, 'M', 'o', 's', 'q', 'u', 'i', 't', 'o'};

void* make_packet(void)
{
	/* LE PDU header */
	/* uint8_t pdu_type = 0b0000; /\* ADV_IND *\/ */
	uint8_t pdu_type = 0b0010; /* ADV_NONCONN_IND */
	uint8_t chsel = 0;	   /* 1 = support chsel algo #2 */
	uint8_t txadd = 1;	   /* public = 0 random = 1 */
	uint8_t rxadd = 0;
	uint8_t length = sizeof(adva) + sizeof(advdata); /* length of ADV payload */

	uint8_t h0 =
		pdu_type |
		(chsel << 5) |
		(txadd << 6) |
		(rxadd << 7);
	uint8_t h1 = length;

	/* ADV_IND PDU: [adva](6) [advdata](0-31) */
	(void)adva;
	(void)advdata;

	uint32_t pdu_length = sizeof(adva) + sizeof(advdata);
	printk("length: %d h0: %x h1: %x\n", pdu_length, h0, h1);

	/* Representation in RAM has only: S0, LENGTH, S1, PAYLOAD */

	/* S0 + LENGTH */
	packet[0] = h0;
	packet[1] = h1;

	/* LE PDU starts here */
	memcpy(&packet[2], adva, sizeof(adva));

	/* ADV_IND PDU starts here */
	memcpy(&packet[2 + sizeof(adva)], advdata, sizeof(advdata));

	LOG_HEXDUMP_ERR(packet, pdu_length, "PAYLOAD");

	/* idk what S0 and LENGTH are used for, don't actually pass them to the radio */
	return &packet[0];
}

#if defined(CONFIG_NATIVE_BUILD)
#define WAIT k_msleep(1)
#else
#define WAIT
#endif

int main(void)
{
	printk("init\n");
	NRF_POWER->TASKS_CONSTLAT = 1;
	NRF_CLOCK->TASKS_HFCLKSTART = 1;
	/* while (!NRF_CLOCK->EVENTS_HFCLKSTARTED) {k_msleep(1);}; */

	nrf_radio_power_set(NRF_RADIO, 0);
	k_msleep(200);
	nrf_radio_power_set(NRF_RADIO, 1);
	k_msleep(200);

start:
	printk("Start loop\n");

	/* Setup automatic START and DISABLE */
	/* READY->START + END->DISABLE */
	nrf_radio_shorts_set(NRF_RADIO, 3);

	nrf_radio_txpower_set(NRF_RADIO, NRF_RADIO_TXPOWER_0DBM);

	/* Ble_1Mbit */
	nrf_radio_mode_set(NRF_RADIO, 3);

	/* Set Access Address */
	uint32_t ble_aa = 0x8E89BED6;
	/* uint32_t ble_aa = 0xd6be898e; */
	nrf_radio_base0_set(NRF_RADIO, ble_aa << 8);
	nrf_radio_prefix0_set(NRF_RADIO, (ble_aa >> 24) & 0xFF);
	/* NRF_RADIO->TXADDRESS = 0; /\* logical address 0 *\/ */

	static nrf_radio_packet_conf_t packet_conf = {
		.lflen = 8UL,
		.s0len = 1UL,
		.s1len = 0UL,
		.s1incl = 0UL,
		.maxlen = 0xFF,
		.statlen = 0UL,
		.balen = 3UL,
		.big_endian = 0,
		.whiteen = 1,
		.plen = NRF_RADIO_PREAMBLE_LENGTH_8BIT};

	nrf_radio_packet_configure(NRF_RADIO, &packet_conf);

	/* CRC:
	 * 24bit: (x24) + x10 + x9 + x6 + x4 + x3 + x1 + x0
	 * init: 0x555555
	 */
	/* uint32_t crcpoly = BIT(10) | BIT(9) | BIT(6) | BIT(4) | BIT(3) | BIT(1) | BIT(0); */
	uint32_t crcpoly = 0x65B;
	nrf_radio_crc_configure(NRF_RADIO, 3, NRF_RADIO_CRC_ADDR_SKIP, crcpoly);
	nrf_radio_crcinit_set(NRF_RADIO, 0x555555);

	/* Whitening:
	 * 7bit: x7 + x4 + 1
	 * init:
	 * 0: 1
	 * 1-6: channel index w/ MSB in [1] LSB in [6]
	 * e.g. ch23 -> 0b1110101
	 *
	 * register: b6 = 1
	 * b0 = SR[6]
	 * b1 = SR[5]
	 * etc..
	 */
	nrf_radio_datawhiteiv_set(NRF_RADIO, 37); /* channel 38 */

	/* Frequency */
	nrf_radio_frequency_set(NRF_RADIO, 2402); /* (page 2682) ch38: 2426 MHz */

	/* Configure the PAYLOAD */
	nrf_radio_packetptr_set(NRF_RADIO, make_packet());

	/* Write packet configuration */

	/* Ramp up radio for TX and start TXing right after */
	printk("Trigger TXEN\n");

	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_READY);
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_PAYLOAD);
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);

	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);
	/* NRF_RADIO->TASKS_TXEN = 1; */
	while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_READY)) {
		WAIT;
	};

	/* Wait until payload has been clocked out */
	while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_PAYLOAD)) {
		WAIT;
	};
	printk("got PAYLOAD\n");

	/* Wait until RADIO is off */
	while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_DISABLED)) {
		WAIT;
	};
	printk("got DISABLED\n");

	printk("radio TX ok\n\n");

	/* k_msleep(40); */
	goto start;

	return 0;
}
