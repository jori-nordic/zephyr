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

static uint8_t packet[100];

/* 0x01 -> random 0xc0 -> static */
static uint8_t adva[6] = {0x13, 0x37, 0x42, 0x69, 0xc0, 0x01};

/* b'\x10\tBluetooth-Shell' */
static uint8_t advdata[] = {0x10, 0x09, 0x42, 0x6c, 0x75, 0x65, 0x74, 0x6f, 0x6f, 0x74, 0x68, 0x2d, 0x53, 0x68, 0x65, 0x6c, 0x6c};

void* make_packet(void)
{
	/* LE PDU header */
	uint8_t pdu_type = 0b0000; /* ADV_IND */
	uint8_t chsel = 0;	   /* 1 = support chsel algo #2 */
	uint8_t txadd = 1;	   /* public = 0 random = 1 */
	uint8_t rxadd = 0;
	uint8_t length = 0;	/* length of ADV payload */

	uint8_t h0 =
		pdu_type |
		(chsel << 5) |
		(txadd << 6) |
		(rxadd << 7);
	uint8_t h1 = length;

	/* ADV_IND PDU: [adva](6) [advdata](0-31) */
	(void)adva;
	(void)advdata;

	uint32_t pdu_length = sizeof(h0) + sizeof(h1) + sizeof(adva) + sizeof(advdata);
	printk("length: %d\n", pdu_length);

	/* Representation in RAM has only: S0, LENGTH, S1, PAYLOAD */

	/* S0: 0 bytes */
	packet[0] = pdu_length;	/* LENGTH: 8 bits */
	/* S1: 0 bits */

	/* PAYLOAD starts here */
	packet[1] = h0;
	packet[2] = h1;

	/* LE PDU starts here */
	memcpy(&packet[3], adva, sizeof(adva));

	/* ADV_IND PDU starts here */
	memcpy(&packet[3 + sizeof(adva)], advdata, sizeof(advdata));

	return &packet[0];
}

int main(void)
{
	printk("init\n");
	NRF_POWER->TASKS_CONSTLAT = 1;
	NRF_CLOCK->TASKS_HFCLKSTART = 1;
	while (!NRF_CLOCK->EVENTS_HFCLKSTARTED) {k_msleep(1);};

	NRF_RADIO->POWER = 0;
	k_msleep(200);
	NRF_RADIO->POWER = 1;
	k_msleep(200);

start:
	printk("Start loop\n");
	uint32_t pcnf0 = 0;
	uint32_t pcnf1 = 0;

	/* Setup automatic START and DISABLE */
	/* READY->START + END->DISABLE */
	NRF_RADIO->SHORTS = 3;

	/* Ble_1Mbit */
	NRF_RADIO->MODE = 3;

	/* Set Access Address */
	uint32_t ble_aa = 0x8E89BED6;
	NRF_RADIO->BASE0 = ble_aa << 8;
	NRF_RADIO->PREFIX0 = (ble_aa >> 24) & 0xFF;
	NRF_RADIO->TXADDRESS = 0; /* logical address 0 */

	/* Set S0, S1, LENGTH to 0-length */
	pcnf0 |=
		8UL | 		/* LENGTH = 8 bits */
		0 << 8 | 	/* S0 = 0 bytes */
		0 << 16;	/* S1 = 0 bits */

	/* Preamble config */
	pcnf0 |= 0 << 24;	/* 1 octet */

	pcnf1 |=
		0xFF |		/* maxlen */
		0 << 8 |	/* static len (extra data) */
		3UL << 16;	/* address is 4 bytes: 3 base addr + 1 prefix */

	/* Frequency */
	NRF_RADIO->FREQUENCY = 26; /* (page 2682) ch38: 2426 MHz */

	/* CRC:
	 * 24bit: (x24) + x10 + x9 + x6 + x4 + x3 + x1 + x0
	 * init: 0x555555
	 */
	NRF_RADIO->CRCCNF =
		3 |	/* length = 24bit */
		BIT(8); /* skip access addr for calculation */
	NRF_RADIO->CRCPOLY = BIT(10) | BIT(9) | BIT(6) | BIT(4) | BIT(3) | BIT(1) | BIT(0);
	NRF_RADIO->CRCINIT = 0x555555;

	/* Enable whitening:
	 * 7bit: x7 + x4 + 1
	 * init:
	 * 0: 1
	 * 1-6: channel index w/ MSB in [1] LSB in [6]
	 * e.g. ch23 -> 0b1110101
	 *
	 * register: b6 = 1
	 * b0 = SR[6]
	 * b1 = SR[5]
	 */
	pcnf1 |= BIT(25);	/* WHITEEN */
	NRF_RADIO->DATAWHITEIV = 38; /* channel 38 */

	/* Configure the PAYLOAD */
	NRF_RADIO->PACKETPTR = (uint32_t)make_packet();

	/* Write packet configuration */
	NRF_RADIO->PCNF0 = pcnf0;
	NRF_RADIO->PCNF1 = pcnf1;

	/* Ramp up radio for TX and start TXing right after */
	printk("Trigger TXEN\n");

	NRF_RADIO->EVENTS_READY = 0;
	NRF_RADIO->EVENTS_PAYLOAD = 0;
	NRF_RADIO->EVENTS_DISABLED = 0;

	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);
	NRF_RADIO->TASKS_TXEN = 1;
	while (!NRF_RADIO->EVENTS_READY) {k_msleep(1);};

	/* Wait until payload has been clocked out */
	while (!NRF_RADIO->EVENTS_PAYLOAD) {k_msleep(1);};
	printk("got PAYLOAD\n");

	/* Wait until RADIO is off */
	while (!NRF_RADIO->EVENTS_DISABLED) {k_msleep(1);};
	printk("got DISABLED\n");

	printk("radio TX ok\n\n");

	k_msleep(100);
	goto start;

	return 0;
}
