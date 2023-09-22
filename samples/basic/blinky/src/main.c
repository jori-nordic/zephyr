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
#include <hal/nrf_ppi.h>

#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static uint8_t packet[100];

/* 0x01 -> random 0xc0 -> static */
static uint8_t adva[6] = {0xc0, 0x01, 0x13, 0x37, 0x42, 0xc0};

/* b'\x10\tBluetooth-Shell' */
/* static uint8_t advdata[] = {0x10, 0x09, 0x42, 0x6c, 0x75, 0x65, 0x74, 0x6f, 0x6f, 0x74, 0x68, 0x2d, 0x53, 0x68, 0x65, 0x6c, 0x6c}; */
static uint8_t advdata[] = {0x02, 0x01, 0x06, 0x09, 0x09, 'M', 'o', 's', 'q', 'u', 'i', 't', 'o'};
static uint8_t hello[] = "hello-micro";

void* make_packet(void)
{
	/* LE PDU header */
	/* uint8_t pdu_type = 0b0000; /\* ADV_IND *\/ */
	uint8_t pdu_type = 0b0010; /* ADV_NONCONN_IND */
	uint8_t chsel = 0;	   /* 1 = support chsel algo #2 */
	uint8_t txadd = 1;	   /* public = 0 random = 1 */
	uint8_t rxadd = 0;
	uint8_t length = sizeof(adva) + sizeof(advdata) - 8 + sizeof(hello) - 1; /* length of ADV payload */

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
	advdata[3] = sizeof(hello);
	memcpy(&packet[2 + sizeof(adva)], advdata, sizeof(advdata));
	memcpy(&packet[2 + sizeof(adva) + 5], hello, sizeof(hello) - 1);

	LOG_HEXDUMP_ERR(packet, pdu_length + 2, "PAYLOAD");

	return &packet[0];
}

#if defined(CONFIG_NATIVE_BUILD)
#define WAIT k_msleep(1)
#else
#define WAIT
#endif

uint16_t freqs[] = {2402, 2426, 2480};
uint8_t indices[] = {37, 38, 39};

uint8_t inc_freq(void)
{
	static uint8_t i = 0;

	nrf_radio_datawhiteiv_set(NRF_RADIO, indices[i]);
	nrf_radio_frequency_set(NRF_RADIO, freqs[i]);

	i++;
	if (i>2) {
		i = 0;
	}

	return i;
}

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

	printk("Start loop\n");

	/* READY->START + END->DISABLE */
	nrf_radio_shorts_set(NRF_RADIO, 3);

	/* POWER = 0 */
	nrf_radio_txpower_set(NRF_RADIO, NRF_RADIO_TXPOWER_0DBM);

	/* Ble_1Mbit */
	/* MODE = 3 */
	nrf_radio_mode_set(NRF_RADIO, 3);

	/* Set Access Address */
	uint32_t ble_aa = 0x8E89BED6;
	/* uint32_t ble_aa = 0xd6be898e; */
	nrf_radio_base0_set(NRF_RADIO, ble_aa << 8);
	nrf_radio_prefix0_set(NRF_RADIO, (ble_aa >> 24) & 0xFF);

	/* PCNF0 = 0x8 | (0x1 << 8)
	 * PCNF1 = 0xFF | (0x3 << 16) | (0x1 << 25)
	 */
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
	/* 0x3 | 0x1 << 8 */
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

	/* Ramp up radio for TX and start TXing right after */
	printk("Trigger TXEN\n");

	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_READY);
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_PAYLOAD);
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);

	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);
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

	k_msleep(2);

	/* nrf_ppi_channel_endpoint_setup(NRF_PPI, */
	/* 			       NRF_PPI_CHANNEL9, */
	/* 			       (uint32_t)&NRF_RADIO->EVENTS_DISABLED, */
	/* 			       (uint32_t)&NRF_RADIO->TASKS_TXEN); */

packetptr:
	/* Configure the PAYLOAD */
	nrf_radio_packetptr_set(NRF_RADIO, &packet[0]);

	/* Setup new frequency */
	uint8_t nextch = inc_freq();
	if (nextch == 1) {
		k_msleep(20);
	}

	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);

	/* Wait until payload has been clocked out */
	while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_DISABLED)) {
		WAIT;
	};

	goto packetptr;

	return 0;
}
