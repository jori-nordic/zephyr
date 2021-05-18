/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>
#include <nrf.h>
#include <hal/nrf_clock.h>

#define GPIO_PIN_CNF_DEFAULT  (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos) | \
                              (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) | \
                              (GPIO_PIN_CNF_PULL_Pulldown << GPIO_PIN_CNF_PULL_Pos) | \
                              (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos)

#define DBP_CNF_OUT(_pin)                                                      \
	{                                                                      \
		(NRF_P0->PIN_CNF[_pin] =                                       \
			 GPIO_PIN_CNF_DEFAULT |                                \
			 (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos));   \
	}

static inline void debug_pin_outcnf(uint32_t bitf)
{
  for (uint8_t _i = 0; _i <= 31; _i++)
  {
    if (bitf & ((uint64_t)1<<_i))
    {
      if (_i <32)
      {
        DBP_CNF_OUT(_i);
      }
    }
  }
}

static void m_setup_gpio(uint32_t bitf)
{
	debug_pin_outcnf(bitf);

	uint32_t p0 = bitf;
	NRF_P0->DIRSET = p0;
	NRF_P0->OUTCLR = p0;
}

volatile bool done = false;

ISR_DIRECT_DECLARE(power_handler)
{

	if (NRF_CLOCK->EVENTS_DONE == 1) {
		NRF_CLOCK->EVENTS_DONE = 0;
		NRF_P0->OUTCLR = 1 << 7;
		done = 1;
	}
	else if (NRF_CLOCK->EVENTS_HFCLKSTARTED == 1){
		NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
		NRF_P0->OUTSET = 1 << 5;
		k_busy_wait(2);
		NRF_P0->OUTCLR = 1 << 5;
	}
	else if(NRF_CLOCK->EVENTS_LFCLKSTARTED == 1) {
		NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
		NRF_P0->OUTCLR = 1 << 6;
	}

	ISR_DIRECT_PM();

	return 0;
}

void start_hfclk(bool enable)
{
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTOP);
	while(nrf_clock_is_running(NRF_CLOCK, NRF_CLOCK_DOMAIN_HFCLK, NULL));
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED);
	
	if(enable)
	{
		nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTART);
		while(!nrf_clock_hf_is_running(NRF_CLOCK, NRF_CLOCK_HFCLK_HIGH_ACCURACY)) __WFE();
	}
}

void main(void)
{
	bool first_cal_ok = false;

	/* Issue description:
	 * It looks like the very first calibration task does not result
	 * in getting a clock EVENTS_DONE.
	 * The issue seems to disappear when adding a bit of delay between
	 * starting up the LFRC and triggering the CAL task.
	 * An interesting tidbit is that this issue is very reproducible on some boards
	 * but not at all on others. v0.11 yoda DKs. */

	/* Debug pins:
	 * p0.04: General timing
	 * p0.05: HFCLK (pulse on STARTED)
	 * p0.06: LFCLK enable (time between trigger and event)
	 * p0.07: DONE (time between trigger and event) */

	/* Enable ISR for clock events */
	IRQ_DIRECT_CONNECT(CLOCK_POWER_IRQn, 5, power_handler, 0);
	irq_enable(CLOCK_POWER_IRQn);
	nrf_clock_int_enable(NRF_CLOCK, NRF_CLOCK_INT_HF_STARTED_MASK |
						NRF_CLOCK_INT_LF_STARTED_MASK |
						NRF_CLOCK_INT_DONE_MASK);

	m_setup_gpio(1 << 4 | 1 << 5 | 1 << 6 | 1 << 7);
	NRF_P0->OUTSET = 1 << 4;
	__NOP();
	__NOP();
	__NOP();
	NRF_P0->OUTCLR = 1 << 4;

	start_hfclk(1);

	NRF_P0->OUTSET = 1 << 4;
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED);
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_LFCLKSTARTED);

	// Start LFCLK with RC
	nrf_clock_lf_src_set(NRF_CLOCK, NRF_CLOCK_LFCLK_RC);
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_LFCLKSTART);
	NRF_P0->OUTSET = 1 << 6;

	while(!nrf_clock_lf_is_running(NRF_CLOCK))
		while(nrf_clock_lf_actv_src_get(NRF_CLOCK) != NRF_CLOCK_LFCLK_RC) __WFE();
	NRF_P0->OUTCLR = 1 << 4;
	k_busy_wait(2);

	/* If we don't busy-wait here for ~800us, we never get the DONE event */
	/* k_busy_wait(1000); */

	// Trigger CAL a first time
	NRF_P0->OUTSET = 1 << 4;
	NRF_P0->OUTSET = 1 << 7;
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_DONE);
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_CAL);
	// Wait for event. Should never come on faulty boards
	// Event usually comes after ~32ms
	/* while(!done) __WFE(); */
	for(int i=0; i<500 && !done; i++)
		k_busy_wait(100);
	if(done)
		first_cal_ok = 1;
	done = 0;
	NRF_P0->OUTCLR = 1 << 4;
	start_hfclk(1);

	NRF_P0->OUTSET = 1 << 4;
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED);
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_LFCLKSTARTED);

	// Trigger CAL a second time
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_DONE);
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_CAL);

	// Observe we get DONE now
	NRF_P0->OUTSET = 1 << 4;
	NRF_P0->OUTSET = 1 << 7;
	while(!done) __WFE();
	done = 0;
	NRF_P0->OUTCLR = 1 << 4;
	start_hfclk(1);

	// Trigger CAL a third time
	nrf_clock_event_clear(NRF_CLOCK, NRF_CLOCK_EVENT_DONE);
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_CAL);

	// Observe we get DONE again
	NRF_P0->OUTSET = 1 << 4;
	NRF_P0->OUTSET = 1 << 7;
	while(!done) __WFE();
	NRF_P0->OUTCLR = 1 << 4;

	printk("First calibration status: %d\n", first_cal_ok);
	printk("End of program\n");
}
