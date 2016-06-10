/*

Copyright (C) 2016, Matthijs Kooijman <matthijs@stdin.nl>
Copyright (C) 2007, Dean Camera <dean [at] fourwalledcubicle [dot] com>

Released under the GPL Licence, Version 2.

Based on code take from BUTTLOAD, the Butterfly ISP Programmer, improved to:
 - Use binary search plus neighbour search, as recommended by Atmel,
   instead of linear search, as well as search both the upper and lower
   range. This should ensure the optimal value is always found.
 - Write the calibration value to EEPROM.
 - Write out debug info to the UART if DEBUG is defined
 - Use the GTCCR to start both timers at exactly the same moment
 - Compensate for delay in update TOV2.
 - Allow more time for timer2 to stabilize
*/

#include <avr/io.h>
#include <avr/eeprom.h>
#include <util/atomic.h>
#include <util/delay.h>
#include <stdlib.h>

#ifdef DEBUG
#include "uart.h"
uint8_t factory_osccal;
#endif

#if !defined(__AVR_ATmega328P__)
#error "This program has not been verified for your MCU. Register mapping should be checked and modified if needed."
#endif

// Calibrate to F_CPU
#define OSCCAL_TARGETCOUNT         (uint16_t)(F_CPU / (32768 / 256)) // (Target Freq / Reference Freq)
// Address in EEPROM to save the result
#define EEPROM_ADDRESS             0x04

static void Setup(void)
{
#ifdef DEBUG
	uart_init();
	stdout = &uart_output;
	stdin  = &uart_input;
	factory_osccal = OSCCAL;

	printf("Factory value = %d\n", OSCCAL);
#endif
	// Make sure all clock division is turned off (8MHz RC clock)
	CLKPR  = (1 << CLKPCE);
	CLKPR  = 0x00;

	// Disable timer interrupts
	TIMSK1 = 0;
	TIMSK2 = 0;

	// Set timer 2 to asyncronous mode (32.768KHz crystal)
	ASSR   = (1 << AS2);

	// Configure timers
	TCCR1A = 0;
	TCCR2A = 0;

	// Output Timer2 clock on PB3
/*
	TCCR2A = (0 << WGM20) | (1 << COM2A0);
	DDRB |= (1 << PB3);
	OCR2A = 2;
*/

	// Start both counters with no prescaling
	TCCR1B = (1 << CS10);
	TCCR2B = (1 << CS20);

	// Wait until the changes are synchronized to the asynchronous timer2
	while (ASSR & ((1 << TCR2AUB) | (1 << TCR2BUB)));

	// Wait for the crystal to start. This is a very conservative
	// delay, but startup times of over 2 seconds have been seen on
	// a breadboard (probably with too much capacitance, though).
	_delay_ms(3000);

}

static int32_t CheckOscCal(uint8_t osccal) {
	OSCCAL = osccal;
	// NOP after writes to OSCCAL to avoid CPU errors during
	// oscillator stabilization
	asm volatile(" NOP");

	int32_t result, cnt1;
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
	{
		// Reset and lock timer prescalers
		GTCCR = (1 << TSM) | (1 << PSRASY) | (1 << PSRSYNC);

		// Clear the timer values. The interrupt flags on the
		// asynchronous timer2 lag behind 1 timer cycle (and 3
		// cpu cycles, but we ignore that). To compensate, start
		// timer2 at 1 instead of 0.
		TCNT1 = 0;
		TCNT2 = 1;

		// Wait until TCNT2 is updated asynchronously
		while (ASSR & (1 << TCN2UB));

		// Clear timer overflow flags
		TIFR2 = (1 << TOV2);
		TIFR1 = (1 << TOV1);

		// Clear prescaler reset to start timers
		GTCCR = 0;

		// Wait until timer 2 overflows
		while (!(TIFR2 & (1 << TOV2)));

		// Stop timer 1 so it can be read
		TCCR1B = 0x00;

		// Read the timer. Since our target value is close to
		// overflow, look at the overflow flag as well (just
		// handling a single overflow should be sufficient,
		// since further overflows do not influence the
		// sign of the result).
		cnt1 = TCNT1;
		if (TIFR1 & (1 << TOV1))
			cnt1 += 65536;

		// restart timer 1
		TCCR1B = (1 << CS10);

		// If TCNT1 is too low, the RC clock runs too slow, so
		// return positive to increase OSCCAL
		result = OSCCAL_TARGETCOUNT - cnt1;

#ifdef DEBUG
		// For lack of a better value, use the factory value to
		// get at least semi-readable UART output
		OSCCAL = factory_osccal;
		asm volatile(" NOP");
		printf("Tried %d, result = %ld\n", osccal, result);
		uart_flush();
		OSCCAL = osccal;
		asm volatile(" NOP");
#endif
	}
	return result;
}

static int32_t CalibrateRange(uint8_t center, uint8_t step) {
	int32_t result;
	uint8_t *p = (uint8_t*)2;

	// Increasing values of the OSCCAL register usually result in a
	// higher frequency, but with some increases the frequency will
	// end up (slightly) lower. Increasing OSCCAL by two will always
	// increase the frequency.
	//
	// To find the best OSCCAL value, this starts with a binary
	// search, which stops just before the last step. A binary
	// search is optimal when the values tried are sorted /
	// monotomely increasing, so skipping the last step will limit
	// the binary search to even OSCCAL values only, which are
	// guaranteed to be monotome.
	//
	// After the binary search, there are three values remaining
	// (which might not be strictly increasing), all of which are
	// tried and the best one is used.

	// binary search
	while (step > 1) {
		result = CheckOscCal(center);
		p++; p++;
		if (result == 0) // Perfect, we're done
			return 0;

		if (result > 0)
			center += step;
		else
			center -= step;

		step >>= 1;
	}

	// Neighbour search
	result = abs(CheckOscCal(center));
	int32_t next_result = abs(CheckOscCal(center + 1));
	int32_t prev_result = abs(CheckOscCal(center - 1));

	if (result < next_result && result < prev_result) {
		OSCCAL = center;
		asm volatile(" NOP");
		return result;
	} else if (next_result < prev_result) {
		OSCCAL = center + 1;
		asm volatile(" NOP");
		return next_result;
	} else {
		OSCCAL = center - 1;
		asm volatile(" NOP");
		return prev_result;
	}
}

static void Calibrate(void) {
	// For single-range OSCCAL devices:
	if (0) {
		CalibrateRange(128, 64);
	} else {
		// Some devices have two separate overlapping ranges for
		// the OSCCAL register, selected by the OSCCAL MSB. Test
		// each range separately and pick the best result.
		int32_t low_result = CalibrateRange(64, 32);
		uint8_t low_osccal = OSCCAL;
		int32_t high_result = CalibrateRange(128+64, 32);
		if (low_result < high_result)
			OSCCAL = low_osccal;
	}
}

static void Shutdown(void) {
	// Stop the timers
	TCCR1B = 0x00;
	TCCR2B = 0x00;

	// Turn off timer 2 asynchronous mode
	ASSR  &= ~(1 << AS2);

#ifdef DEBUG
	uart_shutdown();
#endif
}

int main(void) {
	Setup();

	Calibrate();

#ifdef DEBUG
	printf("Selected value %d\n", OSCCAL);
	uart_flush();
#endif

	Shutdown();

	// Save result in EEPROM
	eeprom_update_byte((uint8_t*)EEPROM_ADDRESS, OSCCAL);

	while(1) /* nothing */;
}
