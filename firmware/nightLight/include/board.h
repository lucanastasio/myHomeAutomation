/* Copyright (c) 2024 Luca Anastasio
 *
 * Board definitions
 *
 */

#ifndef BOARD_H
#define BOARD_H

#include <Arduino.h>
#include "wiring_private.h"

/* Pins
 * IO	Peripheral	Usage			Active level
 * PB0	OC0A		LED				High
 * PB1	IN			PIR				High
 * PB2	ADC1?		Vsense			*Low
 * PB3	ADC3		Ambient light	*Low
 * PB4	IN			User button		Low
 */

// NOTE: Suggested LED PWM frequency = 20kHz to 100kHz

#define ADC_BITS 10

#ifdef __AVR_ATmega328P__

#include <Debounce.h>

#define millis_t uint16_t

#define LED_PIN 13
// #define LED_PWM
#define PIR_PIN 11
#define VSN_PIN A1
#define ALS_PIN A3
#define BTN_PIN 12
#define VSN_ADC 1
#define ALS_ADC 3

#define ADC_PRESCALER 7 // prescale 16MHz by 128 -> 125kHz ADC clock
#define ADC_REFERENCE 0 // Vcc as reference

#define SET_LED(x) digitalWrite(LED_PIN, x)
#define PIR_DETECT() digitalRead(PIR_PIN)
#define USRBTN_PRESSED() !debouncedDigitalRead(BTN_PIN)
#define ADC_START() sbi(ADCSRA, ADSC)
#define ADC_COMPLETE() bit_is_clear(ADCSRA, ADSC)
#define ADC_MUX_SET(m) ADMUX = (ADC_REFERENCE << 6) | m
#define ADC_MUX_GET() (ADMUX & 0x03)

static inline void board_init()
{
	debouncePins(BTN_PIN, BTN_PIN);
	pinMode(LED_PIN, OUTPUT);
	pinMode(BTN_PIN, INPUT_PULLUP);
	ADC_MUX_SET(VSN_ADC); // set the ADC mux for the first conversion
	ADCSRA = _BV(ADEN) | _BV(ADSC) | ADC_PRESCALER;
}

#elif defined(__AVR_ATtiny13__)

#define millis_t uint16_t

#define LED_PIN PB0
#define LED_PWM OCR0A
#define PIR_PIN PB1
#define VSN_PIN PB2
#define ALS_PIN PB3
#define BTN_PIN PB4
#define VSN_ADC 1
#define ALS_ADC 3

#define ADC_PRESCALER 7 // prescale 9.6MHz by 128 -> 75kHz ADC clock
#define ADC_REFERENCE 0 // Vcc as reference

#define SET_LED(x) LED_PWM = x
#define PIR_DETECT() bit_is_set(PINB, PIR_PIN)
#define USRBTN_PRESSED() bit_is_clear(PINB, BTN_PIN)
#define ADC_START() sbi(ADCSRA, ADSC)
#define ADC_COMPLETE() bit_is_clear(ADCSRA, ADSC)
#define ADC_MUX_GET() (ADMUX & 0x03)
#if ADC_BITS == 8
	#define ADC_MUX_SET(m) ADMUX = (ADC_REFERENCE << 6) | _BV(ADLAR) | m
	#define ADC_VAL_GET() ADCH
	#define adc_t uint8_t
	#define ADC_VAL_INIT 0xFF
#else
	#define ADC_MUX_SET(m) ADMUX = (ADC_REFERENCE << 6) | m
	#define ADC_VAL_GET() ADCW
	#define adc_t uint16_t
	#define ADC_VAL_INIT 0xFFFF
#endif



static inline void board_init()
{
	sbi(DDRB, LED_PIN);						// set the LED pin as the only output
	DIDR0 = (_BV(ALS_ADC) | _BV(VSN_ADC)) << 2; // disable digital inputs for ADC channels
	ADC_MUX_SET(VSN_ADC);						// set the ADC mux for the first conversion
	ADCSRA = _BV(ADEN) | _BV(ADSC) | ADC_PRESCALER;
	TCCR0B = _BV(CS00); // 9.6MHz sys clock -> 9.6MHz timer clock -> 37.5kHz PWM frequency
	TCCR0A = _BV(WGM00) | _BV(WGM01) | _BV(COM0A1);
}

#endif

#endif // BOARD_H